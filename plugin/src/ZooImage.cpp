// ZooImage — FileMaker 画像プレビュー/ビューアプラグイン。
// 薄い IPC クライアントに徹し、表示処理は ZooImage ヘルパーへ委譲する。
// エントリポイント・ディスパッチ・関数登録・zim_ 関数の実装を担う。

#include "FMWrapper/FMXBinaryData.h"
#include "FMWrapper/FMXCalcEngine.h"
#include "FMWrapper/FMXData.h"
#include "FMWrapper/FMXExtern.h"
#include "FMWrapper/FMXFixPt.h"
#include "FMWrapper/FMXText.h"
#include "FMWrapper/FMXTypes.h"

#include <string>

#include "container.hpp"
#include "event_queue.hpp"
#include "helper_client.hpp"
#include "nlohmann/json.hpp"

#define ZOOIMAGE_VERSION_STR "0.1.0"

using zimg::HelperClient;
using json = nlohmann::json;

// FMXExtern.h が extern "C" で宣言しているエントリポイント用グローバル。
FMX_ExternCallPtr gFMX_ExternCallPtr(nullptr);

namespace {

const char* const kPluginID = "Zimg";

// Init で退避するコールバック（ワーカースレッドから gFMX_ExternCallPtr を触らない鉄則）。
FMX_StartScriptCall gStartScript = nullptr;
FMX_CurrentEnvCall gCurrentEnv = nullptr;

// 直近のエラーメッセージ（zim_LastError で取得）。
std::string gLastError;

enum {
    kFuncVersion = 100,
    kFuncIsRunning = 110,
    kFuncLastError = 120,
    kFuncShow = 200,
    kFuncClose = 210,
    kFuncSetZoom = 220,
    kFuncSetWindow = 230,
    kFuncSetTheme = 240,
    kFuncSetTitle = 250,
    kFuncFocus = 260,
    kFuncSetFullscreen = 270,
    kFuncLoadList = 280,
    kFuncNavigate = 290,
    kFuncGetState = 300,
    kFuncSetScript = 310,
};

// ---- 文字列変換 ----
void CopyUTF8(const char* in, fmx::uint32 outSize, fmx::unichar16* out) {
    if (!out || outSize == 0) return;
    fmx::TextUniquePtr t;
    t->Assign(in, fmx::Text::kEncoding_UTF8);
    fmx::uint32 n = t->GetSize();
    if (n > outSize - 1) n = outSize - 1;
    t->GetUnicode(out, 0, n);
    out[n] = 0;
}

std::string TextToUtf8(const fmx::Text& t) {
    fmx::uint32 chars = t.GetSize();
    if (chars == 0) return std::string();
    std::string s(static_cast<size_t>(chars) * 4 + 1, '\0');
    fmx::uint32 n = t.GetBytesEx(&s[0], static_cast<fmx::uint32>(s.size()), 0,
                                fmx::Text::kSize_End, fmx::Text::kEncoding_UTF8);
    s.resize(n);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

// ---- 結果セット ----
fmx::errcode SetText(fmx::Data& results, const std::string& utf8) {
    fmx::TextUniquePtr t;
    t->Assign(utf8.c_str(), fmx::Text::kEncoding_UTF8);
    fmx::LocaleUniquePtr loc;
    results.SetAsText(*t, *loc);
    return 0;
}

fmx::errcode SetInt(fmx::Data& results, fmx::int32 v) {
    fmx::FixPtUniquePtr n(v);
    results.SetAsNumber(*n);
    return 0;
}

// ---- 引数取り出し ----
std::string ArgText(const fmx::DataVect& parms, fmx::uint32 i) {
    if (i >= parms.Size()) return std::string();
    return TextToUtf8(parms.At(i).GetAsText());
}
bool ArgEmpty(const fmx::DataVect& parms, fmx::uint32 i) {
    return i >= parms.Size() || parms.At(i).GetAsText().GetSize() == 0;
}
long ArgLong(const fmx::DataVect& parms, fmx::uint32 i) {
    if (i >= parms.Size()) return 0;
    return parms.At(i).GetAsNumber().AsLong();
}

// 数値なら number、そうでなければ string の JSON 値にする（zoom/navigate 用）。
json NumberOrString(const std::string& s) {
    if (s.empty()) return json(s);
    try {
        size_t idx = 0;
        double d = std::stod(s, &idx);
        if (idx == s.size()) return json(d);
    } catch (...) {
    }
    return json(s);
}

// 成功なら successValue、失敗なら "ERROR: msg" を返し gLastError に記録。
fmx::errcode Reply(fmx::Data& results, const HelperClient::Reply& r, const std::string& successValue) {
    if (r.ok) return SetText(results, successValue);
    gLastError = r.message;
    return SetText(results, "ERROR: " + r.message);
}

std::string ViewerFromOptions(const json& options) {
    if (options.is_object() && options.contains("viewer") && options["viewer"].is_string()) {
        return options["viewer"].get<std::string>();
    }
    return "main";
}

// ==== zim_ 関数 ====

FMX_PROC(fmx::errcode)
Do_Version(short, const fmx::ExprEnv&, const fmx::DataVect&, fmx::Data& results) {
    std::string v = "Zimg " ZOOIMAGE_VERSION_STR;
    std::string hv;
    if (HelperClient::instance().isRunning(&hv) && !hv.empty()) v += " / helper " + hv;
    return SetText(results, v);
}

FMX_PROC(fmx::errcode)
Do_IsRunning(short, const fmx::ExprEnv&, const fmx::DataVect&, fmx::Data& results) {
    return SetInt(results, HelperClient::instance().isRunning() ? 1 : 0);
}

FMX_PROC(fmx::errcode)
Do_LastError(short, const fmx::ExprEnv&, const fmx::DataVect&, fmx::Data& results) {
    return SetText(results, gLastError);
}

FMX_PROC(fmx::errcode)
Do_Show(short, const fmx::ExprEnv&, const fmx::DataVect& parms, fmx::Data& results) {
    try {
        // arg0: コンテナ or パス
        std::string path;
        try {
            const fmx::BinaryData& bin = parms.At(0).GetBinaryData();
            if (bin.GetCount() > 0) path = zimg::extractContainerToTempFile(bin);
        } catch (...) {
        }
        if (path.empty()) path = ArgText(parms, 0);
        if (path.empty()) {
            gLastError = "empty image path/container";
            return SetText(results, "ERROR: " + gLastError);
        }
        // arg1: options JSON（省略可）
        json options = json::object();
        if (!ArgEmpty(parms, 1)) {
            try {
                options = json::parse(ArgText(parms, 1));
            } catch (...) {
                options = json::object();
            }
        }
        std::string viewer = ViewerFromOptions(options);
        json params = {{"viewer", viewer}, {"path", path}, {"options", options}};
        auto r = HelperClient::instance().request("show", params, 1500);
        std::string vw = r.ok ? r.result.value("viewer", viewer) : viewer;
        return Reply(results, r, vw);
    } catch (...) {
        return SetText(results, "ERROR: exception in zim_Show");
    }
}

FMX_PROC(fmx::errcode)
Do_Close(short, const fmx::ExprEnv&, const fmx::DataVect& parms, fmx::Data& results) {
    std::string viewer = ArgEmpty(parms, 0) ? "main" : ArgText(parms, 0);
    auto r = HelperClient::instance().request("close", {{"viewer", viewer}});
    return Reply(results, r, "OK");
}

FMX_PROC(fmx::errcode)
Do_SetZoom(short, const fmx::ExprEnv&, const fmx::DataVect& parms, fmx::Data& results) {
    std::string viewer = ArgEmpty(parms, 0) ? "main" : ArgText(parms, 0);
    json params = {{"viewer", viewer}, {"zoom", NumberOrString(ArgText(parms, 1))}};
    auto r = HelperClient::instance().request("setZoom", params);
    return Reply(results, r, "OK");
}

FMX_PROC(fmx::errcode)
Do_SetWindow(short, const fmx::ExprEnv&, const fmx::DataVect& parms, fmx::Data& results) {
    std::string viewer = ArgEmpty(parms, 0) ? "main" : ArgText(parms, 0);
    json params = {{"viewer", viewer}};
    if (!ArgEmpty(parms, 1)) params["x"] = ArgLong(parms, 1);
    if (!ArgEmpty(parms, 2)) params["y"] = ArgLong(parms, 2);
    if (!ArgEmpty(parms, 3)) params["w"] = ArgLong(parms, 3);
    if (!ArgEmpty(parms, 4)) params["h"] = ArgLong(parms, 4);
    auto r = HelperClient::instance().request("setWindow", params);
    return Reply(results, r, "OK");
}

FMX_PROC(fmx::errcode)
Do_SetTheme(short, const fmx::ExprEnv&, const fmx::DataVect& parms, fmx::Data& results) {
    std::string viewer = ArgEmpty(parms, 0) ? "main" : ArgText(parms, 0);
    auto r = HelperClient::instance().request(
        "setTheme", {{"viewer", viewer}, {"theme", ArgText(parms, 1)}});
    return Reply(results, r, "OK");
}

FMX_PROC(fmx::errcode)
Do_SetTitle(short, const fmx::ExprEnv&, const fmx::DataVect& parms, fmx::Data& results) {
    std::string viewer = ArgEmpty(parms, 0) ? "main" : ArgText(parms, 0);
    auto r = HelperClient::instance().request(
        "setTitle", {{"viewer", viewer}, {"title", ArgText(parms, 1)}});
    return Reply(results, r, "OK");
}

FMX_PROC(fmx::errcode)
Do_Focus(short, const fmx::ExprEnv&, const fmx::DataVect& parms, fmx::Data& results) {
    std::string viewer = ArgEmpty(parms, 0) ? "main" : ArgText(parms, 0);
    auto r = HelperClient::instance().request("focus", {{"viewer", viewer}});
    return Reply(results, r, "OK");
}

FMX_PROC(fmx::errcode)
Do_SetFullscreen(short, const fmx::ExprEnv&, const fmx::DataVect& parms, fmx::Data& results) {
    std::string viewer = ArgEmpty(parms, 0) ? "main" : ArgText(parms, 0);
    bool on = ArgLong(parms, 1) != 0;
    auto r = HelperClient::instance().request("setFullscreen", {{"viewer", viewer}, {"on", on}});
    return Reply(results, r, "OK");
}

FMX_PROC(fmx::errcode)
Do_LoadList(short, const fmx::ExprEnv&, const fmx::DataVect& parms, fmx::Data& results) {
    std::string viewer = ArgEmpty(parms, 0) ? "main" : ArgText(parms, 0);
    json items = json::array();
    try {
        json parsed = json::parse(ArgText(parms, 1));
        if (parsed.is_array()) items = parsed;
    } catch (...) {
        gLastError = "items must be a JSON array of paths";
        return SetText(results, "ERROR: " + gLastError);
    }
    long index = ArgEmpty(parms, 2) ? 0 : ArgLong(parms, 2);
    auto r = HelperClient::instance().request(
        "loadList", {{"viewer", viewer}, {"items", items}, {"index", index}});
    return Reply(results, r, r.ok ? r.result.dump() : std::string());
}

FMX_PROC(fmx::errcode)
Do_Navigate(short, const fmx::ExprEnv&, const fmx::DataVect& parms, fmx::Data& results) {
    std::string viewer = ArgEmpty(parms, 0) ? "main" : ArgText(parms, 0);
    auto r = HelperClient::instance().request(
        "navigate", {{"viewer", viewer}, {"to", NumberOrString(ArgText(parms, 1))}});
    return Reply(results, r, r.ok ? r.result.dump() : std::string());
}

FMX_PROC(fmx::errcode)
Do_GetState(short, const fmx::ExprEnv&, const fmx::DataVect& parms, fmx::Data& results) {
    std::string viewer = ArgEmpty(parms, 0) ? "main" : ArgText(parms, 0);
    auto r = HelperClient::instance().request("getState", {{"viewer", viewer}});
    return Reply(results, r, r.ok ? r.result.dump() : std::string());
}

FMX_PROC(fmx::errcode)
Do_SetScript(short, const fmx::ExprEnv&, const fmx::DataVect& parms, fmx::Data& results) {
    std::string viewer = ArgEmpty(parms, 0) ? "main" : ArgText(parms, 0);
    json params = {{"viewer", viewer}, {"file", ArgText(parms, 1)}, {"script", ArgText(parms, 2)}};
    auto r = HelperClient::instance().request("setScript", params);
    return Reply(results, r, "OK");
}

// ==== 登録テーブル ====
struct FuncDef {
    short id;
    const char* name;
    const char* proto;
    const char* desc;
    short minArgs;
    short maxArgs;
    fmx::ExtPluginType fn;
};

const FuncDef kFuncs[] = {
    {kFuncVersion, "zim_Version", "zim_Version", "Zimg|zim_Version — plug-in (and helper) version", 0, 0, Do_Version},
    {kFuncIsRunning, "zim_IsRunning", "zim_IsRunning", "Zimg|zim_IsRunning — 1 if the ZooImage helper is running", 0, 0, Do_IsRunning},
    {kFuncLastError, "zim_LastError", "zim_LastError", "Zimg|zim_LastError — last error message", 0, 0, Do_LastError},
    {kFuncShow, "zim_Show", "zim_Show( image {; optionsJSON} )", "Zimg|zim_Show — show a container or path in a viewer window", 1, 2, Do_Show},
    {kFuncClose, "zim_Close", "zim_Close( {viewer} )", "Zimg|zim_Close — close a viewer (or \"*\" for all)", 0, 1, Do_Close},
    {kFuncSetZoom, "zim_SetZoom", "zim_SetZoom( viewer ; zoom )", "Zimg|zim_SetZoom — number or fit/fill/actual/in/out", 2, 2, Do_SetZoom},
    {kFuncSetWindow, "zim_SetWindow", "zim_SetWindow( viewer ; x ; y ; w ; h )", "Zimg|zim_SetWindow — move/resize (empty = keep)", 1, 5, Do_SetWindow},
    {kFuncSetTheme, "zim_SetTheme", "zim_SetTheme( viewer ; theme )", "Zimg|zim_SetTheme — rich | minimal", 2, 2, Do_SetTheme},
    {kFuncSetTitle, "zim_SetTitle", "zim_SetTitle( viewer ; title )", "Zimg|zim_SetTitle — window title", 2, 2, Do_SetTitle},
    {kFuncFocus, "zim_Focus", "zim_Focus( viewer )", "Zimg|zim_Focus — bring viewer to front", 1, 1, Do_Focus},
    {kFuncSetFullscreen, "zim_SetFullscreen", "zim_SetFullscreen( viewer ; on )", "Zimg|zim_SetFullscreen — toggle fullscreen", 2, 2, Do_SetFullscreen},
    {kFuncLoadList, "zim_LoadList", "zim_LoadList( viewer ; jsonArray ; index )", "Zimg|zim_LoadList — set a playlist for next/prev", 2, 3, Do_LoadList},
    {kFuncNavigate, "zim_Navigate", "zim_Navigate( viewer ; to )", "Zimg|zim_Navigate — next/prev/first/last/index", 2, 2, Do_Navigate},
    {kFuncGetState, "zim_GetState", "zim_GetState( {viewer} )", "Zimg|zim_GetState — viewer state as JSON (\"*\" for all)", 0, 1, Do_GetState},
    {kFuncSetScript, "zim_SetScript", "zim_SetScript( viewer ; file ; script )", "Zimg|zim_SetScript — FileMaker script for viewer events", 3, 3, Do_SetScript},
};

fmx::ptrtype DoInit(fmx::int16 version) {
    if (gFMX_ExternCallPtr) {
        gStartScript = gFMX_ExternCallPtr->cStartScript;
        gCurrentEnv = gFMX_ExternCallPtr->cCurrentEnv;
    }
    if (version < k160ExtnVersion) {
        return static_cast<fmx::ptrtype>(kDoNotEnable);
    }
    const fmx::QuadCharUniquePtr id(kPluginID[0], kPluginID[1], kPluginID[2], kPluginID[3]);
    const fmx::uint32 flags = fmx::ExprEnv::kDisplayInAllDialogs | fmx::ExprEnv::kFutureCompatible;
    for (const FuncDef& f : kFuncs) {
        fmx::TextUniquePtr name, proto, desc;
        name->Assign(f.name, fmx::Text::kEncoding_UTF8);
        proto->Assign(f.proto, fmx::Text::kEncoding_UTF8);
        desc->Assign(f.desc, fmx::Text::kEncoding_UTF8);
        fmx::ExprEnv::RegisterExternalFunctionEx(*id, f.id, *name, *proto, *desc, f.minArgs,
                                                 f.maxArgs, flags, f.fn);
    }
    return static_cast<fmx::ptrtype>(kCurrentExtnVersion);
}

void DoShutdown(fmx::int16 version) {
    HelperClient::instance().shutdown();
    if (version < k160ExtnVersion) return;
    const fmx::QuadCharUniquePtr id(kPluginID[0], kPluginID[1], kPluginID[2], kPluginID[3]);
    for (const FuncDef& f : kFuncs) {
        fmx::ExprEnv::UnRegisterExternalFunction(*id, f.id);
    }
}

void DoGetString(fmx::uint32 whichString, fmx::uint32 /*winLangID*/, fmx::uint32 outSize,
                 fmx::unichar16* out) {
    switch (whichString) {
        case kFMXT_NameStr:
            CopyUTF8("ZooImage (Zimg)", outSize, out);
            break;
        case kFMXT_AppConfigStr:
            CopyUTF8("Zimg — image preview/viewer for FileMaker. Drives the ZooImage helper app.",
                     outSize, out);
            break;
        case kFMXT_OptionsStr:
            CopyUTF8("Zimg1nnYYnn", outSize, out);
            break;
        default:
            if (outSize) out[0] = 0;
            break;
    }
}

void DoIdle(FMX_IdleLevel level, fmx::ptrtype /*sessionId*/) {
    // kFMXT_Unsafe のとき（非メインスレッド相当）は FM API を触らない。
    if (level == kFMXT_Unsafe) return;
    if (!gStartScript) return;
    auto events = zimg::drainEvents();
    for (const zimg::ZiEvent& ev : events) {
        if (ev.file.empty() || ev.script.empty()) continue; // setScript 未登録は捨てる。
        try {
            fmx::TextUniquePtr file, script, paramText;
            file->Assign(ev.file.c_str(), fmx::Text::kEncoding_UTF8);
            script->Assign(ev.script.c_str(), fmx::Text::kEncoding_UTF8);
            paramText->Assign(ev.payload.c_str(), fmx::Text::kEncoding_UTF8);
            fmx::DataUniquePtr param;
            fmx::LocaleUniquePtr loc;
            param->SetAsText(*paramText, *loc);
            // 小さな識別子(イベント JSON)のみ渡す。結果本体は zim_GetState 等で取りに行く。
            gStartScript(&(*file), &(*script), kFMXT_Pause, &(*param));
        } catch (...) {
        }
    }
}

} // namespace

void FMX_ENTRYPT FMExternCallProc(FMX_ExternCallPtr pb) {
    gFMX_ExternCallPtr = pb;
    switch (pb->whichCall) {
        case kFMXT_Init:
            pb->result = DoInit(pb->extnVersion);
            break;
        case kFMXT_Shutdown:
            DoShutdown(pb->extnVersion);
            break;
        case kFMXT_GetString:
            DoGetString(pb->parm1, pb->parm2, pb->parm3,
                        reinterpret_cast<fmx::unichar16*>(pb->result));
            break;
        case kFMXT_Idle:
            DoIdle(static_cast<FMX_IdleLevel>(pb->parm1), pb->parm2);
            break;
        default:
            break;
    }
}
