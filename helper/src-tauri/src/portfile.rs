//! ポートファイル（daemon.json）の生成とトークン。plugin はこのファイルを読んで接続する。
//! 場所は plugin 側とハードコードで一致させる（protocol.md §2）:
//!   macOS   : ~/Library/Application Support/ZooImage/daemon.json
//!   Windows : %LOCALAPPDATA%\ZooImage\daemon.json

use rand::RngCore;
use serde_json::json;
use std::io::Write;
use std::path::PathBuf;

/// daemon.json のフルパス。
pub fn port_file_path() -> Option<PathBuf> {
    // macOS: dirs::data_dir() = ~/Library/Application Support
    // Windows: dirs::data_local_dir() = %LOCALAPPDATA%
    #[cfg(target_os = "windows")]
    let base = dirs::data_local_dir();
    #[cfg(not(target_os = "windows"))]
    let base = dirs::data_dir();

    base.map(|b| b.join("ZooImage").join("daemon.json"))
}

/// 32 バイト乱数トークンを hex 64 文字で生成する。
pub fn generate_token() -> String {
    let mut buf = [0u8; 32];
    rand::thread_rng().fill_bytes(&mut buf);
    buf.iter().map(|b| format!("{:02x}", b)).collect()
}

/// daemon.json をアトミックに書き込む（tmp に書いて rename、パーミッション 0600）。
pub fn write_port_file(port: u16, token: &str, pid: u32, version: &str) -> std::io::Result<()> {
    let path = port_file_path()
        .ok_or_else(|| std::io::Error::new(std::io::ErrorKind::NotFound, "no data dir"))?;
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir)?;
    }
    let contents = json!({
        "port": port,
        "token": token,
        "pid": pid,
        "version": version,
        "protocol": crate::protocol::PROTOCOL_VERSION,
    })
    .to_string();

    let tmp = path.with_extension("json.tmp");
    {
        let mut f = std::fs::File::create(&tmp)?;
        f.write_all(contents.as_bytes())?;
        f.flush()?;
        // パーミッション 0600（Unix のみ）。
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let mut perms = f.metadata()?.permissions();
            perms.set_mode(0o600);
            std::fs::set_permissions(&tmp, perms)?;
        }
    }
    std::fs::rename(&tmp, &path)?;
    Ok(())
}

/// 終了時に daemon.json を削除する（残骸で plugin が誤接続しないように）。
pub fn remove_port_file() {
    if let Some(path) = port_file_path() {
        let _ = std::fs::remove_file(path);
    }
}
