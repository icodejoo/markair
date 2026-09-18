// mdvn 字体子系统实现:见 font.h。字体族名基础来自
// 01-requirements.md §8 裁决 #10,2026-09-19 追加修订:正文主族改为跟随
// 系统默认消息字体(见 QuerySystemBodyFamily),不再写死 "Segoe UI" 字面量;
// 查询走 SystemParametersInfoW(SPI_GETNONCLIENTMETRICS),不触发
// GetSystemFontCollection 全量枚举,与裁决 #10 "禁止枚举"的精神不冲突。
#include "font.h"

#include <windows.h>

namespace mdvn {

namespace {

// 正文西文主族的兜底默认值:SystemParametersInfoW 查询失败时使用
// (裁决 #10 原文的静态白名单值)。
const wchar_t kBodyPrimaryFamily[] = L"Segoe UI";

// 中文回退族名(裁决 #10),正文链与等宽链共用这一份字面量,避免重复。
const wchar_t kCjkYaHeiUi[] = L"Microsoft YaHei UI";
const wchar_t kCjkYaHei[] = L"Microsoft YaHei";
const wchar_t kCjkSimSun[] = L"SimSun";

// 2026-09-18 bug 修复追加:emoji 兜底族名,不属于裁决 #10 原文,只追加在
// 正文回退链末尾,前面 3 个中文族名的顺序/名字不受影响。
const wchar_t kEmojiFallback[] = L"Segoe UI Emoji";

// 正文中文回退链(裁决 #10):Microsoft YaHei UI → Microsoft YaHei → SimSun,
// 末尾追加 Segoe UI Emoji 作 emoji 字形兜底(2026-09-18)。
// IDWriteFontFallbackBuilder::AddMapping 要求 const WCHAR**(非 const WCHAR* const*),
// 数组元素本身不加 const 以匹配该签名。
const wchar_t* kBodyFallbackFamilies[] = {
    kCjkYaHeiUi,
    kCjkYaHei,
    kCjkSimSun,
    kEmojiFallback,
};

// 等宽主族(裁决 #10)。
const wchar_t kMonoPrimaryFamily[] = L"Cascadia Mono";

// 等宽回退链(裁决 #10 + 2026-09-17 修订):西文等宽 Consolas → Courier New 之后
// 再接中文回退,代码块里的中文注释/字符串才不会显示成方块。中文族名复用正文
// 已加载的那三个,不新增字体文件,内存开销可忽略。
const wchar_t* kMonoFallbackFamilies[] = {
    L"Consolas",
    L"Courier New",
    kCjkYaHeiUi,
    kCjkYaHei,
    kCjkSimSun,
};

// 等宽回退链元素个数(BuildMonoFallback 与只读暴露入口共用)。
constexpr u32 kMonoFallbackCount =
    static_cast<u32>(sizeof(kMonoFallbackFamilies) / sizeof(kMonoFallbackFamilies[0]));

// 覆盖整个 Unicode 码位空间的单条范围,用于把回退链登记为"主族匹配不到
// 才尝试"的兜底,而不按字符范围拆分(拆分对我们的白名单场景没有必要)。
const DWRITE_UNICODE_RANGE kFullUnicodeRange = {0x0, 0x10FFFF};

// 正文文本格式的字号(DIP),对齐架构 §4 第 4 条"最多 3 种字号"的最小实现。
// 2026-09-19:16 → 14。
constexpr float kBodyFontSize = 14.0f;

// 等宽文本格式的字号(DIP)。2026-09-19:16 → 12 → 14(与正文同号)。
constexpr float kMonoFontSize = 14.0f;

// 存放 QuerySystemBodyFamily 查到的族名;进程生命周期内只查一次(查询本身
// 走 SystemParametersInfoW,开销远小于 DirectWrite 调用,没必要缓存到实例里)。
wchar_t g_systemBodyFamily[LF_FACESIZE] = {};
bool g_systemBodyFamilyQueried = false;

// 查询 Windows 当前配置的系统消息字体族名(即"系统默认字体"),
// 用 SystemParametersInfoW(SPI_GETNONCLIENTMETRICS) 读取 lfMessageFont,
// 不调用任何字体枚举 API。查询失败或返回空族名时回退到 kBodyPrimaryFamily。
const wchar_t* QuerySystemBodyFamily() {
    if (g_systemBodyFamilyQueried) {
        return g_systemBodyFamily[0] != 0 ? g_systemBodyFamily : kBodyPrimaryFamily;
    }
    g_systemBodyFamilyQueried = true;

    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) &&
        metrics.lfMessageFont.lfFaceName[0] != 0) {
        u32 i = 0;
        while (metrics.lfMessageFont.lfFaceName[i] != 0 && i + 1 < LF_FACESIZE) {
            g_systemBodyFamily[i] = metrics.lfMessageFont.lfFaceName[i];
            ++i;
        }
        g_systemBodyFamily[i] = 0;
    }
    return g_systemBodyFamily[0] != 0 ? g_systemBodyFamily : kBodyPrimaryFamily;
}

// 正文文本格式使用的 locale,西文场景下用 en-us 即可,中文字符靠回退链解决。
const wchar_t kBodyLocale[] = L"en-us";

// 等宽文本格式使用的 locale。
const wchar_t kMonoLocale[] = L"en-us";

// T29 缩放档位表(裁决:离散档位避免任意浮点缩放导致 layout 缓存抖动)。
constexpr float kZoomLevels[] = {0.8f, 0.9f, 1.0f, 1.15f, 1.3f, 1.5f, 1.75f, 2.0f};
constexpr u32 kZoomLevelCount = static_cast<u32>(sizeof(kZoomLevels) / sizeof(kZoomLevels[0]));

// 1.0 档在表中的下标,ResetZoom 与初始状态都用它。
constexpr u32 kDefaultZoomIndex = 2;

// 族名覆盖缓冲的容量(与 font.h 里的成员数组保持一致)。
constexpr u32 kFamilyOverrideCap = 64;

// 把一个可为空的宽字符串拷进固定缓冲(超长截断),空指针/空串写成空串。
void CopyOverride(const wchar_t* src, wchar_t* dst, u32 cap) {
    u32 i = 0;
    while (src && src[i] != 0 && i + 1 < cap) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}

// 覆盖值非空时用覆盖值,否则用默认值。
const wchar_t* PickFamily(const wchar_t* override_, const wchar_t* fallbackDefault) {
    return (override_ && override_[0] != 0) ? override_ : fallbackDefault;
}

// 在 kZoomLevels 里找与 scale 最接近的档位下标;SetScale 与
// ClampToNearestZoomLevel 共用同一份查找逻辑,避免两处各写一遍最近邻算法。
u32 FindNearestZoomLevelIndex(float scale) {
    u32 best = 0;
    float bestDiff = -1.0f;
    for (u32 i = 0; i < kZoomLevelCount; ++i) {
        float diff = scale - kZoomLevels[i];
        if (diff < 0.0f) diff = -diff;
        if (bestDiff < 0.0f || diff < bestDiff) {
            bestDiff = diff;
            best = i;
        }
    }
    return best;
}

}  // namespace

// 构造一个未初始化的子系统,所有指针清零。
FontSubsystem::FontSubsystem()
    : factory_(nullptr),
      bodyFallback_(nullptr),
      monoFallback_(nullptr),
      bodyFormat_(nullptr),
      monoFormat_(nullptr),
      zoomIndex_(kDefaultZoomIndex) {
    bodyPrimaryOverride_[0] = 0;
    bodyFallbackOverride_[0] = 0;
    monoPrimaryOverride_[0] = 0;
    monoFallbackOverride_[0] = 0;
}

void FontSubsystem::SetFamilyOverrides(const wchar_t* bodyPrimary, const wchar_t* bodyFallback,
                                        const wchar_t* monoPrimary, const wchar_t* monoFallback) {
    CopyOverride(bodyPrimary, bodyPrimaryOverride_, kFamilyOverrideCap);
    CopyOverride(bodyFallback, bodyFallbackOverride_, kFamilyOverrideCap);
    CopyOverride(monoPrimary, monoPrimaryOverride_, kFamilyOverrideCap);
    CopyOverride(monoFallback, monoFallbackOverride_, kFamilyOverrideCap);
}

const wchar_t* FontSubsystem::MonoFamily() const {
    return PickFamily(monoPrimaryOverride_, kMonoPrimaryFamily);
}

// 释放所有已创建的 DirectWrite 对象(工厂/回退链/文本格式)。
FontSubsystem::~FontSubsystem() {
    if (monoFormat_) monoFormat_->Release();
    if (bodyFormat_) bodyFormat_->Release();
    if (monoFallback_) monoFallback_->Release();
    if (bodyFallback_) bodyFallback_->Release();
    if (factory_) factory_->Release();
}

// 创建 DirectWrite 工厂并构建正文角色的文本格式与回退链。只应调用一次。
bool FontSubsystem::Init() {
    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2),
        reinterpret_cast<IUnknown**>(&factory_));
    if (FAILED(hr) || !factory_) return false;

    return CreateBodyFormat();
}

// 登记裁决 #10 的中文回退链,只用 IDWriteFontFallbackBuilder 登记这 3 个族名,
// 不调用 GetSystemFontCollection 做全量枚举。
IDWriteFontFallback* FontSubsystem::BuildBodyFallback() {
    if (!factory_) return nullptr;

    IDWriteFontFallbackBuilder* builder = nullptr;
    HRESULT hr = factory_->CreateFontFallbackBuilder(&builder);
    if (FAILED(hr) || !builder) return nullptr;

    // T39:配置里的回退族名插在默认链最前面,默认链仍在后面兜底 ——
    // 配置写错字体名也不会导致中文彻底显示不出来。
    const wchar_t* families[1 + sizeof(kBodyFallbackFamilies) / sizeof(kBodyFallbackFamilies[0])];
    u32 count = 0;
    if (bodyFallbackOverride_[0] != 0) families[count++] = bodyFallbackOverride_;
    for (u32 i = 0; i < sizeof(kBodyFallbackFamilies) / sizeof(kBodyFallbackFamilies[0]); ++i) {
        families[count++] = kBodyFallbackFamilies[i];
    }
    builder->AddMapping(&kFullUnicodeRange, 1, families, count, nullptr, nullptr, nullptr, 1.0f);

    IDWriteFontFallback* fallback = nullptr;
    builder->CreateFontFallback(&fallback);
    builder->Release();
    return fallback;
}

// 登记裁决 #10 的等宽回退链,同样只登记白名单族名。
IDWriteFontFallback* FontSubsystem::BuildMonoFallback() {
    if (!factory_) return nullptr;

    IDWriteFontFallbackBuilder* builder = nullptr;
    HRESULT hr = factory_->CreateFontFallbackBuilder(&builder);
    if (FAILED(hr) || !builder) return nullptr;

    const wchar_t* families[1 + kMonoFallbackCount];
    u32 count = 0;
    if (monoFallbackOverride_[0] != 0) families[count++] = monoFallbackOverride_;
    for (u32 i = 0; i < kMonoFallbackCount; ++i) {
        families[count++] = kMonoFallbackFamilies[i];
    }
    builder->AddMapping(&kFullUnicodeRange, 1, families, count, nullptr, nullptr, nullptr, 1.0f);

    IDWriteFontFallback* fallback = nullptr;
    builder->CreateFontFallback(&fallback);
    builder->Release();
    return fallback;
}

// 创建正文 IDWriteTextFormat(主族 Segoe UI)并绑定中文回退链,字号按当前缩放档位。
bool FontSubsystem::CreateBodyFormat() {
    if (!factory_) return false;

    HRESULT hr = factory_->CreateTextFormat(
        PickFamily(bodyPrimaryOverride_, QuerySystemBodyFamily()), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, kBodyFontSize * Scale(),
        kBodyLocale, &bodyFormat_);
    if (FAILED(hr) || !bodyFormat_) return false;

    bodyFallback_ = BuildBodyFallback();
    return true;
}

// 创建等宽 IDWriteTextFormat(主族 Cascadia Mono)并绑定等宽回退链,惰性调用,
// 字号同样按当前缩放档位。
bool FontSubsystem::CreateMonoFormat() {
    if (!factory_) return false;

    HRESULT hr = factory_->CreateTextFormat(
        MonoFamily(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, kMonoFontSize * Scale(),
        kMonoLocale, &monoFormat_);
    if (FAILED(hr) || !monoFormat_) return false;

    monoFallback_ = BuildMonoFallback();
    return true;
}

// 等宽字体主族名(裁决 #10),布局层按行内代码 run 单独 SetFontFamilyName 时使用。
const wchar_t* FontSubsystem::MonoFamilyName() { return kMonoPrimaryFamily; }

// 等宽默认回退链(不含 ini 覆盖),只读暴露给测试与诊断,不触碰 DirectWrite。
const wchar_t* const* FontSubsystem::MonoFallbackFamilies(u32* outCount) {
    if (outCount) *outCount = kMonoFallbackCount;
    return kMonoFallbackFamilies;
}

// 当前生效的缩放系数。
float FontSubsystem::Scale() const { return kZoomLevels[zoomIndex_]; }

// 按 newIndex(已钳制)重建正文格式,等宽格式回到惰性状态。
float FontSubsystem::ApplyZoomIndex(i32 newIndex) {
    if (newIndex < 0) newIndex = 0;
    if (newIndex >= static_cast<i32>(kZoomLevelCount)) newIndex = static_cast<i32>(kZoomLevelCount) - 1;
    zoomIndex_ = static_cast<u32>(newIndex);

    if (bodyFormat_) { bodyFormat_->Release(); bodyFormat_ = nullptr; }
    if (monoFormat_) { monoFormat_->Release(); monoFormat_ = nullptr; }
    if (bodyFallback_) { bodyFallback_->Release(); bodyFallback_ = nullptr; }
    if (monoFallback_) { monoFallback_->Release(); monoFallback_ = nullptr; }

    CreateBodyFormat();  // 正文格式即时重建,与 Init 的契约保持一致
    return Scale();
}

// 设置缩放系数,钳制到最近的离散档位。
float FontSubsystem::SetScale(float scale) {
    return ApplyZoomIndex(static_cast<i32>(FindNearestZoomLevelIndex(scale)));
}

// 把任意浮点缩放钳制到最近的离散档位(T57:供 ini.cpp 的 zoom 键解析复用,
// 不依赖实例、不触发 DirectWrite 调用)。
float FontSubsystem::ClampToNearestZoomLevel(float scale) {
    return kZoomLevels[FindNearestZoomLevelIndex(scale)];
}

// 放大一档。
float FontSubsystem::ZoomIn() { return ApplyZoomIndex(static_cast<i32>(zoomIndex_) + 1); }

// 缩小一档。
float FontSubsystem::ZoomOut() { return ApplyZoomIndex(static_cast<i32>(zoomIndex_) - 1); }

// 复位到 1.0 档。
float FontSubsystem::ResetZoom() { return ApplyZoomIndex(static_cast<i32>(kDefaultZoomIndex)); }

// 按角色取得对应的 IDWriteTextFormat,非正文角色在首次请求时才惰性创建。
IDWriteTextFormat* FontSubsystem::GetTextFormat(FontRole role) {
    switch (role) {
        case FontRole::Body:
            return bodyFormat_;
        case FontRole::Mono:
            if (!monoFormat_) CreateMonoFormat();
            return monoFormat_;
    }
    return nullptr;
}

// 按角色创建一段文本的排版对象,并自动绑定该角色的白名单回退链。
IDWriteTextLayout* FontSubsystem::CreateTextLayout(const wchar_t* text,
                                                    u32 length, FontRole role,
                                                    float maxWidth,
                                                    float maxHeight) {
    if (!factory_) return nullptr;

    IDWriteTextFormat* format = GetTextFormat(role);
    if (!format) return nullptr;

    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = factory_->CreateTextLayout(text, length, format, maxWidth,
                                             maxHeight, &layout);
    if (FAILED(hr) || !layout) return nullptr;

    IDWriteFontFallback* fallback =
        (role == FontRole::Mono) ? monoFallback_ : bodyFallback_;
    if (fallback) {
        IDWriteTextLayout2* layout2 = nullptr;
        if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout2),
                                              reinterpret_cast<void**>(&layout2))) &&
            layout2) {
            layout2->SetFontFallback(fallback);
            layout2->Release();
        }
    }

    return layout;
}

}  // namespace mdvn
