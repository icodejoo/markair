// mdvn 的 D2D 渲染模块(T11):设备/渲染目标管理 + 把 BlockLayoutEngine 的
// 布局结果画到窗口上。渲染目标默认软件光栅化(架构决策,见 memory.md),
// 本模块不做"硬件优先、失败再降级"的运行时探测。
#pragma once

#include <d2d1.h>
#include <windows.h>

#include "../assets/image.h"
#include "../doc/search.h"
#include "../layout/layout.h"
#include "theme.h"

namespace mdvn {

class Renderer;

/**
 * 外壳层叠加层视图(T36/T37/T38):查找命中高亮 + 顶部查找条 + 窗口内提示文字。
 *
 * 渲染层**只读**,不拥有其中任何一块内存,生命周期由外壳层(window.cpp)保证
 * 覆盖一次 `RenderFrame` 调用。所有字段都允许为空/为 0,全空时等价于不画叠加层。
 *
 * @example
 *   mdvn::ShellOverlay overlay{find.Matches().data, find.MatchCount(),
 *                               find.CurrentIndex(), &doc, find.Visible(),
 *                               find.Query(), nullptr,
 *                               mdvn::kInvalidIndex, mdvn::kInvalidIndex};
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
};

/**
 * 判断一个 D2D 调用返回的 HRESULT 是否要求重建渲染目标。
 * 抽成纯函数是因为这条判断本身不依赖真实设备,可以脱离 D2D 单独做单元测试;
 * 真正的重建(释放旧目标/下次绘制时惰性重新创建)仍需要真实 HWND,留在
 * Renderer::RenderFrame 里。
 * @param hr 某次 D2D 调用(通常是 EndDraw)返回的 HRESULT。
 * @return hr 等于 D2DERR_RECREATE_TARGET 时返回 true。
 * @example if (mdvn::ShouldRecreateRenderTarget(hr)) { / * 释放旧目标 * / }
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
 * @example const wchar_t* t = mdvn::ImagePlaceholderText(mdvn::ImageStatus::Unsupported);
 */
const wchar_t* ImagePlaceholderText(ImageStatus status);

/**
 * 降采样提示标签(T33 追加)的文案:图片被 T30 缩小过时,在其右下角显示。
 * @return 以 '\0' 结尾的静态宽字符串。
 * @example const wchar_t* tag = mdvn::DownsampledBadgeText();
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
 *   mdvn::ImageResidencyManager residency;
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
 *   mdvn::Renderer renderer;
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
     * @return 本帧是否真正完成了一次成功的 `EndDraw`(渲染目标创建失败或
     *         `EndDraw` 返回失败 HRESULT 时为 false),供调用方判断"首帧是否
     *         已真正显示"(T14 性能埋点用)。
     * @example bool ok = renderer.RenderFrame(hwnd, layoutEngine, 0.0f, 12.0f, &overlay);
     */
    bool RenderFrame(HWND hwnd, const BlockLayoutEngine& layout, float scrollY,
                      float leftPaddingDip, const ShellOverlay* overlay = nullptr);

    /**
     * 切换当前调色板(T46,为 T49 主题切换打基础):只改一个指针,不拷贝
     * `Palette`、不触发任何重排/重建渲染目标,下一帧 `RenderFrame` 起生效。
     * @param palette 新调色板,生命周期须覆盖本对象(通常传 `&kLightPalette`
     *                或 `&kDarkPalette` 这类静态常量);传 nullptr 时忽略,
     *                保持当前调色板不变。
     * @example renderer.SetPalette(&mdvn::kDarkPalette);
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
                    ID2D1SolidColorBrush* hrBrush,
                    ID2D1SolidColorBrush* linkBrush,
                    ID2D1SolidColorBrush* tableHeaderBrush,
                    ID2D1SolidColorBrush* tableGridBrush,
                    ID2D1SolidColorBrush* checkboxBorderBrush,
                    ID2D1SolidColorBrush* checkboxCheckBrush,
                    ID2D1SolidColorBrush* placeholderBgBrush,
                    ID2D1SolidColorBrush* placeholderBorderBrush,
                    ID2D1SolidColorBrush* badgeBgBrush,
                    ID2D1SolidColorBrush* badgeTextBrush,
                    ID2D1SolidColorBrush* findHighlightBrush,
                    ID2D1SolidColorBrush* findCurrentBrush,
                    ID2D1SolidColorBrush* copyIconBrush,
                    ID2D1SolidColorBrush* copyHoverBgBrush,
                    ID2D1SolidColorBrush* copyPaperBrush,
                    ID2D1SolidColorBrush* copyDoneBrush,
                    ID2D1SolidColorBrush* const* hlBrushes);

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
    // 右下角的"已压缩·点击看原图"提示标签。
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

    // 画表格网格线与表头背景(T26):只依赖 BlockGeometry 里已存好的
    // tableColWidths/tableRowTops/tableHeadRowCount,不依赖 Document。
    void DrawTableChrome(const BlockGeometry& g, float scrollY,
                          ID2D1SolidColorBrush* tableHeaderBrush,
                          ID2D1SolidColorBrush* tableGridBrush);

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

    // 画顶部浮出的查找条(T37)与窗口内提示(T36 的"路径不存在"),
    // 两者共用同一套"顶部圆角条 + 小号文字"的画法,不新增菜单栏/工具栏(§9)。
    void DrawOverlayBar(float targetWidth, const wchar_t* text, u32 textLen,
                         ID2D1SolidColorBrush* bgBrush, ID2D1SolidColorBrush* textBrush,
                         float topOffset);

    ID2D1Factory* factory_;              // 不拥有,生命周期由调用方保证
    FontSubsystem* fonts_;                // 不拥有,可为空;供 T28 脚注标签与 T33 占位文案使用
    ImageCache* images_;                  // 不拥有,可为空;T33 按 href 取已解码位图
    ID2D1HwndRenderTarget* target_;       // 懒创建,可在 D2DERR_RECREATE_TARGET 后重建
    float dpi_;                           // 渲染目标 DPI,0 表示跟随系统默认
    const ShellOverlay* overlay_;         // 仅在一次 RenderFrame 期间有效的叠加层视图,不拥有
    const Palette* palette_;              // 当前调色板(T46),不拥有;默认指向 kLightPalette
};

}  // namespace mdvn
