#include "helper_client.hpp"

#include <arpa/inet.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

#include "event_queue.hpp"

extern char** environ;

namespace zimg {

HelperClient& HelperClient::instance() {
    static HelperClient c;
    return c;
}

std::string HelperClient::portFilePath() const {
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/Library/Application Support/ZooImage/daemon.json";
}

bool HelperClient::readPortFile(uint16_t& port, std::string& token, long& pid,
                                std::string& version) {
    std::ifstream f(portFilePath());
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    try {
        json j = json::parse(ss.str());
        port = static_cast<uint16_t>(j.value("port", 0));
        token = j.value("token", std::string());
        pid = j.value("pid", 0L);
        version = j.value("version", std::string());
        return port != 0 && !token.empty();
    } catch (...) {
        return false;
    }
}

void HelperClient::closeSocket() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    readBuf_.clear();
}

bool HelperClient::connectSocket(int timeoutMs) {
    uint16_t port = 0;
    std::string token, version;
    long pid = 0;
    if (!readPortFile(port, token, pid, version)) return false;

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return false;
    }
    fd_ = fd;
    token_ = token;
    readBuf_.clear();
    return true;
}

bool HelperClient::writeLine(const std::string& line, int /*timeoutMs*/) {
    if (fd_ < 0) return false;
    std::string data = line;
    data.push_back('\n');
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::send(fd_, data.data() + off, data.size() - off, 0);
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

bool HelperClient::readLine(std::string& out, int /*timeoutMs*/) {
    if (fd_ < 0) return false;
    size_t nl;
    while ((nl = readBuf_.find('\n')) == std::string::npos) {
        char buf[4096];
        ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
        if (n <= 0) return false; // タイムアウト or 切断
        readBuf_.append(buf, static_cast<size_t>(n));
    }
    out = readBuf_.substr(0, nl);
    readBuf_.erase(0, nl + 1);
    return true;
}

bool HelperClient::sendHello(int timeoutMs) {
    json req = {{"id", "hello"},
                {"method", "hello"},
                {"params", {{"token", token_}, {"protocol", 1}, {"client", "ZooImage-plugin/0.1.0"}}}};
    if (!writeLine(req.dump(), timeoutMs)) return false;
    std::string line;
    if (!readLine(line, timeoutMs)) return false;
    try {
        json j = json::parse(line);
        return j.value("ok", false);
    } catch (...) {
        return false;
    }
}

// ディレクトリ内で最初に見つかった実行可能な通常ファイルのパスを返す。
static std::string firstExecutableIn(const std::string& dir) {
    DIR* d = ::opendir(dir.c_str());
    if (!d) return std::string();
    std::string found;
    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        std::string path = dir + "/" + e->d_name;
        struct stat st;
        if (::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR)) {
            found = path;
            break;
        }
    }
    ::closedir(d);
    return found;
}

std::string HelperClient::helperBinaryPath() const {
    // プラグインバイナリのパスから .fmplugin バンドルを特定し、同梱ヘルパーを指す。
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(&HelperClient::instance), &info) == 0 || !info.dli_fname) {
        return "";
    }
    std::string p = info.dli_fname; // .../ZooImage.fmplugin/Contents/MacOS/ZooImage
    const std::string marker = ".fmplugin/";
    auto pos = p.find(marker);
    if (pos == std::string::npos) return "";
    std::string bundle = p.substr(0, pos + marker.size() - 1); // .../ZooImage.fmplugin
    // .app 内の実行ファイル名は Tauri が Cargo 名で付ける（productName ではない）ため、
    // Contents/MacOS 内の実行ファイルを走査して特定する。
    std::string macos = bundle + "/Contents/Resources/helper/ZooImage.app/Contents/MacOS";
    return firstExecutableIn(macos);
}

bool HelperClient::spawnHelper() {
    std::string bin = helperBinaryPath();
    if (bin.empty() || ::access(bin.c_str(), X_OK) != 0) return false;

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    // 独立セッションにして FileMaker 終了の巻き添えを避ける。
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);

    const char* argv[] = {bin.c_str(), "--from-plugin", nullptr};
    pid_t pid = 0;
    int rc = posix_spawn(&pid, bin.c_str(), nullptr, &attr,
                         const_cast<char* const*>(argv), environ);
    posix_spawnattr_destroy(&attr);
    return rc == 0;
}

bool HelperClient::ensureConnected(int timeoutMs) {
    if (fd_ >= 0) return true;
    if (connectSocket(timeoutMs) && sendHello(timeoutMs)) return true;
    closeSocket();

    if (!spawnHelper()) return false;
    // portfile 出現 + 接続を最大 ~4s ポーリング。
    for (int i = 0; i < 40; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (connectSocket(timeoutMs) && sendHello(timeoutMs)) return true;
        closeSocket();
    }
    return false;
}

HelperClient::Reply HelperClient::request(const std::string& method, const json& params,
                                          int timeoutMs) {
    std::lock_guard<std::mutex> lk(mutex_);
    Reply reply;

    // 1 度目に IO 失敗したら再接続して 1 回だけリトライ。
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!ensureConnected(timeoutMs)) {
            reply.ok = false;
            reply.code = -1;
            reply.message = "helper not reachable";
            return reply;
        }
        ensureEventThread(); // 初回接続時にイベント受信スレッドを起動。
        std::string id = "p" + std::to_string(nextId_++);
        json req = {{"id", id}, {"method", method}, {"params", params}};
        if (!writeLine(req.dump(), timeoutMs)) {
            closeSocket();
            continue;
        }
        std::string line;
        if (!readLine(line, timeoutMs)) {
            closeSocket();
            continue;
        }
        try {
            json j = json::parse(line);
            if (j.value("ok", false)) {
                reply.ok = true;
                reply.result = j.contains("result") ? j["result"] : json::object();
            } else {
                reply.ok = false;
                auto err = j.value("error", json::object());
                reply.code = err.value("code", 0);
                reply.message = err.value("message", "error");
            }
            return reply;
        } catch (...) {
            closeSocket();
            reply.ok = false;
            reply.code = -2;
            reply.message = "bad response";
            return reply;
        }
    }
    reply.ok = false;
    reply.code = -1;
    reply.message = "helper IO failed";
    return reply;
}

void HelperClient::ensureEventThread() {
    bool expected = false;
    if (!evStarted_.compare_exchange_strong(expected, true)) return;
    evThread_ = std::thread(&HelperClient::eventLoop, this);
}

void HelperClient::eventLoop() {
    std::string buf;
    while (!evStop_.load()) {
        uint16_t port = 0;
        std::string token, version;
        long pid = 0;
        if (!readPortFile(port, token, pid, version)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            continue;
        }
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            continue;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        timeval tv{1, 0}; // 1s ごとに evStop_ を確認できるように。
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            continue;
        }
        evFd_.store(fd);

        json hello = {{"id", "hello"},
                      {"method", "hello"},
                      {"params",
                       {{"token", token}, {"protocol", 1}, {"client", "ZooImage-plugin-events/0.1.0"}}}};
        json sub = {{"id", "sub"}, {"method", "subscribe"}, {"params", json::object()}};
        std::string helloLine = hello.dump() + "\n";
        std::string subLine = sub.dump() + "\n";
        ::send(fd, helloLine.data(), helloLine.size(), 0);
        ::send(fd, subLine.data(), subLine.size(), 0);

        buf.clear();
        while (!evStop_.load()) {
            char tmp[4096];
            ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
            if (n > 0) {
                buf.append(tmp, static_cast<size_t>(n));
                size_t nl;
                while ((nl = buf.find('\n')) != std::string::npos) {
                    std::string line = buf.substr(0, nl);
                    buf.erase(0, nl + 1);
                    if (line.empty()) continue;
                    try {
                        json j = json::parse(line);
                        if (!j.contains("event")) continue; // hello/subscribe 応答は無視。
                        ZiEvent ev;
                        ev.file = j.value("file", std::string());
                        ev.script = j.value("script", std::string());
                        ev.payload = line; // イベント JSON をそのまま引数に。
                        pushEvent(ev);
                    } catch (...) {
                    }
                }
            } else if (n == 0) {
                break; // 切断 → 再接続。
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue; // タイムアウト。
                break;
            }
        }
        evFd_.store(-1);
        ::close(fd);
        if (!evStop_.load()) std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
}

bool HelperClient::isRunning(std::string* helperVersion) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (fd_ >= 0) return true;
    // spawn せずに接続だけ試す。
    if (connectSocket(300) && sendHello(300)) {
        if (helperVersion) {
            uint16_t port;
            std::string token, version;
            long pid;
            if (readPortFile(port, token, pid, version)) *helperVersion = version;
        }
        return true;
    }
    closeSocket();
    return false;
}

void HelperClient::shutdown() {
    // イベントスレッドを止めて join（残留はアンロード時クラッシュの原因）。
    evStop_.store(true);
    int efd = evFd_.exchange(-1);
    if (efd >= 0) ::shutdown(efd, SHUT_RDWR); // recv をすぐ解除。
    if (evThread_.joinable()) evThread_.join();

    std::lock_guard<std::mutex> lk(mutex_);
    if (fd_ >= 0) {
        json req = {{"id", "bye"}, {"method", "shutdown"}, {"params", json::object()}};
        writeLine(req.dump(), 200);
    }
    closeSocket();
}

} // namespace zimg
