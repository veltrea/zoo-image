# ZooImage — Third-party licenses / 第三者ライセンス

**Every third-party component below is permissively licensed. The only obligation is
attribution — there are no copyleft obligations.** This file, together with `LICENSE`,
satisfies that obligation.

**以下の第三者部品はすべてパーミッシブライセンスです。義務は帰属表示のみで、コピーレフト
義務はありません。** このファイルと `LICENSE` でその義務を満たしています。

ZooImage's own code is BSD-3-Clause, `Copyright (c) 2026 veltrea` — see [LICENSE](LICENSE).
ZooImage 自身のコードは BSD-3-Clause（`Copyright (c) 2026 veltrea`）です。

---

## FileMaker Plug-In API (FMWrapper)

- **Used by**: `ZooImage.fmplugin` (headers + weak-linked framework)
- **License**: BSD-style license printed at the top of each header file
- **Copyright**: Claris International Inc.
- **Upstream**: Claris FileMaker Plug-In SDK
- **Bundled?**: **No.** Not redistributed in this repository. Obtain the SDK from Claris and
  point the build at it with `-DFM_SDK_DIR=<PlugInSDK>`.
- 本リポジトリには**同梱していません**。Claris の FileMaker Plug-In SDK から入手してください。

The FMWrapper license permits redistribution provided the copyright notice is retained.
These files are not covered by the ZooImage license.

---

## nlohmann/json

- **Used by**: `ZooImage.fmplugin` — JSON parsing / serialization for IPC and options
- **Version bundled**: 3.11.3
- **License**: MIT
- **Copyright**: Copyright (c) 2013-2022 Niels Lohmann
- **Upstream**: https://github.com/nlohmann/json
- **Full text**: `plugin/third_party/nlohmann/json.hpp` (the MIT text is embedded in the
  header's leading comment block)

---

## easyexif

- **Used by**: `ZooImage.fmplugin` — EXIF reading for `zim_ExifRead` / `zim_ExifReadPath`
- **Version bundled**: upstream `master` (2 source files, vendored 2026-07-30)
- **License**: BSD 2-Clause
- **Copyright**: Copyright (c) 2010-2016 Mayank Lahiri
- **Upstream**: https://github.com/mayanklahiri/easyexif
- **Full text**: in the leading comment block of `plugin/third_party/easyexif/exif.h`

### Why not libexif / なぜ libexif ではないのか

An earlier revision read EXIF with **libexif (LGPL-2.1)** and **libiptcdata
(LGPL-2.0)**, statically linked. Both are copyleft: linking them statically
obliges the distributor to give recipients a way to relink against a modified
version of the library (LGPL-2.1 §6). To keep the whole distribution
permissive and attribution-only, they were removed and replaced with easyexif.

The trade-off is deliberate: **ZooImage reads EXIF but does not write it.**
Writing metadata without re-encoding needs the heavier libexif/libiptcdata
machinery. If you need to edit EXIF/IPTC/XMP, use the separate ZooEXIF plug-in.

以前の版は **libexif (LGPL-2.1)** と **libiptcdata (LGPL-2.0)** を静的リンクして
EXIF を読んでいました。どちらもコピーレフトで、静的リンクすると受領者がライブラリを
差し替えて再リンクできる手段を用意する義務（LGPL-2.1 §6）が生じます。配布物全体を
パーミッシブ（義務は帰属表示のみ）に保つため、これらを外して easyexif に置き換えました。

代償は意図的なものです。**ZooImage は EXIF を読みますが、書きません。** 画質を落とさずに
メタデータだけ書き換えるには libexif/libiptcdata 相当の仕組みが要ります。編集が必要な
場合は別プラグインの ZooEXIF を使ってください。

---

## Tauri

- **Used by**: `ZooImage.app` (the bundled helper) — application framework, windowing, WebView
- **Version bundled**: 2.11.5 (`tauri`), with `tauri-plugin-opener` 2.5.4 and
  `tauri-plugin-dialog` 2.7.1
- **License**: MIT **or** Apache-2.0 (dual-licensed; **MIT selected**)
- **Copyright**: Copyright (c) 2019-2024 Tauri Programme within The Commons Conservancy
- **Upstream**: https://github.com/tauri-apps/tauri

Dual-licensed components are taken under the permissive MIT option, consistent with the
project-wide policy of avoiding copyleft obligations.

---

## Rust crates linked into the helper

All are dual MIT / Apache-2.0 unless noted; **MIT is selected** in each case.

| Crate | Version | License | Copyright / Upstream |
|---|---|---|---|
| `serde` | 1.0.228 | MIT or Apache-2.0 | Erick Tryzelaar, David Tolnay — https://serde.rs |
| `serde_json` | 1.0.150 | MIT or Apache-2.0 | Erick Tryzelaar, David Tolnay — https://github.com/serde-rs/json |
| `tokio` | 1.52.3 | MIT | Tokio Contributors — https://github.com/tokio-rs/tokio |
| `rand` | 0.8.6 | MIT or Apache-2.0 | The Rand Project Developers — https://github.com/rust-random/rand |
| `dirs` | 5.0.1 | MIT or Apache-2.0 | Simon Ochsenreither — https://github.com/dirs-dev/dirs-rs |
| `imagesize` | 0.13.0 | MIT | Aloys Baillet / contributors — https://github.com/Roughsketch/imagesize |

These are the **direct** dependencies. Each pulls in further transitive crates; the complete,
authoritative list with exact versions is `helper/src-tauri/Cargo.lock`. All transitive
dependencies in that lockfile are permissive (MIT / Apache-2.0 / BSD / ISC / Zlib) — none are
copyleft.

直接依存のみを列挙しています。推移的依存を含む完全なリストは `helper/src-tauri/Cargo.lock`
が正本です。ロックファイル中の推移的依存もすべてパーミッシブ（MIT / Apache-2.0 / BSD /
ISC / Zlib）で、コピーレフトのものはありません。

---

## JavaScript / TypeScript packages

The helper's frontend is built with these. Build-time tooling (`devDependencies`) is **not**
shipped inside `ZooImage.app`; only the runtime `@tauri-apps/*` API shims are.

| Package | License | Upstream |
|---|---|---|
| `@tauri-apps/api` | MIT or Apache-2.0 (**MIT selected**) | https://github.com/tauri-apps/tauri |
| `@tauri-apps/plugin-dialog` | MIT or Apache-2.0 (**MIT selected**) | https://github.com/tauri-apps/plugins-workspace |
| `@tauri-apps/plugin-opener` | MIT or Apache-2.0 (**MIT selected**) | https://github.com/tauri-apps/plugins-workspace |
| `vite` (build only) | MIT | https://github.com/vitejs/vite |
| `typescript` (build only) | Apache-2.0 | https://github.com/microsoft/TypeScript |
| `tsx` (build only) | MIT | https://github.com/privatenumber/tsx |

Exact resolved versions are in `helper/pnpm-lock.yaml`.
正確な解決済みバージョンは `helper/pnpm-lock.yaml` にあります。

---

## Notes on dual-licensed components / デュアルライセンスの選択について

Where a component offers a choice (typically MIT **or** Apache-2.0), **the MIT option is
selected**, keeping the whole distribution permissive and attribution-only.

選択制のライセンス（多くは MIT **または** Apache-2.0）については、**MIT 側を選択**しています。
配布物全体をパーミッシブ（義務は帰属表示のみ）に保つためです。

## Image decoding / 画像のデコードについて

ZooImage does not vendor an image codec. Decoding is performed by the operating system's
WebView (WebKit on macOS), so the supported formats and their codec licenses are the
platform's, not this project's.

ZooImage は画像コーデックを同梱していません。デコードは OS の WebView（macOS では WebKit）が
行うため、対応形式とそのコーデックのライセンスはプラットフォーム側のものです。
