//! ZooImage ヘルパー — ZooImage FileMaker プラグイン用の画像ビューア。
//! plugin から spawn されると IPC 経由でビューアを駆動し、単体起動では通常の画像ビューア
//! として動く。

mod commands;
mod ipc;
mod portfile;
mod protocol;
mod state;
mod viewer;

use state::{AppState, ViewerMeta};
use std::sync::Arc;
use tauri::Manager;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    // plugin から spawn された場合は `--from-plugin` が付く。単体起動なら付かない。
    let spawned_by_plugin = std::env::args().any(|a| a == "--from-plugin");
    let token = portfile::generate_token();
    let version = env!("CARGO_PKG_VERSION").to_string();
    let app_state = Arc::new(AppState::new(token, spawned_by_plugin, version));

    let state_for_setup = app_state.clone();

    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_dialog::init())
        .manage(app_state)
        .invoke_handler(tauri::generate_handler![commands::report_event])
        .setup(move |app| {
            let handle = app.handle().clone();

            // IPC サーバをバックグラウンドで起動（bind → ポートファイル書込 → accept ループ）。
            tauri::async_runtime::spawn(ipc::serve(handle.clone(), state_for_setup.clone()));

            // 単体起動時は空のビューア（"main"）を 1 枚開く。plugin 起動時は show を待つ。
            if !spawned_by_plugin {
                let opts = viewer::WindowOpts {
                    focus: true,
                    ..Default::default()
                };
                match viewer::ensure_window(&handle, "main", &opts) {
                    Ok(_) => {
                        state_for_setup
                            .viewers
                            .lock()
                            .unwrap()
                            .insert("main".to_string(), ViewerMeta::new("main"));
                    }
                    Err(e) => eprintln!("[ZooImage] failed to open default window: {e}"),
                }
            }
            Ok(())
        })
        .build(tauri::generate_context!())
        .expect("error while building tauri application")
        .run(|app_handle, event| match event {
            tauri::RunEvent::ExitRequested { api, .. } => {
                let state = app_handle.state::<Arc<AppState>>();
                if state.spawned_by_plugin {
                    // plugin 起動時は窓が全部閉じても即終了しない。
                    // 実際の終了は refcount + 猶予タイマー(ipc::maybe_quit)が app.exit で行う。
                    api.prevent_exit();
                }
            }
            // macOS: Finder「このアプリで開く」/ドロップでファイルが渡される。
            tauri::RunEvent::Opened { urls } => {
                let paths: Vec<String> = urls
                    .iter()
                    .filter_map(|u| u.to_file_path().ok())
                    .filter_map(|p| p.to_str().map(String::from))
                    .collect();
                viewer::open_paths(app_handle, "main", &paths);
            }
            _ => {}
        });
}
