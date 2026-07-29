// FileMaker コンテナ(BinaryData)の画像を一時ファイルへ書き出す。
#pragma once

#include <string>

#include "FMWrapper/FMXBinaryData.h"

namespace zimg {

// bd の画像データを ~/Library/Caches/ZooImage/ に書き出し、絶対パスを返す。
// コンテナでない/失敗時は空文字列を返す。
std::string extractContainerToTempFile(const fmx::BinaryData& bd);

} // namespace zimg
