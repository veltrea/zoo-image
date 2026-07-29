//! ワイヤプロトコルの型とエラー定義。詳細は protocol/protocol.md（プロトコルバージョン 1）。

use serde::Deserialize;
use serde_json::{json, Value};

/// プロトコルバージョン。plugin と helper で一致していること。
pub const PROTOCOL_VERSION: u32 = 1;

/// クライアントからのリクエスト（1 行 = 1 メッセージ）。
#[derive(Debug, Deserialize)]
pub struct Request {
    pub id: Option<String>,
    pub method: String,
    #[serde(default)]
    pub params: Value,
}

/// プロトコルのエラーコード（protocol.md §10）。
#[derive(Debug, Clone, Copy)]
pub enum ErrCode {
    Auth = 1,
    Protocol = 2,
    BadParams = 3,
    NoViewer = 4,
    LoadFailed = 5,
    Unsupported = 6,
    Internal = 7,
}

/// コマンド処理のエラー。応答の `error` オブジェクトに変換される。
#[derive(Debug)]
pub struct ProtoError {
    pub code: ErrCode,
    pub message: String,
}

impl std::fmt::Display for ProtoError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "[{}] {}", self.code as i64, self.message)
    }
}

impl std::error::Error for ProtoError {}

impl ProtoError {
    pub fn new(code: ErrCode, message: impl Into<String>) -> Self {
        ProtoError { code, message: message.into() }
    }
    pub fn auth(msg: impl Into<String>) -> Self { Self::new(ErrCode::Auth, msg) }
    pub fn protocol(msg: impl Into<String>) -> Self { Self::new(ErrCode::Protocol, msg) }
    pub fn bad_params(msg: impl Into<String>) -> Self { Self::new(ErrCode::BadParams, msg) }
    pub fn no_viewer(name: &str) -> Self {
        Self::new(ErrCode::NoViewer, format!("no such viewer: {name}"))
    }
    pub fn load_failed(msg: impl Into<String>) -> Self { Self::new(ErrCode::LoadFailed, msg) }
    pub fn unsupported(method: &str) -> Self {
        Self::new(ErrCode::Unsupported, format!("unknown method: {method}"))
    }
    pub fn internal(msg: impl Into<String>) -> Self { Self::new(ErrCode::Internal, msg) }
}

/// `{"id":…,"ok":true,"result":…}` を組み立てる。
pub fn ok_response(id: &Option<String>, result: Value) -> String {
    json!({ "id": id, "ok": true, "result": result }).to_string()
}

/// `{"id":…,"ok":false,"error":{…}}` を組み立てる。
pub fn err_response(id: &Option<String>, err: &ProtoError) -> String {
    json!({
        "id": id,
        "ok": false,
        "error": { "code": err.code as i64, "message": err.message }
    })
    .to_string()
}
