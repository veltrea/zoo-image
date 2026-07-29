<!-- 日本語版: CHANGELOG.ja.md / Japanese version: CHANGELOG.ja.md -->

# Changelog

All notable changes to this project are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/); this project uses semantic versioning.

## [0.1.0] — 2026-07-29

First release. macOS only.

### Added
- **Helper "ZooImage"** (Tauri v2, Rust + TS): localhost JSONL IPC server (token-authenticated,
  ephemeral port, `daemon.json` discovery), multi-window viewer, rich + minimal themes,
  zoom/pan engine (pure, unit-tested), playlist navigation with filmstrip, drag-and-drop,
  File ▸ Open, macOS file associations, standalone mode, and event push to subscribers.
- **Plug-in `ZooImage`** (C++ FMX): thin IPC client with 15 `zim_` functions (`zim_Show`, `zim_SetZoom`,
  `zim_SetWindow`, `zim_SetTheme`, `zim_LoadList`, `zim_Navigate`, `zim_GetState`, `zim_SetScript`, …),
  container→temp-file extraction, helper spawn/launch management, and an event-listener thread
  that delivers viewer events to FileMaker scripts via `FMX_StartScript` on Idle.
- IPC protocol spec (`protocol/protocol.md`), build/sign/package scripts, an IPC test client,
  sample FileMaker scripts, and bilingual docs.
- Per-function reference ([FUNCTIONS.md](FUNCTIONS.md) / [FUNCTIONS.ja.md](FUNCTIONS.ja.md)),
  BSD-3-Clause [LICENSE](LICENSE), and [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
- `scripts/verify-all.sh` — one command that runs unit tests, builds helper + plug-in, bundles
  and signs, checks naming and signatures, packages, and runs the end-to-end test.
- `scripts/e2e-test.mjs` — spawns the bundled helper exactly as the plug-in does and verifies
  the IPC conversation (18 checks), with no FileMaker required.
- Distribution: ad-hoc signed `ZooImage.fmplugin` with the helper embedded, plus a
  container-installable `ZooImage.fmplugin.gz`.

### Fixed
- Code signing is now performed inside-out (helper `.app` first, then the plug-in bundle).
  Tauri's build-time ad-hoc signature leaves resources unsealed, and signing only the parent
  with `--deep --force` did not replace it, so `codesign --verify` failed with
  "code has no resources but signature indicates they must be present".

### Verified
- `scripts/verify-all.sh` passes every check: unit tests, universal (arm64 + x86_64) build,
  naming (Plug-in ID `Zimg`, 15 `zim_` functions, bundle identifiers), signature validity and
  sealed resources for both the plug-in and the embedded helper, packaging, and end-to-end IPC.
- End-to-end IPC (`hello` → `show` → `getState` → `setZoom` → `close` → `shutdown`) against the
  helper actually spawned out of the built plug-in bundle — no FileMaker required.

### Known limitations / next
- macOS only so far (Windows `.fmx64` / Linux `.fmx` planned).
- Info panel shows filename/dimensions/zoom; file size + EXIF are future enhancements.
- Loading inside FileMaker Pro, and event delivery via `FMX_StartScript`, are not yet verified
  on a real FileMaker installation — the end-to-end test covers the IPC layer only.
