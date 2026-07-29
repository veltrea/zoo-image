// ZooImage ヘルパーへの IPC クライアント（薄い）。
// portfile を読んで 127.0.0.1 に接続し、必要なら spawn する。コマンド接続 1 本を
// mutex で直列化して使う（zim_ 関数は FileMaker の計算スレッドから同期的に呼ばれる）。
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "nlohmann/json.hpp"

namespace zimg {

using json = nlohmann::json;

class HelperClient {
public:
    static HelperClient& instance();

    struct Reply {
        bool ok = false;
        json result;         // ok=true のとき
        int code = 0;        // ok=false のとき
        std::string message; // ok=false のとき
    };

    // method/params を送り応答を返す。接続が無ければ確立（必要なら spawn）する。
    Reply request(const std::string& method, const json& params, int timeoutMs = 800);

    // portfile を読んで到達確認（spawn しない）。ヘルパーのバージョンも返す。
    bool isRunning(std::string* helperVersion = nullptr);

    // プラグイン Shutdown 時に接続を閉じる。
    void shutdown();

private:
    HelperClient() = default;
    HelperClient(const HelperClient&) = delete;
    HelperClient& operator=(const HelperClient&) = delete;

    bool ensureConnected(int timeoutMs);
    bool connectSocket(int timeoutMs);
    bool sendHello(int timeoutMs);
    bool spawnHelper();
    void closeSocket();

    bool readPortFile(uint16_t& port, std::string& token, long& pid, std::string& version);
    std::string portFilePath() const;
    std::string helperBinaryPath() const;

    bool writeLine(const std::string& line, int timeoutMs);
    bool readLine(std::string& out, int timeoutMs);

    // イベント受信スレッド（subscribe 専用接続）。
    void ensureEventThread();
    void eventLoop();

    std::mutex mutex_;
    int fd_ = -1;
    std::string token_;
    std::string readBuf_;
    std::uint64_t nextId_ = 1;

    std::thread evThread_;
    std::atomic<bool> evStarted_{false};
    std::atomic<bool> evStop_{false};
    std::atomic<int> evFd_{-1};
};

} // namespace zimg
