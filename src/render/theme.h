// mdvn 调色板模块(T46):把散落在 renderer.cpp 里的颜色常量集中到一处,
// 为后续主题切换(T49)打基础——Renderer 只持有一个 `const Palette*`,
// 切换主题时只改指针,不做任何其他事。
#pragma once

#include <d2d1.h>

namespace mdvn {

/**
 * 把一个 0xRRGGBB 十六进制常量换算成 `D2D1_COLOR_F`,换算公式与
 * `D2D1::ColorF(UINT32)` 一致,但写成纯 constexpr 函数以保证
 * `kLightPalette`/`kDarkPalette` 能在编译期构造(零副作用全局构造)。
 * @param rgb 形如 0xRRGGBB 的颜色值。
 * @param alpha 不透明度,默认 1.0(不透明)。
 * @return 对应的 `D2D1_COLOR_F`。
 * @example constexpr D2D1_COLOR_F white = mdvn::MakeColor(0xFFFFFFu);
 */
constexpr D2D1_COLOR_F MakeColor(UINT32 rgb, float alpha = 1.0f) {
    return D2D1_COLOR_F{
        static_cast<float>((rgb >> 16) & 0xFFu) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFFu) / 255.0f,
        static_cast<float>(rgb & 0xFFu) / 255.0f,
        alpha,
    };
}

/**
 * 调色板:渲染层用到的全部语义颜色槽位(T46 从 renderer.cpp 抽取)。
 *
 * 正文与各级标题共用 `text` 槽位——现有渲染代码里标题和正文本就画在同一个
 * `IDWriteTextLayout` 上、共用同一支 `textBrush`(字号差异由布局层的
 * `FontRole` 决定,不是颜色差异),因此不为标题单独开槽位,避免引入一个
 * 实际从未被渲染路径读取的"影子字段"。
 *
 * 必须是 `constexpr` 或无副作用的 `static const`,不得写成有副作用的全局
 * 构造函数(架构硬性约束)。
 */
struct Palette {
    D2D1_COLOR_F background;                  // 页面背景(清屏色)
    D2D1_COLOR_F text;                        // 正文与各级标题共用的文字色
    D2D1_COLOR_F quoteBar;                    // 引用块左侧竖线
    D2D1_COLOR_F codeBackground;              // 行内代码/代码块的底色
    D2D1_COLOR_F thematicBreak;                // 分割线颜色
    D2D1_COLOR_F link;                        // 链接/自动链接文字色
    D2D1_COLOR_F tableHeaderBackground;       // 表格表头底色
    D2D1_COLOR_F tableGrid;                   // 表格网格线颜色
    D2D1_COLOR_F checkboxBorder;              // 任务列表勾选框边框色
    D2D1_COLOR_F checkboxCheck;               // 任务列表勾选框对勾色
    D2D1_COLOR_F imagePlaceholderBackground;  // 图片占位块底色
    D2D1_COLOR_F imagePlaceholderBorder;      // 图片占位块边框色
    D2D1_COLOR_F imagePlaceholderIcon;        // 图片占位块中央图标色
    D2D1_COLOR_F downsampledBadgeBackground;  // 降采样提示标签底色
    D2D1_COLOR_F downsampledBadgeText;        // 降采样提示标签文字色
    D2D1_COLOR_F codeCopyIcon;                // 代码块复制按钮:默认态图标线条色
    D2D1_COLOR_F codeCopyHoverBackground;     // 代码块复制按钮:悬浮态底色
    D2D1_COLOR_F codeCopyPaper;               // 代码块复制按钮:图标"纸面"填充色(与页面底色一致)
    D2D1_COLOR_F codeCopyDone;                // 代码块复制按钮:已复制态(边框+对勾)
    D2D1_COLOR_F findHighlight;               // 查找命中高亮底色
    D2D1_COLOR_F findCurrentHighlight;        // 查找当前命中高亮底色
    D2D1_COLOR_F overlayBarBackground;        // 浮出条(查找条/大纲侧栏共用)底色
    D2D1_COLOR_F overlayBarText;              // 浮出条文字色
    D2D1_COLOR_F outlineHighlightBackground;  // T63:大纲侧栏当前阅读位置条目底色
    D2D1_COLOR_F outlineHighlightText;        // T63:大纲侧栏当前阅读位置条目前景色
    // T53 代码语法着色:与 hl/lexer.h 的 TokenType 七类逐一对应,只在
    // 语言被识别(languageId != kLanguageNone)时才会用到。
    D2D1_COLOR_F hlKeyword;                   // 关键字
    D2D1_COLOR_F hlString;                    // 字符串
    D2D1_COLOR_F hlNumber;                    // 数字字面量
    D2D1_COLOR_F hlComment;                   // 注释
    D2D1_COLOR_F hlPunct;                     // 标点/操作符
    D2D1_COLOR_F hlBuiltin;                   // 内置类型名/内置标识符/结构化字面量
    D2D1_COLOR_F hlOther;                     // 其他(默认,含普通标识符)
};

/**
 * 浅色调色板:M0~M1 阶段的默认配色,数值与重构前 renderer.cpp 里各颜色
 * 自由函数的返回值逐一对应(纯重构,未改动任何颜色值)。
 */
inline constexpr Palette kLightPalette{
    /* background                 */ MakeColor(0xFFFFFFu),
    /* text                       */ MakeColor(0x000000u),
    /* quoteBar                   */ MakeColor(0x808080u),
    /* codeBackground             */ MakeColor(0xF0F0F0u),
    /* thematicBreak              */ MakeColor(0xC0C0C0u),
    /* link                       */ MakeColor(0x0366D6u),
    /* tableHeaderBackground      */ MakeColor(0xF6F8FAu),
    /* tableGrid                  */ MakeColor(0xD0D7DEu),
    /* checkboxBorder             */ MakeColor(0x808080u),
    /* checkboxCheck              */ MakeColor(0x22863Au),
    /* imagePlaceholderBackground */ MakeColor(0xF0F0F0u),
    /* imagePlaceholderBorder     */ MakeColor(0xC0C0C0u),
    /* imagePlaceholderIcon       */ MakeColor(0xA8AEB4u),
    /* downsampledBadgeBackground */ MakeColor(0x1F2328u, 0.55f),
    /* downsampledBadgeText       */ MakeColor(0xFFFFFFu, 0.95f),
    /* codeCopyIcon               */ MakeColor(0x6A737Du),
    /* codeCopyHoverBackground    */ MakeColor(0xD8DEE4u),
    /* codeCopyPaper              */ MakeColor(0xFFFFFFu),
    /* codeCopyDone               */ MakeColor(0x22863Au),
    /* findHighlight              */ MakeColor(0xFFE066u, 0.55f),
    /* findCurrentHighlight       */ MakeColor(0xFF8C42u, 0.55f),
    /* overlayBarBackground       */ MakeColor(0x24292Fu, 0.92f),
    /* overlayBarText             */ MakeColor(0xFFFFFFu, 0.95f),
    /* outlineHighlightBackground */ MakeColor(0x0366D6u, 0.16f),
    /* outlineHighlightText       */ MakeColor(0x0366D6u),
    /* hlKeyword                  */ MakeColor(0xD73A49u),
    /* hlString                   */ MakeColor(0x032F62u),
    /* hlNumber                   */ MakeColor(0x005CC5u),
    /* hlComment                  */ MakeColor(0x6A737Du),
    /* hlPunct                    */ MakeColor(0x24292Eu),
    /* hlBuiltin                  */ MakeColor(0x6F42C1u),
    /* hlOther                    */ MakeColor(0x000000u),
};

/**
 * 深色调色板背景色的原始 0xRRGGBB 值。单独具名一份是因为 T48 窗口类背景刷
 * 也要用同一个颜色(深色主题下 `CreateSolidBrush` 用的就是它),避免和这里的
 * `MakeColor` 调用各写一份字面量导致两处颜色不一致。
 */
inline constexpr UINT32 kDarkBackgroundRgb = 0x0D1117u;

/**
 * 深色调色板(T46 新增,T49 起接入切换):以 GitHub Dark 的配色基调为参考,
 * 背景/正文对比度按 WCAG 相对亮度公式核算 ≥ 4.5:1(实测约 12.3:1,
 * 见 `tests/test_theme.cpp`),装饰性/半透明槽位(降采样标签、查找高亮、
 * 浮出条)本身已是深底配色,浅色/深色两套主题下都可读,予以保留复用。
 */
inline constexpr Palette kDarkPalette{
    /* background                 */ MakeColor(kDarkBackgroundRgb),
    /* text                       */ MakeColor(0xC9D1D9u),
    /* quoteBar                   */ MakeColor(0x8B949Eu),
    /* codeBackground             */ MakeColor(0x161B22u),
    /* thematicBreak              */ MakeColor(0x30363Du),
    /* link                       */ MakeColor(0x58A6FFu),
    /* tableHeaderBackground      */ MakeColor(0x161B22u),
    /* tableGrid                  */ MakeColor(0x30363Du),
    /* checkboxBorder             */ MakeColor(0x8B949Eu),
    /* checkboxCheck              */ MakeColor(0x3FB950u),
    /* imagePlaceholderBackground */ MakeColor(0x161B22u),
    /* imagePlaceholderBorder     */ MakeColor(0x30363Du),
    /* imagePlaceholderIcon       */ MakeColor(0x6E7681u),
    /* downsampledBadgeBackground */ MakeColor(0x1F2328u, 0.55f),
    /* downsampledBadgeText       */ MakeColor(0xFFFFFFu, 0.95f),
    /* codeCopyIcon               */ MakeColor(0x8B949Eu),
    /* codeCopyHoverBackground    */ MakeColor(0x30363Du),
    /* codeCopyPaper              */ MakeColor(0x0D1117u),
    /* codeCopyDone               */ MakeColor(0x3FB950u),
    /* findHighlight              */ MakeColor(0xFFE066u, 0.55f),
    /* findCurrentHighlight       */ MakeColor(0xFF8C42u, 0.55f),
    /* overlayBarBackground       */ MakeColor(0x24292Fu, 0.92f),
    /* overlayBarText             */ MakeColor(0xFFFFFFu, 0.95f),
    /* outlineHighlightBackground */ MakeColor(0x58A6FFu, 0.18f),
    /* outlineHighlightText       */ MakeColor(0x58A6FFu),
    /* hlKeyword                  */ MakeColor(0xFF7B72u),
    /* hlString                   */ MakeColor(0xA5D6FFu),
    /* hlNumber                   */ MakeColor(0x79C0FFu),
    /* hlComment                  */ MakeColor(0x8B949Eu),
    /* hlPunct                    */ MakeColor(0xC9D1D9u),
    /* hlBuiltin                  */ MakeColor(0xD2A8FFu),
    /* hlOther                    */ MakeColor(0xC9D1D9u),
};

/**
 * 按 WCAG 2.x 相对亮度公式算单通道的"相对亮度"贡献值(sRGB -> 线性空间)。
 * 纯数字函数,不依赖任何 D2D 设备,可脱离渲染上下文单测。
 * @param c 单个颜色通道,取值 [0, 1]。
 * @return 线性化后的通道值,取值 [0, 1]。
 * @example float lin = mdvn::LinearizeChannel(0.5f);
 */
float LinearizeChannel(float c);

/**
 * 按 WCAG 2.x 相对亮度公式计算两个颜色的对比度。
 * 纯数字函数,不依赖任何 D2D 设备,可脱离渲染上下文单测。
 * @param a 颜色 A(忽略 alpha,按不透明处理)。
 * @param b 颜色 B(忽略 alpha,按不透明处理)。
 * @return 对比度,取值范围 [1, 21],数值越大对比越强。
 * @example float ratio = mdvn::ContrastRatio(mdvn::kDarkPalette.background, mdvn::kDarkPalette.text);
 */
float ContrastRatio(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b);

}  // namespace mdvn
