// プレイリスト(送り)の純粋ロジック。DOM 非依存。

export type NavTarget = 'next' | 'prev' | 'first' | 'last' | number;

export class Playlist {
  items: string[] = [];
  index = 0;

  load(items: string[], index = 0): void {
    this.items = items.slice();
    this.index = this.items.length ? Math.min(Math.max(0, index), this.items.length - 1) : 0;
  }

  get count(): number {
    return this.items.length;
  }

  get current(): string | undefined {
    return this.items[this.index];
  }

  /** 送り先を解決して index を更新し、新しい index を返す。空なら 0。 */
  go(to: NavTarget): number {
    const n = this.count;
    if (n === 0) return 0;
    if (typeof to === 'number') {
      this.index = Math.min(Math.max(0, Math.trunc(to)), n - 1);
    } else {
      switch (to) {
        case 'next':
          this.index = (this.index + 1) % n;
          break;
        case 'prev':
          this.index = (this.index + n - 1) % n;
          break;
        case 'first':
          this.index = 0;
          break;
        case 'last':
          this.index = n - 1;
          break;
      }
    }
    return this.index;
  }
}
