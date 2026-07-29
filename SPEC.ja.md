<!-- English version: SPEC.md / 英語版: SPEC.md -->

# ZooImage — 仕様書

`ZooImage` FileMaker プラグインの関数リファレンスと挙動。プラグインと ZooImage ヘルパー間の
IPC ワイヤプロトコルは [protocol/protocol.md](protocol/protocol.md)、コピペ用の FileMaker
スクリプトは [fmp/scripts.md](fmp/scripts.md) を参照。

## 規約

- すべての `zim_` 関数は成功時に値を、失敗時に `ERROR: <message>` を返す。直近のエラーは
  `zim_LastError` でも取得できる。
- `viewer` はウィンドウ名。既定 `main`。`"*"` は全体（明記した箇所のみ）。使用可能文字 `[A-Za-z0-9_-]`。
- `zim_SetWindow` では空引数は「据置」を意味する。
- 最初の `zim_Show` は同梱ヘルパーを起動するため一瞬待つ。以後は即時。

## `zim_` 関数リファレンス

| 関数 | 引数 | 戻り値 |
|---|---|---|
| `zim_Version` | — | `Zimg x.y.z`（起動中なら ` / helper a.b.c`） |
| `zim_IsRunning` | — | ヘルパー生存で `1`、それ以外 `0` |
| `zim_LastError` | — | 直近のエラーメッセージ |
| `zim_Show( image ; options )` | `image` = コンテナ **または** パス、`options` = JSON（省略可） | ビューア名 |
| `zim_Close( viewer )` | `viewer`（省略可、または `"*"`） | `OK` |
| `zim_SetZoom( viewer ; zoom )` | `zoom` = 数値 または `fit`/`fill`/`actual`/`in`/`out` | `OK` |
| `zim_SetWindow( viewer ; x ; y ; w ; h )` | 論理px。空は据置 | `OK` |
| `zim_SetTheme( viewer ; theme )` | `rich` \| `minimal` | `OK` |
| `zim_SetTitle( viewer ; title )` | テキスト | `OK` |
| `zim_Focus( viewer )` | — | `OK` |
| `zim_SetFullscreen( viewer ; on )` | `on` = 0/1 | `OK` |
| `zim_LoadList( viewer ; jsonArray ; index )` | パス配列 + 開始インデックス | `{count,index,path}` JSON |
| `zim_Navigate( viewer ; to )` | `next`/`prev`/`first`/`last`/`<index>` | `{index,path,count}` JSON |
| `zim_GetState( viewer )` | `viewer`（省略可、または `"*"`） | 状態 JSON |
| `zim_SetScript( viewer ; file ; script )` | FileMaker ファイル名 + スクリプト名 | `OK` |

## `zim_Show` の options（JSON オブジェクト、すべて省略可）

| キー | 型 | 意味 |
|---|---|---|
| `viewer` | string | ウィンドウ名（既定 `main`） |
| `x`, `y`, `w`, `h` | number | 論理ピクセルの位置・サイズ |
| `zoom` | number \| string | 初期ズーム（既定 `fit`） |
| `theme` | string | `rich` \| `minimal` |
| `title` | string | ウィンドウタイトル |
| `fullscreen` | bool | 全画面で開く |
| `focus` | bool | 前面化 |

## `zim_GetState` の結果

```json
{ "open": true, "viewer": "main", "x": 200, "y": 160, "w": 1000, "h": 760,
  "zoom": 1.5, "theme": "rich", "image": "/…/b.png", "index": 1, "count": 3, "fullscreen": false }
```

`zim_GetState("*")` は `{ "viewers": [ <state>, … ] }` を返す。

## イベント（`zim_SetScript` 経由）

登録した FileMaker スクリプトが、イベント JSON を引数として起動する。`event` は次のいずれか:

| `event` | `data` |
|---|---|
| `loaded` | `{path, w, h}` |
| `zoomed` | `{zoom}` |
| `navigated` | `{index, path, count}` |
| `clicked` | `{x, y, imgX, imgY, button}` |
| `closed` | `{}` |
| `dropped` | `{paths[]}` |
| `error` | `{message, path}` |

イベントはベストエフォート（プラグインの Idle で発火）。確実に観測するには `OnTimer`
スクリプトで `zim_GetState` をポーリングする。[fmp/scripts.md](fmp/scripts.md) §5 参照。

## エラー

`ERROR:` メッセージはプロトコルのエラーコード（`auth`, `bad_params`, `no_viewer`,
`load_failed`, `unsupported`, `internal`）に対応する。[protocol/protocol.md](protocol/protocol.md)
§10 参照。
