<!-- English version: FUNCTIONS.md / 英語版: FUNCTIONS.md -->

# ZooImage — 関数リファレンス

FileMaker 用の画像**プレビュー／ビューア**。`ZooImage.fmplugin` は薄い IPC クライアントで、
描画はすべて同梱ヘルパーアプリ（`ZooImage.app`）が行います。そのため重い画像を扱っても
FileMaker の計算エンジンが固まりません。

- プラグイン ID: `Zimg` — 外部関数プレフィックス: `zim_` — 全 15 関数。
- すべての関数は成功時に値を、失敗時に `ERROR: <メッセージ>` を返します。直近のエラーは
  `zim_LastError` でも取得できます。
- `viewer` はウィンドウ名です。既定は `main`。使える文字は `[A-Za-z0-9_-]`。
  記載のある関数では `"*"` で開いている全ビューアを指定できます。
- `options` 引数はすべて **JSON オブジェクト**です。省略または空なら既定値になります。
- 最初の `zim_Show` はヘルパーを起動するため一瞬待ちます。以後の呼び出しは即時です。

---

## zim_Version

```
zim_Version
```

プラグインのバージョンを返します（例: `Zimg 0.1.0`）。ヘルパーが起動中なら、そのバージョンが
続けて付きます（`Zimg 0.1.0 / helper 0.1.0`）。ロード確認にも使えます — この関数が
FileMaker に認識されないなら、プラグインが読み込まれていません。

---

## zim_IsRunning

```
zim_IsRunning
```

ヘルパープロセスが起動していて IPC で到達できるなら `1`、それ以外は `0` を返します。
この関数はヘルパーを**起動しません**。

---

## zim_LastError

```
zim_LastError
```

直近のエラーメッセージを返します。最後の呼び出しが成功していれば空です。`ERROR: …` が
返ってきたあと、呼び出しをやり直さずに詳細を見たいときに使います。

---

## zim_Show ( image {; optionsJSON} )

```
zim_Show ( image )
zim_Show ( image ; optionsJSON )
```

`image` をビューアウィンドウで開き、ビューア名（例: `main`）を返します。

| 引数 | 意味 |
|---|---|
| `image` | **コンテナフィールド**の値、**または**ファイルパスのテキスト。コンテナは一時ファイルに書き出してから開きます。 |
| `optionsJSON` | 省略可の JSON オブジェクト。下表を参照。 |

### `optionsJSON` のキー（すべて省略可）

| キー | 型 | 意味 |
|---|---|---|
| `viewer` | string | ウィンドウ名。既定 `main`。同じ名前を再利用すると、そのウィンドウの画像を差し替えます。 |
| `x`, `y`, `w`, `h` | number | ウィンドウ位置・サイズ（論理ピクセル）。 |
| `zoom` | number \| string | 初期ズーム。既定 `fit`。指定できる文字列は `zim_SetZoom` を参照。 |
| `theme` | string | `rich`（ツールバー + フィルムストリップ + 情報パネル）または `minimal`（枠なし・キーボード操作）。 |
| `title` | string | ウィンドウタイトル。 |
| `fullscreen` | bool | 全画面で開く。 |
| `focus` | bool | ウィンドウを最前面にする。 |

```
Set Variable [ $v ;
  zim_Show ( Photos::Image ; "{\"theme\":\"rich\",\"zoom\":\"fit\",\"title\":\"請求書 42\"}" )
]
```

画像を開けない・デコードできない場合（`load_failed`）や、options の JSON が壊れている場合
（`bad_params`）は `ERROR:` を返します。

---

## zim_Close ( {viewer} )

```
zim_Close
zim_Close ( viewer )
zim_Close ( "*" )
```

ビューアウィンドウを閉じて `OK` を返します。引数なしなら `main` を閉じます。`"*"` を渡すと
開いている全ビューアを閉じます。すでに閉じているビューアを閉じてもエラーにはなりません。

---

## zim_SetZoom ( viewer ; zoom )

```
zim_SetZoom ( viewer ; zoom )
```

ズーム倍率を設定し `OK` を返します。

| `zoom` | 意味 |
|---|---|
| 数値 | 絶対倍率 — `1` = 100 %、`2` = 200 %、`0.5` = 50 %。 |
| `fit` | 画像全体がウィンドウに収まるように合わせる。 |
| `fill` | ウィンドウを埋める（はみ出した分は切れる）。 |
| `actual` | 100 %。画像 1 ピクセル = 論理 1 ピクセル。 |
| `in` | 現在の倍率から 1 段拡大。 |
| `out` | 現在の倍率から 1 段縮小。 |

ユーザーはマウスホイールでのズームやドラッグでのパンも行えます。その結果の倍率は
`zim_GetState` で取得できます。

---

## zim_SetWindow ( viewer ; x ; y ; w ; h )

```
zim_SetWindow ( viewer ; x ; y ; w ; h )
```

ウィンドウを移動・リサイズします（論理ピクセル）。`OK` を返します。

**引数を空にするとその値は据え置き**になります。`zim_SetWindow ( "main" ; "" ; "" ; 1200 ; 900 )`
なら、位置を変えずにサイズだけ変更します。

---

## zim_SetTheme ( viewer ; theme )

```
zim_SetTheme ( viewer ; theme )
```

ビューアのデザインを切り替えて `OK` を返します。`theme` は `rich` または `minimal`。

---

## zim_SetTitle ( viewer ; title )

```
zim_SetTitle ( viewer ; title )
```

ウィンドウタイトルを設定し `OK` を返します。

---

## zim_Focus ( viewer )

```
zim_Focus ( viewer )
```

ビューアウィンドウを最前面に出し、キーボードフォーカスを与えます。`OK` を返します。

---

## zim_SetFullscreen ( viewer ; on )

```
zim_SetFullscreen ( viewer ; on )
```

全画面に入る（`on` = `1`）／抜ける（`on` = `0`）。`OK` を返します。

---

## zim_LoadList ( viewer ; jsonArray {; index} )

```
zim_LoadList ( viewer ; jsonArray )
zim_LoadList ( viewer ; jsonArray ; index )
```

画像パスの**プレイリスト**を読み込み、ユーザー（または `zim_Navigate`）が順に送れるように
します。`jsonArray` はパスの JSON 配列、`index` は 0 始まりの開始位置（既定 `0`）。

結果の状態を JSON で返します:

```json
{ "count": 3, "index": 0, "path": "/Users/me/Pictures/a.jpg" }
```

リストを読み込むと ← → キーで送れるようになり、`rich` テーマではフィルムストリップが出ます。

---

## zim_Navigate ( viewer ; to )

```
zim_Navigate ( viewer ; to )
```

`zim_LoadList` で読み込んだプレイリスト内を移動し、新しい状態を JSON で返します。

| `to` | 意味 |
|---|---|
| `next` | 次の画像。 |
| `prev` | 前の画像。 |
| `first` | 最初の画像。 |
| `last` | 最後の画像。 |
| 数値 | その 0 始まりインデックスへジャンプ。 |

```json
{ "index": 1, "path": "/Users/me/Pictures/b.png", "count": 3 }
```

---

## zim_GetState ( {viewer} )

```
zim_GetState
zim_GetState ( viewer )
zim_GetState ( "*" )
```

ビューアの状態を JSON で返します。ビューアの状態を確実に知る手段であり、イベント
コールバックに頼れない場面での推奨手段です。

```json
{ "open": true, "viewer": "main", "x": 200, "y": 160, "w": 1000, "h": 760,
  "zoom": 1.5, "theme": "rich", "image": "/…/b.png", "index": 1, "count": 3,
  "fullscreen": false }
```

`"*"` を渡すと全ビューアを返します: `{ "viewers": [ <state>, … ] }`

---

## zim_SetScript ( viewer ; file ; script )

```
zim_SetScript ( viewer ; file ; script )
```

ビューアがイベントを発生させたときに実行する FileMaker スクリプトを登録します。`OK` を
返します。`file` は FileMaker のファイル名（通常 `Get(FileName)`）、`script` はその中の
スクリプト名です。

```
Set Variable [ $_ ; zim_SetScript ( "main" ; Get(FileName) ; "OnImageEvent" ) ]
```

スクリプトはイベントをスクリプト引数として受け取ります:

```json
{ "viewer": "main", "event": "navigated", "data": { "index": 1, "path": "/…/b.png", "count": 3 } }
```

### イベント一覧

| `event` | `data` |
|---|---|
| `loaded` | `{path, w, h}` |
| `zoomed` | `{zoom}` |
| `navigated` | `{index, path, count}` |
| `clicked` | `{x, y, imgX, imgY, button}` |
| `closed` | `{}` |
| `dropped` | `{paths[]}` |
| `error` | `{message, path}` |

イベントは**ベストエフォート**です。プラグインの Idle で配送されますが、FileMaker は
すべての文脈で Idle を呼ぶとは限りません（FileMaker Server や一部のモーダル状態）。
取りこぼしが許されない場合は、`OnTimer` スクリプトから `zim_GetState` をポーリングして
ください（[fmp/scripts.md](fmp/scripts.md) §5 参照）。

---

## zim_ExifRead ( image )

```
zim_ExifRead ( image )
```

**コンテナ**の画像から EXIF を読み、JSON で返します。ビューアが起動していなくても
動きます（解析はプラグイン内で完結するため、FileMaker Server でも使えます）。

```json
{
  "hasExif": true,
  "camera":   { "make": "SONY", "model": "ILCE-7M4" },
  "lens":     { "model": "FE 50mm F1.8", "focalLength": 50 },
  "exposure": { "time": 0.005, "fNumber": 2.8, "iso": 400 },
  "dateTime": { "original": "2026:07:05 13:20:11" },
  "image":    { "orientation": 6, "description": "Site survey photo" },
  "gps":      { "latitude": 35.6586, "longitude": 139.7454, "altitude": 12.3 },
  "copyright": "(C) 2026 veltrea"
}
```

- タグが無い項目はキーごと出ません。読む前にキーの有無を確認してください。
- EXIF を持たない画像はエラーではなく `{"hasExif":false}` を返します。
- JPEG でない、または EXIF が壊れている場合は `ERROR: …` を返します。
- `gps` は実際に座標が入っている場合だけ現れます。

```
Set Variable [ $exif ; zim_ExifRead ( Photos::Image ) ]
Set Variable [ $shot ; JSONGetElement ( $exif ; "dateTime.original" ) ]
Set Variable [ $lat  ; JSONGetElement ( $exif ; "gps.latitude" ) ]
```

---

## zim_ExifReadPath ( filePath )

```
zim_ExifReadPath ( filePath )
```

`zim_ExifRead` と同じですが、コンテナではなくディスク上のファイルから読みます。
`zim_LoadList` でファイルを順に送りながら使うときに便利です。

---

## EXIF の書き込みについて

ZooImage は EXIF を**読みますが、書きません**。タグの編集・GPS の付与・メタデータの
除去はこのプラグインの担当ではありません。書き換えが必要な場合は別プラグインの
**ZooEXIF** を使ってください。

これはライセンス上の意図的な判断です。画質を落とさずにメタデータだけ書き換えるには
libexif / libiptcdata が必要ですが、どちらも LGPL（コピーレフト）です。ZooImage は
読み取りに easyexif（BSD-2-Clause）を使い、依存をパーミッシブに保っています。
詳細は [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) を参照してください。

---

## エラー

失敗した関数は `ERROR: <メッセージ>` を返します。メッセージは下記のプロトコルエラーコードに
対応し、`Get(LastExternalErrorDetail)` からも取得できます。

| コード | 名前 | 意味 |
|---|---|---|
| `1` | `auth` | IPC トークンが無い／不正。多くはヘルパーが古い状態。全ビューアを閉じて再試行してください。 |
| `2` | `protocol` | プラグインとヘルパーのプロトコルバージョン不一致。同じビルドのものを入れ直してください。 |
| `3` | `bad_params` | 引数が不正または不足。多くは `optionsJSON` の JSON が壊れている場合です。 |
| `4` | `no_viewer` | 指定したビューアが存在しない（未オープン、またはすでに閉じている）。 |
| `5` | `load_failed` | 画像を開けない／デコードできない。 |
| `6` | `unsupported` | 未知のメソッド。プラグインが同梱ヘルパーより新しい場合に起きます。 |
| `7` | `internal` | ヘルパー側の内部エラー。 |

ワイヤプロトコルは [protocol/protocol.md](protocol/protocol.md)、挙動の詳細は
[SPEC.ja.md](SPEC.ja.md)、コピペ用の FileMaker スクリプトは [fmp/scripts.md](fmp/scripts.md)
を参照してください。
