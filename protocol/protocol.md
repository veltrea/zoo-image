# ZooImage plug-in ↔ ZooImage helper — IPC Protocol

Single source of truth for the wire protocol between the **`ZooImage` plug-in** (client) and the
**ZooImage helper** (server). Both the C++ and Rust sides implement exactly this document.

Protocol version: **1**

---

## 1. Transport

- **TCP over `127.0.0.1`** (loopback only — never bind `0.0.0.0`).
- Helper listens on an **ephemeral port** (`127.0.0.1:0`, OS-assigned).
- **JSONL framing**: every message is a single UTF-8 JSON value followed by `\n` (`0x0A`).
  - One line = one message. No `Content-Length`, no other framing headers.
  - Messages must not contain a raw newline (JSON escapes `\n` inside strings, so this is automatic).
- A connection is full-duplex. A client may pipeline requests; responses are correlated by `id`.

## 2. Discovery — the port file

On startup the helper writes a **port file** atomically (write temp + `rename`), mode `0600`:

- macOS: `~/Library/Application Support/ZooImage/daemon.json`
- Windows (later): `%LOCALAPPDATA%\ZooImage\daemon.json`

```json
{
  "port": 52341,
  "token": "9f2c…64 hex chars…",
  "pid": 4123,
  "version": "0.1.0",
  "protocol": 1
}
```

Client connect algorithm:

1. Read the port file. If present, `flock` it shared, connect to `port`, send `hello` with `token`.
2. If the file is missing, the connection is refused, or `pid` is dead → **spawn** the helper
   (see §7), wait (bounded, e.g. 5 s with backoff) for the port file to appear, then connect.
3. If `hello` fails auth or protocol mismatch → treat as not-running and re-spawn a fresh helper.

The client keeps a short **lock discipline**: hold an exclusive `flock` only around the
spawn decision so two FileMaker instances don't both spawn.

## 3. Authentication

- The helper generates a **32-byte random token** at startup, hex-encoded (64 chars), stored
  in the port file (mode `0600`).
- The **first message on every connection MUST be `hello`** carrying the token.
- Any other first message, or a wrong token, → the helper replies with an `auth` error and
  closes the connection.

## 4. Message envelope

### Request (client → helper)

```json
{ "id": "<uuid-v4>", "method": "<name>", "params": { … } }
```

- `id`: client-generated, unique per in-flight request. Echoed in the response.
- `method`: one of §6.
- `params`: object; may be omitted when empty.

### Response (helper → client)

```json
{ "id": "<same id>", "ok": true,  "result": { … } }
{ "id": "<same id>", "ok": false, "error": { "code": <int>, "message": "<text>" } }
```

### Event (helper → client, unsolicited)

Only sent on a connection that has issued `subscribe` (§6.14). Has **no `id`**; distinguished
by the `event` key:

```json
{ "event": "<type>", "viewer": "<name>", "file": "<fmFile>", "script": "<fmScript>", "data": { … } }
```

`file`/`script` are the values last set via `setScript` for that viewer (absent if none). See §8.

## 5. Common types

- **viewer**: a string name identifying a viewer window. Default `"main"`. `"*"` means
  "all viewers" where noted.
- **zoom**: either a number (`1.0` = 100 %, `2.0` = 200 %) **or** one of the strings
  `"fit"` (fit whole image), `"fill"` (cover window), `"actual"` / `"1:1"` (100 %),
  `"in"` (one step in), `"out"` (one step out).
- **theme**: `"rich"` | `"minimal"`.
- **image source**: a **filesystem path** string. Container bytes are written to a temp file
  by the plug-in first (see §9); the protocol only ever carries paths, never image bytes.
- **bounds fields** (`x`, `y`, `w`, `h`): integers in **logical screen pixels**. Any field may
  be `null`/omitted to leave that dimension unchanged.

## 6. Methods

### 6.1 `hello`
Auth + capability handshake. Must be first on a connection.
```json
params: { "token": "<hex>", "protocol": 1, "client": "ZooImage-plugin/0.1.0" }
result: { "protocol": 1, "version": "0.1.0", "name": "ZooImage" }
```
Errors: `auth` (bad token), `protocol` (version mismatch).

### 6.2 `show`
Create the viewer if needed, load `path`, apply `options`. Idempotent per `viewer`.
```json
params: {
  "viewer": "main",
  "path": "/abs/path/to/image.png",
  "options": {
    "x": 100, "y": 100, "w": 1000, "h": 800,
    "zoom": "fit", "theme": "rich", "title": "Invoice 42",
    "monitor": 0, "fullscreen": false, "focus": true
  }
}
result: { "viewer": "main", "w_img": 3024, "h_img": 4032 }
```
`options` and every field within it are optional. Missing window fields → helper default /
existing value. `path` may be empty to just (re)configure an existing viewer.

### 6.3 `close`
```json
params: { "viewer": "main" }     // or { "viewer": "*" }
result: { "closed": ["main"] }
```

### 6.4 `setZoom`
```json
params: { "viewer": "main", "zoom": 1.5 }   // or "fit"/"fill"/"actual"/"in"/"out"
result: { "zoom": 1.5 }                       // resolved numeric zoom after applying
```

### 6.5 `setWindow`
```json
params: { "viewer": "main", "x": 200, "y": 150, "w": 1200, "h": 900 }  // any subset
result: { "x": 200, "y": 150, "w": 1200, "h": 900 }
```

### 6.6 `setTheme`
```json
params: { "viewer": "main", "theme": "minimal" }
result: { "theme": "minimal" }
```

### 6.7 `setTitle`
```json
params: { "viewer": "main", "title": "Invoice 42" }
result: { "title": "Invoice 42" }
```

### 6.8 `focus`
```json
params: { "viewer": "main" }
result: { "focused": true }
```

### 6.9 `setFullscreen`
```json
params: { "viewer": "main", "on": true }
result: { "fullscreen": true }
```

### 6.10 `loadList`
Set an ordered playlist for a viewer, enabling `navigate`. Items are paths.
```json
params: { "viewer": "main", "items": ["/a.jpg", "/b.png", "/c.gif"], "index": 0 }
result: { "count": 3, "index": 0, "path": "/a.jpg" }
```

### 6.11 `navigate`
```json
params: { "viewer": "main", "to": "next" }   // "next"|"prev"|"first"|"last"| <int index>
result: { "index": 1, "path": "/b.png", "count": 3 }
```

### 6.12 `getState`
```json
params: { "viewer": "main" }                 // or { "viewer": "*" }
result: {                                     // single viewer:
  "open": true, "viewer": "main",
  "x": 200, "y": 150, "w": 1200, "h": 900,
  "zoom": 1.5, "theme": "rich",
  "image": "/b.png", "index": 1, "count": 3,
  "fullscreen": false
}
// for "*": { "viewers": [ <state>, <state>, … ] }
```

### 6.13 `setScript`
Register (or clear) the FileMaker script to invoke for this viewer's events. Stored by the
helper and echoed in every event for that viewer.
```json
params: { "viewer": "main", "file": "Invoices", "script": "OnZiEvent" }  // omit file/script to clear
result: { "viewer": "main" }
```

### 6.14 `subscribe`
Mark **this connection** as the event channel for the client. The helper will push events
(§8) on this connection. A client keeps exactly one subscribed connection open for its lifetime.
```json
params: { }
result: { "subscribed": true }
```

### 6.15 `shutdown`
Decrement the client reference count. When it reaches zero **and** no viewer windows remain
**and** the helper was plug-in-spawned, the helper exits after a grace period (§7).
```json
params: { }
result: { "bye": true }
```

## 7. Lifecycle & reference counting

- **Spawn**: the plug-in spawns the helper on first use (macOS: `posix_spawn` + `setsid` so it
  survives FileMaker quitting). The helper binary is bundled at
  `ZooImage.fmplugin/Contents/Resources/helper/ZooImage.app`.
- **Single instance**: the port file `flock` prevents double-spawn. A live helper is shared by
  all FileMaker instances / files.
- **Reference count**: incremented per connected client (a `hello`'d connection). Decremented on
  `shutdown` or socket close.
- **Self-quit**: only when **(spawned-by-plug-in) AND (refcount == 0) AND (no open viewer
  windows) AND (grace timer, e.g. 3 min, elapsed)**. A helper launched **standalone by the
  user** never self-quits.
- **Version upgrade**: if `hello` reports a helper `version` older than the plug-in's bundled
  version, the plug-in sends `shutdown`, waits for exit, and re-spawns the newer binary.

## 8. Events

Pushed on the subscribed connection. Envelope per §4. `data` schema by type:

| `event` | `data` |
|---|---|
| `loaded` | `{ "path": "/b.png", "w": 3024, "h": 4032 }` |
| `zoomed` | `{ "zoom": 1.75 }` |
| `navigated` | `{ "index": 2, "path": "/c.gif", "count": 3 }` |
| `clicked` | `{ "x": 512, "y": 320, "imgX": 1420, "imgY": 890, "button": "left" }` |
| `closed` | `{ }` (viewer window was closed) |
| `error` | `{ "message": "…", "path": "/x.raw" }` |
| `dropped` | `{ "paths": ["/dropped1.jpg", "/dropped2.jpg"] }` (user drag-and-dropped) |
| `activated` | `{ }` (viewer window focused/brought to front) |

- Events are **best-effort**. The plug-in queues them and fires the FileMaker script on the next
  safe Idle (never from the socket thread). FileMaker-side polling via `zim_GetState` on an
  `OnTimer` script is the supported fallback.
- **Coalescing**: high-frequency events (`zoomed`, `clicked` during drag) are throttled by the
  helper — intermediate values may be dropped; the last state always arrives.

## 9. Large data / container images

- The protocol carries **paths only**. The plug-in extracts container `BinaryData` to a temp
  file with the correct extension (from the container's `FNAM` / stream type) before calling
  `show`. Location: `~/Library/Caches/ZooImage/` (macOS).
- Temp files are cleaned when the viewer closes or on plug-in shutdown; a TTL sweep guards leaks.
- Rationale: images are frequently multi-MB; base64-in-JSON over the socket is wasteful and
  slow. A path lets the webview stream the file directly.

## 10. Error codes

| `code` | meaning |
|---|---|
| `1` | `auth` — missing/invalid token, or non-`hello` first message |
| `2` | `protocol` — unsupported protocol version |
| `3` | `bad_params` — malformed / missing required params |
| `4` | `no_viewer` — named viewer does not exist |
| `5` | `load_failed` — image could not be opened/decoded |
| `6` | `unsupported` — unknown method |
| `7` | `internal` — helper-side error |

On the plug-in side these map onto `Get(LastExternalErrorDetail)` via `kPluginErrResult1..8`.

## 11. Example session

```jsonl
→ {"id":"1","method":"hello","params":{"token":"9f2c…","protocol":1,"client":"ZooImage-plugin/0.1.0"}}
← {"id":"1","ok":true,"result":{"protocol":1,"version":"0.1.0","name":"ZooImage"}}
→ {"id":"2","method":"subscribe","params":{}}
← {"id":"2","ok":true,"result":{"subscribed":true}}
→ {"id":"3","method":"setScript","params":{"viewer":"main","file":"Invoices","script":"OnZiEvent"}}
← {"id":"3","ok":true,"result":{"viewer":"main"}}
→ {"id":"4","method":"show","params":{"viewer":"main","path":"/tmp/zi/inv42.png","options":{"zoom":"fit","theme":"rich"}}}
← {"id":"4","ok":true,"result":{"viewer":"main","w_img":3024,"h_img":4032}}
← {"event":"loaded","viewer":"main","file":"Invoices","script":"OnZiEvent","data":{"path":"/tmp/zi/inv42.png","w":3024,"h":4032}}
→ {"id":"5","method":"setZoom","params":{"viewer":"main","zoom":"in"}}
← {"id":"5","ok":true,"result":{"zoom":1.25}}
← {"event":"closed","viewer":"main","file":"Invoices","script":"OnZiEvent","data":{}}
```
