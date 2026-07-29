<!-- 日本語版: FUNCTIONS.ja.md / Japanese version: FUNCTIONS.ja.md -->

# ZooImage — Function Reference

Image **preview / viewer** for FileMaker. `ZooImage.fmplugin` is a thin IPC client; all
rendering happens in the bundled helper app (`ZooImage.app`), so a heavy image never blocks
the FileMaker calculation engine.

- Plug-in ID: `Zimg` — external function prefix: `zim_` — 15 functions.
- Every function returns its value on success, or the text `ERROR: <message>` on failure.
  The last error is also available from `zim_LastError`.
- `viewer` names a window. Default `main`. Allowed characters: `[A-Za-z0-9_-]`.
  Where noted, `"*"` addresses every open viewer.
- Every `options` argument is a **JSON object**. Omitted or empty means defaults.
- The first `zim_Show` cold-starts the helper (spawn + launch), so it takes a moment.
  Every later call is instant.

---

## zim_Version

```
zim_Version
```

Returns the plug-in version, e.g. `Zimg 0.1.0`. When the helper is running, its version is
appended: `Zimg 0.1.0 / helper 0.1.0`. Doubles as a load probe — if the function is unknown
to FileMaker, the plug-in did not load.

---

## zim_IsRunning

```
zim_IsRunning
```

Returns `1` if the helper process is up and reachable over IPC, otherwise `0`. Does **not**
start the helper.

---

## zim_LastError

```
zim_LastError
```

Returns the most recent error message, or empty if the last call succeeded. Useful after a
function returned `ERROR: …` and you want the detail without re-running the call.

---

## zim_Show ( image {; optionsJSON} )

```
zim_Show ( image )
zim_Show ( image ; optionsJSON )
```

Opens `image` in a viewer window and returns the viewer name (e.g. `main`).

| Argument | Meaning |
|---|---|
| `image` | A **container field** value, **or** a text file path. Containers are extracted to a temp file first. |
| `optionsJSON` | Optional JSON object — see the table below. |

### `optionsJSON` keys (all optional)

| Key | Type | Meaning |
|---|---|---|
| `viewer` | string | Window name. Default `main`. Reusing a name replaces that window's image. |
| `x`, `y`, `w`, `h` | number | Window bounds in logical pixels. |
| `zoom` | number \| string | Initial zoom. Default `fit`. See `zim_SetZoom` for accepted strings. |
| `theme` | string | `rich` (toolbar + filmstrip + info panel) or `minimal` (borderless, keyboard-driven). |
| `title` | string | Window title. |
| `fullscreen` | bool | Open fullscreen. |
| `focus` | bool | Bring the window to the front. |

```
Set Variable [ $v ;
  zim_Show ( Photos::Image ; "{\"theme\":\"rich\",\"zoom\":\"fit\",\"title\":\"Invoice 42\"}" )
]
```

Returns `ERROR:` when the image cannot be opened or decoded (`load_failed`), or when the
options JSON is malformed (`bad_params`).

---

## zim_Close ( {viewer} )

```
zim_Close
zim_Close ( viewer )
zim_Close ( "*" )
```

Closes a viewer window and returns `OK`. With no argument, closes `main`. With `"*"`,
closes every open viewer. Closing an already-closed viewer is not an error.

---

## zim_SetZoom ( viewer ; zoom )

```
zim_SetZoom ( viewer ; zoom )
```

Sets the zoom level and returns `OK`.

| `zoom` | Meaning |
|---|---|
| number | Absolute scale — `1` = 100 %, `2` = 200 %, `0.5` = 50 %. |
| `fit` | Fit the whole image inside the window. |
| `fill` | Fill the window, cropping the overflow. |
| `actual` | 100 %, one image pixel per logical pixel. |
| `in` | One step in from the current level. |
| `out` | One step out from the current level. |

The user can also zoom with the mouse wheel and pan by dragging; `zim_GetState` reports the
resulting level.

---

## zim_SetWindow ( viewer ; x ; y ; w ; h )

```
zim_SetWindow ( viewer ; x ; y ; w ; h )
```

Moves and resizes the window, in logical pixels. Returns `OK`.

**Any argument may be left empty to keep the current value** — `zim_SetWindow ( "main" ; "" ; "" ; 1200 ; 900 )`
resizes without moving.

---

## zim_SetTheme ( viewer ; theme )

```
zim_SetTheme ( viewer ; theme )
```

Switches the viewer design and returns `OK`. `theme` is `rich` or `minimal`.

---

## zim_SetTitle ( viewer ; title )

```
zim_SetTitle ( viewer ; title )
```

Sets the window title and returns `OK`.

---

## zim_Focus ( viewer )

```
zim_Focus ( viewer )
```

Brings the viewer window to the front and gives it keyboard focus. Returns `OK`.

---

## zim_SetFullscreen ( viewer ; on )

```
zim_SetFullscreen ( viewer ; on )
```

Enters (`on` = `1`) or leaves (`on` = `0`) fullscreen. Returns `OK`.

---

## zim_LoadList ( viewer ; jsonArray {; index} )

```
zim_LoadList ( viewer ; jsonArray )
zim_LoadList ( viewer ; jsonArray ; index )
```

Loads a **playlist** of image paths so the user (or `zim_Navigate`) can page through them.
`jsonArray` is a JSON array of paths; `index` is the 0-based starting position (default `0`).

Returns the resulting state as JSON:

```json
{ "count": 3, "index": 0, "path": "/Users/me/Pictures/a.jpg" }
```

Once a list is loaded, the ← and → keys navigate it, and the `rich` theme shows a filmstrip.

---

## zim_Navigate ( viewer ; to )

```
zim_Navigate ( viewer ; to )
```

Moves within the playlist loaded by `zim_LoadList` and returns the new state as JSON.

| `to` | Meaning |
|---|---|
| `next` | Next image. |
| `prev` | Previous image. |
| `first` | First image. |
| `last` | Last image. |
| number | Jump to that 0-based index. |

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

Returns the viewer state as JSON — the reliable way to observe the viewer, and the
recommended fallback when you cannot rely on event callbacks.

```json
{ "open": true, "viewer": "main", "x": 200, "y": 160, "w": 1000, "h": 760,
  "zoom": 1.5, "theme": "rich", "image": "/…/b.png", "index": 1, "count": 3,
  "fullscreen": false }
```

With `"*"`, returns every viewer: `{ "viewers": [ <state>, … ] }`.

---

## zim_SetScript ( viewer ; file ; script )

```
zim_SetScript ( viewer ; file ; script )
```

Registers the FileMaker script that runs when the viewer raises an event. Returns `OK`.
`file` is a FileMaker file name (usually `Get(FileName)`), `script` a script name in it.

```
Set Variable [ $_ ; zim_SetScript ( "main" ; Get(FileName) ; "OnImageEvent" ) ]
```

The script receives the event as its script parameter:

```json
{ "viewer": "main", "event": "navigated", "data": { "index": 1, "path": "/…/b.png", "count": 3 } }
```

### Events

| `event` | `data` |
|---|---|
| `loaded` | `{path, w, h}` |
| `zoomed` | `{zoom}` |
| `navigated` | `{index, path, count}` |
| `clicked` | `{x, y, imgX, imgY, button}` |
| `closed` | `{}` |
| `dropped` | `{paths[]}` |
| `error` | `{message, path}` |

Events are **best-effort**: they are delivered on the plug-in's Idle, which FileMaker does not
guarantee to call in every context (FileMaker Server, some modal states). When you must not
miss a change, poll `zim_GetState` from an `OnTimer` script instead — see
[fmp/scripts.md](fmp/scripts.md) §5.

---

## zim_ExifRead ( image )

```
zim_ExifRead ( image )
```

Reads the EXIF of a **container** image and returns it as JSON. Works without the
viewer running — the parsing happens inside the plug-in, so it is also usable on
FileMaker Server.

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

- Keys are omitted when the tag is absent, so test for a key before reading it.
- An image **without** EXIF is not an error — it returns `{"hasExif":false}`.
- A non-JPEG or a corrupt EXIF block returns `ERROR: …`.
- `gps` appears only when the file actually carries coordinates.

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

Same as `zim_ExifRead`, but reads from a file on disk instead of a container.
Useful together with `zim_LoadList` when you are paging through files.

---

## A note on writing EXIF

ZooImage **reads** EXIF but does not write it. Editing tags, setting GPS, or
stripping metadata are not part of this plug-in — use the separate **ZooEXIF**
plug-in for that.

This is a deliberate licensing decision: writing metadata without re-encoding the
image requires libexif / libiptcdata, both of which are LGPL (copyleft). ZooImage
keeps its dependencies permissive by reading with easyexif (BSD-2-Clause). See
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

---

## Errors

A failing function returns `ERROR: <message>`. The message maps onto the protocol error codes
below, which are also surfaced through `Get(LastExternalErrorDetail)`.

| Code | Name | Meaning |
|---|---|---|
| `1` | `auth` | Missing or invalid IPC token. Usually means a stale helper — close every viewer and retry. |
| `2` | `protocol` | Plug-in and helper disagree on the protocol version. Reinstall so both come from the same build. |
| `3` | `bad_params` | Malformed or missing arguments — most often invalid `optionsJSON`. |
| `4` | `no_viewer` | The named viewer does not exist (never opened, or already closed). |
| `5` | `load_failed` | The image could not be opened or decoded. |
| `6` | `unsupported` | Unknown method — plug-in newer than the bundled helper. |
| `7` | `internal` | Helper-side failure. |

See [protocol/protocol.md](protocol/protocol.md) for the wire protocol, [SPEC.md](SPEC.md)
for behavioral detail, and [fmp/scripts.md](fmp/scripts.md) for copy-pasteable FileMaker
scripts.
