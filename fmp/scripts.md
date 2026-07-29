# ZooImage — FileMaker sample scripts / サンプルスクリプト

Copy-pasteable examples for driving the ZooImage viewer from FileMaker.
ZooImage ビューアを FileMaker から動かすためのコピペ用サンプル。

All `zim_` functions return their value on success, or the text `ERROR: <message>` on
failure (also retrievable via `zim_LastError`).
すべての `zim_` 関数は成功時に値を、失敗時に `ERROR: <message>` を返す（`zim_LastError` でも取得可）。

---

## 0. Prerequisite — `fmplugin` extended privilege / 前提: 拡張アクセス権

Event callbacks use `FMX_StartScript`, which on FileMaker 19.2+ requires the **`fmplugin`
extended privilege** to be enabled in the file's privilege set (otherwise error 825).
イベントコールバックは `FMX_StartScript` を使うため、FileMaker 19.2 以降ではファイルの
アクセス権セットで **`fmplugin` 拡張アクセス権**を有効にする必要がある（無効だとエラー 825）。

`File ▸ Manage ▸ Security ▸ (privilege set) ▸ Extended Privileges ▸ enable fmplugin`

---

## 1. Install the plug-in from a container / コンテナからインストール

Store `ZooImage.fmplugin.gz` (from `scripts/package-container.sh`) in a container field, then:

```
# Script: Install ZooImage
Set Error Capture [ Off ]          # 初回は Off（未署名確認ダイアログを出すため）
Install Plug-In File [ Globals::PluginContainer ]
Show Custom Dialog [ "Installed" ; zim_Version ]
```

macOS の Pro クライアントは初回に「このプラグインを信頼するか」を確認する。以後は
`Get(InstalledFMPluginsAsJSON)` でバージョン確認できる。

---

## 2. Show an image / 画像を表示

Container or path — `zim_Show` accepts either. コンテナでもパスでもよい。

```
# コンテナを表示（リッチテーマ・全体表示）
Set Variable [ $r ; Value:
  zim_Show ( Photos::Image ; "{\"theme\":\"rich\",\"zoom\":\"fit\",\"title\":\"" & Photos::Name & "\"}" )
]

# ファイルパスを表示（ミニマルテーマ・位置とサイズ指定）
Set Variable [ $r ; Value:
  zim_Show ( "/Users/me/Pictures/cat.jpg" ;
            "{\"viewer\":\"preview\",\"theme\":\"minimal\",\"x\":200,\"y\":120,\"w\":1000,\"h\":800}" )
]
```

Options (all optional): `viewer, x, y, w, h, zoom, theme, title, fullscreen, focus`.

---

## 3. Live control / ライブ制御

```
Set Variable [ $_ ; zim_SetZoom ( "main" ; 2 ) ]          # 200%
Set Variable [ $_ ; zim_SetZoom ( "main" ; "fit" ) ]       # 全体
Set Variable [ $_ ; zim_SetZoom ( "main" ; "in" ) ]        # 1 段拡大
Set Variable [ $_ ; zim_SetWindow ( "main" ; 300 ; 200 ; 1200 ; 900 ) ]
Set Variable [ $_ ; zim_SetTheme ( "main" ; "minimal" ) ]
Set Variable [ $_ ; zim_SetTitle ( "main" ; "Invoice 42" ) ]
Set Variable [ $_ ; zim_SetFullscreen ( "main" ; 1 ) ]
Set Variable [ $_ ; zim_Focus ( "main" ) ]
Set Variable [ $state ; zim_GetState ( "main" ) ]          # JSON
Set Variable [ $_ ; zim_Close ( "main" ) ]                 # or zim_Close("*")
```

---

## 4. Playlist / プレイリスト（送り）

```
# パス配列を JSON でセットして送りを有効化
Set Variable [ $list ; Value: "[\"/img/a.jpg\",\"/img/b.png\",\"/img/c.gif\"]" ]
Set Variable [ $_ ; zim_LoadList ( "main" ; $list ; 0 ) ]
Set Variable [ $_ ; zim_Navigate ( "main" ; "next" ) ]     # next/prev/first/last/<index>
Set Variable [ $_ ; zim_Navigate ( "main" ; 2 ) ]
```

ユーザーがビューア上で ← → キーを押した送りも、下記コールバックで受け取れる。

---

## 5. Event callbacks + polling fallback / イベントと OnTimer ポーリング

Register a script to run on viewer events (closed / navigated / clicked / zoomed / loaded /
error / dropped). ビューアのイベントで起動するスクリプトを登録する。

```
# 表示前にコールバックを登録
Set Variable [ $_ ; zim_SetScript ( "main" ; Get(FileName) ; "OnZiEvent" ) ]
Set Variable [ $_ ; zim_Show ( Photos::Image ) ]
```

```
# Script: OnZiEvent  （引数 = イベント JSON）
Set Variable [ $ev  ; Value: Get ( ScriptParameter ) ]
Set Variable [ $type ; Value: JSONGetElement ( $ev ; "event" ) ]
Set Variable [ $vwr  ; Value: JSONGetElement ( $ev ; "viewer" ) ]
If [ $type = "navigated" ]
    Set Variable [ $path ; JSONGetElement ( $ev ; "data.path" ) ]
    # … 表示中レコードを $path に同期する等 …
Else If [ $type = "clicked" ]
    Set Variable [ $x ; JSONGetElement ( $ev ; "data.imgX" ) ]
    Set Variable [ $y ; JSONGetElement ( $ev ; "data.imgY" ) ]
Else If [ $type = "closed" ]
    # ビューアが閉じられた
End If
```

**Polling fallback / ポーリング代替**: environments where StartScript is unreliable
(Server 等) や、通知を取りこぼしたくない場合は、レイアウトの OnTimer で `zim_GetState` を
ポーリングして差分を見る。

```
# Layout OnTimer script（例: 0.5s 間隔）
Set Variable [ $s ; zim_GetState ( "main" ) ]
If [ JSONGetElement ( $s ; "open" ) ≠ True ]
    Install OnTimer Script [ ]          # ビューアが閉じたらタイマー解除
End If
```

---

## 6. Notes / 補足

- First `zim_Show` cold-starts the helper (spawn + launch); subsequent calls are instant.
  最初の `zim_Show` はヘルパーを起動するため一瞬待つ。以後は即時。
- `zim_IsRunning` → `1` if the helper is up. `zim_Version` includes the helper version when running.
- Multiple viewers: pass a distinct `viewer` name (default `main`). 複数ビューアは `viewer` 名で分ける。
