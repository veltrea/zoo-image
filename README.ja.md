<!-- English version: README.md / 英語版: README.md -->

# ZooImage — FileMaker 用の画像プレビュー／ビューア

FileMaker の画像**プレビュー／ビューア**。薄い **C++ プラグイン（`ZooImage.fmplugin`）** が、独立した
**Tauri 製ヘルパーアプリ（`ZooImage.app`）** を駆動する構成です。FileMaker からコンテナ値
またはファイルパスを渡すと、**拡大縮小（ズーム）・パン可能な**独立ウィンドウで表示し、
プラグインからウィンドウ位置・サイズ・ズーム・テーマ・プレイリスト送り・全画面を制御でき
ます。ヘルパーは単体でも通常の画像ビューアとして動作します（D&D・File ▸ Open・キーボード）。

- プラグイン ID: `Zimg` — 外部関数プレフィックス: `zim_`
- ヘルパー: Tauri（Rust + Web UI）。まず macOS、Windows は予定。
- 選択できる 2 デザイン: **リッチ**（ツールバー・フィルムストリップ・情報パネル）と
  **ミニマル**（ボーダーレス・キーボード駆動）。

## なぜ プラグイン **＋** ヘルパー なのか

FileMaker の外部関数は計算エンジン上で**同期実行**される。重い画像描画をそこで行うと UI が
固まる。そこで `ZooImage` は薄い IPC クライアントに徹し、表示処理を常駐ヘルパーへ `127.0.0.1`
（JSONL・トークン認証）で委譲する。プラグインは初回使用時にヘルパーを spawn する。コマンドは
プラグイン → ヘルパー、ユーザー操作イベント（閉じた・送り・クリック等）は
ヘルパー → プラグイン → FileMaker スクリプト（`FMX_StartScript`）へ戻る。

```
FileMaker Pro ──(計算)── ZooImage.fmplugin ──JSONL/TCP 127.0.0.1──▶ ZooImage ヘルパー (Tauri)
                              ▲                                      │ ビューアウィンドウ
                              └──── イベント (Idle → StartScript) ───┘ ズーム・パン・テーマ
```

## 機能

- **コンテナ** または **ファイルパス**を表示: `zim_Show( image ; optionsJSON )`
- ズーム（`fit` / `fill` / `actual` / `in` / `out` / 数値）、ホイールズーム、ドラッグでパン
- ウィンドウ位置・サイズ・タイトル・全画面・フォーカス・テーマを FileMaker から制御
- **プレイリスト**送り（`zim_LoadList` + `zim_Navigate`）、キーボード ← →
- FileMaker スクリプトへの**イベントコールバック**、および `OnTimer` + `zim_GetState` ポーリング代替
- 複数ビューア同時表示（`viewer` 名で識別、既定 `main`）
- 単体ビューアモード（FileMaker なしで使える）
- **EXIF の読み取り** — カメラ・レンズ・露出・撮影日時・GPS を JSON で取得
  （`zim_ExifRead` / `zim_ExifReadPath`）。ビューアなしで動くので FileMaker Server でも
  使えます。読み取り専用で、書き換えは ZooEXIF プラグインの担当です。

関数ごとのリファレンスは [FUNCTIONS.ja.md](FUNCTIONS.ja.md)、挙動と規約は
[SPEC.ja.md](SPEC.ja.md)、ワイヤプロトコルは [protocol/protocol.md](protocol/protocol.md)、
コピペ用の FileMaker スクリプトは [fmp/scripts.md](fmp/scripts.md) を参照。

## インストール（利用者向け）

`dist/ZooImage.fmplugin.gz`（`scripts/package-container.sh` で生成）をコンテナフィールドに格納し、
**「プラグインファイルのインストール」**スクリプトステップでインストールする（quarantine が
付かず、管理者権限も不要）。手順は [fmp/scripts.md](fmp/scripts.md) §1。macOS の初回ロード時に
（ad-hoc 署名の）プラグインを信頼するか一度だけ確認される。

## ソースからビルド

必要環境: Rust + `cargo`、Node + `pnpm`、CMake、Xcode コマンドラインツール、FileMaker
Plug-in SDK（既定の場所は `../PlugInSDK`）。

```bash
scripts/build-helper.sh          # → "ZooImage.app"（'universal' 引数で arm64+x86_64）
scripts/build-plugin.sh          # → plugin/build/ZooImage.fmplugin
scripts/bundle.sh                # ヘルパーをプラグインに同梱 → deep ad-hoc 署名
scripts/package-container.sh     # → dist/ZooImage.fmplugin.gz（コンテナインストール用）
```

ヘルパーの単体テスト（DOM 非依存のズーム/パン + プレイリストの純粋ロジック）:
`cd helper && pnpm test`。FileMaker 抜きでヘルパーを駆動: ヘルパー起動後に
`node scripts/ipc-test.mjs <画像>`。

## リポジトリ構成

| パス | 内容 |
|---|---|
| `plugin/` | C++ FMX プラグイン（`ZooImage`）— エントリポイント・IPC クライアント・起動管理・コンテナ抽出 |
| `helper/` | Tauri アプリ "ZooImage" — Rust IPC サーバ + TS ビューアフロントエンド |
| `protocol/` | IPC プロトコル仕様（単一の参照元） |
| `scripts/` | ビルド／署名／配布／IPC テストスクリプト |
| `fmp/` | サンプル FileMaker スクリプト |

## ステータス

macOS 版のヘルパー・プラグインはビルドが通り、エンドツーエンドの IPC テストに合格。Windows
対応、単体モードの作り込み、サンプル `.fmp12` は作業中。
