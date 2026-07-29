<!-- 日本語版: README.ja.md / Japanese version: README.ja.md -->

# ZooImage — image preview / viewer for FileMaker

A FileMaker image **preview / viewer** built as a thin **C++ plug-in (`ZooImage.fmplugin`)** that
drives a standalone **Tauri helper app (`ZooImage.app`)**. Pass a container value or a file path from
FileMaker and it opens in an independent, **zoomable / pannable** window that the plug-in
controls — window position, size, zoom, theme, playlist navigation, fullscreen. The helper
also works as a normal image viewer on its own (drag-and-drop, File ▸ Open, keyboard).

- Plug-in ID: `Zimg` — external function prefix: `zim_`
- Helper: Tauri (Rust + Web UI). macOS first; Windows planned.
- Two selectable designs: **Rich** (toolbar, filmstrip, info panel) and **Minimal** (borderless, keyboard-driven).

## Why a plug-in **plus** a helper?

FileMaker external functions run **synchronously** on the calculation engine — heavy image
rendering there would freeze the UI. So `ZooImage` stays a thin IPC client and hands all display
work to an always-on helper over `127.0.0.1` (JSONL, token-authenticated). The plug-in spawns
the helper on first use; commands flow plug-in → helper, and user events (closed, navigated,
clicked, …) flow back helper → plug-in → a FileMaker script via `FMX_StartScript`.

```
FileMaker Pro ──(calc)── ZooImage.fmplugin ──JSONL/TCP 127.0.0.1──▶ ZooImage helper (Tauri)
                              ▲                                      │ viewer windows
                              └──── events (Idle → StartScript) ─────┘ zoom · pan · themes
```

## Features

- Show a **container** or a **file path**: `zim_Show( image ; optionsJSON )`
- Zoom (`fit` / `fill` / `actual` / `in` / `out` / numeric), wheel-zoom, drag-pan
- Control window bounds, title, fullscreen, focus, theme from FileMaker
- **Playlist** navigation (`zim_LoadList` + `zim_Navigate`) with keyboard ← →
- **Event callbacks** to FileMaker scripts, plus an `OnTimer` + `zim_GetState` polling fallback
- Multiple simultaneous viewers, addressed by name (default `main`)
- Standalone viewer mode (usable without FileMaker)
- **Read EXIF** — camera, lens, exposure, capture time and GPS as JSON
  (`zim_ExifRead` / `zim_ExifReadPath`). Works without the viewer, so it also runs
  on FileMaker Server. Reading only; use the ZooEXIF plug-in to edit metadata.

See [FUNCTIONS.md](FUNCTIONS.md) for the per-function reference, [SPEC.md](SPEC.md) for behavior
and conventions, [protocol/protocol.md](protocol/protocol.md) for the wire protocol, and
[fmp/scripts.md](fmp/scripts.md) for copy-pasteable FileMaker scripts.

## Install (end users)

Distribute `dist/ZooImage.fmplugin.gz` inside a container field and install it with the
**Install Plug-In File** script step (no quarantine, no admin rights). See
[fmp/scripts.md](fmp/scripts.md) §1. First load on macOS prompts once to trust the (ad-hoc
signed) plug-in.

## Build from source

Requirements: Rust + `cargo`, Node + `pnpm`, CMake, Xcode command-line tools, the FileMaker
Plug-in SDK (default location `../PlugInSDK`).

```bash
scripts/build-helper.sh          # → "ZooImage.app"   (add 'universal' for arm64+x86_64)
scripts/build-plugin.sh          # → plugin/build/ZooImage.fmplugin
scripts/bundle.sh                # embed helper into the plug-in, then ad-hoc deep sign
scripts/package-container.sh     # → dist/ZooImage.fmplugin.gz  (for container install)
```

Helper unit tests (pure zoom/pan + playlist logic, no DOM): `cd helper && pnpm test`.
Drive the helper without FileMaker: start it, then `node scripts/ipc-test.mjs <image>`.

## Repository layout

| Path | What |
|---|---|
| `plugin/` | C++ FMX plug-in (`ZooImage`) — entrypoint, IPC client, launcher, container extraction |
| `helper/` | Tauri app "ZooImage" — Rust IPC server + TS viewer frontend |
| `protocol/` | IPC protocol spec (single source of truth) |
| `scripts/` | build / sign / package / IPC-test scripts |
| `fmp/` | sample FileMaker scripts |

## Status

macOS helper + plug-in build and pass an end-to-end IPC test. Windows support, richer
standalone polish, and a sample `.fmp12` are in progress.
