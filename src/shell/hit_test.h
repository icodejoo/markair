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
    None,           // 没命中任何可交互内容(块间隙、行尾空白、纯装饰区域)
    Link,           // 命中链接/自动链接 run
    Image,          // 命中图片或可点击的图片占位块
    CodeCopyButton, // 命中代码块右上角的复制按钮(T45),blockIndex 即该代码块
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
 * 纯几何命中:哪个代码块的复制按钮(T45)包含指定文档坐标。
 *
 * 单独暴露出来是为了给"鼠标移动只更新悬浮态"这条高频路径用 —— 它不需要
 * `HitTestDocument` 里的 DirectWrite 文本命中,纯矩形判定,零 COM 调用。
 *
 * @param geometries 块几何数组,非空。
 * @param count 数组长度。
 * @param docX 文档坐标 x(DIP)。
 * @param docY 文档坐标 y(DIP)。
 * @return 命中的代码块下标;没命中返回 `kInvalidIndex`。
 * @example u32 b = mdvn::FindCodeCopyButtonAt(&layout.Geometry(0), layout.BlockCount(), x, y);
 */
u32 FindCodeCopyButtonAt(const BlockGeometry* geometries, u32 count, float docX, float docY);

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
 * `HitTestPoint` 拿到 UTF-16 位置,最后查链接 run 表。代码块复制按钮最优先
 * (它浮在代码块之上),其次图片(图片矩形本身就是一块独立的可点击区域),
 * 最后才是文本。
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
 * 判断一次鼠标点击是否落在大纲侧栏区域内(T64)。侧栏是悬浮覆盖,纵向铺满
 * 整个客户区,因此只按横坐标判定;仅用于命中测试的"短路"入口 —— 侧栏打开
 * 且点在其区域内时,正文的链接/图片/复制按钮命中一律不应该再发生,否则会
 * 出现"点侧栏结果打开了底下的链接"。
 *
 * @param clientX 客户区横坐标(物理像素)。
 * @param dipScale DPI 缩放系数(实际 DPI / 96),须大于 0;非正数按 1.0 处理。
 * @param panelWidthDip 侧栏宽度(DIP),调用方传入 `kOutlinePanelWidthDip`。
 * @return 点落在侧栏区域内返回 true。
 * @example bool inPanel = mdvn::IsPointInOutlinePanel(50, 1.5f, 220.0f); // true
 */
bool IsPointInOutlinePanel(int clientX, float dipScale, float panelWidthDip);

/**
 * 判断一次鼠标点击是否落在大纲侧栏打开时的背景蒙层区域内(侧栏矩形之外的
 * 正文区域)——点这块区域应该关闭侧栏。与 `IsPointInOutlinePanel` 互补
 * (不在侧栏内即在蒙层内),单独具名成一个函数,让调用点的意图("点蒙层关侧栏")
 * 一望即知,也方便单测直接覆盖这条验收项。
 *
 * @param clientX 客户区横坐标(物理像素)。
 * @param dipScale DPI 缩放系数(实际 DPI / 96),须大于 0;非正数按 1.0 处理。
 * @param panelWidthDip 侧栏宽度(DIP),调用方传入 `kOutlinePanelWidthDip`。
 * @return 点落在蒙层区域内返回 true。
 * @example bool inMask = mdvn::IsPointInOutlineOverlayMask(500, 1.5f, 220.0f); // true
 */
bool IsPointInOutlineOverlayMask(int clientX, float dipScale, float panelWidthDip);

/**
 * 判断一个点(客户区物理像素坐标)是否落在大纲侧栏矩形内,同时校验纵坐标
 * (侧栏铺满整个客户区高度这一约定本身也要能被单测校验,不是想当然认为
 * "点在客户区内纵坐标必然合法")。供 `WM_MOUSEWHEEL` 判断鼠标是否落在
 * 侧栏区域内、该滚侧栏自身还是滚正文使用。
 *
 * @param clientX 客户区横坐标(物理像素)。
 * @param clientY 客户区纵坐标(物理像素)。
 * @param dipScale DPI 缩放系数(实际 DPI / 96),须大于 0;非正数按 1.0 处理。
 * @param panelWidthDip 侧栏宽度(DIP),调用方传入 `kOutlinePanelWidthDip`。
 * @param clientHeightDip 客户区高度(DIP)。
 * @return 点落在侧栏矩形内返回 true。
 * @example bool inPanel = mdvn::IsPointInOutlinePanelRect(50, 300, 1.5f, 220.0f, 600.0f);
 */
bool IsPointInOutlinePanelRect(int clientX, int clientY, float dipScale, float panelWidthDip,
                                float clientHeightDip);

/**
 * 大纲侧栏可拖拽调宽度的抓手宽度(DIP),横跨侧栏右边界左右各一半。
 * 与 `kOutlinePanelWidthDip` 一样,数值只在 shell 侧定义一份;render 层不画
 * 这个抓手(它没有独立视觉,只是紧贴侧栏边界的一条不可见可拖拽区)。
 */
constexpr float kOutlinePanelResizeHandleWidthDip = 6.0f;

/**
 * 判断一次鼠标点击是否落在大纲侧栏右边缘的拖拽抓手上(T63b,调整侧栏宽度)。
 * 抓手横跨边界左右各 `kOutlinePanelResizeHandleWidthDip / 2`,纵向铺满整个
 * 客户区高度,与侧栏本身同一约定。抓手比自绘滚动条的横向范围更靠右一点
 * (滚动条贴在 `panelWidthDip` 以内,抓手横跨 `panelWidthDip` 本身),两者
 * 不会互相误判。
 *
 * @param clientX 客户区横坐标(物理像素)。
 * @param dipScale DPI 缩放系数(实际 DPI / 96),须大于 0;非正数按 1.0 处理。
 * @param panelWidthDip 侧栏当前宽度(DIP)。
 * @return 点落在抓手范围内返回 true。
 * @example bool onHandle = mdvn::IsPointInOutlinePanelResizeHandle(220, 1.5f, 220.0f);
 */
bool IsPointInOutlinePanelResizeHandle(int clientX, float dipScale, float panelWidthDip);

/**
 * 一次"文本位置"命中结果(供文本拖选使用,T80)。与 HitResult 不同——
 * 后者只关心"命中了哪种可交互元素",这里只关心"这个屏幕点对应文档里
 * 哪个块的哪个字符位置",用于选区的起点/终点标记。
 */
struct DocTextHit {
    bool valid;        // 文档当前没有任何可选文本(如空文档)时为 false
    u32 blockIndex;    // 命中的内容块下标
    u32 charOffset;    // 该块 IDWriteTextLayout 文本里的 UTF-16 位置(半开区间的起点)
};

/**
 * 命中测试:屏幕坐标(经 ClientToDocument 换算后的文档坐标)落在哪个内容块的
 * 哪个字符位置上,用于鼠标拖选的起点/终点标记(T80)。
 *
 * 与 `HitTestDocument` 的取舍不同:后者严格要求点落在某个内容块的矩形内,
 * 落在块间隙/文档上下边界外一律"没命中";这里要支持"像浏览器一样,从文档
 * 任意位置拖到任意位置都能选中中间的连续文本",所以点落在块间隙时会取
 * 竖直方向最近的那个有文本的内容块,超出文档顶部/底部时钳到第一个/最后一个
 * 块的开头/末尾。
 *
 * @param layout 已完成 `Relayout`/`UpdateVisibleRange` 的布局引擎。
 * @param docX 文档坐标 x(DIP)。
 * @param docY 文档坐标 y(DIP)。
 * @return 命中的文本位置;整份文档都没有可选文本(没有任何块持有
 *         `textLayout`)时 `valid` 为 false。
 * @example
 *   mdvn::DocPoint p = mdvn::ClientToDocument(x, y, scale, scrollY);
 *   mdvn::DocTextHit hit = mdvn::HitTestTextPosition(layout, p.x, p.y);
 */
DocTextHit HitTestTextPosition(const BlockLayoutEngine& layout, float docX, float docY);

/**
 * 命中结果是否应当显示手型光标(链接、可点击的图片/占位块、代码块复制按钮)。
 * @param hit 命中结果。
 * @return 需要手型光标返回 true。
 * @example if (mdvn::ShouldUseHandCursor(hit)) SetCursor(handCursor);
 */
bool ShouldUseHandCursor(const HitResult& hit);

}  // namespace mdvn
