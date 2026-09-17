// mdvn 的命中测试(T35):屏幕坐标 →(滚动偏移换算)→ 块下标 → inline run →
// 链接目标 / 图片下标。
//
// 与 `shell/scroll.h` 同样的设计口径:**不依赖 HWND、不依赖 D2D**,全部是可脱离
// Win32 直接单测的纯函数(见 tests/test_hit_test.cpp)。唯一用到 DirectWrite 的是
// `HitTestDocument`,它把 `IDWriteTextLayout::HitTestPoint` 的结果接到下面这些
// 纯查表函数上;真正的 `SetCursor(IDC_HAND)` 之类 Win32 调用留在 window.cpp。
#pragma once

#include "../layout/layout.h"
#include "../util/types.h"

namespace mdvn {

/** 一次命中测试的结果种类。 */
enum class HitKind : u8 {
    None,  // 没命中任何可交互内容(块间隙、行尾空白、纯装饰区域)
    Link,  // 命中链接/自动链接 run
    Image, // 命中图片或可点击的图片占位块
};

/** 一次命中测试的完整结果。 */
struct HitResult {
    HitKind kind;       // 命中种类
    u32 blockIndex;      // 命中所在块下标;kind == None 时为 kInvalidIndex
    u32 linkTargetIdx;   // Link 时指向 Document::linkTargets;否则 kInvalidIndex
    u32 imageIndex;      // Image 时是该块 imageBoxes 内的下标;否则 kInvalidIndex
};

/** 文档坐标系下的一个点(DIP,已含滚动偏移)。 */
struct DocPoint {
    float x;
    float y;
};

/**
 * 客户区物理像素坐标 → 文档坐标(DIP + 滚动偏移)。
 *
 * @param clientX 客户区横坐标(物理像素)。
 * @param clientY 客户区纵坐标(物理像素)。
 * @param dipScale DPI 缩放系数(实际 DPI / 96),须大于 0;非正数按 1.0 处理。
 * @param scrollY 当前纵向滚动偏移(DIP)。
 * @return 文档坐标系下的点。
 * @example mdvn::DocPoint p = mdvn::ClientToDocument(120, 80, 1.5f, 200.0f); // {80, 253.33}
 */
DocPoint ClientToDocument(int clientX, int clientY, float dipScale, float scrollY);

/**
 * 某个块类型是否承载"可被命中的内容"。
 * 容器类块(文档根/列表/引用块/表格分组行/脚注区)一律返回 false —— 块与块之间
 * 的垂直间距落在容器上,这样间隙处的命中会正确地得到"什么都没命中"。
 *
 * @param type 块类型。
 * @return 是内容块返回 true。
 * @example bool ok = mdvn::IsContentBlockType(mdvn::BlockType::Paragraph); // true
 */
bool IsContentBlockType(BlockType type);

/**
 * 纯几何命中:在一组块几何里找到包含指定文档坐标的**内容块**。
 *
 * 块几何按前序排列,深层块下标更大,因此取"最后一个包含该点的内容块"即最深的
 * 那个。表格单元格额外用 x 与 cellWidth 判定列,避免同一行的相邻单元格互相误命中。
 *
 * @param geometries 块几何数组,非空。
 * @param count 数组长度。
 * @param docX 文档坐标 x(DIP)。
 * @param docY 文档坐标 y(DIP)。
 * @return 命中的块下标;落在块间隙/文档之外返回 `kInvalidIndex`。
 * @example u32 b = mdvn::FindContentBlockAt(geoms, n, 10.0f, 42.0f);
 */
u32 FindContentBlockAt(const BlockGeometry* geometries, u32 count, float docX, float docY);

/**
 * 纯几何命中:在一个块的图片数组里找到包含指定文档坐标的图片/占位块。
 *
 * @param boxes 图片数组,可为空。
 * @param count 数组长度。
 * @param docX 文档坐标 x(DIP)。
 * @param docY 文档坐标 y(DIP)。
 * @return 命中的图片下标;没命中返回 `kInvalidIndex`。
 * @example u32 i = mdvn::FindImageBoxAt(g.imageBoxes.data, g.imageBoxes.len, x, y);
 */
u32 FindImageBoxAt(const ImageBox* boxes, u32 count, float docX, float docY);

/**
 * 纯查表:某个 UTF-16 文本位置落在哪个链接 run 上。
 *
 * 半开区间 `[textPosition, textPosition + textLength)` —— 正好落在 run 末尾的位置
 * (典型的"行尾空白"命中)判为不在链接内,不会误触发跳转。
 *
 * @param boxes 该块的链接 run 数组,可为空。
 * @param count 数组长度。
 * @param textPosition 待判定的 UTF-16 位置。
 * @return 命中的链接目标下标(`LinkBox::linkTargetIdx`);没命中返回 `kInvalidIndex`。
 * @example u32 t = mdvn::LinkTargetAtTextPosition(g.linkBoxes.data, g.linkBoxes.len, 7);
 */
u32 LinkTargetAtTextPosition(const LinkBox* boxes, u32 count, u32 textPosition);

/**
 * 整份布局的命中测试:先按几何定位块,再在该块的 `IDWriteTextLayout` 上调用
 * `HitTestPoint` 拿到 UTF-16 位置,最后查链接 run 表。图片优先于文本命中
 * (图片矩形本身就是一块独立的可点击区域)。
 *
 * @param layout 已完成 `Relayout` / `UpdateVisibleRange` 的布局引擎。
 * @param docX 文档坐标 x(DIP)。
 * @param docY 文档坐标 y(DIP)。
 * @return 命中结果;没命中任何可交互内容时 `kind == HitKind::None`。
 * @example
 *   mdvn::DocPoint p = mdvn::ClientToDocument(x, y, scale, scrollY);
 *   mdvn::HitResult hit = mdvn::HitTestDocument(layout, p.x, p.y);
 */
HitResult HitTestDocument(const BlockLayoutEngine& layout, float docX, float docY);

/**
 * 命中结果是否应当显示手型光标(链接与可点击的图片/占位块)。
 * @param hit 命中结果。
 * @return 需要手型光标返回 true。
 * @example if (mdvn::ShouldUseHandCursor(hit)) SetCursor(handCursor);
 */
bool ShouldUseHandCursor(const HitResult& hit);

}  // namespace mdvn
