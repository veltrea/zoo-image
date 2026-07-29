//! TCP(127.0.0.1) + JSONL の IPC サーバ。1 接続 = 1 セッション（全二重）。
//! 各接続は 1 本の mpsc「outbox」を持ち、writer タスクがソケットへ直列に書き出す。
//! 応答もイベントも同じ outbox を通るので競合しない。

use crate::commands::apply_command;
use crate::protocol::{err_response, ok_response, ProtoError, Request, PROTOCOL_VERSION};
use crate::state::AppState;
use std::sync::atomic::Ordering;
use std::sync::Arc;
use std::time::Duration;
use tauri::{AppHandle, Manager};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::mpsc;

/// 最後のクライアント切断からこの秒数だけ待って（かつ窓が無ければ）自殺する。
const QUIT_GRACE_SECS: u64 = 180;

/// 127.0.0.1 のエフェメラルポートで listen し、ポートファイルを書き、accept ループに入る。
pub async fn serve(app: AppHandle, state: Arc<AppState>) {
    let listener = match TcpListener::bind(("127.0.0.1", 0)).await {
        Ok(l) => l,
        Err(e) => {
            eprintln!("[ZooImage] failed to bind IPC socket: {e}");
            return;
        }
    };
    let port = match listener.local_addr() {
        Ok(a) => a.port(),
        Err(e) => {
            eprintln!("[ZooImage] failed to read local addr: {e}");
            return;
        }
    };

    if let Err(e) =
        crate::portfile::write_port_file(port, &state.token, std::process::id(), &state.version)
    {
        eprintln!("[ZooImage] failed to write port file: {e}");
    }
    eprintln!("[ZooImage] IPC listening on 127.0.0.1:{port}");

    loop {
        match listener.accept().await {
            Ok((stream, _addr)) => {
                let app = app.clone();
                let state = state.clone();
                tokio::spawn(async move {
                    if let Err(e) = handle_conn(stream, app, state).await {
                        eprintln!("[ZooImage] connection ended: {e}");
                    }
                });
            }
            Err(e) => {
                eprintln!("[ZooImage] accept error: {e}");
            }
        }
    }
}

async fn handle_conn(
    stream: TcpStream,
    app: AppHandle,
    state: Arc<AppState>,
) -> std::io::Result<()> {
    let _ = stream.set_nodelay(true);
    let (read_half, mut write_half) = stream.into_split();

    // outbox: 応答・イベントをまとめてソケットへ書き出すチャネル。
    let (outbox, mut rx) = mpsc::unbounded_channel::<String>();
    let writer = tokio::spawn(async move {
        while let Some(line) = rx.recv().await {
            if write_half.write_all(line.as_bytes()).await.is_err() {
                break;
            }
            if write_half.write_all(b"\n").await.is_err() {
                break;
            }
        }
    });

    let mut reader = BufReader::new(read_half);
    let mut line = String::new();

    // 最初のメッセージは必ず hello（トークン認証）。
    line.clear();
    let n = reader.read_line(&mut line).await?;
    if n == 0 {
        return Ok(());
    }
    if !do_hello(&state, &line, &outbox) {
        return Ok(()); // 認証失敗。writer は outbox drop で終了する。
    }

    state.refcount.fetch_add(1, Ordering::SeqCst);
    let mut sub_id: Option<u64> = None;

    // 本ループ
    loop {
        line.clear();
        let n = reader.read_line(&mut line).await?;
        if n == 0 {
            break; // 切断
        }
        let req: Request = match serde_json::from_str(line.trim_end()) {
            Ok(r) => r,
            Err(e) => {
                let _ = outbox.send(err_response(
                    &None,
                    &ProtoError::bad_params(format!("invalid JSON: {e}")),
                ));
                continue;
            }
        };

        match req.method.as_str() {
            "hello" => {
                // 2 回目以降の hello は無視して ok を返す。
                let _ = outbox.send(ok_response(
                    &req.id,
                    serde_json::json!({
                        "protocol": PROTOCOL_VERSION,
                        "version": state.version,
                        "name": "ZooImage"
                    }),
                ));
            }
            "subscribe" => {
                if sub_id.is_none() {
                    let id = state.alloc_sub_id();
                    state.add_subscriber(id, outbox.clone());
                    sub_id = Some(id);
                }
                let _ = outbox.send(ok_response(
                    &req.id,
                    serde_json::json!({ "subscribed": true }),
                ));
            }
            "shutdown" => {
                let _ = outbox.send(ok_response(&req.id, serde_json::json!({ "bye": true })));
                break;
            }
            _ => {
                let resp = match apply_command(&app, &req.method, req.params).await {
                    Ok(result) => ok_response(&req.id, result),
                    Err(e) => err_response(&req.id, &e),
                };
                let _ = outbox.send(resp);
            }
        }
    }

    // 後始末
    if let Some(id) = sub_id {
        state.remove_subscriber(id);
    }
    state.refcount.fetch_sub(1, Ordering::SeqCst);
    drop(outbox);
    let _ = writer.await;

    maybe_quit(&app);
    Ok(())
}

/// hello を検証し、成功なら ok 応答を outbox に積んで true を返す。
fn do_hello(state: &AppState, line: &str, outbox: &mpsc::UnboundedSender<String>) -> bool {
    let req: Request = match serde_json::from_str(line.trim_end()) {
        Ok(r) => r,
        Err(_) => {
            let _ = outbox.send(err_response(
                &None,
                &ProtoError::auth("first message must be a JSON hello"),
            ));
            return false;
        }
    };
    if req.method != "hello" {
        let _ = outbox.send(err_response(
            &req.id,
            &ProtoError::auth("first message must be 'hello'"),
        ));
        return false;
    }
    let token = req.params.get("token").and_then(|v| v.as_str()).unwrap_or("");
    if token != state.token {
        let _ = outbox.send(err_response(&req.id, &ProtoError::auth("invalid token")));
        return false;
    }
    let client_proto = req.params.get("protocol").and_then(|v| v.as_u64()).unwrap_or(0);
    if client_proto != PROTOCOL_VERSION as u64 {
        let _ = outbox.send(err_response(
            &req.id,
            &ProtoError::protocol(format!(
                "protocol mismatch: client={client_proto} helper={PROTOCOL_VERSION}"
            )),
        ));
        return false;
    }
    let _ = outbox.send(ok_response(
        &req.id,
        serde_json::json!({
            "protocol": PROTOCOL_VERSION,
            "version": state.version,
            "name": "ZooImage"
        }),
    ));
    true
}

/// 自殺条件を満たしていれば猶予後にプロセスを終了する。
/// 条件: plugin から spawn された & 接続クライアント 0 & 開いている窓 0。
pub fn maybe_quit(app: &AppHandle) {
    let state = app.state::<Arc<AppState>>().inner().clone();
    if !state.spawned_by_plugin {
        return; // ユーザーが単体起動した場合は自殺しない。
    }
    if state.refcount.load(Ordering::SeqCst) > 0 {
        return;
    }
    if !app.webview_windows().is_empty() {
        return;
    }
    let app = app.clone();
    tokio::spawn(async move {
        tokio::time::sleep(Duration::from_secs(QUIT_GRACE_SECS)).await;
        let state = app.state::<Arc<AppState>>();
        if state.refcount.load(Ordering::SeqCst) == 0 && app.webview_windows().is_empty() {
            crate::portfile::remove_port_file();
            app.exit(0);
        }
    });
}
