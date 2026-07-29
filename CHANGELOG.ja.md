<!-- English version: CHANGELOG.md / 英語版: CHANGELOG.md -->

# 変更履歴

本プロジェクトの主要な変更をここに記録します。形式は
[Keep a Changelog](https://keepachangelog.com/ja/) に緩やかに従い、セマンティック
バージョニングを採用します。

## [0.1.0] — 2026-07-29

最初のリリース。macOS のみ。

### Added
- **ヘルパー "ZooImage"**（Tauri v2、Rust + TS）: localhost JSONL IPC サーバ（トークン認証・
  エフェメラルポート・`daemon.json` 探索）、多重ウィンドウビューア、リッチ + ミニマルテーマ、
  ズーム/パンエンジン（純粋・単体テスト済み）、フィルムストリップ付きプレイリスト送り、
  ドラッグ&ドロップ、File ▸ Open、macOS ファイル関連付け、単体モード、subscribe へのイベント push。
- **プラグイン `ZooImage`**（C++ FMX）: 15 個の `zim_` 関数（`zim_Show`、`zim_SetZoom`、`zim_SetWindow`、
  `zim_SetTheme`、`zim_LoadList`、`zim_Navigate`、`zim_GetState`、`zim_SetScript` ほか）を持つ薄い IPC
  クライアント、コンテナ→一時ファイル抽出、ヘルパーの spawn/起動管理、ビューアイベントを Idle で
  `FMX_StartScript` により FileMaker スクリプトへ配送するイベント受信スレッド。
- IPC プロトコル仕様（`protocol/protocol.md`）、ビルド/署名/配布スクリプト、IPC テストクライアント、
  サンプル FileMaker スクリプト、対訳ドキュメント。
- 関数ごとのリファレンス（[FUNCTIONS.md](FUNCTIONS.md) / [FUNCTIONS.ja.md](FUNCTIONS.ja.md)）、
  BSD-3-Clause の [LICENSE](LICENSE)、[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)。
- `scripts/verify-all.sh` — 単体テスト → ヘルパー + プラグインのビルド → 同梱と署名 →
  命名と署名の検証 → パッケージ → E2E を 1 コマンドで通す。
- `scripts/e2e-test.mjs` — プラグインと同じ手順で同梱ヘルパーを起動し、IPC のやり取りを
  検証する（18 チェック）。FileMaker 不要。
- 配布物: ヘルパー同梱の ad-hoc 署名済み `ZooImage.fmplugin`、およびコンテナインストール可能な
  `ZooImage.fmplugin.gz`。

### Fixed（修正）
- 署名を inside-out（先にヘルパー `.app`、次にプラグインバンドル）で行うようにした。
  Tauri がビルド時に付ける ad-hoc 署名はリソースを封印しておらず、親に `--deep --force` を
  かけてもそれが置き換わらないため、`codesign --verify` が
  "code has no resources but signature indicates they must be present" で失敗していた。

### Verified（検証済み）
- `scripts/verify-all.sh` が全項目パス: 単体テスト、universal（arm64 + x86_64）ビルド、
  命名（プラグイン ID `Zimg`・15 個の `zim_` 関数・各 Bundle ID）、プラグインと同梱ヘルパー
  双方の署名有効性とリソース封印、パッケージング、エンドツーエンド IPC。
- ビルド済みプラグインバンドルから実際に起動したヘルパーに対して、エンドツーエンドの IPC
  （`hello` → `show` → `getState` → `setZoom` → `close` → `shutdown`）を確認（FileMaker 不要）。

### Known limitations / next（既知の制限・次の予定）
- 現状 macOS のみ（Windows `.fmx64` / Linux `.fmx` は予定）。
- 情報パネルはファイル名/寸法/ズームを表示。ファイルサイズ + EXIF は将来拡張。
- 実機の FileMaker Pro でのロードと、`FMX_StartScript` によるイベント配送は未検証
  （E2E がカバーするのは IPC 層まで）。
