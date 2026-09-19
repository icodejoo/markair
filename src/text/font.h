// markair 字体子系统:DirectWrite 工厂 + 裁决 #10 白名单回退链 +
// IDWriteTextFormat 复用池。全程不调用 GetSystemFontCollection 做全量枚举。
#pragma once

#include <dwrite_2.h>

#include "../util/types.h"

namespace markair {

/**
 * 字体角色:决定用哪一条裁决 #10 白名单链路创建文本格式。
 */
enum class FontRole {
    Body,  // 正文:西文 Segoe UI,中文回退 Microsoft YaHei UI → Microsoft YaHei → SimSun
    Mono,  // 等宽(围栏代码块):Cascadia Mono → Consolas → Courier New
};

/**
 * 字体子系统:创建 DirectWrite 工厂,登记裁决 #10 的固定字体白名单回退链,
 * 并管理 IDWriteTextFormat 的复用池(启动只建正文一个,其余角色惰性创建)。
 *
 * 代码中不得出现 IDWriteFactory::GetSystemFontCollection —— 字体匹配全部
 * 交给按需内存映射的系统字体服务,不做进程内全量枚举。
 *
 * @example
 *   markair::FontSubsystem fonts;
 *   if (fonts.Init()) {
 *       IDWriteTextLayout* layout = fonts.CreateTextLayout(
 *           L"你好 Hello", 8, markair::FontRole::Body, 600.0f, 200.0f);
 *       if (layout) layout->Release();
 *   }
 */
class FontSubsystem {
public:
    // 构造一个未初始化的子系统,所有指针清零。
    FontSubsystem();

    // 释放所有已创建的 DirectWrite 对象(工厂/回退链/文本格式)。
    ~FontSubsystem();

    FontSubsystem(const FontSubsystem&) = delete;
    FontSubsystem& operator=(const FontSubsystem&) = delete;

    /**
     * 创建 DirectWrite 工厂并构建正文角色的文本格式与回退链。只应调用一次。
     * @return 成功返回 true;DirectWrite 工厂或正文文本格式创建失败返回 false。
     * @example fonts.Init();
     */
    bool Init();

    /**
     * 设置字号缩放(T29):钳制到最近的离散档位
     * {0.8, 0.9, 1.0, 1.15, 1.3, 1.5, 1.75, 2.0},档位固定是为了避免任意浮点
     * 缩放导致 layout 缓存抖动。调用后会释放当前的 IDWriteTextFormat/回退链
     * 并按新档位重建正文格式(等宽格式惰性重建),调用方需自行释放并重建
     * 所有 IDWriteTextLayout(BlockLayoutEngine::Relayout already 会先淘汰
     * 全部 layout)。
     * @param scale 期望的缩放系数,会被钳制到最近的离散档位。
     * @return 钳制后实际生效的缩放系数。
     * @example float actual = fonts.SetScale(1.2f); // 得 1.15
     */
    float SetScale(float scale);

    // 当前生效的缩放系数(离散档位之一)。
    float Scale() const;

    /**
     * 把任意浮点缩放钳制到 T29 固定的 8 个离散档位
     * {0.8, 0.9, 1.0, 1.15, 1.3, 1.5, 1.75, 2.0} 中最近的一个。静态函数,
     * 不依赖 `FontSubsystem` 实例、不触发任何 DirectWrite 调用,供
     * `state.ini` 的 `zoom` 键解析(T57)复用同一份档位表,避免两处各存
     * 一份容易走样的常量。
     * @param scale 任意缩放浮点值。
     * @return 8 个档位中距 `scale` 最近的一个。
     * @example float z = markair::FontSubsystem::ClampToNearestZoomLevel(1.31f); // 1.3f
     */
    static float ClampToNearestZoomLevel(float scale);

    // 放大一档(Ctrl+=);已在最大档时保持不变。返回生效后的缩放系数。
    float ZoomIn();

    // 缩小一档(Ctrl+-);已在最小档时保持不变。返回生效后的缩放系数。
    float ZoomOut();

    // 复位到 1.0 档(Ctrl+0)。返回生效后的缩放系数(恒为 1.0)。
    float ResetZoom();

    // 等宽字体的默认主族名(裁决 #10 原文),不含 T39 的配置覆盖。
    static const wchar_t* MonoFamilyName();

    /**
     * 等宽角色的默认回退链族名,按优先级排列(不含 T39 的配置覆盖项)。
     * 只读,不触发任何 DirectWrite 调用,可在未 Init 时使用。
     *
     * @param outCount 输出元素个数,可为 nullptr。
     * @return 指向内部常量族名表的指针,恒非空,生命周期同进程。
     * @example
     *   markair::u32 n = 0;
     *   const wchar_t* const* chain = FontSubsystem::MonoFallbackFamilies(&n);
     */
    static const wchar_t* const* MonoFallbackFamilies(u32* outCount);

    /**
     * 用 `state.ini` 的 `font_body_*` / `font_mono_*` 覆盖字体族名(T39)。
     * **须在 `Init` 之前调用**;任一参数为 nullptr 或空串表示该项不覆盖,沿用
     * 裁决 #10 的白名单默认值。覆盖的回退族名会被**插到默认回退链最前面**,
     * 默认链仍然保留在后面兜底,因此配置写错字体名也不会导致中文显示不出来。
     *
     * @param bodyPrimary 正文主族名覆盖,可为 nullptr。
     * @param bodyFallback 正文回退族名覆盖(通常是中文字体),可为 nullptr。
     * @param monoPrimary 等宽主族名覆盖,可为 nullptr。
     * @param monoFallback 等宽回退族名覆盖,可为 nullptr。
     * @example
     *   fonts.SetFamilyOverrides(settings.fontBodyPrimary, settings.fontBodyFallback,
     *                            settings.fontMonoPrimary, settings.fontMonoFallback);
     *   fonts.Init();
     */
    void SetFamilyOverrides(const wchar_t* bodyPrimary, const wchar_t* bodyFallback,
                             const wchar_t* monoPrimary, const wchar_t* monoFallback);

    /**
     * 当前实际生效的等宽主族名(已考虑 T39 的配置覆盖),供布局层给行内代码
     * run 单独切等宽族用。
     * @return 以 '\0' 结尾的族名,恒非空。
     * @example layout->SetFontFamilyName(fonts.MonoFamily(), range);
     */
    const wchar_t* MonoFamily() const;

    /**
     * 按角色取得对应的 IDWriteTextFormat,非正文角色在首次请求时才惰性创建。
     * @param role 字体角色(正文/等宽)。
     * @return 对应的文本格式指针;创建失败返回 nullptr。所有权归本对象持有,
     *         调用方不应 Release。
     * @example IDWriteTextFormat* mono = fonts.GetTextFormat(markair::FontRole::Mono);
     */
    IDWriteTextFormat* GetTextFormat(FontRole role);

    /**
     * 按角色创建一段文本的排版对象,并自动绑定该角色的白名单回退链。
     * @param text UTF-16 文本指针,不要求以 '\0' 结尾。
     * @param length text 的 UTF-16 code unit 长度。
     * @param role 字体角色,决定使用哪一条白名单回退链(正文/等宽)。
     * @param maxWidth 排版可用的最大宽度(DIP)。
     * @param maxHeight 排版可用的最大高度(DIP)。
     * @return 新建的布局对象,所有权转移给调用方(需自行 Release);失败返回 nullptr。
     * @example
     *   IDWriteTextLayout* layout = fonts.CreateTextLayout(text, len, markair::FontRole::Body, 760.0f, 400.0f);
     */
    IDWriteTextLayout* CreateTextLayout(const wchar_t* text, u32 length,
                                         FontRole role,
                                         float maxWidth, float maxHeight);

    // 取得内部 DirectWrite 工厂,供确需直接调用 DWrite API 的调用方使用。
    IDWriteFactory* Factory() const { return factory_; }

private:
    // 登记裁决 #10 的中文回退链(Microsoft YaHei UI → Microsoft YaHei → SimSun),
    // 只用 IDWriteFontFallbackBuilder 登记这 3 个族名,不做全量枚举。
    IDWriteFontFallback* BuildBodyFallback();

    // 登记裁决 #10 的等宽回退链(Consolas → Courier New),同样只登记白名单族名。
    IDWriteFontFallback* BuildMonoFallback();

    // 创建正文 IDWriteTextFormat(主族 Segoe UI)并绑定中文回退链。
    bool CreateBodyFormat();

    // 创建等宽 IDWriteTextFormat(主族 Cascadia Mono)并绑定等宽回退链,惰性调用。
    bool CreateMonoFormat();

    // 按 zoomIndex_ 钳制并应用新缩放档位:释放已创建的文本格式/回退链,
    // 重新即时创建正文格式,等宽格式回到惰性状态。
    float ApplyZoomIndex(i32 newIndex);

    IDWriteFactory2* factory_;          // DirectWrite 工厂(取 2 版接口以支持自定义回退)
    IDWriteFontFallback* bodyFallback_; // 正文角色的中文回退链
    IDWriteFontFallback* monoFallback_; // 等宽角色的回退链
    IDWriteTextFormat* bodyFormat_;     // 启动即创建
    IDWriteTextFormat* monoFormat_;     // 惰性创建
    u32 zoomIndex_;                      // 当前缩放档位下标,见 font.cpp 的 kZoomLevels
    // T39 的族名覆盖(空串 = 不覆盖)。固定长度数组,不做任何动态分配。
    wchar_t bodyPrimaryOverride_[64];
    wchar_t bodyFallbackOverride_[64];
    wchar_t monoPrimaryOverride_[64];
    wchar_t monoFallbackOverride_[64];
};

}  // namespace markair
