// FileMaker のコンテナ/パス引数と EXIF 読み取りをつなぐグルー層。
//
// ZooImage は「見る」ための道具なので、EXIF は **読み取り専用**。書き換え
// （タグの更新・GPS の付与・メタデータ除去）は担当しない。それが必要なときは
// ZooEXIF プラグインを使う。
//
// 読み取りエンジンは easyexif（BSD-2-Clause / 依存なし）。当初は ZooEXIF と同じ
// libexif + libiptcdata を使う実装だったが、どちらも LGPL のコピーレフトで、
// 静的リンクすると利用者にライブラリ差し替えの手段を用意する義務が生じるため、
// パーミッシブな easyexif に置き換えた。
#pragma once

#include <string>

// FMWrapper のヘッダは単体で自己完結していない（FMXCalcEngine.h は QuadChar / FixPt を
// 前提にする）。ZooImage.cpp と同じ一式を同じ順序で入れる。
#include "FMWrapper/FMXBinaryData.h"
#include "FMWrapper/FMXCalcEngine.h"
#include "FMWrapper/FMXData.h"
#include "FMWrapper/FMXFixPt.h"
#include "FMWrapper/FMXText.h"
#include "FMWrapper/FMXTypes.h"

namespace zimg {
namespace exif {

// どちらも成功なら true を返し、results にメタデータ JSON を格納する。
// 失敗なら false を返し、err に理由を入れる（results には触れない）。

// (container) → メタデータ JSON
bool Read(const fmx::DataVect& parms, fmx::Data& results, std::string& err);

// (filePath) → メタデータ JSON
bool ReadPath(const fmx::DataVect& parms, fmx::Data& results, std::string& err);

// JPEG バイト列 → JSON の変換そのものは exif_json.hpp（FMX 非依存）にある。

} // namespace exif
} // namespace zimg
