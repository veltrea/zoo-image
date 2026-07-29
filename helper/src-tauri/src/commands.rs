//! コマンド振り分け（apply_command）と各ハンドラ、フロントエンド→バックエンドの
//! イベント受け取り（report_event）。ウィンドウ操作は `on_main` でメインスレッドに集約。

use crate::protocol::ProtoError;
use crate::state::{AppState, ViewerMeta};
use crate::viewer::{
    ensure_window, logical_bounds, sanitize_label, WindowOpts,
};
use serde_json::{json, Value};
use std::sync::Arc;
use tauri::{AppHandle, Manager, State};

/// クロージャをメインスレッドで実行し、結果を受け取る。
pub async fn on_main<T, F>(app: &AppHandle, f: F) -> T
where
    T: Send + 'static,
    F: FnOnce(&AppHandle) -> T + Send + 'static,
{
    let (tx, rx) = tokio::sync::oneshot::channel();
    let app2 = app.clone();
    app.run_on_main_thread(move || {
        let _ = tx.send(f(&app2));
    })
    .expect("run_on_main_thread failed");
    rx.await.expect("main-thread closure dropped")
}

// ---- パラメータ取り出しヘルパー ----

fn viewer_of(params: &Value) -> Result<String, ProtoError> {
    let name = params.get("viewer").and_then(|v| v.as_str()).unwrap_or("main");
    sanitize_label(name)
}

fn as_i32(v: Option<&Value>) -> Option<i32> {
    v.and_then(|v| v.as_f64()).map(|n| n.round() as i32)
}
fn as_u32(v: Option<&Value>) -> Option<u32> {
    v.and_then(|v| v.as_f64()).map(|n| n.round().max(0.0) as u32)
}
fn as_str(v: Option<&Value>) -> Option<String> {
    v.and_then(|v| v.as_str()).map(|s| s.to_string())
}

fn parse_window_opts(options: &Value) -> WindowOpts {
    WindowOpts {
        x: as_i32(options.get("x")),
        y: as_i32(options.get("y")),
        w: as_u32(options.get("w")),
        h: as_u32(options.get("h")),
        title: as_str(options.get("title")),
        theme: as_str(options.get("theme")),
        fullscreen: options.get("fullscreen").and_then(|v| v.as_bool()),
        focus: options.get("focus").and_then(|v| v.as_bool()).unwrap_or(false),
    }
}

fn image_dims(path: &str) -> Option<(u32, u32)> {
    imagesize::size(path).ok().map(|d| (d.width as u32, d.height as u32))
}

// ---- ディスパッチ ----

/// hello / subscribe / shutdown 以外のコマンドを処理する（それらは ipc.rs で処理）。
pub async fn apply_command(
    app: &AppHandle,
    method: &str,
    params: Value,
) -> Result<Value, ProtoError> {
    match method {
        "show" => cmd_show(app, params).await,
        "close" => cmd_close(app, params).await,
        "setZoom" => cmd_set_zoom(app, params).await,
        "setWindow" => cmd_set_window(app, params).await,
        "setTheme" => cmd_set_theme(app, params).await,
        "setTitle" => cmd_set_title(app, params).await,
        "focus" => cmd_focus(app, params).await,
        "setFullscreen" => cmd_set_fullscreen(app, params).await,
        "loadList" => cmd_load_list(app, params).await,
        "navigate" => cmd_navigate(app, params).await,
        "getState" => cmd_get_state(app, params).await,
        "setScript" => cmd_set_script(app, params).await,
        other => Err(ProtoError::unsupported(other)),
    }
}

// ---- 各コマンド ----

async fn cmd_show(app: &AppHandle, params: Value) -> Result<Value, ProtoError> {
    let viewer = viewer_of(&params)?;
    let path = as_str(params.get("path"));
    let options = params.get("options").cloned().unwrap_or(Value::Null);
    let opts = parse_window_opts(&options);
    let zoom = options.get("zoom").cloned().unwrap_or(json!("fit"));

    // 画像寸法はファイル読みだけなのでメインスレッド外で取得。
    let dims = path.as_deref().and_then(image_dims);

    on_main(app, move |app| {
        let win = ensure_window(app, &viewer, &opts)?;
        let theme = opts.theme.clone().unwrap_or_else(|| "rich".to_string());

        // メタ更新
        {
            let state = app.state::<Arc<AppState>>();
            let mut map = state.viewers.lock().unwrap();
            let meta = map.entry(viewer.clone()).or_insert_with(|| ViewerMeta::new(&viewer));
            meta.label = viewer.clone();
            if let Some(p) = &path {
                meta.path = Some(p.clone());
                meta.playlist = vec![p.clone()];
                meta.index = 0;
            }
            meta.theme = theme.clone();
            if let Some(fs) = opts.fullscreen {
                meta.fullscreen = fs;
            }
        }

        // フロントへ状態パッチ
        let mut patch = json!({ "theme": theme, "zoom": zoom });
        if let Some(p) = &path {
            patch["path"] = json!(p);
            patch["items"] = json!([p]);
            patch["index"] = json!(0);
            patch["count"] = json!(1);
        }
        if let Some(t) = &opts.title {
            patch["title"] = json!(t);
        }
        crate::viewer::emit_patch(app, &viewer, patch);
        let _ = win;

        let (wi, hi) = dims.unwrap_or((0, 0));
        Ok(json!({ "viewer": viewer, "w_img": wi, "h_img": hi }))
    })
    .await
}

async fn cmd_close(app: &AppHandle, params: Value) -> Result<Value, ProtoError> {
    let target = as_str(params.get("viewer")).unwrap_or_else(|| "main".to_string());
    on_main(app, move |app| {
        let mut closed = Vec::new();
        if target == "*" {
            for (label, win) in app.webview_windows() {
                let _ = win.close();
                closed.push(label);
            }
            app.state::<Arc<AppState>>().viewers.lock().unwrap().clear();
        } else {
            let label = sanitize_label(&target)?;
            if let Some(win) = app.get_webview_window(&label) {
                let _ = win.close();
                closed.push(label.clone());
            }
            app.state::<Arc<AppState>>().viewers.lock().unwrap().remove(&label);
        }
        Ok(json!({ "closed": closed }))
    })
    .await
}

async fn cmd_set_zoom(app: &AppHandle, params: Value) -> Result<Value, ProtoError> {
    let viewer = viewer_of(&params)?;
    let zoom = params
        .get("zoom")
        .cloned()
        .ok_or_else(|| ProtoError::bad_params("zoom is required"))?;
    on_main(app, move |app| {
        require_window(app, &viewer)?;
        crate::viewer::emit_patch(app, &viewer, json!({ "zoom": zoom }));
        // 数値ならメタに反映（fit/in/out はフロントの zoomed イベントで後追い更新）。
        let resolved = {
            let state = app.state::<Arc<AppState>>();
            let mut map = state.viewers.lock().unwrap();
            let meta = map.entry(viewer.clone()).or_insert_with(|| ViewerMeta::new(&viewer));
            if let Some(z) = zoom.as_f64() {
                meta.zoom = z;
            }
            meta.zoom
        };
        Ok(json!({ "zoom": resolved }))
    })
    .await
}

async fn cmd_set_window(app: &AppHandle, params: Value) -> Result<Value, ProtoError> {
    let viewer = viewer_of(&params)?;
    let opts = WindowOpts {
        x: as_i32(params.get("x")),
        y: as_i32(params.get("y")),
        w: as_u32(params.get("w")),
        h: as_u32(params.get("h")),
        ..Default::default()
    };
    on_main(app, move |app| {
        let win = require_window(app, &viewer)?;
        // 意図した(マージ後の)値を返す。macOS では set_position/set_size が非同期で
        // 反映されるため、直後に logical_bounds を読むと古い値になることがある。
        let cur = logical_bounds(&win).unwrap_or((0, 0, 1000, 800));
        let x = opts.x.unwrap_or(cur.0);
        let y = opts.y.unwrap_or(cur.1);
        let w = opts.w.unwrap_or(cur.2);
        let h = opts.h.unwrap_or(cur.3);
        crate::viewer::apply_window_opts(&win, &opts)?;
        Ok(json!({ "x": x, "y": y, "w": w, "h": h }))
    })
    .await
}

async fn cmd_set_theme(app: &AppHandle, params: Value) -> Result<Value, ProtoError> {
    let viewer = viewer_of(&params)?;
    let theme = as_str(params.get("theme"))
        .ok_or_else(|| ProtoError::bad_params("theme is required"))?;
    if theme != "rich" && theme != "minimal" {
        return Err(ProtoError::bad_params("theme must be 'rich' or 'minimal'"));
    }
    on_main(app, move |app| {
        let win = require_window(app, &viewer)?;
        let _ = win.set_decorations(theme != "minimal");
        crate::viewer::emit_patch(app, &viewer, json!({ "theme": theme }));
        {
            let state = app.state::<Arc<AppState>>();
            let mut map = state.viewers.lock().unwrap();
            if let Some(m) = map.get_mut(&viewer) {
                m.theme = theme.clone();
            }
        }
        Ok(json!({ "theme": theme }))
    })
    .await
}

async fn cmd_set_title(app: &AppHandle, params: Value) -> Result<Value, ProtoError> {
    let viewer = viewer_of(&params)?;
    let title = as_str(params.get("title"))
        .ok_or_else(|| ProtoError::bad_params("title is required"))?;
    on_main(app, move |app| {
        let win = require_window(app, &viewer)?;
        let _ = win.set_title(&title);
        crate::viewer::emit_patch(app, &viewer, json!({ "title": title }));
        Ok(json!({ "title": title }))
    })
    .await
}

async fn cmd_focus(app: &AppHandle, params: Value) -> Result<Value, ProtoError> {
    let viewer = viewer_of(&params)?;
    on_main(app, move |app| {
        let win = require_window(app, &viewer)?;
        let _ = win.set_focus();
        Ok(json!({ "focused": true }))
    })
    .await
}

async fn cmd_set_fullscreen(app: &AppHandle, params: Value) -> Result<Value, ProtoError> {
    let viewer = viewer_of(&params)?;
    let on = params.get("on").and_then(|v| v.as_bool()).unwrap_or(false);
    on_main(app, move |app| {
        let win = require_window(app, &viewer)?;
        let _ = win.set_fullscreen(on);
        {
            let state = app.state::<Arc<AppState>>();
            let mut map = state.viewers.lock().unwrap();
            if let Some(m) = map.get_mut(&viewer) {
                m.fullscreen = on;
            }
        }
        Ok(json!({ "fullscreen": on }))
    })
    .await
}

async fn cmd_load_list(app: &AppHandle, params: Value) -> Result<Value, ProtoError> {
    let viewer = viewer_of(&params)?;
    let items: Vec<String> = params
        .get("items")
        .and_then(|v| v.as_array())
        .map(|arr| arr.iter().filter_map(|x| x.as_str().map(String::from)).collect())
        .unwrap_or_default();
    if items.is_empty() {
        return Err(ProtoError::bad_params("items must be a non-empty array of paths"));
    }
    let start = params.get("index").and_then(|v| v.as_u64()).unwrap_or(0) as usize;
    let index = start.min(items.len() - 1);
    let path = items[index].clone();
    on_main(app, move |app| {
        require_window(app, &viewer)?;
        let count = items.len();
        let items_for_patch = items.clone();
        {
            let state = app.state::<Arc<AppState>>();
            let mut map = state.viewers.lock().unwrap();
            let meta = map.entry(viewer.clone()).or_insert_with(|| ViewerMeta::new(&viewer));
            meta.playlist = items;
            meta.index = index;
            meta.path = Some(path.clone());
        }
        crate::viewer::emit_patch(
            app,
            &viewer,
            json!({ "items": items_for_patch, "path": path, "index": index, "count": count }),
        );
        Ok(json!({ "count": count, "index": index, "path": path }))
    })
    .await
}

async fn cmd_navigate(app: &AppHandle, params: Value) -> Result<Value, ProtoError> {
    let viewer = viewer_of(&params)?;
    let to = params.get("to").cloned().unwrap_or(json!("next"));
    on_main(app, move |app| {
        require_window(app, &viewer)?;
        let state = app.state::<Arc<AppState>>();
        let (path, index, count) = {
            let mut map = state.viewers.lock().unwrap();
            let meta = map
                .get_mut(&viewer)
                .ok_or_else(|| ProtoError::no_viewer(&viewer))?;
            let count = meta.playlist.len();
            if count == 0 {
                return Err(ProtoError::bad_params("no playlist; call loadList first"));
            }
            let new_index = resolve_index(&to, meta.index, count)?;
            meta.index = new_index;
            meta.path = Some(meta.playlist[new_index].clone());
            (meta.playlist[new_index].clone(), new_index, count)
        };
        crate::viewer::emit_patch(
            app,
            &viewer,
            json!({ "path": path, "index": index, "count": count }),
        );
        Ok(json!({ "index": index, "path": path, "count": count }))
    })
    .await
}

fn resolve_index(to: &Value, cur: usize, count: usize) -> Result<usize, ProtoError> {
    if let Some(n) = to.as_i64() {
        let n = n.clamp(0, count as i64 - 1) as usize;
        return Ok(n);
    }
    match to.as_str() {
        Some("next") => Ok((cur + 1) % count),
        Some("prev") => Ok((cur + count - 1) % count),
        Some("first") => Ok(0),
        Some("last") => Ok(count - 1),
        _ => Err(ProtoError::bad_params(
            "to must be next|prev|first|last|<index>",
        )),
    }
}

async fn cmd_get_state(app: &AppHandle, params: Value) -> Result<Value, ProtoError> {
    let target = as_str(params.get("viewer")).unwrap_or_else(|| "main".to_string());
    on_main(app, move |app| {
        let state = app.state::<Arc<AppState>>();
        let map = state.viewers.lock().unwrap();
        let build = |label: &str, meta: &ViewerMeta| -> Value {
            let (open, bounds) = match app.get_webview_window(label) {
                Some(win) => (true, logical_bounds(&win)),
                None => (false, None),
            };
            meta.to_state_json(open, bounds)
        };
        if target == "*" {
            let arr: Vec<Value> = map.iter().map(|(l, m)| build(l, m)).collect();
            Ok(json!({ "viewers": arr }))
        } else {
            let label = sanitize_label(&target)?;
            match map.get(&label) {
                Some(meta) => Ok(build(&label, meta)),
                None => Ok(json!({ "open": false, "viewer": label })),
            }
        }
    })
    .await
}

async fn cmd_set_script(app: &AppHandle, params: Value) -> Result<Value, ProtoError> {
    let viewer = viewer_of(&params)?;
    let file = as_str(params.get("file"));
    let script = as_str(params.get("script"));
    on_main(app, move |app| {
        let state = app.state::<Arc<AppState>>();
        let mut map = state.viewers.lock().unwrap();
        let meta = map.entry(viewer.clone()).or_insert_with(|| ViewerMeta::new(&viewer));
        meta.fm_file = file;
        meta.fm_script = script;
        Ok(json!({ "viewer": viewer }))
    })
    .await
}

/// 対象ビューアのウィンドウが存在することを要求する。
fn require_window(
    app: &AppHandle,
    viewer: &str,
) -> Result<tauri::WebviewWindow, ProtoError> {
    app.get_webview_window(viewer)
        .ok_or_else(|| ProtoError::no_viewer(viewer))
}

// ---- フロントエンド → バックエンド（ユーザー操作イベント） ----

/// フロントエンドがユーザー操作イベントを報告する Tauri コマンド。
/// メタを更新し、subscribe 中の plugin へ push する。
#[tauri::command]
pub fn report_event(state: State<'_, Arc<AppState>>, viewer: String, event: String, data: Value) {
    {
        let mut map = state.viewers.lock().unwrap();
        if let Some(m) = map.get_mut(&viewer) {
            match event.as_str() {
                "zoomed" => {
                    if let Some(z) = data.get("zoom").and_then(|v| v.as_f64()) {
                        m.zoom = z;
                    }
                }
                "navigated" => {
                    if let Some(i) = data.get("index").and_then(|v| v.as_u64()) {
                        m.index = i as usize;
                    }
                    if let Some(p) = data.get("path").and_then(|v| v.as_str()) {
                        m.path = Some(p.to_string());
                    }
                }
                "loaded" => {
                    if let Some(p) = data.get("path").and_then(|v| v.as_str()) {
                        m.path = Some(p.to_string());
                    }
                }
                _ => {}
            }
        }
    }
    state.emit_event(&viewer, &event, data);
}
