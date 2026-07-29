// ZooImage フロントエンドのエントリ。ビューアを 1 つ初期化する。
import { Viewer } from './viewer/viewer';

function boot(): void {
  const app = document.getElementById('app');
  if (!app) {
    console.error('#app not found');
    return;
  }
  // ウィンドウ 1 枚 = ビューア 1 つ。以後は Rust からの zi://apply とユーザー操作で駆動。
  new Viewer(app);
}

// module スクリプトは HTML パース後に実行されるが、念のため readyState を見る。
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', boot);
} else {
  boot();
}
