// markair 的 D2D 渲染模块(T11):设备/渲染目标管理 + 把 BlockLayoutEngine 的
// 布局结果画到窗口上。渲染目标默认软件光栅化(架构决策,见 memory.md),
// 本模块不做"硬件优先、失败再降级"的运行时探测。
#pragma once

#include <d2d1.h>
#include <windows.h>

#include "../assets/image.h"
#include "../doc/outline.h"
#include "../doc/search.h"
#include "../layout/layout.h"
#include "../shell/button.h"    // Button/IconButton 统一按钮抽象(ButtonRectDip/ButtonIconPaintFn)
#include "../shell/edit_box.h"  // EditBoxBorderStyle(输入框边框视觉枚举)
#include "../util/arena.h"
#include "../util/recent_files.h"
#include "theme.h"

namespace markair {

class Renderer;
struct FolderEntry;

// 底部栏按钮总数,数值必须与 shell/bottom_bar.h 的 kBottomBarButtonCount
// 保持一致(render 不反向 include shell 头文件,同一条既有约束)。这里单独
// 起名是因为 renderer.cpp 内部也有一份同名的文件作用域常量,避免撞名。
constexpr u32 kBottomBarButtonCountRender = 10;

/**
 * 外壳层叠加层视图(T36/T37/T38):查找命中高亮 + 顶部查找条 + 窗口内提示文字。
 *
 * 渲染层**只读**,不拥有其中任何一块内存,生命周期由外壳层(window.cpp)保证
 * 覆盖一次 `RenderFrame` 调用。所有字段都允许为空/为 0,全空时等价于不画叠加层。
 *
 * @example
 *   markair::ShellOverlay overlay{find.Matches().data, find.MatchCount(),
 *                               find.CurrentIndex(), &doc, find.Visible(),
 *                               find.Query(), nullptr,
 *                               markair::kInvalidIndex, markair::kInvalidIndex};
 *   renderer.RenderFrame(hwnd, layout, scrollY, 12.0f, &overlay);
 */
struct ShellOverlay {
    const Match* matches;          // 查找命中数组(按块下标升序),可为 nullptr
    u32 matchCount;                // 命中总数
    u32 currentMatch;              // 当前命中下标;kInvalidIndex 表示无
    const Document* doc;           // 把命中换算成 UTF-16 区间用;为空则不画高亮
    bool findBarVisible;           // 顶部查找条是否可见
    const wchar_t* findQuery;      // 查找条里的查询串(以 '\0' 结尾),可为 nullptr
    const wchar_t* statusMessage;  // 窗口内提示(如"文件不存在"),可为 nullptr;不弹 MessageBox
    // T45 代码块复制按钮的两种瞬时态,都是"块下标",`kInvalidIndex` 表示没有。
    // 外壳层零初始化 ShellOverlay 时务必显式写成 kInvalidIndex —— 0 会被当成
    // "第 0 个块的按钮处于该状态"。
    u32 copyButtonHoverBlock;      // 鼠标当前悬浮的复制按钮所属块
    u32 copyButtonCopiedBlock;     // 处于"已复制"反馈态的复制按钮所属块

    // 表格行悬浮态(斑马纹之上叠加高亮):hoverTableBlock 是鼠标所在 Table
    // 容器块的下标,hoverTableRow 是该表**表体**内的行号(从 0 开始,不含
    // 表头行——即相对 BlockGeometry::tableRowTops[tableHeadRowCount..] 的
    // 区间下标)。两者都为 kInvalidIndex 表示当前没有悬浮任何表格行。
    u32 hoverTableBlock;
    u32 hoverTableRow;

    // T80 鼠标拖选文本高亮:selectionActive 为 false 时以下四个字段无意义、
    // 不画任何高亮(与查找高亮同一套"没有就不画"口径)。选区可能跨越当前
    // 不可见的块,DrawSelectionHighlights 只画落在"可见 ± 1 屏"内、持有
    // textLayout 的那部分,不会为此额外实例化任何块的 layout。
    bool selectionActive;   // 是否存在非空选区(拖动了至少一个字符)
    u32 selStartBlock;      // 选区起点所在块下标
    u32 selStartOffset;     // 选区起点在该块文本里的 UTF-16 偏移
    u32 selEndBlock;        // 选区终点所在块下标
    u32 selEndOffset;       // 选区终点在该块文本里的 UTF-16 偏移

    // T63 大纲侧栏:全部为空/为 0 时不画侧栏,零额外开销(与侧栏关闭时
    // window.cpp 根本不构造 OutlinePanel 是同一套"不存在即不画"口径)。
    const OutlineItem* outlineItems;   // 大纲条目数组(按块下标升序),可为 nullptr
    u32 outlineItemCount;              // 条目总数
    u32 outlineCurrentItem;            // 当前高亮条目下标;kInvalidIndex 表示无
    float outlineScrollY;              // 侧栏自身滚动偏移(DIP)
    // 自绘滚动条(方案A)悬浮/拖动态:鼠标落在侧栏滚动条区域内、或正在拖动
    // 它时为 true,渲染层据此在 Idle/Active 两档透明度间切换(见 theme.h)。
    bool outlineScrollbarActive;
    // T63b:侧栏当前宽度(DIP,已经是拖拽调整后的实际值)。render 层不知道
    // "拖拽调宽"这个外壳层概念,只接收这一个数字,与 leftPaddingDip 同一套
    // "shell 算好了才传给 render"的约定。为 0 时(侧栏关闭,聚合初始化零值)
    // 不会被读取——DrawOutlinePanel/DrawOutlineOverlayMask 只在
    // `outlineItems` 非空时才会被调用。
    float outlinePanelWidthDip;

    // Outline drawer slide & mask fade animation progress in [0.0f, 1.0f].
    // 0.0f = completely closed, 1.0f = fully open and interactive.
    //
    // 大纲侧栏滑动与蒙层淡入淡出动画进度，取值范围 [0.0f, 1.0f]。
    // 0.0f 表示完全收起，1.0f 表示完全展开并可交互。
    float outlineAnimProgress;

    // History sidebar fields:
    // Recent files entries array (ordered from newest to oldest).
    //
    // 历史记录侧栏字段：
    // 最近文件条目数组（按从新到旧从前向后排序）。
    const RecentFileEntry* historyEntries;

    // Total number of recent files in history.
    //
    // 历史记录条目总数。
    u32 historyItemCount;

    // Currently hovered history item index; kInvalidIndex indicates none.
    //
    // 当前鼠标悬浮的历史记录条目下标；kInvalidIndex 表示无。
    u32 historyHoverItem;

    // Vertical scroll offset of history sidebar drawer in DIPs.
    //
    // 历史记录侧栏自身的纵向滚动偏移（DIP）。
    float historyScrollY;

    // Whether history sidebar scrollbar is in hover/drag active state.
    //
    // 历史记录侧栏滚动条是否处于悬浮/拖动激活态。
    bool historyScrollbarActive;

    // Current width of history sidebar drawer in DIPs.
    //
    // 历史记录侧栏当前宽度（DIP）。
    float historyPanelWidthDip;

    // History drawer slide & mask fade animation progress in [0.0f, 1.0f].
    //
    // 历史记录侧栏滑动与蒙层淡入淡出动画进度，取值范围 [0.0f, 1.0f]。
    float historyAnimProgress;

    // Folder entries for recursive folder penetrating browser:
    // 文件夹穿透浏览侧边栏字段:
    const FolderEntry* folderEntries;
    u32 folderEntryCount;
    const wchar_t* folderRootName;
    u32 folderHoverItem;
    u32 folderCurrentItem;
    float folderScrollY;
    bool folderScrollbarActive;
    float folderPanelWidthDip;
    float folderAnimProgress;
    const u32* folderFilteredIndices;
    u32 folderFilteredCount;
    bool folderHeaderButtonHover;
    const wchar_t* folderFilterQuery;
    // hover 行右侧"打开所在文件夹"/"新窗口打开"两个按钮各自的悬浮态,
    // 用来在按钮正上方画一个简短的标题提示气泡(与 folderHoverItem 的路径
    // 提示气泡是两码事:一个是整行的完整路径,一个是单个按钮的功能说明)。
    bool folderItemFolderButtonHover;
    bool folderItemNewWindowButtonHover;
};

/**
 * 判断一个 D2D 调用返回的 HRESULT 是否要求重建渲染目标。
 * 抽成纯函数是因为这条判断本身不依赖真实设备,可以脱离 D2D 单独做单元测试;
 * 真正的重建(释放旧目标/下次绘制时惰性重新创建)仍需要真实 HWND,留在
 * Renderer::RenderFrame 里。
 * @param hr 某次 D2D 调用(通常是 EndDraw)返回的 HRESULT。
 * @return hr 等于 D2DERR_RECREATE_TARGET 时返回 true。
 * @example if (markair::ShouldRecreateRenderTarget(hr)) { / * 释放旧目标 * / }
 */
inline bool ShouldRecreateRenderTarget(HRESULT hr) {
    return hr == D2DERR_RECREATE_TARGET;
}

/**
 * 取占位块(T33)在某个图片状态下要显示的文案。五种非 Ok 状态共用同一个绘制
 * 函数,只换这里返回的文字;抽成纯函数以便脱离 D2D 单测。
 *
 * @param status 图片状态。
 * @return 以 '\0' 结尾的静态宽字符串,状态为 Ok 时返回空串(不画占位块)。
 * @example const wchar_t* t = markair::ImagePlaceholderText(markair::ImageStatus::Unsupported);
 */
const wchar_t* ImagePlaceholderText(ImageStatus status);

/**
 * 降采样提示标签(T33 追加)的文案:图片被 T30 缩小过时,在其右下角显示。
 * @return 以 '\0' 结尾的静态宽字符串。
 * @example const wchar_t* tag = markair::DownsampledBadgeText();
 */
const wchar_t* DownsampledBadgeText();

/**
 * 图片驻留管理器(T32):`ImageResidencyController` 的具体实现。
 *
 * 放在 render 层是因为解码出来的 `ID2D1Bitmap` 必须绑定到当前渲染目标,而渲染
 * 目标由 `Renderer` 持有;布局层只负责在正确的时机(块进出"可见 ± 1 屏")回调,
 * 不知道也不需要知道解码细节。
 *
 * 行为:
 *   - `EnsureResident`:位图已在 -> 跳过;已判定为终态失败(损坏/SVG/超限)-> 跳过,
 *     不每帧重试;否则按 `LinkTargetKind` 分流到本地文件 / data: URI / 已下载的
 *     网络字节三条解码入口,结果连同尺寸写进 `ImageCache`。
 *   - `ReleaseResident`:调用 `ImageCache::ReleaseBitmap`,只丢位图、保留尺寸。
 *
 * @example
 *   markair::ImageResidencyManager residency;
 *   residency.Init(&renderer, &cache, &scratchArena, docDir);
 *   layout.UpdateVisibleRange(top, bottom, fonts, &residency);
 */
class ImageResidencyManager : public ImageResidencyController {
public:
    // 构造一个未绑定的管理器,不做任何 COM 调用(WIC 仍是惰性初始化)。
    ImageResidencyManager();

    ~ImageResidencyManager() override;

    ImageResidencyManager(const ImageResidencyManager&) = delete;
    ImageResidencyManager& operator=(const ImageResidencyManager&) = delete;

    /**
     * 绑定依赖。
     * @param renderer 渲染器,用于取当前 `ID2D1RenderTarget`;不拥有,可为 nullptr
     *                 (此时只探测尺寸、不创建位图)。
     * @param cache 图片缓存,非空,不拥有。
     * @param scratch 解码 data: URI 用的临时 Arena,非空,不拥有(每次解码前 Reset)。
     * @param documentDirectory 当前文档所在目录(宽字符),相对路径图片据此解析;
     *                          可为 nullptr(按当前工作目录解析)。
     * @example residency.Init(&renderer, &cache, &imageArena, docDir);
     */
    void Init(Renderer* renderer, ImageCache* cache, Arena* scratch,
              const wchar_t* documentDirectory);

    // 块进入"可见 ± 1 屏":按需解码,见类注释。
    void EnsureResident(const ImageBox* boxes, u32 count) override;

    // 块离开"可见 ± 1 屏":释放解码位图,保留尺寸信息。
    void ReleaseResident(const ImageBox* boxes, u32 count) override;

    /**
     * 解码一张图片并写入缓存(不管它当前是否已经解码过),供外部在拿到新数据时
     * 主动刷新——例如 T34 的网络图片下载完成后。
     * @param box 图片布局信息。
     * @return 解码后的状态。
     * @example residency.DecodeNow(box);
     */
    ImageStatus DecodeNow(const ImageBox& box);

    /** 内部 WIC 解码器是否已经被真正初始化过(用于验证"无图文档不碰 WIC")。 */
    bool WicInitialized() const { return decoder_.IsInitialized(); }

private:
    ImageDecoder decoder_;      // 惰性初始化的 WIC 解码器
    Renderer* renderer_;        // 不拥有,用于取渲染目标
    ImageCache* cache_;         // 不拥有
    Arena* scratch_;            // 不拥有,data: URI 解码缓冲
    const wchar_t* docDir_;     // 不拥有,文档所在目录
};

// 侧边栏单行项按钮类型位掩码。
enum SidebarItemButtonMask : u32 {
    kSidebarItemBtnNone      = 0,
    kSidebarItemBtnClose     = 1 << 0,  // 关闭/删除按钮 (X)
    kSidebarItemBtnFolder    = 1 << 1,  // 打开所在文件夹按钮 (Folder)
    kSidebarItemBtnNewWindow = 1 << 2,  // 新窗口打开按钮 (仅文件夹侧栏使用)
};

// 侧边栏列表单行项绘制参数。
struct SidebarListItemParams {
    float rowLeft;
    float rowTop;
    float rowWidth;
    float rowHeight;
    const wchar_t* text;
    u32 textLen;
    bool isCurrent;
    bool isHover;
    u32 buttonMask;  // 支持的按钮组合 (SidebarItemButtonMask)
};

/**
 * D2D 渲染器:持有并懒创建 `ID2D1HwndRenderTarget`(默认软件光栅化),
 * 把 `BlockLayoutEngine` 算出的块几何 + `IDWriteTextLayout` 画到窗口上。
 *
 * 渲染目标重建(`D2DERR_RECREATE_TARGET`)时只释放/重新创建渲染目标本体,
 * 传入的 `BlockLayoutEngine` 不受影响 —— 布局对象(几何数组、
 * `IDWriteTextLayout`)不依赖任何具体渲染目标实例,因此重建后无需重跑
 * `Relayout`,调用方按原样传入同一个布局引擎即可继续渲染。
 *
 * @example
 *   markair::Renderer renderer;
 *   renderer.Init(d2dFactory);
 *   renderer.RenderFrame(hwnd, layoutEngine, 0.0f, 12.0f);
 */
class Renderer {
public:
    // 构造一个未初始化的渲染器,渲染目标延迟到首次绘制时才创建。
    Renderer();

    // 释放当前持有的渲染目标(工厂由调用方持有,不在此释放)。
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /**
     * 绑定 D2D 工厂,不立即创建渲染目标。
     * @param factory 已创建好的 `ID2D1Factory`,生命周期须覆盖本对象。
     * @param fonts 可选的字体子系统,仅用于 T28 脚注编号标签("[n]")这种
     *              不参与虚拟化、临建临绘的极小号文本;传 nullptr 时静默跳过
     *              该项装饰,不影响其余渲染。生命周期须覆盖本对象,不持有所有权。
     * @param images 可选的图片缓存(T32/T33),用于按 ImageBox::href 取位图;
     *               传 nullptr 时全部图片一律画占位块。渲染目标重建时本对象会
     *               调用其 ReleaseAllBitmaps()(旧位图绑定在旧目标上,必须丢弃)。
     *               不持有所有权。
     * @example renderer.Init(g_d2dFactory, &fonts, &imageCache);
     */
    void Init(ID2D1Factory* factory, FontSubsystem* fonts = nullptr, ImageCache* images = nullptr);

    /**
     * 取得当前渲染目标,供上层在需要时把图片解码成绑定该目标的 ID2D1Bitmap。
     * @return 渲染目标指针;尚未创建/刚被释放时为 nullptr,调用方须判空。
     * @example ID2D1RenderTarget* rt = renderer.Target();
     */
    ID2D1RenderTarget* Target() const;

    /**
     * 确保渲染目标已创建(尚未创建时按 hwnd 当前客户区尺寸惰性创建)。
     *
     * 存在的理由:图片解码出的 `ID2D1Bitmap` 必须绑定到渲染目标,而渲染目标本来
     * 只在 `RenderFrame` 内部才惰性创建 —— 那样首帧的图片解码会拿到空目标、白白
     * 解一次又建不出位图。外壳层在跑虚拟化/解码之前先调一次本函数即可避免。
     *
     * @param hwnd 目标窗口。
     * @return 渲染目标可用返回 true;创建失败返回 false(调用方应跳过解码,不崩溃)。
     * @example renderer.EnsureTarget(hwnd);  // 再 layout.UpdateVisibleRange(...)
     */
    bool EnsureTarget(HWND hwnd);

    /**
     * 窗口尺寸变化通知:若渲染目标已存在则同步 Resize;尚未创建时忽略
     * (下次绘制会按窗口当前客户区尺寸创建)。
     * @param width 新客户区宽度(像素)。
     * @param height 新客户区高度(像素)。
     * @example renderer.OnResize(LOWORD(lparam), HIWORD(lparam));
     */
    void OnResize(UINT32 width, UINT32 height);

    /**
     * DPI 变化通知(`WM_DPICHANGED`):记下新 DPI 并释放当前渲染目标,
     * 下次 `RenderFrame` 会按新 DPI 惰性重建 —— 即"DPI 变化必须重建 D2D 资源"。
     * 传入的布局引擎不受影响,无需重跑 `Relayout`。
     * @param dpi 新的每英寸点数(如 96 / 144 / 192);传 0 表示跟随系统默认 DPI。
     * @example renderer.OnDpiChanged(static_cast<float>(LOWORD(wparam)));
     */
    void OnDpiChanged(float dpi);

    /**
     * 绘制一帧:清屏后按 scrollY 平移绘制布局引擎里全部块的可见内容——
     * 正文用 `DrawTextLayout`,引用竖线/分割线/代码块背景全部用 D2D
     * 几何图元(矩形/直线)画,不用图标字体模拟。
     *
     * 内部处理设备丢失:`EndDraw` 返回 `D2DERR_RECREATE_TARGET` 时释放
     * 渲染目标并直接返回,不崩溃;下次调用会惰性重新创建。
     *
     * @param hwnd 目标窗口,渲染目标未创建时用它的客户区尺寸创建。
     * @param layout 已完成 `Relayout`/`UpdateVisibleRange` 的布局引擎,
     *               只读取其几何与 `IDWriteTextLayout`,不修改。
     * @param scrollY 当前纵向滚动偏移(DIP),0 表示文档顶部对齐窗口顶部。
     * @param leftPaddingDip 内容整体的左内边距(DIP);同时用于把客户区宽度
     *              收窄成正文可用宽度(分割线/脚注分隔线的右边界据此计算),
     *              避免画进右侧内边距的空白区域。由外壳层(window.cpp)按其
     *              统一定义的内边距常量传入——渲染层不认识"内边距"这个外壳层
     *              概念,只接收一个数字,保持 `shell → render` 单向依赖(不反向
     *              include shell 目录下的头文件)。
     * @param overlay 可选的叠加层视图(T37/T38 的查找高亮与查找条、T36 的窗口内
     *                提示);传 nullptr 表示不画任何叠加层,零额外开销。只在本次
     *                调用期间被引用,函数返回后不再持有。
     * @param mainScrollbarActive 正文自绘滚动条(方案A)是否处于悬浮/拖动态——
     *                鼠标落在其区域内或正在拖动滑块时为 true,决定画 Idle 还是
     *                Active 档透明度(见 theme.h);大纲侧栏打开时正文滚动条本来
     *                就不画,这个参数被忽略。
     * @return 本帧是否真正完成了一次成功的 `EndDraw`(渲染目标创建失败或
     *         `EndDraw` 返回失败 HRESULT 时为 false),供调用方判断"首帧是否
     *         已真正显示"(T14 性能埋点用)。
     * @param documentPath 当前文档完整路径,画进底部栏右侧状态区;为
     *              nullptr/空串表示未打开文件,状态区不画任何文字。
     * @param documentSizeBytes 当前文档字节数,随 documentPath 一起画进状态区。
     * @param bottomBarHoverButtonIndex 鼠标当前悬浮的底部栏按钮下标(0~4);
     *              >= kBottomBarButtonCountRender 表示未悬浮任何按钮。
     * @param bottomBarPathCopied 复制路径按钮当前是否处于点击后的短暂
     *              "已复制"成功态;documentPath 为空时忽略。
     * @param welcomeScreen 为 true 时不画正文块循环,改画欢迎屏
     *              ("Welcome to markair" 标题 + "打开文件"大按钮),用于
     *              当前没有已加载文档的场景;底部栏/滚动条等其余元素画法
     *              不受影响(滚动条因内容为空天然不出现)。
     * @param welcomeButtonHover 欢迎屏"打开文件"按钮当前是否处于鼠标悬浮态,
     *              welcomeScreen 为 false 时忽略。
     * @example bool ok = renderer.RenderFrame(hwnd, layoutEngine, 0.0f, 12.0f, &overlay, true);
     */
    bool RenderFrame(HWND hwnd, const BlockLayoutEngine& layout, float scrollY,
                      float leftPaddingDip, const ShellOverlay* overlay = nullptr,
                      bool mainScrollbarActive = false, const wchar_t* documentPath = nullptr,
                      u64 documentSizeBytes = 0,
                      u32 bottomBarHoverButtonIndex = kBottomBarButtonCountRender,
                      bool bottomBarPathCopied = false, bool welcomeScreen = false,
                      bool welcomeButtonHover = false, bool welcomeFolderButtonHover = false);

    /**
     * 切换当前调色板(T46,为 T49 主题切换打基础):只改一个指针,不拷贝
     * `Palette`、不触发任何重排/重建渲染目标,下一帧 `RenderFrame` 起生效。
     * @param palette 新调色板,生命周期须覆盖本对象(通常传 `&kLightPalette`
     *                或 `&kDarkPalette` 这类静态常量);传 nullptr 时忽略,
     *                保持当前调色板不变。
     * @example renderer.SetPalette(&markair::kDarkPalette);
     */
    void SetPalette(const Palette* palette);

private:
    // 渲染目标不存在时按 hwnd 当前客户区尺寸创建(软件光栅化,架构决策)。
    bool EnsureRenderTarget(HWND hwnd);

    // 释放当前渲染目标并置空,供 D2DERR_RECREATE_TARGET 与析构复用。
    void ReleaseRenderTarget();

    // 画单个块:文本用 DrawTextLayout,引用竖线/代码背景/表格网格/任务勾选框
    // 用几何图元(矩形/直线/圆角矩形),分割线/脚注分隔线用 DrawLine,
    // 容器块(无文本/无装饰)什么都不画。
    void DrawBlock(const BlockGeometry& g, u32 blockIndex, float scrollY, float targetWidth,
                    ID2D1SolidColorBrush* textBrush,
                    ID2D1SolidColorBrush* quoteBrush,
                    ID2D1SolidColorBrush* codeBgBrush,
                    ID2D1SolidColorBrush* codeBorderBrush,
                    ID2D1SolidColorBrush* hrBrush,
                    ID2D1SolidColorBrush* linkBrush,
                    ID2D1SolidColorBrush* tableHeaderBrush,
                    ID2D1SolidColorBrush* tableGridBrush,
                    ID2D1SolidColorBrush* tableZebraBrush,
                    ID2D1SolidColorBrush* tableRowHoverBrush,
                    ID2D1SolidColorBrush* checkboxBorderBrush,
                    ID2D1SolidColorBrush* checkboxCheckBrush,
                    ID2D1SolidColorBrush* placeholderBgBrush,
                    ID2D1SolidColorBrush* placeholderBorderBrush,
                    ID2D1SolidColorBrush* badgeBgBrush,
                    ID2D1SolidColorBrush* badgeTextBrush,
                    ID2D1SolidColorBrush* findHighlightBrush,
                    ID2D1SolidColorBrush* findCurrentBrush,
                    ID2D1SolidColorBrush* selectionBrush,
                    ID2D1SolidColorBrush* copyIconBrush,
                    ID2D1SolidColorBrush* copyHoverBgBrush,
                    ID2D1SolidColorBrush* copyPaperBrush,
                    ID2D1SolidColorBrush* copyDoneBrush,
                    ID2D1SolidColorBrush* const* hlBrushes);

    // The "copy" glyph itself (two overlapping rounded sheets, T45): pure
    // D2D geometry, no icon font, no bitmap. Shared by the code-block copy
    // button and the bottom-bar "copy path" button so both icons look
    // identical; only draws the sheets, not the button background/hover/
    // copied-state — those are each caller's own shell logic.
    //
    // "复制"图标本体(两张叠压的圆角纸,T45):纯 D2D 几何,零图标字体零位图。
    // 代码块复制按钮与底部栏"复制路径"按钮共用这一份画法,保证两处图标
    // 长得一模一样;只画纸张,不画按钮底色/悬浮态/已复制态,那些是各调用方
    // 自己的外壳逻辑。
    // @param left/top 图标外接正方形左上角(DIP)。
    // @param size 图标外接正方形边长(DIP),各线条/纸张比例相对它换算。
    // @param strokeBrush 纸张描边刷子。
    // @param paperBrush 前纸"纸面"填充色,可为 nullptr(不填充,只描边)。
    void DrawCopySheetsGlyph(float left, float top, float size,
                              ID2D1SolidColorBrush* strokeBrush,
                              ID2D1SolidColorBrush* paperBrush);

    // 画代码块右上角的"复制"按钮(T45):纯 D2D 几何,零图标字体零位图。
    // 三态视觉区分——默认态只画灰色双层纸张轮廓;悬浮态先铺一层浅灰圆角底、
    // 图标不变(底色出现即反馈);已复制态换成绿色对勾 + 绿色圆角边框。
    // 悬浮/已复制两种状态从 overlay_ 里按块下标读(见 ShellOverlay 的两个字段),
    // 没有 overlay 时一律按默认态画。
    void DrawCodeCopyButton(const BlockGeometry& g, u32 blockIndex, float scrollY,
                             ID2D1SolidColorBrush* iconBrush,
                             ID2D1SolidColorBrush* hoverBgBrush,
                             ID2D1SolidColorBrush* paperBrush,
                             ID2D1SolidColorBrush* doneBrush);

    // 画一个块内的全部图片(T33):缓存里有位图就 DrawBitmap,否则画统一样式的
    // 占位块(灰底圆角矩形 + 居中文案);画完位图后若该图被降采样过,再叠加
    // 右下角的"查看原图"提示标签。
    void DrawImages(const BlockGeometry& g, float scrollY,
                     ID2D1SolidColorBrush* textBrush,
                     ID2D1SolidColorBrush* placeholderBgBrush,
                     ID2D1SolidColorBrush* placeholderBorderBrush,
                     ID2D1SolidColorBrush* badgeBgBrush,
                     ID2D1SolidColorBrush* badgeTextBrush);

    // 占位块统一画法(T33,2026-09-17 追加图标):灰底圆角矩形 + 1px 边框 +
    // 居中"图片"图标(相框+太阳+山峰,纯 D2D 几何,零图标字体零位图)+ 图标下方文案。
    // 五种状态(未加载/失败/SVG/超限/网络未加载)共用本函数,只有 text 不同。
    void DrawImagePlaceholder(const D2D1_RECT_F& rect, const wchar_t* text, u32 textLen,
                               ID2D1SolidColorBrush* bgBrush,
                               ID2D1SolidColorBrush* borderBrush,
                               ID2D1SolidColorBrush* textBrush);

    // 降采样提示标签(T33 追加):图片右下角的半透明圆角小标签 + 小号文字,
    // 纯 D2D 几何图元,零图标字体、零位图(与 T27 勾选框同一套画法)。
    void DrawDownsampledBadge(const D2D1_RECT_F& imageRect,
                               ID2D1SolidColorBrush* badgeBgBrush,
                               ID2D1SolidColorBrush* badgeTextBrush);

    // 画欢迎屏(未打开任何文档时,取代正文 DrawBlock 循环):居中标题
    // "Welcome to markair"(与正文一级标题同一套字号/加粗手法)+ 下方一个
    // 宽大的"打开文件"按钮(圆角矩形描边/填充 + 纯 D2D 几何画的文件夹图标
    // + 居中文字)。不参与任何布局/虚拟化,每帧原样重画,开销可忽略。
    // @param targetWidth/targetHeight 渲染目标当前尺寸(DIP)。
    // @param buttonHover 按钮当前是否处于鼠标悬浮态,决定按钮底色深浅。
    // @param textBrush 标题与按钮文字共用的文字色(复用正文文字色槽位)。
    // @param buttonFillBrush 按钮默认态底色。
    // @param buttonHoverFillBrush 按钮悬浮态底色。
    // @param buttonBorderBrush 按钮描边色。
    void DrawWelcomeScreen(float targetWidth, float targetHeight,
                            bool buttonHover, bool folderButtonHover,
                            ID2D1SolidColorBrush* textBrush,
                            ID2D1SolidColorBrush* buttonFillBrush,
                            ID2D1SolidColorBrush* buttonHoverFillBrush,
                            ID2D1SolidColorBrush* buttonBorderBrush);

    // 画表格网格线/表头背景/表体斑马纹/悬浮行高亮:只依赖 BlockGeometry 里
    // 已存好的 tableColWidths/tableRowTops/tableHeadRowCount,不依赖 Document。
    // @param hoverRow 当前悬浮的表体行(body-relative,0 起),kInvalidIndex 表示无。
    void DrawTableChrome(const BlockGeometry& g, float scrollY,
                          ID2D1SolidColorBrush* tableHeaderBrush,
                          ID2D1SolidColorBrush* tableGridBrush,
                          ID2D1SolidColorBrush* tableZebraBrush,
                          ID2D1SolidColorBrush* tableRowHoverBrush,
                          u32 hoverRow);

    // 给链接/自动链接 run 单独着色(T24):不用 SetDrawingEffect,而是对每个
    // link range 的 HitTestTextRange 矩形做 PushAxisAlignedClip 后重画一次
    // 整个 layout(裁剪区之外的部分不可见),不引入自定义渲染器。
    void DrawLinkOverlays(const BlockGeometry& g, float scrollY, ID2D1SolidColorBrush* linkBrush);

    // 给围栏代码块的语法着色 run 单独换色(T53):与 DrawLinkOverlays 同一手法——
    // 不用 SetDrawingEffect/自定义渲染器,对每个 token range 的 HitTestTextRange
    // 矩形做 PushAxisAlignedClip 后重画一次整个 layout。hlBrushes 是按
    // hl/lexer.h::TokenType 取值下标的 7 支画笔数组,调用方保证非空且长度为 7。
    void DrawCodeHighlights(const BlockGeometry& g, float scrollY,
                              ID2D1SolidColorBrush* const* hlBrushes);

    // 画任务列表勾选框(T27):圆角矩形 + 已勾选时叠加两段折线对勾,零字体零位图。
    void DrawTaskCheckbox(const BlockGeometry& g, float scrollY,
                           ID2D1SolidColorBrush* borderBrush,
                           ID2D1SolidColorBrush* checkBrush);

    // 画脚注定义的 "[n]" 编号标签(T28):临时创建一个极小的 IDWriteTextLayout,
    // 画完立即释放,不参与虚拟化/不缓存——脚注定义数量通常很少,这点开销可忽略。
    // fonts_ 为空(未提供字体子系统)时静默跳过。
    void DrawFootnoteLabel(const BlockGeometry& g, float scrollY, ID2D1SolidColorBrush* textBrush);

    // 画列表项前的符号(T44):无序列表用 D2D 几何图元(实心圆点/空心圆/实心方块,
    // 按 listMarkerLevel 三档循环),有序列表临时创建一个极小的 IDWriteTextLayout
    // 画 "N." 这样的序号(用法同 DrawFootnoteLabel,画完立即释放)。任务列表项
    // (g.taskCheckbox 非空)已经在 DrawTaskCheckbox 画了勾选框,这里用
    // g.listMarker.width <= 0 直接跳过,不会重复画。
    void DrawListMarker(const BlockGeometry& g, float scrollY, ID2D1SolidColorBrush* markerBrush);

    // 画一个块内的查找命中高亮(T38):用 HitTestTextRange 拿矩形,在**文本下方**
    // 填半透明底色(不改文本颜色,避免和链接蓝冲突),当前命中换另一种底色。
    void DrawFindHighlights(const BlockGeometry& g, u32 blockIndex, float scrollY,
                             ID2D1SolidColorBrush* fillBrush,
                             ID2D1SolidColorBrush* currentFillBrush);

    // 画一个块内的鼠标拖选高亮(T80):与 DrawFindHighlights 同一手法——
    // HitTestTextRange 拿矩形,画在文本下方。只处理选区与本块相交的那一段
    // (选区跨块时,起止块各自裁到块内偏移,中间块整块高亮)。
    void DrawSelectionHighlights(const BlockGeometry& g, u32 blockIndex, float scrollY,
                                  ID2D1SolidColorBrush* fillBrush);

    // 画顶部浮出的查找条(T37)与窗口内提示(T36 的"路径不存在"),
    // Both share the same "rounded top bar + small text" style; no new
    // menu/toolbar is introduced (§9).
    //
    // 两者共用同一套"顶部圆角条 + 小号文字"的画法,不新增菜单栏/工具栏(§9)。
    void DrawOverlayBar(float targetWidth, const wchar_t* text, u32 textLen,
                         ID2D1SolidColorBrush* bgBrush, ID2D1SolidColorBrush* textBrush,
                         float topOffset);

    // Draw the find bar (T37, 2026-09-19 revision): a fixed-width rounded
    // bar that no longer resizes with query content — the query text itself
    // is shown (with a cursor) by the native Edit child window window.cpp
    // creates. This only draws the background + "Find: " prefix + the
    // right-side status text ("press Enter" / "3/12" / "no matches"),
    // leaving an empty rectangle in the middle for the Edit control. Geometry
    // constants must stay numerically consistent with the same-named
    // constants in shell/find_bar.h — render must not include shell headers
    // (existing constraint), so a few plain numbers are duplicated here
    // (same convention as the other "floating bar" widgets).
    //
    // 画查找条(T37,2026-09-19 改版):固定宽度圆角条,不再随查询串内容
    // 自适应——查询串本身由 window.cpp 创建的原生 Edit 子窗口显示(带光标),
    // 这里只画背景 + "查找: " 前缀 + 右侧状态文字("回车搜索"/"3/12"/
    // "无匹配"),中间给 Edit 控件让出一块空矩形,不画任何内容。几何常量
    // 与 shell/find_bar.h 的同名常量保持一致——render 不反向 include shell
    // 头文件,这里只重复几个纯数字(与其余"浮出条"类控件同一条既有约束)。
    void DrawFindBar(float targetWidth, const wchar_t* statusText, u32 statusTextLen,
                      ID2D1SolidColorBrush* bgBrush, ID2D1SolidColorBrush* textBrush);

    // 画大纲侧栏打开时盖在侧栏外正文区域的半透明蒙层:只盖侧栏矩形之外的
    // 区域(侧栏本身随后单独画,不会被这层蒙层盖住),与 DrawOutlinePanel
    // 共用同一个"侧栏是否打开"的判断依据(overlay_->outlineItems 非空),
    // 侧栏关闭时不产生任何额外绘制。
    void DrawOutlineOverlayMask(float targetWidth, float targetHeight,
                                ID2D1SolidColorBrush* maskBrush);

    // 画大纲侧栏(T63):悬浮在正文左侧上方的一条固定宽度面板,与
    // DrawOverlayBar 同一套"浮出条"底色/风格,只是画成整块矩形 + 逐行文字。
    // 只在 overlay_->outlineItems 非空时被调用,不参与任何布局重排。
    void DrawOutlinePanel(float targetHeight,
                          ID2D1SolidColorBrush* bgBrush, ID2D1SolidColorBrush* textBrush,
                          ID2D1SolidColorBrush* highlightBgBrush,
                          ID2D1SolidColorBrush* highlightTextBrush,
                          ID2D1SolidColorBrush* scrollbarTrackBrush,
                          ID2D1SolidColorBrush* scrollbarThumbBrush);

    /**
     * Draw translucent mask overlay when history sidebar is opening or open.
     *
     * 历史记录侧栏展开或正在展开时，绘制覆盖在侧栏外部正文区域的半透明蒙层。
     *
     * @param targetWidth Client render target width in DIPs.
     *
     *   以 DIP 为单位的客户区渲染目标宽度。
     *
     * @param targetHeight Client render target height in DIPs.
     *
     *   以 DIP 为单位的客户区渲染目标高度。
     *
     * @param maskBrush Brush for drawing translucent mask overlay.
     *
     *   绘制半透明蒙层的画刷。
     */
    void DrawHistoryOverlayMask(float targetWidth, float targetHeight,
                                ID2D1SolidColorBrush* maskBrush);

    /**
     * Draw history drawer sidebar panel on the right side of the window.
     *
     * 在窗口右侧绘制抽屉式历史记录侧边栏面板。
     *
     * @param targetWidth Client render target width in DIPs.
     *
     *   以 DIP 为单位的客户区渲染目标宽度。
     *
     * @param targetHeight Client render target height in DIPs.
     *
     *   以 DIP 为单位的客户区渲染目标高度。
     *
     * @param bgBrush Sidebar background brush.
     *
     *   侧边栏背景画刷。
     *
     * @param textBrush Normal item text brush.
     *
     *   常规条目文本画刷。
     *
     * @param highlightBgBrush Hovered item background highlight brush.
     *
     *   悬浮条目背景高亮画刷。
     *
     * @param buttonBgBrush Opaque background brush for the hovered row's
     *        folder/close buttons — deliberately a different (opaque) brush
     *        from highlightBgBrush, which is semi-transparent and would let
     *        the row's own text show through underneath the buttons.
     *
     *   悬浮行"文件夹/关闭"按钮的不透明底色画刷——刻意与半透明的
     *   highlightBgBrush 区分开,后者会让按钮下方的文案透出来。
     *
     * @param scrollbarTrackBrush Scrollbar track background brush.
     *
     *   滚动条轨道背景画刷。
     *
     * @param scrollbarThumbBrush Scrollbar thumb brush.
     *
     *   滚动条滑块画刷。
     */
    void DrawHistoryPanel(float targetWidth, float targetHeight,
                          ID2D1SolidColorBrush* bgBrush, ID2D1SolidColorBrush* textBrush,
                          ID2D1SolidColorBrush* highlightBgBrush,
                          ID2D1SolidColorBrush* buttonBgBrush,
                          ID2D1SolidColorBrush* scrollbarTrackBrush,
                          ID2D1SolidColorBrush* scrollbarThumbBrush);

    /**
     * 按边框视觉方案(EditBoxBorderStyle)绘制一个输入框容器的边框——
     * Bordered 画四周完整圆角描边，UnderlineOnly 只画底部一条线。
     * 只画边框线，不填充背景，调用方需要背景色时自行 FillRoundedRectangle。
     *
     * @param rect 输入框容器矩形(DIP)。
     * @param borderBrush 描边颜色。
     * @param style 边框视觉方案。
     * @param strokeWidth 描边宽度(DIP)。
     * @example renderer.DrawEditBoxBorder(rect, textBrush, EditBoxBorderStyle::Bordered, 0.8f);
     */
    void DrawEditBoxBorder(D2D1_RECT_F rect, ID2D1SolidColorBrush* borderBrush,
                           EditBoxBorderStyle style, float strokeWidth);

    /**
     * 在窗口左侧绘制抽屉式文件夹 Markdown 列表侧边栏。
     *
     * 注意:文件夹侧栏是"挤压模式"(与悬浮模式的大纲/历史侧栏不同)——
     * 展开时不再画半透明蒙层盖住正文,正文可用宽度改由外壳层
     * (window.cpp/main.cpp)按 FolderSqueezeWidthDip 动态收窄并重新换行,
     * 因此本函数不再需要配套的 DrawFolderOverlayMask。
     *
     * @param tooltipBgBrush hover 行完整路径提示气泡的底色(与窗口内提示条
     *   overlayBar 共用同一套"主题反差半透明深色气泡"画刷,不跟随侧栏
     *   自身底色,保证在亮/暗主题下都能跟正文/侧栏拉开视觉反差)。
     * @param tooltipTextBrush 提示气泡的文字色,同上与 overlayBar 共用。
     */
    void DrawFolderPanel(float targetWidth, float targetHeight,
                         ID2D1SolidColorBrush* bgBrush, ID2D1SolidColorBrush* textBrush,
                         ID2D1SolidColorBrush* highlightBgBrush,
                         ID2D1SolidColorBrush* currentItemBrush,
                         ID2D1SolidColorBrush* buttonBgBrush,
                         ID2D1SolidColorBrush* scrollbarTrackBrush,
                         ID2D1SolidColorBrush* scrollbarThumbBrush,
                         ID2D1SolidColorBrush* tooltipBgBrush,
                         ID2D1SolidColorBrush* tooltipTextBrush);

    /**
     * 绘制通用的侧边栏列表行项（背景、单行省略号截断文本与悬浮操作按钮）。
     *
     * @param params 列表项几何与状态参数。
     * @param textBrush 文本与图标画刷。
     * @param highlightBgBrush 悬浮行背景画刷。
     * @param currentItemBrush 当前激活项背景画刷。
     * @param buttonBgBrush 悬浮操作按钮遮罩底色画刷。
     */
    void DrawSidebarListItem(const SidebarListItemParams& params,
                            ID2D1SolidColorBrush* textBrush,
                            ID2D1SolidColorBrush* highlightBgBrush,
                            ID2D1SolidColorBrush* currentItemBrush,
                            ID2D1SolidColorBrush* buttonBgBrush);

    // 画底部操作栏(2026-09-18 改版):左侧 5 个固定宽度图标按钮(纯 D2D 几何
    // 线条,不带常驻文字标签),按钮间用竖分隔线区分;右侧状态区画当前文档
    // 路径 + 大小(documentPath 为空/空串时不画任何文字)。不参与任何布局
    // 重排,几何常量与 shell/bottom_bar.h 的命中测试同一套口径(数值常量
    // 各自复制一份,render 不反向 include shell 头文件,与大纲侧栏同一约束)。
    // @param documentPath 当前文档完整路径,为 nullptr/空串表示未打开文件。
    // @param documentSizeBytes 当前文档字节数,documentPath 为空时不参与格式化。
    // @param hoverButtonIndex 鼠标当前悬浮的按钮下标(0~4);>= 按钮总数
    //        (含 shell 侧 BottomBarButton::None 的数值)表示未悬浮任何按钮,
    //        不画提示气泡——render 层不认识 BottomBarButton 这个 shell 概念,
    //        只接收一个下标数字,保持单向依赖。
    // @param pathCopied 复制路径按钮当前是否处于点击后的短暂"已复制"成功态
    //        (勾选图标),documentPath 为空时忽略(该按钮本就不存在)。
    void DrawBottomBar(float targetWidth, float targetHeight,
                        ID2D1SolidColorBrush* bgBrush, ID2D1SolidColorBrush* iconBrush,
                        ID2D1SolidColorBrush* textBrush, ID2D1SolidColorBrush* dividerBrush,
                        const wchar_t* documentPath = nullptr, u64 documentSizeBytes = 0,
                        u32 hoverButtonIndex = kBottomBarButtonCountRender,
                        bool pathCopied = false);

    // 自绘滚动条(方案A):轨道(常驻,标示可滚动范围)+ 滑块,圆角矩形,
    // 正文与大纲侧栏共用本函数,区别只是调用方传入的
    // viewportWidth/viewportHeight/totalHeight/scrollY 不同。内容不超过一屏
    // (shell/scrollbar.h::CalcScrollbarMetrics 判定)时两者都不画,零额外开销。
    // 必须在 Identity 变换下调用——不能跟着正文的 leftPaddingDip 平移,否则
    // 位置会整体偏移。
    void DrawScrollbar(float viewportWidth, float viewportHeight, float totalHeight, float scrollY,
                        ID2D1SolidColorBrush* trackBrush, ID2D1SolidColorBrush* thumbBrush);

    // 底部栏"放大/缩小"图标几何(2026-09-19,按给定 SVG viewBox 0 0 24 24 的
    // 直线路径精确复刻:字母 "A" + 右下角 "+"/"-"),设备无关资源,首次用到才
    // 建、跟工厂同生命周期,不必每帧重建。
    ID2D1PathGeometry* zoomInIconGeometry_;
    ID2D1PathGeometry* zoomOutIconGeometry_;

    // Build the fill geometry for the "zoom in" or "zoom out" icon from its
    // SVG path data (nonzero winding rule, matching SVG's default
    // fill-rule); called once and cached the first time that icon is drawn.
    // @param zoomIn true builds zoom-in ("A+"), false builds zoom-out ("A-").
    // @return The newly built path geometry; nullptr if factory_ isn't ready
    //         or creation fails.
    //
    // 按给定 SVG 路径数据构建"放大"或"缩小"图标的填充几何(nonzero 缠绕规则,
    // 与 SVG 默认 fill-rule 一致),只在首次绘制该图标时调用一次并缓存。
    // @param zoomIn true 建放大("A+"),false 建缩小("A-")。
    // @return 新建的路径几何;factory_ 未就绪或创建失败返回 nullptr。
    ID2D1PathGeometry* BuildZoomFontIconGeometry(bool zoomIn);

    // 以下是 5 套按钮系统统一接入 Button/IconButton(shell/button.h)之后,
    // 各自图标内容的 ButtonIconPaintFn 实现——按钮"这是一个按钮"这件事
    // (矩形/命中测试/悬浮态)已经由 Button/IconButton 承载,这里只负责在
    // 给定矩形内画出图标本身(手绘矢量图形,不是图标字体/位图)。全部是
    // static 成员函数,签名匹配 ButtonIconPaintFn;renderCtx 固定是发起
    // 调用的 Renderer* this,按需 static_cast 回来访问 target_/画笔等成员。

    // 底部栏图标(时钟/放大镜/文档等,按 userData 里的按钮下标分派到原有画法)。
    static void PaintBottomBarIcon(void* renderCtx, const ButtonRectDip& rect, void* userData);
    // 欢迎屏"打开文件"按钮的文档图标。
    static void PaintWelcomeFileIcon(void* renderCtx, const ButtonRectDip& rect, void* userData);
    // 欢迎屏"打开文件夹"按钮的文件夹图标。
    static void PaintWelcomeFolderIcon(void* renderCtx, const ButtonRectDip& rect, void* userData);
    // 侧栏行内"关闭"按钮图标(X 两条交叉线)。
    static void PaintSidebarCloseIcon(void* renderCtx, const ButtonRectDip& rect, void* userData);
    // 侧栏行内"打开所在文件夹"按钮图标。
    static void PaintSidebarFolderIcon(void* renderCtx, const ButtonRectDip& rect, void* userData);
    // 侧栏行内"新窗口打开"按钮图标。
    static void PaintSidebarNewWindowIcon(void* renderCtx, const ButtonRectDip& rect, void* userData);
    // 侧栏头部"打开根目录文件夹"按钮图标。
    static void PaintSidebarHeaderFolderIcon(void* renderCtx, const ButtonRectDip& rect, void* userData);
    // 代码块复制按钮默认态的"两张叠压纸"图标(复用 DrawCopySheetsGlyph)。
    static void PaintCodeCopySheetsIcon(void* renderCtx, const ButtonRectDip& rect, void* userData);

    ID2D1Factory* factory_;              // 不拥有,生命周期由调用方保证
    FontSubsystem* fonts_;                // 不拥有,可为空;供 T28 脚注标签与 T33 占位文案使用
    ImageCache* images_;                  // 不拥有,可为空;T33 按 href 取已解码位图
    ID2D1HwndRenderTarget* target_;       // 懒创建,可在 D2DERR_RECREATE_TARGET 后重建
    float dpi_;                           // 渲染目标 DPI,0 表示跟随系统默认
    const ShellOverlay* overlay_;         // 仅在一次 RenderFrame 期间有效的叠加层视图,不拥有
    // T76:本帧客户区高度(DIP),RenderFrame 开头写入。链接/语法着色的逐 run
    // 重绘据此把滚出屏幕的 run 直接跳过——它们画了也看不见,却每个都要整份
    // layout 重画一次(见 DrawLinkOverlays/DrawCodeHighlights 的注释)。
    float frameViewportHeight_;
    const Palette* palette_;              // 当前调色板(T46),不拥有;默认指向 kLightPalette

    // T63 大纲侧栏标题文本的临时拼接/UTF-16 转换缓冲。惰性 Init——侧栏从未
    // 打开过(overlay_->outlineItems 始终为空)时永远不会调用 Init,保持
    // "侧栏关闭时不分配任何 Arena"的口径;一旦打开过,后续帧复用同一块地址
    // 空间(每帧 DrawOutlinePanel 开头 Reset)。
    Arena outlineScratch_;
    bool outlineScratchInited_;
};

}  // namespace markair
