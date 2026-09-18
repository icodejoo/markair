// T63 大纲侧栏覆盖测试:条目几何(y 坐标/缩进)、超长标题省略号截断、
// 侧栏自身滚动裁剪、以及高亮定位二分查找的纯函数用例(含 300 条标题的
// 二分/线性对拍)。全部走纯函数/纯类,不依赖真实 HWND/D2D。
#include "mdvn_test.h"
#include "../src/shell/outline_panel.h"
#include "../src/doc/model.h"
#include "../src/doc/parser.h"
#include "../src/util/arena.h"
#include "../src/util/str.h"

using mdvn::Arena;
using mdvn::Document;
using mdvn::FindCurrentOutlineItem;
using mdvn::kInvalidIndex;
using mdvn::kOutlineIndentStepDip;
using mdvn::kOutlineItemHeightDip;
using mdvn::OutlineItem;
using mdvn::OutlineItemIndentDip;
using mdvn::OutlineItemTopDip;
using mdvn::OutlinePanel;
using mdvn::OutlinePanelContentHeightDip;
using mdvn::OutlineTextMeasurer;
using mdvn::ParseMarkdown;
using mdvn::StrSlice;
using mdvn::TruncateOutlineTitle;

namespace {

// 单测用的假测量函数:每个字符固定宽度,不依赖 DirectWrite,纯数字估算,
// 只用来验证 TruncateOutlineTitle 的二分截断逻辑本身是否正确。
constexpr float kFakeCharWidthDip = 10.0f;
float FakeMeasure(void*, const wchar_t*, mdvn::u32 len) {
    return static_cast<float>(len) * kFakeCharWidthDip;
}

}  // namespace

// 条目 y 坐标 = 下标 * 行高。
MDVN_TEST(OutlinePanel_ItemTopDipIsIndexTimesRowHeight) {
    MDVN_CHECK(OutlineItemTopDip(0) == 0.0f);
    MDVN_CHECK(OutlineItemTopDip(1) == kOutlineItemHeightDip);
    MDVN_CHECK(OutlineItemTopDip(5) == 5.0f * kOutlineItemHeightDip);
}

// 缩进随 level 递增:level 1 不缩进,每升一级多缩进一步。
MDVN_TEST(OutlinePanel_IndentIncreasesWithLevel) {
    MDVN_CHECK(OutlineItemIndentDip(1) == 0.0f);
    MDVN_CHECK(OutlineItemIndentDip(2) == kOutlineIndentStepDip);
    MDVN_CHECK(OutlineItemIndentDip(3) == 2.0f * kOutlineIndentStepDip);
    MDVN_CHECK(OutlineItemIndentDip(6) == 5.0f * kOutlineIndentStepDip);
    // 越界钳制:level 0 按 0 处理,不产生负缩进。
    MDVN_CHECK(OutlineItemIndentDip(0) == 0.0f);
}

// 短标题放得下:原样拷贝,不截断。
MDVN_TEST(OutlinePanel_TruncateShortTitleKeepsFullText) {
    const wchar_t* title = L"Intro";  // 5 字符
    OutlineTextMeasurer measurer{&FakeMeasure, nullptr};
    wchar_t buf[64];
    mdvn::u32 n = TruncateOutlineTitle(title, 5, 200.0f, measurer, buf, 64);
    MDVN_CHECK_EQ(n, 5u);
    MDVN_CHECK(buf[5] == L'\0');
    for (mdvn::u32 i = 0; i < 5; ++i) MDVN_CHECK(buf[i] == title[i]);
}

// 超长标题:放不下时截断并追加省略号,结果宽度(前缀+"...")必须 <= maxWidth,
// 且比"多一个字符"的前缀更宽(验证二分找的是"能放下的最大前缀")。
MDVN_TEST(OutlinePanel_TruncateLongTitleAddsEllipsis) {
    const wchar_t* title = L"A Very Long Heading Title That Overflows The Panel Width";
    mdvn::u32 len = 0;
    while (title[len] != 0) ++len;
    OutlineTextMeasurer measurer{&FakeMeasure, nullptr};
    // 100 DIP 宽度(每字符 10 DIP)够放 7 个原文字符 + 省略号(3 字符),
    // 逼着截断逻辑生效,同时留够余量让"能放下的最大前缀"这条断言有意义。
    constexpr float kMaxWidth = 100.0f;
    wchar_t buf[64];
    mdvn::u32 n = TruncateOutlineTitle(title, len, kMaxWidth, measurer, buf, 64);
    MDVN_CHECK(n >= 3);  // 至少包含省略号本身
    MDVN_CHECK(n < len);  // 确实被截短了
    // 结果必须以省略号结尾。
    MDVN_CHECK(buf[n - 1] == L'.' && buf[n - 2] == L'.' && buf[n - 3] == L'.');
    // 结果整体宽度不能超过 maxWidthDip。
    float w = FakeMeasure(nullptr, buf, n);
    MDVN_CHECK(w <= kMaxWidth);
    // 再往前多留一个原文字符(去掉一个省略号字符换成原文字符)得到的宽度应
    // 大于 maxWidthDip(说明二分确实找到了"最大能放下的前缀",不是随意截断)。
    mdvn::u32 prefixLen = n - 3;  // 去掉 "..." 的前缀长度
    float widerW = FakeMeasure(nullptr, title, prefixLen + 1) + FakeMeasure(nullptr, L"...", 3);
    MDVN_CHECK(widerW > kMaxWidth);
}

// 输出缓冲过小时不会越界写(极端边界,不崩溃)。
MDVN_TEST(OutlinePanel_TruncateTinyBufferStaysSafe) {
    const wchar_t* title = L"Whatever";
    OutlineTextMeasurer measurer{&FakeMeasure, nullptr};
    wchar_t buf[4];
    mdvn::u32 n = TruncateOutlineTitle(title, 8, 5.0f, measurer, buf, 4);
    MDVN_CHECK(n <= 3);
    MDVN_CHECK(buf[n] == L'\0');
}

// 条目数超过侧栏可视高度时,侧栏自身可滚动(夹到 [0, 内容总高度 - 侧栏高度])。
// 用真实 Document(20 个 H1 标题)驱动 Rebuild,而不是手工摆数字,确保
// ItemCount() 是 Rebuild 真正写进去的结果,不是测试自己假设出来的。
MDVN_TEST(OutlinePanel_SelfScrollClampsWhenContentExceedsPanelHeight) {
    Arena docArena;
    docArena.Init(1 * 1024 * 1024);
    char src[20 * 8 + 8];
    mdvn::u32 pos = 0;
    for (int i = 0; i < 20; ++i) {
        src[pos++] = '#';
        src[pos++] = ' ';
        src[pos++] = 'H';
        src[pos++] = '\n';
        src[pos++] = '\n';
    }
    mdvn::Document doc =
        mdvn::ParseMarkdown(mdvn::StrSlice{src, pos}, &docArena);

    Arena arena;
    arena.Init(1 * 1024 * 1024);
    OutlinePanel panel(&arena);
    mdvn::u32 itemCount = panel.Rebuild(doc);
    MDVN_CHECK_EQ(itemCount, 20u);

    float panelHeight = 200.0f;  // 侧栏可视高度,明显小于 20 * 28 = 560
    float contentHeight = OutlinePanelContentHeightDip(panel.ItemCount());
    MDVN_CHECK(contentHeight > panelHeight);  // 前提:确实超出了侧栏高度

    // 未超出内容总高度时,滚动偏移原样生效。
    panel.SetScrollY(100.0f, panelHeight);
    MDVN_CHECK(panel.ScrollY() == 100.0f);

    // 滚动过头会被夹到 [0, contentHeight - panelHeight]。
    panel.SetScrollY(100000.0f, panelHeight);
    float maxScroll = contentHeight - panelHeight;
    MDVN_CHECK(panel.ScrollY() == maxScroll);

    // 负值夹到 0。
    panel.SetScrollY(-50.0f, panelHeight);
    MDVN_CHECK(panel.ScrollY() == 0.0f);
}

// 条目数不超过侧栏高度时,滚动范围退化为 [0, 0](没有可滚动余量)。
MDVN_TEST(OutlinePanel_SelfScrollStaysZeroWhenContentFitsPanel) {
    Arena arena;
    arena.Init(1 * 1024 * 1024);
    OutlinePanel panel(&arena);
    panel.SetScrollY(999.0f, 1000.0f);  // 内容总高度是 0(还没 Rebuild 过),侧栏很高
    MDVN_CHECK(panel.ScrollY() == 0.0f);
}

// 高亮定位:视口顶在第一个标题之前 -> 无高亮(裁决二选一,本实现选"无高亮",
// 见 outline_panel.h 里 FindCurrentOutlineItem 的口径说明)。
MDVN_TEST(OutlineHighlight_BeforeFirstItemIsNoHighlight) {
    float tops[3] = {100.0f, 200.0f, 300.0f};
    MDVN_CHECK_EQ(FindCurrentOutlineItem(tops, 3, 0.0f), kInvalidIndex);
    MDVN_CHECK_EQ(FindCurrentOutlineItem(tops, 3, 99.9f), kInvalidIndex);
}

// 视口顶正好落在某标题顶部 -> 高亮该标题本身。
MDVN_TEST(OutlineHighlight_ExactlyAtItemTopHighlightsThatItem) {
    float tops[4] = {0.0f, 100.0f, 250.0f, 400.0f};
    MDVN_CHECK_EQ(FindCurrentOutlineItem(tops, 4, 0.0f), 0u);
    MDVN_CHECK_EQ(FindCurrentOutlineItem(tops, 4, 100.0f), 1u);
    MDVN_CHECK_EQ(FindCurrentOutlineItem(tops, 4, 250.0f), 2u);
    MDVN_CHECK_EQ(FindCurrentOutlineItem(tops, 4, 400.0f), 3u);
}

// 视口顶落在两个标题之间 -> 取上面那一条。
MDVN_TEST(OutlineHighlight_BetweenTwoItemsHighlightsUpperOne) {
    float tops[4] = {0.0f, 100.0f, 250.0f, 400.0f};
    MDVN_CHECK_EQ(FindCurrentOutlineItem(tops, 4, 50.0f), 0u);
    MDVN_CHECK_EQ(FindCurrentOutlineItem(tops, 4, 249.9f), 1u);
    MDVN_CHECK_EQ(FindCurrentOutlineItem(tops, 4, 399.9f), 2u);
}

// 视口顶落在最后一个标题之后 -> 高亮最后一条。
MDVN_TEST(OutlineHighlight_AfterLastItemHighlightsLastItem) {
    float tops[4] = {0.0f, 100.0f, 250.0f, 400.0f};
    MDVN_CHECK_EQ(FindCurrentOutlineItem(tops, 4, 999999.0f), 3u);
}

// 空数组 -> 无高亮,不崩溃。
MDVN_TEST(OutlineHighlight_EmptyArrayIsNoHighlight) {
    MDVN_CHECK_EQ(FindCurrentOutlineItem(nullptr, 0, 500.0f), kInvalidIndex);
}

// 300 条标题:二分查找结果与暴力线性扫描逐一致(对拍)。线性版本的定义与
// FindCurrentOutlineItem 的口径完全一致:"最后一个 top <= viewportTop 的
// 下标",找不到(视口在第一个之前)返回 kInvalidIndex——两个实现分别独立
// 写出来做交叉验证,不是同一份代码复制两遍。
namespace {
mdvn::u32 LinearScanBruteForce(const float* tops, mdvn::u32 count, float viewportTop) {
    mdvn::u32 result = kInvalidIndex;
    for (mdvn::u32 i = 0; i < count; ++i) {
        if (tops[i] <= viewportTop) {
            result = i;  // 持续往后覆盖,保留"最后一个满足条件"的下标
        }
    }
    return result;
}
}  // namespace

MDVN_TEST(OutlineHighlight_BinarySearchMatchesLinearScanOn300Items) {
    constexpr mdvn::u32 kCount = 300;
    float tops[kCount];
    for (mdvn::u32 i = 0; i < kCount; ++i) {
        tops[i] = static_cast<float>(i) * 37.5f;  // 任意单调递增的间距,模拟真实块高
    }

    // 采样一批视口位置:每个条目的顶部、顶部前一点、顶部后一点,外加两端极值。
    bool allMatch = true;
    for (mdvn::u32 i = 0; i < kCount && allMatch; ++i) {
        float samples[3] = {tops[i], tops[i] - 1.0f, tops[i] + 10.0f};
        for (float s : samples) {
            mdvn::u32 viaBinary = FindCurrentOutlineItem(tops, kCount, s);
            mdvn::u32 viaLinear = LinearScanBruteForce(tops, kCount, s);
            if (viaBinary != viaLinear) {
                allMatch = false;
                break;
            }
        }
    }
    MDVN_CHECK(allMatch);
    // 额外验证两端极值。
    MDVN_CHECK_EQ(FindCurrentOutlineItem(tops, kCount, -100.0f),
                  LinearScanBruteForce(tops, kCount, -100.0f));
    MDVN_CHECK_EQ(FindCurrentOutlineItem(tops, kCount, 1000000.0f),
                  LinearScanBruteForce(tops, kCount, 1000000.0f));
}

// OutlinePanel 关闭态口径的编译期证据:字段布局只有一个 u32 currentItem_ +
// 一个 float scrollY_ + 一个 Vec(内部就是 3 个裸指针/整数,不含任何动态
// 常驻缓冲),没有额外的数组状态——用 sizeof 上限做一个粗粒度的把关,
// 真正的"关闭时不存在实例"由 window.cpp 的指针置空逻辑保证(见
// tests 里对应的 window 集成没有覆盖真实 HWND,这里只能验证类型本身足够
// 精简,不会藏着一份隐藏的常驻大数组)。
MDVN_TEST(OutlinePanel_StaysSmallNoHiddenArrays) {
    size_t sz = sizeof(OutlinePanel);
    MDVN_CHECK(sz <= 64);
}
