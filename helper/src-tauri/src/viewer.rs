//! ビューアウィンドウの生成と駆動。ここの関数はすべてメインスレッドで呼ぶ前提
//! （commands.rs が `on_main` でラップして呼ぶ）。

use crate::protocol::ProtoError;
use crate::state::{AppState, ViewerMeta};
use serde_json::{json, Value};
use std::sync::Arc;
use tauri::{
    AppHandle, Emitter, LogicalPosition, LogicalSize, Manager, WebviewUrl, WebviewWindow,
    WebviewWindowBuilder, WindowEvent,
};

/// フロントエンドが購読する状態パッチイベント名。
pub const APPLY_EVENT: &str = "zi://apply";

/// ビューア名の検証。ラベルに使える文字だけ許可する。
pub fn sanitize_label(name: &str) -> Result<String, ProtoError> {
    if name.is_empty() || name.len() > 64 {
        return Err(ProtoError::bad_params("viewer name must be 1..=64 chars"));
    }
    if !name.chars().all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-') {
        return Err(ProtoError::bad_params(
            "viewer name may contain only [A-Za-z0-9_-]",
        ));
    }
    Ok(name.to_string())
}

/// ウィンドウ生成／更新オプション。
#[derive(Default, Debug, Clone)]
pub struct WindowOpts {
    pub x: Option<i32>,
    pub y: Option<i32>,
    pub w: Option<u32>,
    pub h: Option<u32>,
    pub title: Option<String>,
    pub theme: Option<String>,
    pub fullscreen: Option<bool>,
    pub focus: bool,
}

/// 論理ピクセルでのウィンドウ位置・サイズ。
pub fn logical_bounds(win: &WebviewWindow) -> Option<(i32, i32, u32, u32)> {
    let sf = win.scale_factor().ok()?;
    let pos = win.outer_position().ok()?;
    let size = win.inner_size().ok()?;
    let x = (pos.x as f64 / sf).round() as i32;
    let y = (pos.y as f64 / sf).round() as i32;
    let w = (size.width as f64 / sf).round() as u32;
    let h = (size.height as f64 / sf).round() as u32;
    Some((x, y, w, h))
}

/// 既存ウィンドウを取得。無ければ生成する。opts を適用して返す。
pub fn ensure_window(
    app: &AppHandle,
    label: &str,
    opts: &WindowOpts,
) -> Result<WebviewWindow, ProtoError> {
    if let Some(win) = app.get_webview_window(label) {
        apply_window_opts(&win, opts)?;
        return Ok(win);
    }

    let theme = opts.theme.as_deref().unwrap_or("rich");
    let decorations = theme != "minimal";
    let w = opts.w.unwrap_or(1000);
    let h = opts.h.unwrap_or(800);
    let title = opts.title.clone().unwrap_or_else(|| "ZooImage".to_string());

    let mut builder = WebviewWindowBuilder::new(app, label, WebviewUrl::App("index.html".into()))
        .title(title)
        .inner_size(w as f64, h as f64)
        .min_inner_size(320.0, 240.0)
        .decorations(decorations)
        .visible(true);

    match (opts.x, opts.y) {
        (Some(x), Some(y)) => builder = builder.position(x as f64, y as f64),
        _ => builder = builder.center(),
    }

    let win = builder
        .build()
        .map_err(|e| ProtoError::internal(format!("window build failed: {e}")))?;

    if opts.fullscreen == Some(true) {
        let _ = win.set_fullscreen(true);
    }
    if opts.focus {
        let _ = win.set_focus();
    }

    attach_close_handler(app, &win, label);
    Ok(win)
}

/// 閉じられたら FileMaker へ `closed` を通知し、レジストリから外す。
fn attach_close_handler(app: &AppHandle, win: &WebviewWindow, label: &str) {
    let app = app.clone();
    let label = label.to_string();
    win.on_window_event(move |ev| {
        if let WindowEvent::CloseRequested { .. } = ev {
            let state = app.state::<Arc<AppState>>();
            state.emit_event(&label, "closed", json!({}));
            state.viewers.lock().unwrap().remove(&label);
            crate::ipc::maybe_quit(&app);
        }
    });
}

/// 生成済みウィンドウへ opts を反映（setWindow / setTheme などの共通処理）。
pub fn apply_window_opts(win: &WebviewWindow, opts: &WindowOpts) -> Result<(), ProtoError> {
    // 位置・サイズは現在値をベースに、指定されたフィールドだけ差し替える。
    if opts.x.is_some() || opts.y.is_some() || opts.w.is_some() || opts.h.is_some() {
        let cur = logical_bounds(win).unwrap_or((0, 0, 1000, 800));
        let x = opts.x.unwrap_or(cur.0);
        let y = opts.y.unwrap_or(cur.1);
        let w = opts.w.unwrap_or(cur.2);
        let h = opts.h.unwrap_or(cur.3);
        let _ = win.set_position(LogicalPosition::new(x as f64, y as f64));
        let _ = win.set_size(LogicalSize::new(w as f64, h as f64));
    }
    if let Some(title) = &opts.title {
        let _ = win.set_title(title);
    }
    if let Some(theme) = &opts.theme {
        let _ = win.set_decorations(theme != "minimal");
    }
    if let Some(fs) = opts.fullscreen {
        let _ = win.set_fullscreen(fs);
    }
    if opts.focus {
        let _ = win.set_focus();
    }
    Ok(())
}

/// フロントエンドへ状態パッチを送る（対象ウィンドウのみ）。
/// patch 例: `{ "path":…, "zoom":…, "theme":…, "index":…, "count":…, "title":… }`
pub fn emit_patch(app: &AppHandle, label: &str, patch: Value) {
    let _ = app.emit_to(label, APPLY_EVENT, patch);
}

/// パス列をビューアで開く（単体モードのファイルオープン/ドロップ用）。メインスレッドで呼ぶ。
pub fn open_paths(app: &AppHandle, label: &str, paths: &[String]) {
    if paths.is_empty() {
        return;
    }
    let opts = WindowOpts { focus: true, ..Default::default() };
    if ensure_window(app, label, &opts).is_err() {
        return;
    }
    {
        let state = app.state::<Arc<AppState>>();
        let mut map = state.viewers.lock().unwrap();
        let meta = map.entry(label.to_string()).or_insert_with(|| ViewerMeta::new(label));
        meta.playlist = paths.to_vec();
        meta.index = 0;
        meta.path = Some(paths[0].clone());
    }
    emit_patch(
        app,
        label,
        json!({
            "items": paths,
            "index": 0,
            "count": paths.len(),
            "path": paths[0],
            "zoom": "fit"
        }),
    );
}
