// 純粋ロジック(zoompan / playlist)の単体テスト。DOM 不要。
//   pnpm test   （= tsx tests/pure.test.ts）
// で実行し、失敗があれば exit code 1。

import { ZoomPan } from '../src/viewer/zoompan';
import { Playlist } from '../src/viewer/playlist';

let failures = 0;
function check(name: string, cond: boolean): void {
  // eslint-disable-next-line no-console
  console.log(`${cond ? 'PASS' : 'FAIL'} — ${name}`);
  if (!cond) failures++;
}
function approx(a: number, b: number, eps = 0.001): boolean {
  return Math.abs(a - b) <= eps;
}

// ---- ZoomPan ----
{
  const zp = new ZoomPan();
  zp.setContainer(1000, 800);
  zp.setImage(2000, 1000);

  check('fitScale = 0.5 (contain)', approx(zp.fitScale(), 0.5));
  check('fillScale = 0.8 (cover)', approx(zp.fillScale(), 0.8));

  zp.apply('fit');
  check('fit → scale 0.5', approx(zp.scale, 0.5));
  check('fit → centered tx=0', approx(zp.tx, 0));
  check('fit → centered ty=150', approx(zp.ty, 150));

  zp.apply('actual');
  check('actual → scale 1', approx(zp.scale, 1));
  check('actual → tx clamped -500', approx(zp.tx, -500));
  check('actual → ty clamped -100', approx(zp.ty, -100));

  const zp2 = new ZoomPan();
  zp2.setContainer(1000, 800);
  zp2.setImage(2000, 1000);
  zp2.apply('fit'); // scale 0.5, tx 0, ty 150
  zp2.zoomTo(1.0, 0, 150);
  check('zoomTo keeps x anchor (tx=0)', approx(zp2.tx, 0));
  check('zoomTo then clamps ty to 0', approx(zp2.ty, 0));

  const zp3 = new ZoomPan();
  zp3.setContainer(500, 500);
  zp3.setImage(100, 100);
  check('resolve(number) clamps to maxScale', approx(zp3.resolve(1000), zp3.maxScale));
  zp3.scale = 1;
  check("resolve('in') = 1.25", approx(zp3.resolve('in'), 1.25));
  check("resolve('out') = 0.8", approx(zp3.resolve('out'), 0.8));

  // 画像が container より小さいときは中央固定
  zp3.apply('actual'); // scale 1, image 100x100 in 500x500
  check('small image centered tx=200', approx(zp3.tx, 200));
  check('small image centered ty=200', approx(zp3.ty, 200));
  zp3.panBy(1000, 1000); // 動かしても中央固定に戻る
  check('small image stays centered after pan', approx(zp3.tx, 200) && approx(zp3.ty, 200));
}

// ---- Playlist ----
{
  const pl = new Playlist();
  pl.load(['a', 'b', 'c'], 1);
  check('load index 1 → current b', pl.current === 'b');
  check('count = 3', pl.count === 3);
  check("go('next') → 2 (c)", pl.go('next') === 2 && pl.current === 'c');
  check("go('next') wraps → 0", pl.go('next') === 0);
  check("go('prev') wraps → 2", pl.go('prev') === 2);
  check("go('first') → 0", pl.go('first') === 0);
  check("go('last') → 2", pl.go('last') === 2);
  check('go(5) clamps → 2', pl.go(5) === 2);
  check('go(-3) clamps → 0', pl.go(-3) === 0);

  const empty = new Playlist();
  empty.load([]);
  check('empty count 0', empty.count === 0);
  check("empty go('next') → 0", empty.go('next') === 0);
  check('empty current undefined', empty.current === undefined);
}

if (failures > 0) {
  // eslint-disable-next-line no-console
  console.error(`\n${failures} test(s) FAILED`);
  process.exit(1);
} else {
  // eslint-disable-next-line no-console
  console.log('\nall tests passed');
}
