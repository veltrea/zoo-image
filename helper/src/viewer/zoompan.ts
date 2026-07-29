// ズーム/パンの純粋計算エンジン。DOM に依存しないので tsx/node で単体テストできる。
// 座標モデル: 画像の左上を (tx, ty)、倍率 scale で配置する。
// 画像は container 座標系で [tx, tx + image.w*scale] × [ty, ty + image.h*scale] を占める。

export type Size = { w: number; h: number };

/** ズーム指示。数値は絶対倍率(1 = 100%)。 */
export type ZoomCommand = number | 'fit' | 'fill' | 'actual' | '1:1' | 'in' | 'out';

export type ViewState = { scale: number; tx: number; ty: number };

export class ZoomPan {
  container: Size = { w: 0, h: 0 };
  image: Size = { w: 0, h: 0 };
  scale = 1;
  tx = 0;
  ty = 0;

  readonly minScale = 0.02;
  readonly maxScale = 64;
  /** ホイール1ステップ / in・out の倍率。 */
  readonly stepFactor = 1.25;

  setContainer(w: number, h: number): void {
    this.container = { w, h };
  }

  setImage(w: number, h: number): void {
    this.image = { w, h };
  }

  private clampScale(s: number): number {
    return Math.min(this.maxScale, Math.max(this.minScale, s));
  }

  /** 画像全体が container に収まる倍率(contain)。 */
  fitScale(): number {
    if (!this.image.w || !this.image.h || !this.container.w || !this.container.h) return 1;
    return Math.min(this.container.w / this.image.w, this.container.h / this.image.h);
  }

  /** container を覆う倍率(cover)。 */
  fillScale(): number {
    if (!this.image.w || !this.image.h || !this.container.w || !this.container.h) return 1;
    return Math.max(this.container.w / this.image.w, this.container.h / this.image.h);
  }

  /** ズーム指示を絶対倍率に解決する(現在状態に依存)。 */
  resolve(cmd: ZoomCommand): number {
    if (typeof cmd === 'number') return this.clampScale(cmd);
    switch (cmd) {
      case 'fit':
        return this.clampScale(this.fitScale());
      case 'fill':
        return this.clampScale(this.fillScale());
      case 'actual':
      case '1:1':
        return 1;
      case 'in':
        return this.clampScale(this.scale * this.stepFactor);
      case 'out':
        return this.clampScale(this.scale / this.stepFactor);
      default:
        return this.scale;
    }
  }

  /** 画像を container 中央に配置する(現在の scale を維持)。 */
  center(): void {
    this.tx = (this.container.w - this.image.w * this.scale) / 2;
    this.ty = (this.container.h - this.image.h * this.scale) / 2;
  }

  /**
   * container 座標 (cx, cy) を固定したまま倍率を newScale にする。
   * cx/cy 省略時は container 中心を固定点にする。
   */
  zoomTo(newScale: number, cx?: number, cy?: number): void {
    const s1 = this.clampScale(newScale);
    const px = cx ?? this.container.w / 2;
    const py = cy ?? this.container.h / 2;
    const ratio = s1 / this.scale;
    this.tx = px - (px - this.tx) * ratio;
    this.ty = py - (py - this.ty) * ratio;
    this.scale = s1;
    this.clampPan();
  }

  /** 指示でズーム。fit/fill/actual は中央寄せ、in/out/数値は固定点維持。 */
  apply(cmd: ZoomCommand, cx?: number, cy?: number): number {
    const target = this.resolve(cmd);
    if (cmd === 'fit' || cmd === 'fill' || cmd === 'actual' || cmd === '1:1') {
      this.scale = target;
      this.center();
      this.clampPan();
    } else {
      this.zoomTo(target, cx, cy);
    }
    return this.scale;
  }

  panBy(dx: number, dy: number): void {
    this.tx += dx;
    this.ty += dy;
    this.clampPan();
  }

  /**
   * パンのクランプ。軸方向に画像が container より小さければ中央固定、
   * 大きければ画像の端が container の内側に入り込まない範囲に収める。
   */
  clampPan(): void {
    const iw = this.image.w * this.scale;
    const ih = this.image.h * this.scale;

    if (iw <= this.container.w) {
      this.tx = (this.container.w - iw) / 2;
    } else {
      const min = this.container.w - iw; // 右端がぴったり
      const max = 0; // 左端がぴったり
      this.tx = Math.min(max, Math.max(min, this.tx));
    }

    if (ih <= this.container.h) {
      this.ty = (this.container.h - ih) / 2;
    } else {
      const min = this.container.h - ih;
      const max = 0;
      this.ty = Math.min(max, Math.max(min, this.ty));
    }
  }

  /** CSS transform 文字列。<img> に対して transform-origin: 0 0 で適用する。 */
  transformCss(): string {
    return `translate(${this.tx}px, ${this.ty}px) scale(${this.scale})`;
  }

  toState(): ViewState {
    return { scale: this.scale, tx: this.tx, ty: this.ty };
  }
}
