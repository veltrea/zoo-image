// JPEG バイト列から EXIF を読み、JSON 文字列に変換する。
//
// FileMaker(FMX) に一切依存しない。そのため単体テスト(tests/exif_test.cpp)から
// そのまま呼べる。FMX との橋渡しは exif_glue.{hpp,cpp} が担当する。
//
// 読み取りエンジンは easyexif（BSD-2-Clause / 依存なし）。ZooImage は「見る」道具なので
// EXIF は読み取り専用で、書き換えは扱わない（必要なら ZooEXIF プラグインを使う）。
#pragma once

#include <string>

namespace zimg {
namespace exif {

// 成功なら true を返し out_json にメタデータ JSON を格納する。
// EXIF を持たない JPEG も「異常」ではないので {"hasExif":false} を返して true とする。
// JPEG でない・EXIF が壊れている場合は false を返し err に理由を入れる。
bool ToJson(const unsigned char* data, unsigned len, std::string& out_json, std::string& err);

} // namespace exif
} // namespace zimg
