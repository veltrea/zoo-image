// helper から受信したイベントを貯めるスレッドセーフなキュー。
// producer: HelperClient のイベントスレッド。consumer: kFMXT_Idle(メインスレッド)。
// ここには FMX 型を一切入れない（素の std::string のみ）。
#pragma once

#include <string>
#include <vector>

namespace zimg {

struct ZiEvent {
    std::string file;    // 通知先 FileMaker ファイル名
    std::string script;  // 通知先スクリプト名
    std::string payload; // スクリプト引数として渡すイベント JSON
};

void pushEvent(const ZiEvent& ev);
std::vector<ZiEvent> drainEvents();

} // namespace zimg
