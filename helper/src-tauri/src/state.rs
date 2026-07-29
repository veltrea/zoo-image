//! ヘルパー全体の共有状態。Tauri の `.manage()` で `Arc<AppState>` として保持する。

use serde_json::{json, Value};
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use std::sync::Mutex;
use tokio::sync::mpsc::UnboundedSender;

/// ビューア（ウィンドウ）1 つ分のメタ情報。
#[derive(Debug, Clone)]
pub struct ViewerMeta {
    /// Tauri ウィンドウラベル（= ビューア名）。
    pub label: String,
    /// 現在表示中の画像パス。
    pub path: Option<String>,
    /// フロントエンドから報告された最新の数値ズーム（1.0 = 100%）。
    pub zoom: f64,
    /// "rich" | "minimal"。
    pub theme: String,
    /// プレイリスト（送り対象のパス列）。
    pub playlist: Vec<String>,
    /// プレイリスト内の現在位置。
    pub index: usize,
    pub fullscreen: bool,
    /// イベント時に起動する FileMaker ファイル名／スクリプト名。
    pub fm_file: Option<String>,
    pub fm_script: Option<String>,
}

impl ViewerMeta {
    pub fn new(label: &str) -> Self {
        ViewerMeta {
            label: label.to_string(),
            path: None,
            zoom: 1.0,
            theme: "rich".to_string(),
            playlist: Vec::new(),
            index: 0,
            fullscreen: false,
            fm_file: None,
            fm_script: None,
        }
    }

    /// getState 用の JSON 表現。
    pub fn to_state_json(&self, open: bool, bounds: Option<(i32, i32, u32, u32)>) -> Value {
        let (x, y, w, h) = bounds
            .map(|(x, y, w, h)| (json!(x), json!(y), json!(w), json!(h)))
            .unwrap_or((Value::Null, Value::Null, Value::Null, Value::Null));
        json!({
            "open": open,
            "viewer": self.label,
            "x": x, "y": y, "w": w, "h": h,
            "zoom": self.zoom,
            "theme": self.theme,
            "image": self.path,
            "index": self.index,
            "count": self.playlist.len(),
            "fullscreen": self.fullscreen,
        })
    }
}

/// イベントチャネル（subscribe 済み接続）の 1 本。
pub struct Subscriber {
    pub id: u64,
    pub tx: UnboundedSender<String>,
}

/// アプリ全体の共有状態。
pub struct AppState {
    /// 起動時に生成した認証トークン（hex 64 文字）。
    pub token: String,
    /// プラグインから spawn されたか（true なら自殺条件の対象）。
    pub spawned_by_plugin: bool,
    pub version: String,
    pub viewers: Mutex<HashMap<String, ViewerMeta>>,
    pub subscribers: Mutex<Vec<Subscriber>>,
    /// 接続中クライアント数（hello した接続）。
    pub refcount: AtomicUsize,
    next_sub_id: AtomicU64,
}

impl AppState {
    pub fn new(token: String, spawned_by_plugin: bool, version: String) -> Self {
        AppState {
            token,
            spawned_by_plugin,
            version,
            viewers: Mutex::new(HashMap::new()),
            subscribers: Mutex::new(Vec::new()),
            refcount: AtomicUsize::new(0),
            next_sub_id: AtomicU64::new(1),
        }
    }

    pub fn alloc_sub_id(&self) -> u64 {
        self.next_sub_id.fetch_add(1, Ordering::SeqCst)
    }

    pub fn add_subscriber(&self, id: u64, tx: UnboundedSender<String>) {
        self.subscribers.lock().unwrap().push(Subscriber { id, tx });
    }

    pub fn remove_subscriber(&self, id: u64) {
        self.subscribers.lock().unwrap().retain(|s| s.id != id);
    }

    /// 全 subscribe 接続へイベント行を push（best-effort）。
    /// 送信先が閉じていたら黙って捨てる（切断は接続側の掃除に任せる）。
    pub fn broadcast(&self, line: String) {
        let subs = self.subscribers.lock().unwrap();
        for s in subs.iter() {
            let _ = s.tx.send(line.clone());
        }
    }

    /// ビューアのイベントを、そのビューアに紐づく file/script を添えて push する。
    pub fn emit_event(&self, viewer: &str, event: &str, data: Value) {
        let (file, script) = {
            let map = self.viewers.lock().unwrap();
            map.get(viewer)
                .map(|m| (m.fm_file.clone(), m.fm_script.clone()))
                .unwrap_or((None, None))
        };
        let line = json!({
            "event": event,
            "viewer": viewer,
            "file": file,
            "script": script,
            "data": data,
        })
        .to_string();
        self.broadcast(line);
    }
}
