#include "exif_glue.hpp"

#include "exif_json.hpp"

#include <fstream>
#include <vector>


namespace zimg {
namespace exif {
namespace {

std::string TextToUtf8(const fmx::Text& t) {
    const fmx::uint32 chars = t.GetSize();
    if (chars == 0) return std::string();
    std::string s(static_cast<size_t>(chars) * 4 + 1, '\0');
    const fmx::uint32 n = t.GetBytesEx(&s[0], static_cast<fmx::uint32>(s.size()), 0,
                                       fmx::Text::kSize_End, fmx::Text::kEncoding_UTF8);
    s.resize(n);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::string ArgText(const fmx::DataVect& parms, fmx::uint32 i) {
    if (i >= parms.Size()) return std::string();
    return TextToUtf8(parms.At(i).GetAsText());
}

bool ArgEmpty(const fmx::DataVect& parms, fmx::uint32 i) {
    return i >= parms.Size() || parms.At(i).IsEmpty();
}

void SetText(fmx::Data& results, const std::string& utf8) {
    fmx::TextUniquePtr t;
    t->Assign(utf8.c_str(), fmx::Text::kEncoding_UTF8);
    fmx::LocaleUniquePtr loc;
    results.SetAsText(*t, *loc);
}

// コンテナの JPEG ストリームを取り出す。FILE ストリームにもフォールバックする。
bool ExtractContainer(const fmx::DataVect& parms, fmx::uint32 idx,
                      std::vector<unsigned char>& out, std::string& err) {
    if (parms.Size() <= idx) { err = "missing container parameter"; return false; }
    if (parms.At(idx).GetNativeType() != fmx::Data::kDTBinary) {
        err = "parameter is not a container field"; return false;
    }
    const fmx::BinaryData& bin = parms.AtAsBinaryData(idx);
    if (bin.GetCount() == 0) { err = "container is empty"; return false; }

    fmx::QuadCharUniquePtr jpegType('J', 'P', 'E', 'G');
    fmx::int32 i = bin.GetIndex(*jpegType);
    if (i < 0) {
        fmx::QuadCharUniquePtr fileType('F', 'I', 'L', 'E');
        i = bin.GetIndex(*fileType);
    }
    if (i < 0) { err = "no JPEG image found in the container"; return false; }

    const fmx::uint32 size = bin.GetSize(i);
    out.resize(size);
    bin.GetData(i, 0, size, out.data());
    return true;
}

bool ReadFile(const std::string& path, std::vector<unsigned char>& out, std::string& err) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { err = "cannot open file: " + path; return false; }
    const std::streamsize n = f.tellg();
    if (n <= 0) { err = "file is empty: " + path; return false; }
    f.seekg(0);
    out.resize(static_cast<size_t>(n));
    if (!f.read(reinterpret_cast<char*>(out.data()), n)) {
        err = "cannot read file: " + path; return false;
    }
    return true;
}

} // namespace

bool Read(const fmx::DataVect& parms, fmx::Data& results, std::string& err) {
    std::vector<unsigned char> img;
    if (!ExtractContainer(parms, 0, img, err)) return false;
    std::string out;
    if (!ToJson(img.data(), static_cast<unsigned>(img.size()), out, err)) return false;
    SetText(results, out);
    return true;
}

bool ReadPath(const fmx::DataVect& parms, fmx::Data& results, std::string& err) {
    if (ArgEmpty(parms, 0)) { err = "file path is required"; return false; }
    std::vector<unsigned char> img;
    if (!ReadFile(ArgText(parms, 0), img, err)) return false;
    std::string out;
    if (!ToJson(img.data(), static_cast<unsigned>(img.size()), out, err)) return false;
    SetText(results, out);
    return true;
}

} // namespace exif
} // namespace zimg
