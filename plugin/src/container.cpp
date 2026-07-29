#include "container.hpp"

#include "FMWrapper/FMXText.h"
#include "FMWrapper/FMXTypes.h"

#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>

namespace zimg {

static std::string TextToUtf8(const fmx::Text& t) {
    fmx::uint32 chars = t.GetSize();
    if (chars == 0) return std::string();
    std::string s(static_cast<size_t>(chars) * 4 + 1, '\0');
    fmx::uint32 n = t.GetBytesEx(&s[0], static_cast<fmx::uint32>(s.size()), 0,
                                fmx::Text::kSize_End, fmx::Text::kEncoding_UTF8);
    s.resize(n);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

static std::string basenameOf(const std::string& p) {
    auto slash = p.find_last_of("/\\:");
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

static std::string extOf(const std::string& name) {
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size()) return std::string();
    std::string e = name.substr(dot + 1);
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

static std::string extFromType(const fmx::QuadChar& q) {
    char t[5] = {static_cast<char>(q[0]), static_cast<char>(q[1]), static_cast<char>(q[2]),
                 static_cast<char>(q[3]), 0};
    std::string s(t);
    if (s == "JPEG") return "jpg";
    if (s == "PNGf") return "png";
    if (s == "GIFf") return "gif";
    if (s == "TIFF") return "tif";
    if (s == "BMPf" || s == "BMP ") return "bmp";
    return std::string();
}

std::string extractContainerToTempFile(const fmx::BinaryData& bd) {
    try {
        fmx::int32 count = bd.GetCount();
        if (count <= 0) return std::string();

        // ファイル名/拡張子（FNAM ストリームから）
        std::string ext;
        {
            fmx::TextUniquePtr fnam;
            if (bd.GetFNAMData(*fnam) == 0) {
                std::string list = TextToUtf8(*fnam);
                auto nl = list.find_last_of("\r\n");
                std::string one = (nl == std::string::npos) ? list : list.substr(nl + 1);
                ext = extOf(basenameOf(one));
            }
        }

        // データストリーム選択: 'FILE' 優先、無ければ最大の非メタ(FNAM/SIZE)ストリーム。
        const fmx::QuadCharUniquePtr fileType('F', 'I', 'L', 'E');
        const fmx::QuadCharUniquePtr fnamType('F', 'N', 'A', 'M');
        const fmx::QuadCharUniquePtr sizeType('S', 'I', 'Z', 'E');

        fmx::int32 chosen = bd.GetIndex(*fileType);
        if (chosen < 0) {
            fmx::uint32 best = 0;
            for (fmx::int32 i = 0; i < count; ++i) {
                fmx::QuadCharUniquePtr q;
                bd.GetType(i, *q);
                if (*q == *fnamType || *q == *sizeType) continue;
                fmx::uint32 sz = bd.GetSize(i);
                if (sz >= best) {
                    best = sz;
                    chosen = i;
                    if (ext.empty()) ext = extFromType(*q);
                }
            }
        } else if (ext.empty()) {
            fmx::QuadCharUniquePtr q;
            bd.GetType(chosen, *q);
            ext = extFromType(*q);
        }
        if (chosen < 0) return std::string();
        if (ext.empty()) ext = "img";

        fmx::uint32 size = bd.GetSize(chosen);
        if (size == 0) return std::string();
        std::string bytes;
        bytes.resize(size);
        if (bd.GetData(chosen, 0, size, &bytes[0]) != 0) return std::string();

        const char* home = std::getenv("HOME");
        if (!home) return std::string();
        std::string dir = std::string(home) + "/Library/Caches/ZooImage";
        ::mkdir(dir.c_str(), 0700);

        static std::atomic<std::uint64_t> counter{0};
        std::string path = dir + "/zim_" + std::to_string(static_cast<long>(::getpid())) + "_" +
                           std::to_string(counter++) + "." + ext;

        std::ofstream out(path, std::ios::binary);
        if (!out) return std::string();
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) return std::string();
        return path;
    } catch (...) {
        return std::string();
    }
}

} // namespace zimg
