// ビューアの DOM コントローラ。zoompan/playlist の純粋ロジックを DOM と Tauri に結線する。

import { getCurrentWindow } from '@tauri-apps/api/window';
import { listen } from '@tauri-apps/api/event';
import { invoke, convertFileSrc } from '@tauri-apps/api/core';
import { ZoomPan, type ZoomCommand } from './zoompan';
import { Playlist, type NavTarget } from './playlist';

export type Theme = 'rich' | 'minimal';

export interface ApplyPatch {
  path?: string;
  items?: string[];
  index?: number;
  count?: number;
  zoom?: ZoomCommand;
  theme?: Theme;
  title?: string;
}

const ZOOM_REPORT_MS = 120;

export class Viewer {
  private win = getCurrentWindow();
  /** ビューア名 = ウィンドウラベル。 */
  readonly name = this.win.label;

  private zp = new ZoomPan();
  private playlist = new Playlist();
  private theme: Theme = 'rich';
  /** 直近のズーム指示。'fit'/'fill' のときはリサイズで再フィットする。 */
  private zoomMode: ZoomCommand = 'fit';
  private currentPath?: string;
  private naturalW = 0;
  private naturalH = 0;

  private root: HTMLElement;
  private stage: HTMLElement;
  private img: HTMLImageElement;
  private dropzone: HTMLElement;
  private hud: HTMLElement;
  private filmstrip: HTMLElement | null = null;

  private zoomReportTimer: number | undefined;

  constructor(root: HTMLElement) {
    this.root = root;
    this.stage = root.querySelector<HTMLElement>('#stage')!;
    this.img = root.querySelector<HTMLImageElement>('#image')!;
    this.dropzone = root.querySelector<HTMLElement>('#dropzone')!;
    this.hud = root.querySelector<HTMLElement>('#hud')!;

    this.filmstrip = root.querySelector<HTMLElement>('#filmstrip');

    this.wireImage();
    this.wirePointer();
    this.wireWheel();
    this.wireKeyboard();
    this.wireResize();
    this.wireToolbar();
    this.wireApplyChannel();
    this.wireDragDrop();
    // 保存済みテーマ（単体モード）。plugin の patch が来れば上書きされる。
    const saved = localStorage.getItem('zi-theme');
    this.setTheme(saved === 'minimal' || saved === 'rich' ? (saved as Theme) : this.theme);
    this.refreshStageSize();
  }

  // ---- 画像ロード ----

  private wireImage(): void {
    this.img.addEventListener('load', () => {
      this.naturalW = this.img.naturalWidth;
      this.naturalH = this.img.naturalHeight;
      this.zp.setImage(this.naturalW, this.naturalH);
      this.applyZoom(this.zoomMode);
      this.dropzone.hidden = true;
      this.report('loaded', {
        path: this.currentPath,
        w: this.naturalW,
        h: this.naturalH,
      });
      this.render();
    });
    this.img.addEventListener('error', () => {
      if (!this.currentPath) return;
      this.report('error', { message: 'failed to load image', path: this.currentPath });
    });
  }

  setImage(path: string): void {
    this.currentPath = path;
    this.img.src = convertFileSrc(path);
    this.setStatus('name', baseName(path));
  }

  // ---- 状態パッチ(Rust → フロント) ----

  private wireApplyChannel(): void {
    void listen<ApplyPatch>('zi://apply', (e) => this.applyPatch(e.payload));
  }

  private applyPatch(p: ApplyPatch): void {
    if (p.theme) this.setTheme(p.theme);
    if (p.title !== undefined) {
      document.title = p.title;
      this.setStatus('name', p.title);
    }
    if (p.items) {
      this.playlist.load(p.items, p.index ?? 0);
      this.updateCounter();
      this.buildFilmstrip();
    }
    if (p.path !== undefined) {
      // プレイリストが無い場合でも 1 枚として扱う
      if (!this.playlist.count) this.playlist.load([p.path], 0);
      if (p.index !== undefined) this.playlist.index = p.index;
      this.setImage(p.path);
      this.updateCounter();
      this.setFilmstripActive();
    }
    if (p.zoom !== undefined) {
      this.zoomMode = p.zoom;
      // 画像がまだ無ければ load 時に適用される
      if (this.naturalW) this.applyZoom(p.zoom);
    }
  }

  // ---- ズーム ----

  private applyZoom(cmd: ZoomCommand, cx?: number, cy?: number): void {
    const scale = this.zp.apply(cmd, cx, cy);
    // symbolic mode は保持、数値/in/out は数値として保持(リサイズ再フィット抑止)
    this.zoomMode = cmd === 'fit' || cmd === 'fill' ? cmd : scale;
    this.render();
    this.scheduleZoomReport(scale);
  }

  private scheduleZoomReport(scale: number): void {
    if (this.zoomReportTimer) window.clearTimeout(this.zoomReportTimer);
    this.zoomReportTimer = window.setTimeout(() => {
      this.report('zoomed', { zoom: scale });
    }, ZOOM_REPORT_MS);
  }

  // ---- レンダリング ----

  private render(): void {
    this.img.style.transform = this.zp.transformCss();
    const pct = Math.round(this.zp.scale * 100);
    this.setStatus('zoom', `${pct}%`);
    const zl = this.root.querySelector('#zoom-label');
    if (zl) zl.textContent = `${pct}%`;
    if (this.naturalW) this.setStatus('dims', `${this.naturalW}×${this.naturalH}`);
    this.hud.textContent =
      this.playlist.count > 1
        ? `${pct}%  ·  ${this.playlist.index + 1}/${this.playlist.count}`
        : `${pct}%`;
  }

  private updateCounter(): void {
    const c = this.root.querySelector('#counter');
    if (c) {
      c.textContent =
        this.playlist.count > 1
          ? `${this.playlist.index + 1}/${this.playlist.count}`
          : '–';
    }
  }

  private setStatus(kind: 'name' | 'dims' | 'zoom', text: string): void {
    const map = { name: '#status-name', dims: '#status-dims', zoom: '#status-zoom' };
    const el = this.root.querySelector(map[kind]);
    if (el) el.textContent = text;
  }

  // ---- テーマ ----

  setTheme(theme: Theme): void {
    this.theme = theme;
    this.root.dataset.theme = theme;
  }

  toggleTheme(): void {
    this.setTheme(this.theme === 'rich' ? 'minimal' : 'rich');
    localStorage.setItem('zi-theme', this.theme); // 単体モードの既定として永続化
    // 装飾(枠)も切り替える
    void this.win.setDecorations(this.theme !== 'minimal');
    this.report('themed', { theme: this.theme });
  }

  // ---- ナビゲーション ----

  navigate(to: NavTarget): void {
    if (this.playlist.count <= 1) return;
    const idx = this.playlist.go(to);
    const path = this.playlist.current;
    if (path) {
      this.setImage(path);
      this.updateCounter();
      this.setFilmstripActive();
      this.report('navigated', { index: idx, path, count: this.playlist.count });
    }
  }

  // ---- フィルムストリップ（リッチテーマ・プレイリスト時） ----

  private buildFilmstrip(): void {
    const fs = this.filmstrip;
    if (!fs) return;
    if (this.playlist.count <= 1) {
      fs.hidden = true;
      fs.innerHTML = '';
      return;
    }
    fs.hidden = false;
    fs.innerHTML = '';
    this.playlist.items.forEach((item, i) => {
      const t = document.createElement('img');
      t.src = convertFileSrc(item);
      t.dataset.index = String(i);
      if (i === this.playlist.index) t.className = 'active';
      t.addEventListener('click', () => this.navigate(i));
      fs.appendChild(t);
    });
  }

  private setFilmstripActive(): void {
    const fs = this.filmstrip;
    if (!fs || fs.hidden) return;
    fs.querySelectorAll('img').forEach((el) => {
      const img = el as HTMLImageElement;
      const active = img.dataset.index === String(this.playlist.index);
      img.classList.toggle('active', active);
      if (active) img.scrollIntoView({ block: 'nearest', inline: 'center' });
    });
  }

  // ---- ドラッグ&ドロップ（単体モード） ----

  private wireDragDrop(): void {
    void this.win.onDragDropEvent((event) => {
      if (event.payload.type === 'drop') {
        const paths = event.payload.paths.filter((p) => isImagePath(p));
        if (paths.length === 0) return;
        this.playlist.load(paths, 0);
        this.updateCounter();
        this.buildFilmstrip();
        this.setImage(paths[0]);
        this.report('dropped', { paths });
      }
    });
  }

  // ---- 入力: ホイール ----

  private wireWheel(): void {
    this.stage.addEventListener(
      'wheel',
      (e) => {
        e.preventDefault();
        if (!this.naturalW) return;
        const rect = this.stage.getBoundingClientRect();
        const cx = e.clientX - rect.left;
        const cy = e.clientY - rect.top;
        const factor = e.deltaY < 0 ? this.zp.stepFactor : 1 / this.zp.stepFactor;
        this.applyZoom(this.zp.scale * factor, cx, cy);
      },
      { passive: false },
    );
  }

  // ---- 入力: ドラッグ(パン) & クリック ----

  private wirePointer(): void {
    let dragging = false;
    let moved = 0;
    let lastX = 0;
    let lastY = 0;
    let downX = 0;
    let downY = 0;
    let button = 0;

    this.stage.addEventListener('pointerdown', (e) => {
      dragging = true;
      moved = 0;
      lastX = e.clientX;
      lastY = e.clientY;
      downX = e.clientX;
      downY = e.clientY;
      button = e.button;
      this.stage.setPointerCapture(e.pointerId);
      this.stage.classList.add('grabbing');
    });

    this.stage.addEventListener('pointermove', (e) => {
      if (!dragging) return;
      const dx = e.clientX - lastX;
      const dy = e.clientY - lastY;
      lastX = e.clientX;
      lastY = e.clientY;
      moved += Math.abs(dx) + Math.abs(dy);
      this.zp.panBy(dx, dy);
      this.render();
    });

    const endDrag = (e: PointerEvent) => {
      if (!dragging) return;
      dragging = false;
      this.stage.classList.remove('grabbing');
      try {
        this.stage.releasePointerCapture(e.pointerId);
      } catch {
        /* noop */
      }
      if (moved < 4 && this.naturalW) {
        // クリックとして扱う
        const rect = this.stage.getBoundingClientRect();
        const sx = downX - rect.left;
        const sy = downY - rect.top;
        const imgX = (sx - this.zp.tx) / this.zp.scale;
        const imgY = (sy - this.zp.ty) / this.zp.scale;
        this.report('clicked', {
          x: Math.round(sx),
          y: Math.round(sy),
          imgX: Math.round(imgX),
          imgY: Math.round(imgY),
          button: button === 2 ? 'right' : button === 1 ? 'middle' : 'left',
        });
      }
    };
    this.stage.addEventListener('pointerup', endDrag);
    this.stage.addEventListener('pointercancel', endDrag);
  }

  // ---- 入力: キーボード ----

  private wireKeyboard(): void {
    window.addEventListener('keydown', (e) => {
      switch (e.key) {
        case '+':
        case '=':
          this.applyZoom('in');
          break;
        case '-':
        case '_':
          this.applyZoom('out');
          break;
        case '0':
          this.applyZoom('fit');
          break;
        case '1':
          this.applyZoom('actual');
          break;
        case 'ArrowRight':
        case 'ArrowDown':
        case ' ':
          this.navigate('next');
          break;
        case 'ArrowLeft':
        case 'ArrowUp':
          this.navigate('prev');
          break;
        case 'f':
        case 'F':
          void this.toggleFullscreen();
          break;
        case 't':
        case 'T':
          this.toggleTheme();
          break;
        case 'Escape':
          void this.win.close();
          break;
        default:
          return;
      }
      e.preventDefault();
    });
  }

  private async toggleFullscreen(): Promise<void> {
    const fs = await this.win.isFullscreen();
    await this.win.setFullscreen(!fs);
  }

  // ---- リサイズ ----

  private wireResize(): void {
    const ro = new ResizeObserver(() => this.refreshStageSize());
    ro.observe(this.stage);
  }

  private refreshStageSize(): void {
    this.zp.setContainer(this.stage.clientWidth, this.stage.clientHeight);
    if (!this.naturalW) return;
    if (this.zoomMode === 'fit' || this.zoomMode === 'fill') {
      this.zp.apply(this.zoomMode);
    } else {
      this.zp.clampPan();
    }
    this.render();
  }

  // ---- ツールバー ----

  private wireToolbar(): void {
    const tb = this.root.querySelector('#toolbar');
    if (!tb) return;
    tb.addEventListener('click', (e) => {
      const btn = (e.target as HTMLElement).closest('button');
      if (!btn) return;
      const cmd = btn.dataset.cmd;
      switch (cmd) {
        case 'prev':
          this.navigate('prev');
          break;
        case 'next':
          this.navigate('next');
          break;
        case 'zoom-in':
          this.applyZoom('in');
          break;
        case 'zoom-out':
          this.applyZoom('out');
          break;
        case 'fit':
          this.applyZoom('fit');
          break;
        case 'actual':
          this.applyZoom('actual');
          break;
        case 'fullscreen':
          void this.toggleFullscreen();
          break;
        case 'toggle-theme':
          this.toggleTheme();
          break;
        case 'toggle-info':
          this.toggleInfo();
          break;
        case 'open':
          void this.openDialog();
          break;
      }
    });
  }

  private toggleInfo(): void {
    const info = this.root.querySelector('#info') as HTMLElement | null;
    if (!info) return;
    info.hidden = !info.hidden;
    this.updateInfo();
  }

  private updateInfo(): void {
    const list = this.root.querySelector('#info-list');
    if (!list) return;
    const rows: [string, string][] = [
      ['File', this.currentPath ? baseName(this.currentPath) : '–'],
      ['Dimensions', this.naturalW ? `${this.naturalW} × ${this.naturalH}` : '–'],
      ['Zoom', `${Math.round(this.zp.scale * 100)}%`],
      ['Item', this.playlist.count > 1 ? `${this.playlist.index + 1} / ${this.playlist.count}` : '1 / 1'],
    ];
    list.innerHTML = rows.map(([k, v]) => `<dt>${k}</dt><dd>${escapeHtml(v)}</dd>`).join('');
  }

  /** 単体モード用: ファイル選択ダイアログ(Task #10 でプラグイン経由も対応)。 */
  private async openDialog(): Promise<void> {
    try {
      const { open } = await import('@tauri-apps/plugin-dialog');
      const selected = await open({
        multiple: false,
        filters: [
          { name: 'Images', extensions: ['png', 'jpg', 'jpeg', 'gif', 'webp', 'bmp', 'tiff', 'tif', 'heic', 'avif'] },
        ],
      });
      if (typeof selected === 'string') {
        this.applyPatch({ items: [selected], index: 0, path: selected, zoom: 'fit' });
      }
    } catch (err) {
      console.error('open dialog failed', err);
    }
  }

  // ---- イベント報告(フロント → Rust → plugin) ----

  private report(event: string, data: Record<string, unknown>): void {
    void invoke('report_event', { viewer: this.name, event, data });
  }
}

function baseName(p: string): string {
  const i = Math.max(p.lastIndexOf('/'), p.lastIndexOf('\\'));
  return i >= 0 ? p.slice(i + 1) : p;
}

function isImagePath(p: string): boolean {
  return /\.(png|jpe?g|gif|webp|bmp|tiff?|heic|avif|svg)$/i.test(p);
}

function escapeHtml(s: string): string {
  return s.replace(/[&<>"']/g, (c) =>
    ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]!,
  );
}
