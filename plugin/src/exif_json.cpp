#include "exif_json.hpp"

#include "easyexif/exif.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace zimg {
namespace exif {
namespace {

// 空文字列 / 0 は JSON に出さない。「無い」と「0」を混同させないため。
void putStr(json& o, const char* k, const std::string& v) { if (!v.empty()) o[k] = v; }
void putNum(json& o, const char* k, double v)             { if (v != 0.0)   o[k] = v; }
void putUInt(json& o, const char* k, unsigned v)          { if (v != 0)     o[k] = v; }

} // namespace

bool ToJson(const unsigned char* data, unsigned len, std::string& out_json, std::string& err) {
    easyexif::EXIFInfo info;
    const int rc = info.parseFrom(data, len);

    if (rc == PARSE_EXIF_ERROR_NO_JPEG) { err = "not a JPEG image"; return false; }
    if (rc == PARSE_EXIF_ERROR_CORRUPT) { err = "the EXIF block is corrupt"; return false; }
    if (rc == PARSE_EXIF_ERROR_UNKNOWN_BYTEALIGN) { err = "unknown EXIF byte alignment"; return false; }
    // EXIF が無いのは異常ではない。撮影情報を持たない画像として空の結果を返す。
    if (rc == PARSE_EXIF_ERROR_NO_EXIF) { out_json = R"({"hasExif":false})"; return true; }
    if (rc != PARSE_EXIF_SUCCESS) { err = "could not parse EXIF"; return false; }

    json root;
    root["hasExif"] = true;

    json camera;
    putStr(camera, "make", info.Make);
    putStr(camera, "model", info.Model);
    putStr(camera, "software", info.Software);
    if (!camera.empty()) root["camera"] = camera;

    json lens;
    putStr(lens, "make", info.LensInfo.Make);
    putStr(lens, "model", info.LensInfo.Model);
    putNum(lens, "focalLength", info.FocalLength);
    putUInt(lens, "focalLength35mm", info.FocalLengthIn35mm);
    putNum(lens, "fStopMin", info.LensInfo.FStopMin);
    putNum(lens, "fStopMax", info.LensInfo.FStopMax);
    if (!lens.empty()) root["lens"] = lens;

    json exposure;
    putNum(exposure, "time", info.ExposureTime);
    putNum(exposure, "fNumber", info.FNumber);
    putUInt(exposure, "iso", info.ISOSpeedRatings);
    putNum(exposure, "bias", info.ExposureBiasValue);
    putUInt(exposure, "program", info.ExposureProgram);
    putUInt(exposure, "meteringMode", info.MeteringMode);
    putUInt(exposure, "flashMode", info.FlashMode);
    if (!exposure.empty()) root["exposure"] = exposure;

    json dt;
    putStr(dt, "original", info.DateTimeOriginal);
    putStr(dt, "digitized", info.DateTimeDigitized);
    putStr(dt, "modified", info.DateTime);
    if (!dt.empty()) root["dateTime"] = dt;

    json image;
    putUInt(image, "width", info.ImageWidth);
    putUInt(image, "height", info.ImageHeight);
    putUInt(image, "orientation", info.Orientation);
    putUInt(image, "bitsPerSample", info.BitsPerSample);
    putStr(image, "description", info.ImageDescription);
    if (!image.empty()) root["image"] = image;

    // 緯度経度は 0,0 が有効値になりうるので、他の GPS 情報の有無で判定する。
    const auto& g = info.GeoLocation;
    const bool hasGps = (g.Latitude != 0.0 || g.Longitude != 0.0 || g.Altitude != 0.0);
    if (hasGps) {
        json gps;
        gps["latitude"] = g.Latitude;
        gps["longitude"] = g.Longitude;
        putNum(gps, "altitude", g.Altitude);
        putNum(gps, "dop", g.DOP);
        root["gps"] = gps;
    }

    putStr(root, "copyright", info.Copyright);

    out_json = root.dump();
    return true;
}


} // namespace exif
} // namespace zimg
