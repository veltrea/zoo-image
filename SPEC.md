<!-- 日本語版: SPEC.ja.md / Japanese version: SPEC.ja.md -->

# ZooImage — Specification

Function reference and behavior for the `ZooImage` FileMaker plug-in. The IPC wire protocol between
the plug-in and the ZooImage helper is in [protocol/protocol.md](protocol/protocol.md);
copy-pasteable FileMaker scripts are in [fmp/scripts.md](fmp/scripts.md).

## Conventions

- Every `zim_` function returns its value on success, or the text `ERROR: <message>` on failure.
  The last error is also available via `zim_LastError`.
- `viewer` names a window; default `main`; `"*"` means all (where noted). Allowed chars: `[A-Za-z0-9_-]`.
- Empty arguments are treated as "leave unchanged" for `zim_SetWindow`.
- The first `zim_Show` cold-starts the bundled helper (spawn + launch); later calls are instant.

## `zim_` function reference

| Function | Args | Returns |
|---|---|---|
| `zim_Version` | — | `Zimg x.y.z` (+ ` / helper a.b.c` if running) |
| `zim_IsRunning` | — | `1` if the helper is up, else `0` |
| `zim_LastError` | — | last error message |
| `zim_Show( image ; options )` | `image` = container **or** path; `options` = JSON (optional) | viewer name |
| `zim_Close( viewer )` | `viewer` (optional, or `"*"`) | `OK` |
| `zim_SetZoom( viewer ; zoom )` | `zoom` = number or `fit`/`fill`/`actual`/`in`/`out` | `OK` |
| `zim_SetWindow( viewer ; x ; y ; w ; h )` | logical px; any may be empty | `OK` |
| `zim_SetTheme( viewer ; theme )` | `rich` \| `minimal` | `OK` |
| `zim_SetTitle( viewer ; title )` | text | `OK` |
| `zim_Focus( viewer )` | — | `OK` |
| `zim_SetFullscreen( viewer ; on )` | `on` = 0/1 | `OK` |
| `zim_LoadList( viewer ; jsonArray ; index )` | array of paths + start index | `{count,index,path}` JSON |
| `zim_Navigate( viewer ; to )` | `next`/`prev`/`first`/`last`/`<index>` | `{index,path,count}` JSON |
| `zim_GetState( viewer )` | `viewer` (optional, or `"*"`) | state JSON |
| `zim_SetScript( viewer ; file ; script )` | FileMaker file + script names | `OK` |

## `zim_Show` options (JSON object, all optional)

| Key | Type | Meaning |
|---|---|---|
| `viewer` | string | window name (default `main`) |
| `x`, `y`, `w`, `h` | number | logical-pixel bounds |
| `zoom` | number \| string | initial zoom (default `fit`) |
| `theme` | string | `rich` \| `minimal` |
| `title` | string | window title |
| `fullscreen` | bool | open fullscreen |
| `focus` | bool | bring to front |

## `zim_GetState` result

```json
{ "open": true, "viewer": "main", "x": 200, "y": 160, "w": 1000, "h": 760,
  "zoom": 1.5, "theme": "rich", "image": "/…/b.png", "index": 1, "count": 3, "fullscreen": false }
```

`zim_GetState("*")` returns `{ "viewers": [ <state>, … ] }`.

## Events (via `zim_SetScript`)

The registered FileMaker script runs with the event JSON as its parameter. `event` is one of:

| `event` | `data` |
|---|---|
| `loaded` | `{path, w, h}` |
| `zoomed` | `{zoom}` |
| `navigated` | `{index, path, count}` |
| `clicked` | `{x, y, imgX, imgY, button}` |
| `closed` | `{}` |
| `dropped` | `{paths[]}` |
| `error` | `{message, path}` |

Events are best-effort (fired on the plug-in's Idle). For guaranteed observation, poll
`zim_GetState` from an `OnTimer` script. See [fmp/scripts.md](fmp/scripts.md) §5.

## Errors

`ERROR:` messages map to protocol error codes (`auth`, `bad_params`, `no_viewer`, `load_failed`,
`unsupported`, `internal`) — see [protocol/protocol.md](protocol/protocol.md) §10.
