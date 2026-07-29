#include "event_queue.hpp"

#include <deque>
#include <mutex>

namespace zimg {

static std::mutex gMutex;
static std::deque<ZiEvent> gQueue;

// 高頻度イベントで溢れないよう上限を設ける（超過分は古いものから捨てる）。
static const size_t kMaxQueued = 256;

void pushEvent(const ZiEvent& ev) {
    std::lock_guard<std::mutex> lk(gMutex);
    gQueue.push_back(ev);
    while (gQueue.size() > kMaxQueued) gQueue.pop_front();
}

std::vector<ZiEvent> drainEvents() {
    std::lock_guard<std::mutex> lk(gMutex);
    std::vector<ZiEvent> out(gQueue.begin(), gQueue.end());
    gQueue.clear();
    return out;
}

} // namespace zimg
