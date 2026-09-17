// mdvn 进程入口(T13/T14 后更新)：
// 命令行解析、命名互斥体(同文件重复打开时前置已有窗口)、真实文件加载、
// 各子系统初始化、内置 --bench 性能埋点、弹窗、跑消息循环。
// 窗口本身(窗口类注册、窗口过程、DPI 感知、滚动与快捷键)在
// src/shell/window.h/.cpp(T12);本文件只保留进程入口职责。
//
// 架构决策(已在 memory.md 通过 spike 验证，直接落地):
//   1. 进程级禁用 IME,避免第三方输入法把 TSF 模块注入进来(省内存)。
//   2. D2D 渲染目标默认走软件光栅化,避免触发 Intel iGPU 的着色器编译器。
//   3. 每个文件一个独立窗口,不做单实例复用([裁决 #6]);命名互斥体只用于
//      "同一文件重复打开时把已有窗口前置"这一体验优化,不阻止/不退出新进程。

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <imm.h>
#include <d2d1.h>

#include <cstdint>
#include <cstdio>
#include <cwchar>

#include "bench.h"
#include "../util/arena.h"
#include "../util/cmdline.h"
#include "../util/ini.h"
#include "../util/str.h"
#include "../doc/model.h"
#include "../doc/parser.h"
#include "../doc/file_map.h"
#include "../doc/encoding.h"
#include "../doc/front_matter.h"
#include "../text/font.h"
#include "../layout/layout.h"
#include "../render/renderer.h"
#include "../assets/cache.h"
#include "../assets/remote.h"
#include "../shell/find.h"
#include "../shell/navigate.h"
#include "../shell/window.h"
#include "../../third_party/md4c/md4c.h"

#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace {

// 没有传入文件路径,或传入的文件打不开/映射失败时展示的占位样例:覆盖标题、
// 段落、引用(左侧竖线)、分割线、围栏代码块(背景)几种块类型,用来证明
// "解析 -> 布局 -> 渲染"整条链路走得通。
constexpr char kSampleMarkdown[] =
    "# mdvn 示例文档\n\n"
    "这是一个 **段落**,用来验证解析 -> 布局 -> 渲染整条链路是否走得通。\n\n"
    "## 二级标题\n\n"
    "> 这是一段引用文字,左侧应画出一条用 D2D 几何图元绘制的竖线,\n"
    "> 而不是用某个字体里的竖线字符模拟。\n\n"
    "---\n\n"
    "```\n"
    "fn main() {\n"
    "    println!(\"hello mdvn\");\n"
    "}\n"
    "```\n";

// 以下全局变量均为 POD / 普通指针,零初始化,不含任何有副作用的构造函数。
ID2D1Factory* g_d2dFactory = nullptr;
wchar_t g_displayText[MAX_PATH + 16] = L"mdvn";

// mdvn 命名互斥体/窗口属性统一使用的前缀,避免和系统其它对象重名冲突。
constexpr wchar_t kMutexNamePrefix[] = L"mdvn_filemutex_";
constexpr wchar_t kPathHashPropName[] = L"mdvn_path_hash";

// T44 修复:T42 引入的"--bench 即强制全量解码"曾经不区分具体语料,导致
// BENCH-A(测的是首屏虚拟化)也被误强制物化成全文档,拉高了 private_bytes
// 门禁读数(见 06-m1-tasks.md 阶段 J 的排查记录)。这里改成只对文件名含
// "bench-b"(大小写不敏感)的目标生效,让 BENCH-A 恢复原本的首屏虚拟化语义。
bool BenchTargetWantsFullDecode(const wchar_t* filePath) {
    if (!filePath) return false;
    constexpr wchar_t kMarker[] = L"bench-b";
    constexpr size_t kMarkerLen = 7;
    size_t len = wcslen(filePath);
    if (len < kMarkerLen) return false;
    for (size_t i = 0; i + kMarkerLen <= len; ++i) {
        bool match = true;
        for (size_t j = 0; j < kMarkerLen; ++j) {
            wchar_t c = filePath[i + j];
            if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
            if (c != kMarker[j]) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

// md4c 的 SAX 回调:本阶段只验证链接与调用可行,不构建文档模型。
int OnBlock(MD_BLOCKTYPE, void*, void*) { return 0; }
int OnSpan(MD_SPANTYPE, void*, void*) { return 0; }
int OnText(MD_TEXTTYPE, const MD_CHAR*, MD_SIZE, void*) { return 0; }

// 用一段最小 markdown 文本跑一次 md4c,证明库已正确链接进本 exe。
void RunMd4cSmokeTest() {
    static const char kSample[] = "# mdvn\n\nhello **world**\n";
    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags = 0;
    parser.enter_block = OnBlock;
    parser.leave_block = OnBlock;
    parser.enter_span = OnSpan;
    parser.leave_span = OnSpan;
    parser.text = OnText;
    int rc = md_parse(kSample, static_cast<MD_SIZE>(sizeof(kSample) - 1), &parser, nullptr);
    fprintf(stderr, "md4c_smoke_test_rc=%d\n", rc);
}

// 用几种不同大小/对齐的分配验证 Arena 工作正常,再 Reset 一次证明可复用。
void RunArenaSmokeTest() {
    mdvn::Arena arena;
    if (!arena.Init(64 * 1024 * 1024)) {
        fprintf(stderr, "arena_init_failed\n");
        return;
    }

    void* a = arena.Alloc(16, 8);
    void* b = arena.Alloc(4096, 16);   // 触发跨页提交
    void* c = arena.Alloc(1, 1);

    bool ok = (a != nullptr) && (b != nullptr) && (c != nullptr);
    if (a) {
        *static_cast<unsigned char*>(a) = 0xAB;
        ok = ok && (*static_cast<unsigned char*>(a) == 0xAB);
    }

    arena.Reset();
    void* d = arena.Alloc(32, 32);
    ok = ok && (d != nullptr);

    fprintf(stderr, "arena_smoke_test_ok=%d ptr_a=%p ptr_b=%p ptr_c=%p ptr_d_after_reset=%p\n",
            ok ? 1 : 0, a, b, c, d);
}

// 取路径末尾的文件名部分(不含目录),写入固定大小的窗口标题缓冲区。
void SetDisplayTextFromPath(const wchar_t* path) {
    const wchar_t* fileNameOnly = path;
    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'\\' || *p == L'/') fileNameOnly = p + 1;
    }
    wcsncpy_s(g_displayText, MAX_PATH + 16, fileNameOnly, _TRUNCATE);
}

// FNV-1a 64 位哈希:把任意长度的(已按大小写归一化的)路径压缩成定长值,
// 用来拼出合法的互斥体名字,并顺带作为跨进程识别"同一文件"的窗口属性值。
// 只做 ASCII 大小写折叠(Windows 路径的驱动器号/分隔符/扩展名都是 ASCII,
// 足够覆盖绝大多数场景;极少数含非 ASCII 大小写变体的路径退化为大小写
// 敏感匹配,不影响正确性,只是"前置已有窗口"这个体验优化偶尔失效)。
uint64_t HashPathCaseInsensitive(const wchar_t* path) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const wchar_t* p = path; *p; ++p) {
        wchar_t c = *p;
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
        h ^= static_cast<uint64_t>(c);
        h *= 0x100000001b3ULL;
    }
    return h;
}

// EnumWindows 回调的上下文:按窗口属性里存的路径哈希查找目标窗口。
struct FindWindowContext {
    uint64_t hash;
    HWND found;
};

BOOL CALLBACK FindWindowByHashProc(HWND hwnd, LPARAM lparam) {
    FindWindowContext* ctx = reinterpret_cast<FindWindowContext*>(lparam);
    HANDLE prop = GetPropW(hwnd, kPathHashPropName);
    if (prop != nullptr &&
        static_cast<uint64_t>(reinterpret_cast<UINT_PTR>(prop)) == ctx->hash) {
        ctx->found = hwnd;
        return FALSE;  // 找到即停止枚举
    }
    return TRUE;
}

// 尽力而为地找到"同一文件"已经开着的窗口并前置;找不到/前置失败都不影响
// 本次启动继续往下走(裁决 #6:每个文件仍然独立开一个新窗口)。
void TryFocusExistingWindowForHash(uint64_t hash) {
    FindWindowContext ctx{hash, nullptr};
    EnumWindows(FindWindowByHashProc, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.found == nullptr) return;

    if (IsIconic(ctx.found)) ShowWindow(ctx.found, SW_RESTORE);
    SetForegroundWindow(ctx.found);  // 系统前台锁定等原因导致失败时静默忽略
}

// 按路径哈希创建命名互斥体(T13):不持有/不等待,只用其存在性判断"同一文件
// 是否已有窗口开着"。创建失败(极端情况,如系统资源耗尽)时静默忽略,
// 不影响正常启动;已存在时尝试前置已有窗口,同样是尽力而为。
HANDLE AcquireFileMutexAndMaybeFocusExisting(uint64_t pathHash) {
    wchar_t mutexName[64];
    swprintf_s(mutexName, L"%s%016llx", kMutexNamePrefix,
               static_cast<unsigned long long>(pathHash));

    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName);
    if (mutex == nullptr) return nullptr;

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        TryFocusExistingWindowForHash(pathHash);
    }
    return mutex;
}

// 从磁盘映射文件加载并解析为文档模型:编码嗅探 -> (必要时)解码为 UTF-16
// 再转回 UTF-8 -> md4c 解析。UTF-8(含 BOM)文件直接复用映射内存,不绕一圈
// UTF-16,减少一次转换。
//
// `fileMap` 的映射视图与返回的 Document::source 可能共享同一段内存,调用方
// 须保证 `fileMap` 的生命周期覆盖 Document 的整个使用期(本文件里通过让
// fileMap 与 docArena 一样活到 wWinMain 结尾来满足这一点)。
mdvn::Document LoadMarkdownFile(mdvn::FileMap* fileMap, mdvn::Arena* arena) {
    mdvn::StrSlice raw = fileMap->Data();
    mdvn::EncodingDetection detection = mdvn::DetectEncoding(raw);

    mdvn::StrSlice utf8;
    if (detection.encoding == mdvn::DetectedEncoding::Utf16Le ||
        detection.encoding == mdvn::DetectedEncoding::AnsiFallback) {
        mdvn::Utf16Slice utf16 = mdvn::DecodeToUtf16(raw, detection, arena);
        utf8 = mdvn::Utf16ToUtf8(utf16, arena);
    } else {
        utf8 = mdvn::StrSlice{raw.data + detection.contentOffset,
                               raw.len - detection.contentOffset};
    }

    // T22:跳过 YAML front matter(若存在),两处解析调用点统一走这一函数。
    return mdvn::ParseMarkdown(mdvn::SkipFrontMatter(utf8), arena);
}

// 从完整文件路径里截出所在目录(不含末尾分隔符),写入调用方缓冲。
// 相对路径的图片(T33)与"点击查看原图"(T36b)都要按文档目录解析。
void ExtractDirectory(const wchar_t* path, wchar_t* out, size_t outCap) {
    out[0] = 0;
    if (!path) return;
    size_t lastSep = 0;
    size_t i = 0;
    for (; path[i] != 0 && i + 1 < outCap; ++i) {
        if (path[i] == L'\\' || path[i] == L'/') lastSep = i;
    }
    if (lastSep == 0) return;
    for (size_t k = 0; k < lastSep && k + 1 < outCap; ++k) out[k] = path[k];
    out[lastSep] = 0;
}

// T36 ②:"在当前窗口内替换文档"需要触碰的全部子系统。窗口外壳层不知道文档是
// 怎么加载的,只在链接被点击时回调 `OpenDocumentInPlace`;真正的"释放旧状态 ->
// 加载新文档 -> 重排"发生在这里(裁决 #6:不新开进程,任意时刻只有一份文档状态常驻)。
struct DocumentHost {
    HWND hwnd;                       // 主窗口,用于取视口尺寸与改标题
    mdvn::FileMap* fileMap;          // 旧文档的映射视图,换文档前必须先 Close
    mdvn::Arena* docArena;           // 文档 Arena,换文档时整体 Reset
    mdvn::Document* doc;             // 就地替换的文档模型
    mdvn::BlockLayoutEngine* layout; // 布局引擎(Relayout 会先淘汰全部 layout)
    mdvn::FontSubsystem* fonts;      // 取当前缩放档位
    mdvn::ImageCache* imageCache;    // 图片缓存,换文档时位图与条目一并作废
    mdvn::Arena* imageArena;         // 图片缓存所在 Arena,换文档时整体 Reset
    wchar_t* documentDirectory;      // MAX_PATH 缓冲,换文档后要更新成新文档的目录
};

// T36 ②的实际执行体。返回 false 表示新文档打不开(此时窗口里是一份空文档,
// 外壳层会显示窗口内提示,不弹 MessageBox)。
bool OpenDocumentInPlace(void* userData, const wchar_t* fullPath) {
    DocumentHost* host = static_cast<DocumentHost*>(userData);
    if (!host || !fullPath) return false;

    // ① 先把旧文档的全部状态放掉:解码位图 -> 图片缓存 Arena -> 文件映射 -> 文档 Arena。
    //    顺序不能反 —— 位图必须在缓存条目还在的时候释放,否则就没人认领了。
    host->imageCache->ReleaseAllBitmaps();
    host->imageArena->Reset();
    host->imageCache->Init(host->imageArena);
    host->fileMap->Close();
    host->docArena->Reset();

    // 换行宽度收窄掉左右内边距(kContentPaddingDip),口径与 window.cpp 的
    // ViewportWidthOf 一致,否则窗口内换文档后正文换行宽度会和其余场景对不上。
    float widthDip = mdvn::ContentWidthDip(mdvn::ClientWidthDip(host->hwnd));
    if (widthDip < 1.0f) widthDip = 1.0f;
    float fontScale = host->fonts ? host->fonts->Scale() : 1.0f;

    // ② 打开并解析新文档。
    if (host->fileMap->Open(fullPath) != mdvn::FileMapError::None) {
        *host->doc = mdvn::Document(host->docArena);  // 空文档,保证后续渲染不读到悬空指针
        host->layout->Relayout(*host->doc, widthDip, fontScale, host->imageCache);
        return false;
    }
    *host->doc = LoadMarkdownFile(host->fileMap, host->docArena);

    // ③ 更新"当前文档目录"(相对路径的图片与再下一跳链接都据此解析)与窗口标题。
    ExtractDirectory(fullPath, host->documentDirectory, MAX_PATH);
    SetDisplayTextFromPath(fullPath);
    SetWindowTextW(host->hwnd, g_displayText);

    // ④ 按新文档重排;Relayout 内部会先淘汰上一份文档遗留的全部 IDWriteTextLayout。
    host->layout->Relayout(*host->doc, widthDip, fontScale, host->imageCache);
    return true;
}

// T14 性能埋点的回调钩子:window.h 的 WindowState 只接受不带命名空间知识的
// 通用函数指针,这里用两个薄转发函数把它们接到 bench 模块上,保持 shell 层
// 不直接依赖 app/bench.h。
void OnWindowCreatedBenchHook(void*) { mdvn::bench::MarkWindowCreated(); }

// 首帧真正显示后立即记下时间点并把整份报告输出到 stderr——不等窗口关闭,
// 方便脚本(T15)在首屏渲染完成后就能采集到数据,不必等用户/脚本关掉窗口。
void OnFirstPresentBenchHook(void*) {
    mdvn::bench::MarkFirstPresent();
    mdvn::bench::EmitReport();
}

}  // namespace

// 进程入口:解析命令行、命名互斥体、初始化各子系统、加载文档、弹出窗口、
// 跑消息循环。
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // 命令行解析改用不依赖 shell32.dll 的 mdvn::ParseCommandLine(见 cmdline.h
    // 顶部注释)——CommandLineToArgvW 本身是 shell32 的导出符号,在这里被
    // 无条件调用会让 CMakeLists.txt 里的 /DELAYLOAD:shell32.dll 名存实亡。
    // cmdlineArena 只需装下命令行字符串与 argv 数组,生命周期覆盖整个
    // wWinMain(benchArgs.filePath 后面还会被多处使用),函数退出时随栈析构。
    mdvn::Arena cmdlineArena;
    cmdlineArena.Init(64 * 1024);
    mdvn::Vec<wchar_t*> argv = mdvn::ParseCommandLine(GetCommandLineW(), &cmdlineArena);
    int argc = static_cast<int>(argv.Size());
    mdvn::bench::ParsedArgs benchArgs = mdvn::bench::ParseArgs(argc, argv.Data());
    if (benchArgs.benchEnabled) mdvn::bench::Enable();
    // "进程入口"埋点尽量早地记录;命令行解析本身极轻,可忽略的测量误差。
    mdvn::bench::MarkProcessStart();

    // Per-Monitor V2 DPI 感知必须在创建任何窗口之前开启(T12)。
    mdvn::EnablePerMonitorV2DpiAwareness();

    // mdvn 是只读查看器,不需要文字输入;禁用 IME/TSF 激活可避免第三方
    // 输入法把自己的模块注入进来(架构决策 #1)。
    ImmDisableIME(static_cast<DWORD>(-1));

    RunArenaSmokeTest();
    RunMd4cSmokeTest();

    // T13:同一文件路径的命名互斥体 + 尽力而为的"前置已有窗口"。每个文件
    // 仍然独立开一个新窗口([裁决 #6]),这里不阻止、不提前退出。
    bool hasTarget = benchArgs.filePath != nullptr;
    wchar_t normalizedPath[MAX_PATH]{};
    HANDLE fileMutex = nullptr;
    if (hasTarget) {
        DWORD fullPathLen = GetFullPathNameW(benchArgs.filePath, MAX_PATH, normalizedPath, nullptr);
        if (fullPathLen == 0 || fullPathLen >= MAX_PATH) {
            // 规范化失败/路径过长:退化为直接使用原始路径参与哈希与显示。
            wcsncpy_s(normalizedPath, MAX_PATH, benchArgs.filePath, _TRUNCATE);
        }
        uint64_t pathHash = HashPathCaseInsensitive(normalizedPath);
        fileMutex = AcquireFileMutexAndMaybeFocusExisting(pathHash);
    }

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2dFactory);

    // T39:启动期一次性读 %LOCALAPPDATA%\mdvn\state.ini(单次 < 1KB);
    // 文件不存在就按默认值走,不创建目录、不写盘。
    mdvn::AppSettings settings;
    mdvn::LoadAppSettings(&settings);

    // 字体子系统本体放在栈上(非全局对象),不违反"禁止有副作用的全局
    // 构造函数"约束。族名覆盖必须在 Init 之前生效。
    mdvn::FontSubsystem fonts;
    fonts.SetFamilyOverrides(settings.fontBodyPrimary, settings.fontBodyFallback,
                             settings.fontMonoPrimary, settings.fontMonoFallback);
    fonts.Init();

    // 文档相关数据全部落在这一块 Arena 上;fileMap 与它一样活到 wWinMain
    // 结尾,保证 Document::source 引用的内存(无论来自映射视图还是 arena
    // 解码缓冲区)在整个消息循环期间都有效。
    mdvn::Arena docArena;
    docArena.Init(4 * 1024 * 1024);

    mdvn::FileMap fileMap;
    bool fileOpened = hasTarget &&
                       (fileMap.Open(normalizedPath) == mdvn::FileMapError::None);

    mdvn::Document doc = fileOpened
        ? LoadMarkdownFile(&fileMap, &docArena)
        : mdvn::ParseMarkdown(
              mdvn::SkipFrontMatter(mdvn::StrSlice{
                  kSampleMarkdown, static_cast<mdvn::u32>(sizeof(kSampleMarkdown) - 1)}),
              &docArena);
    mdvn::bench::MarkParseDone();

    if (fileOpened) {
        SetDisplayTextFromPath(normalizedPath);
    }

    // M1 图片子系统(T30~T34/T36b)。全部放在栈上,不引入全局构造函数。
    // 注意:这些对象的构造**不触碰 WIC/WinHTTP** —— ImageDecoder 惰性初始化、
    // RemoteImageLoader 开关默认关闭,所以无图文档全程不加载 windowscodecs.dll /
    // winhttp.dll,配合根 CMakeLists 的 /DELAYLOAD 生效。
    // 两块 Arena 分工明确:imageArena 存"活到文档关闭"的东西(缓存条目、网络
    // 原始字节、临时文件路径清单),imageScratch 只存"用完即弃"的 data: URI 解码
    // 缓冲,每次解码前整体 Reset —— 两者绝不能混用,否则 Reset 会连缓存一起抹掉。
    mdvn::Arena imageArena;
    imageArena.Init(32 * 1024 * 1024);
    mdvn::Arena imageScratch;
    imageScratch.Init(32 * 1024 * 1024);

    mdvn::ImageCache imageCache;
    imageCache.Init(&imageArena);

    wchar_t documentDirectory[MAX_PATH]{};
    if (fileOpened) ExtractDirectory(normalizedPath, documentDirectory, MAX_PATH);

    mdvn::BlockLayoutEngine layout;
    layout.Relayout(doc, 760.0f, 1.0f, &imageCache);
    mdvn::bench::MarkLayoutDone();

    mdvn::Renderer renderer;
    renderer.Init(g_d2dFactory, &fonts, &imageCache);

    mdvn::ImageResidencyManager residency;
    residency.Init(&renderer, &imageCache, &imageScratch, documentDirectory);

    mdvn::RemoteImageLoader remoteLoader;

    // 临时文件清单(T36b)单独用一块小 Arena:它必须活过 T36 的"窗口内换文档"
    // (那一步会 Reset imageArena),否则退出时要删的路径会被一起抹掉。
    mdvn::Arena tempArena;
    tempArena.Init(1 * 1024 * 1024);
    mdvn::TempFileRegistry tempFiles(&tempArena);

    // T37/T38:查找会话的两块 Arena —— results 存命中数组与 UTF-8 查询串
    // (每次重搜整体 Reset),scratch 是逐块拼接缓冲(搜索过程中反复 Reset),
    // 两者必须分开,否则重搜时会把刚存好的命中数组抹掉。
    mdvn::Arena findArena;
    findArena.Init(16 * 1024 * 1024);
    mdvn::Arena findScratch;
    findScratch.Init(16 * 1024 * 1024);
    mdvn::FindSession find(&findArena, &findScratch);

    // T45:代码块复制按钮拼接剪贴板文本用的临时 Arena(每次复制前整体 Reset)。
    // 单独一块而不是复用 imageScratch/findScratch,是为了不让"复制一次代码块"
    // 这个动作把别人正在用的临时缓冲抹掉。
    mdvn::Arena clipboardScratch;
    clipboardScratch.Init(16 * 1024 * 1024);

    // 窗口运行期状态放在栈上,生命周期覆盖整个消息循环;shell 层只借用不拥有。
    mdvn::WindowState windowState{
        &fonts, &layout, &doc, &renderer, 0.0f,
        &imageCache, &residency, &remoteLoader, &tempFiles, &imageScratch, documentDirectory,
        &find, nullptr, &OpenDocumentInPlace,
        &OnWindowCreatedBenchHook, &OnFirstPresentBenchHook, nullptr, false,
        // T42:只在 --bench 且目标语料是 BENCH-B 时开启"首屏之外强制全量解码"
        // (T44 修复,见 BenchTargetWantsFullDecode 注释);正常运行(双击打开
        // 文件/无 --bench)和 BENCH-A 这类非图片密集场景,此字段均为 false,
        // 首屏虚拟化行为与之前完全一致。
        benchArgs.benchEnabled && BenchTargetWantsFullDecode(benchArgs.filePath),
        // T45:剪贴板拼接 Arena;后面两个复制按钮状态字段由 CreateMainWindow
        // 统一初始化成 kInvalidIndex,这里留给聚合初始化补零即可。
        &clipboardScratch};

    if (!mdvn::RegisterMainWindowClass(hInstance)) {
        if (fileMutex) CloseHandle(fileMutex);
        return 1;
    }
    HWND hwnd = mdvn::CreateMainWindow(hInstance, g_displayText, &windowState);
    if (!hwnd) {
        if (fileMutex) CloseHandle(fileMutex);
        return 1;
    }

    // T34/T39:绑定"下载完成"通知窗口与 load_remote_images 开关(现在读自 state.ini,
    // 默认仍是 0)。开关为 false 时 RemoteImageLoader::Request 第一行就返回 false,
    // 进程全程不会调用任何 WinHttp* 函数。
    remoteLoader.Init(hwnd, settings.loadRemoteImages);

    // T36:窗口内换文档所需的上下文,必须在 hwnd 拿到之后才能填完。
    DocumentHost documentHost{hwnd,        &fileMap,     &docArena,   &doc,
                              &layout,     &fonts,       &imageCache, &imageArena,
                              documentDirectory};
    windowState.callbackUserData = &documentHost;

    // 把路径哈希写进窗口属性,供其它进程的 TryFocusExistingWindowForHash
    // 找到本窗口(系统会在窗口销毁时自动清理属性,不需要手动 RemoveProp)。
    if (hasTarget) {
        SetPropW(hwnd, kPathHashPropName,
                 reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(HashPathCaseInsensitive(normalizedPath))));
    }

    int exitCode = mdvn::RunMessageLoop();

    // T36b:正常退出前统一删除本次会话创建过的全部临时文件(裁决:不追踪外部
    // 查看器进程是否退出,异常终止的残留交给 %TEMP% 的系统级清理兜底)。
    tempFiles.CleanupAll();

    if (g_d2dFactory) g_d2dFactory->Release();
    if (fileMutex) CloseHandle(fileMutex);
    // argv/cmdlineArena 随函数返回时的栈析构自动回收,不需要手动释放
    // (对应过去 CommandLineToArgvW 结果专用的 LocalFree)。

    return exitCode;
}
