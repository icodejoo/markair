#include "window.h"

#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

#include "../assets/data_uri.h"

namespace mdvn {

namespace {

// 主窗口类名,全进程唯一。
constexpr wchar_t kWindowClassName[] = L"mdvn_main_window";

// 窗口初始逻辑尺寸(DIP),按窗口所在显示器 DPI 缩放后作为物理像素尺寸。
constexpr int kInitialWidthDip = 800;
constexpr int kInitialHeightDip = 600;

// Windows 的标称基准 DPI,DPI 缩放系数 = 实际 DPI / 96。
constexpr int kBaselineDpi = 96;

// Per-Monitor V2 的 DPI_AWARENESS_CONTEXT 常量值((DPI_AWARENESS_CONTEXT)-4),
// 这里自行定义,避免依赖特定 SDK 版本的头文件宏。
constexpr INT_PTR kDpiAwarenessContextPerMonitorV2 = -4;

// 窗口类是否已注册成功,保证 RegisterMainWindowClass 幂等(POD 全局,零初始化)。
bool g_classRegistered = false;

// 从窗口句柄取回绑定的运行期状态;尚未绑定(WM_NCCREATE 之前)时返回 nullptr。
WindowState* StateOf(HWND hwnd) {
    return reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

// 取窗口当前的 DPI 缩放系数(实际 DPI / 96);系统不支持按窗口查 DPI 时回退 1.0。
float DipScaleOf(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return 1.0f;
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    GetDpiForWindowFn getDpi = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
    if (!getDpi) return 1.0f;
    UINT dpi = getDpi(hwnd);
    if (dpi == 0) return 1.0f;
    return static_cast<float>(dpi) / static_cast<float>(kBaselineDpi);
}

// 客户区高度/宽度(DIP)的内部别名,实现见文件末尾的公开函数 ClientHeightDip /
// ClientWidthDip(T36 的窗口内换文档需要在 app 层拿到同一口径的视口尺寸)。
// 注意:这是"原始"客户区尺寸,不含内边距收窄——正文换行宽度/滚动范围计算
// 分别在使用处另外调用 ContentWidthDip / UsableViewportHeightDip 收窄。
float ViewportHeightOf(HWND hwnd) { return ClientHeightDip(hwnd); }
float ViewportWidthOf(HWND hwnd) { return ContentWidthDip(ClientWidthDip(hwnd)); }

// 滚动范围计算统一用的"可用视口高度"(原始视口高度收窄掉上下内边距),
// 喂给 ClampScrollOffset/MaxScrollOffset/ApplyScrollCommand 的 viewportHeight 参数
// 以及 UpdateVisibleRange 的可见区间宽度,滚到底时才会在文档下方留出内边距空白。
float UsableViewportHeightOf(HWND hwnd) { return UsableViewportHeightDip(ViewportHeightOf(hwnd)); }

// float 滚动范围值夹到 SCROLLINFO 的 int 字段能装下的区间,避免溢出/负数。
// 文档高度在真实场景里远不会逼近这个上限,这里只是防御性夹取。
int ClampToScrollInfoRange(float value) {
    constexpr float kScrollInfoMaxValue = 2000000000.0f;
    if (value <= 0.0f) return 0;
    if (value >= kScrollInfoMaxValue) return static_cast<int>(kScrollInfoMaxValue);
    return static_cast<int>(value + 0.5f);  // 四舍五入,精度损失是滚动条 UI 的固有限制
}

// 把 WindowState::scrollY 同步到原生垂直滚动条(WS_VSCROLL)。任何改动过
// scrollY 或者可能改变了总高度/视口尺寸的地方都应该在收尾调用一次,否则会出现
// "用滚轮/键盘滚动了,滑块位置却没跟着动"这种明显 bug。
void SyncScrollBar(HWND hwnd, const WindowState* state) {
    if (!state || !state->layout) return;

    float totalHeight = state->layout->TotalHeight();
    float usableViewportHeight = UsableViewportHeightOf(hwnd);

    SCROLLINFO si{};
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = ClampToScrollInfoRange(totalHeight);
    si.nPage = static_cast<UINT>(ClampToScrollInfoRange(usableViewportHeight));
    si.nPos = ClampToScrollInfoRange(state->scrollY);
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

// T29:在"释放全部 layout 并重排"之前,找到当前视口顶部对应的块下标——
// 取"top <= scrollY 的块中 top 最大的那个",重排后用同一个块下标的新 top
// 作为新的 scrollY,实现"按块下标恢复,不按像素"(裁决/验收标准原文)。
u32 FindTopBlockIndex(const BlockLayoutEngine& layout, float scrollY) {
    u32 best = 0;
    float bestTop = -1.0f;
    for (u32 i = 0; i < layout.BlockCount(); ++i) {
        float top = layout.Geometry(i).top;
        if (top <= scrollY && top > bestTop) {
            best = i;
            bestTop = top;
        }
    }
    return best;
}

// T33:图片解码完成后,若"按当前缓存重排会得到不同尺寸",触发一次重排。
// 重排时按块下标恢复滚动位置(与 T29 的 ApplyZoomChange 同一手法),因此
// 图片尺寸变化只会改变块的排布,不会让用户当前看的那个块跑掉——这是
// "滚动位置不因图片加载而跳动"的实现保证。
bool RelayoutForImagesIfNeeded(HWND hwnd, WindowState* state) {
    if (!state->layout || !state->doc || !state->fonts || !state->images) return false;
    if (!state->layout->ImagePlacementChanged(*state->images)) return false;

    u32 topBlock = FindTopBlockIndex(*state->layout, state->scrollY);
    state->layout->Relayout(*state->doc, ViewportWidthOf(hwnd), state->fonts->Scale(),
                            state->images);

    float usableViewportHeight = UsableViewportHeightOf(hwnd);
    float newScrollY =
        (topBlock < state->layout->BlockCount()) ? state->layout->Geometry(topBlock).top : 0.0f;
    state->scrollY =
        ClampScrollOffset(newScrollY, state->layout->TotalHeight(), usableViewportHeight);
    SyncScrollBar(hwnd, state);

    // Relayout 会淘汰全部 IDWriteTextLayout,必须在这里立刻按新几何重建一次,
    // 否则紧接着的这一帧会画成"只有图片、没有文字"。
    state->layout->UpdateVisibleRange(state->scrollY, state->scrollY + usableViewportHeight,
                                      *state->fonts, state->residency);
    return true;
}

// 滚动偏移变化后的统一收尾:任何触发滚动的路径(滚轮/键盘/查找跳转/换文档/
// 图片解码重排/原生滚动条拖动)都应该走这里,保证:
//   ① scrollY 按当前视口/文档高度重新夹取一次(调用方传入的值未必已经夹过);
//   ② 原生滚动条(WS_VSCROLL)滑块位置与 scrollY 同步;
//   ③ 刷新可见范围内的 IDWriteTextLayout / 图片解码位图并请求重绘。
// 偏移夹取后没有实际变化时跳过②之后的步骤,避免顶部/底部到界后仍反复重绘,
// 但滚动条仍会同步一次(窗口尺寸/文档高度可能已经变了,即使 scrollY 没变)。
// forceRefresh:换文档这类"scrollY 数值可能凑巧没变、但 layout 已经整个换掉了"
// 的场景传 true,跳过"没变化就不刷新虚拟化"的短路判断。
void SetScrollY(HWND hwnd, WindowState* state, float newY, bool forceRefresh = false) {
    if (!state || !state->layout) return;

    float totalHeight = state->layout->TotalHeight();
    float usableViewportHeight = UsableViewportHeightOf(hwnd);
    float clamped = ClampScrollOffset(newY, totalHeight, usableViewportHeight);

    bool changed = forceRefresh || (clamped != state->scrollY);
    state->scrollY = clamped;
    SyncScrollBar(hwnd, state);

    // 虚拟化刷新只在偏移真正变化时才需要(顶部/底部到界后重复触发没有意义);
    // 但重绘请求总是发出——调用方可能是"当前命中变了但仍在同一屏"这种场景
    // (查找条前后跳转),视觉上仍需要刷新高亮颜色。
    if (changed && state->fonts) {
        state->layout->UpdateVisibleRange(state->scrollY, state->scrollY + usableViewportHeight,
                                          *state->fonts, state->residency);
        RelayoutForImagesIfNeeded(hwnd, state);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

// T35:把一次鼠标事件翻译成命中结果(链接 / 图片 / 什么都没命中)。
// 坐标换算与命中判定全部委托给 shell/hit_test.h 的纯函数,这里只负责取 DPI。
HitResult HitTestAtClientPoint(HWND hwnd, WindowState* state, int px, int py) {
    if (!state || !state->layout) {
        return HitResult{HitKind::None, kInvalidIndex, kInvalidIndex, kInvalidIndex};
    }
    // 内容整体因内边距向右下平移了 kContentPaddingDip(渲染时的水平 SetTransform
    // + effectiveScrollY),命中测试必须用同一套换算,否则点击位置会和视觉内容错位。
    float effectiveScrollY = state->scrollY - kContentPaddingDip;
    DocPoint p = ClientToDocument(px, py, DipScaleOf(hwnd), effectiveScrollY);
    p.x -= kContentPaddingDip;
    return HitTestDocument(*state->layout, p.x, p.y);
}

// 按命中结果取回对应的 ImageBox;不是图片命中时返回 nullptr。
const ImageBox* ImageBoxOfHit(const BlockLayoutEngine& layout, const HitResult& hit) {
    if (hit.kind != HitKind::Image || hit.blockIndex >= layout.BlockCount()) return nullptr;
    const BlockGeometry& g = layout.Geometry(hit.blockIndex);
    if (hit.imageIndex >= g.imageBoxes.len) return nullptr;
    return &g.imageBoxes[hit.imageIndex];
}

// 滚动到指定块的顶部(T36 的锚点跳转、T38 的命中跳转共用)。
// 顺序很重要:**先改滚动偏移,再让虚拟化按新位置补齐 layout** —— 目标块不在
// "可见 ± 1 屏"内时,靠的正是这一次 UpdateVisibleRange 补齐,而不是提前把
// 全文 layout 都实例化(那样就破坏了架构 §5 的虚拟化策略)。
void ScrollToBlock(HWND hwnd, WindowState* state, u32 blockIndex) {
    if (!state || !state->layout || blockIndex >= state->layout->BlockCount()) return;
    SetScrollY(hwnd, state, state->layout->Geometry(blockIndex).top);
}

// T36b:点击一张图片 -> 打开它的原始数据。
// 本地路径图直接打开原文件;data: URI / 已下载的网络图把原始字节写临时文件后打开。
// 网络图片尚未下载时,这一次点击的含义是 T34 的"点击加载",不走打开原图分支。
void OnImageClicked(HWND hwnd, WindowState* state, const ImageBox& box) {
    // T34:网络图片且还没下载过 -> 这次点击是"加载这张图",只发一次请求。
    if (box.kind == LinkTargetKind::External && state->images && state->remote) {
        if (state->images->FindRemoteBytes(box.href, nullptr) == nullptr) {
            state->remote->RequestOnUserClick(box.href);
            return;
        }
    }

    if (!state->tempFiles) return;

    const void* rawBytes = nullptr;
    u32 rawLen = 0;
    const wchar_t* extension = nullptr;

    if (box.kind == LinkTargetKind::DataUri && state->imageScratch) {
        // data: URI:重新解一次 base64 拿原始(未降采样)字节,写临时文件后打开。
        state->imageScratch->Reset();
        DataUriPayload payload = ParseDataUri(box.href, state->imageScratch);
        if (!payload.valid || payload.len == 0) return;
        rawBytes = payload.bytes;
        rawLen = payload.len;
        extension = ExtensionForMime(payload.mime);
    } else if (box.kind == LinkTargetKind::External && state->images) {
        u32 len = 0;
        const u8* raw = state->images->FindRemoteBytes(box.href, &len);
        if (!raw) return;
        rawBytes = raw;
        rawLen = len;
        extension = L".img";  // 网络图片沿用通用扩展名,由系统按内容关联程序
    }

    OpenImageOriginal(box.href, box.kind, state->documentDirectory, rawBytes, rawLen,
                      extension, state->tempFiles);
    (void)hwnd;
}

// T36:点击一个链接 -> 按 DecideLinkAction 的判定分流到三类行为。
// 安全边界(非 http/https/mailto/file 的 scheme)已经在判定里挡掉,这里不会
// 出现任何"先执行再检查"的路径。
void OnLinkClicked(HWND hwnd, WindowState* state, u32 linkTargetIdx) {
    if (!state || !state->doc) return;
    if (linkTargetIdx == kInvalidIndex || linkTargetIdx >= state->doc->linkTargets.Size()) return;

    StrSlice href = state->doc->linkTargets[linkTargetIdx].href;
    switch (DecideLinkAction(href)) {
    case LinkAction::OpenExternal:
        // ① 外链交给系统默认浏览器,不内嵌任何浏览。
        OpenExternalTarget(href);
        return;

    case LinkAction::ScrollToAnchor: {
        // ③ 页内锚点:GitHub 式 slug 匹配标题块,滚动到该块顶部。
        u32 blockIndex = FindAnchorBlock(*state->doc, href);
        if (blockIndex == kInvalidIndex) {
            state->statusMessage = L"未找到该锚点";
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        state->statusMessage = nullptr;
        ScrollToBlock(hwnd, state, blockIndex);
        return;
    }

    case LinkAction::OpenMarkdown: {
        // ② 相对/绝对 .md 路径:规范化后在当前窗口内替换文档(裁决 #6)。
        wchar_t fullPath[MAX_PATH * 2]{};
        if (!ResolveMarkdownPath(href, state->documentDirectory, fullPath, MAX_PATH * 2) ||
            !MarkdownFileExists(fullPath)) {
            // 路径不存在只在窗口内提示,不弹 MessageBox(架构 §9)。
            state->statusMessage = L"链接指向的文件不存在";
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (!state->openDocumentInPlace) return;

        bool ok = state->openDocumentInPlace(state->callbackUserData, fullPath);
        state->statusMessage = ok ? nullptr : L"打开文档失败";
        if (ok) {
            // 新文档从头开始看;旧文档的查找结果指向的是旧的块下标,必须一并作废。
            if (state->find) state->find->Close();
            // forceRefresh=true:新文档的 layout 已整个换掉,即使数值上恰好还是
            // 0.0f 也必须重新跑一次 UpdateVisibleRange,不能被"没变化"短路掉。
            SetScrollY(hwnd, state, 0.0f, /*forceRefresh=*/true);
            return;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    case LinkAction::Reject:
    default:
        // javascript: 之类的目标:什么都不做,连提示都不给(不给可疑链接任何反馈)。
        return;
    }
}

// T37/T38:按当前查询串重搜并跳到第一处命中。
void RerunFind(HWND hwnd, WindowState* state) {
    if (!state || !state->find || !state->doc) return;
    state->find->Rerun(*state->doc);
    const Match* m = state->find->CurrentMatch();
    if (m) {
        ScrollToBlock(hwnd, state, m->blockIdx);
    } else {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

// T38:跳到上一处/下一处命中并滚动过去。
void StepFind(HWND hwnd, WindowState* state, bool forward) {
    if (!state || !state->find) return;
    bool moved = forward ? state->find->GoNext() : state->find->GoPrev();
    if (!moved) {
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    const Match* m = state->find->CurrentMatch();
    if (m) ScrollToBlock(hwnd, state, m->blockIdx);
}

// T34:网络图片下载完成(工作线程 PostMessage 回主线程)。
// 把原始压缩字节登记进缓存去重,立刻解码一次,必要时重排后重绘。
void OnRemoteImageDone(HWND hwnd, WindowState* state, RemoteImageResult* result) {
    if (!result) return;
    if (state && state->images && result->succeeded && result->bytes && result->len > 0) {
        state->images->PutRemoteBytes(result->url, result->bytes, result->len);
    } else if (state && state->images) {
        // 下载失败:切到"加载失败"占位,而不是永远停在"点击加载"。
        state->images->Put(result->url, nullptr, 0, 0, ImageStatus::Failed, false);
    }
    ReleaseRemoteImageResult(result);

    if (state && state->layout && state->fonts) {
        float usableViewportHeight = UsableViewportHeightOf(hwnd);
        state->layout->UpdateVisibleRange(state->scrollY, state->scrollY + usableViewportHeight,
                                          *state->fonts, state->residency);
        RelayoutForImagesIfNeeded(hwnd, state);
        SyncScrollBar(hwnd, state);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

// 把虚拟键码翻译成滚动动作;不是滚动键时返回 false。
bool ScrollCommandFromVirtualKey(WPARAM vk, ScrollCommand* out) {
    switch (vk) {
    case VK_PRIOR: *out = ScrollCommand::PageUp;   return true;
    case VK_NEXT:  *out = ScrollCommand::PageDown; return true;
    case VK_HOME:  *out = ScrollCommand::Home;     return true;
    case VK_END:   *out = ScrollCommand::End;      return true;
    case VK_UP:    *out = ScrollCommand::LineUp;   return true;
    case VK_DOWN:  *out = ScrollCommand::LineDown; return true;
    default:       return false;
    }
}

// 按当前滚动偏移画一帧:先按可见范围刷新 layout 虚拟化,再委托 Renderer 绘制。
// 首次绘制成功后触发一次 onFirstPresent 回调(T14 性能埋点用,为空时零开销)。
void PaintOnce(HWND hwnd, WindowState* state) {
    if (!state || !state->renderer) return;
    if (!state->layout || !state->doc || !state->fonts) return;

    // 先把渲染目标建起来,再跑虚拟化 —— 图片解码需要绑定渲染目标创建位图,
    // 否则首帧会白解一次却建不出位图(见 Renderer::EnsureTarget 的注释)。
    state->renderer->EnsureTarget(hwnd);

    float usableViewportHeight = UsableViewportHeightOf(hwnd);
    state->layout->UpdateVisibleRange(state->scrollY, state->scrollY + usableViewportHeight,
                                      *state->fonts, state->residency);
    // T33:首次解码出真实尺寸后,若与占位尺寸不同就在这里做一次性重排,
    // 之后同一批图片不会再触发(ImagePlacementChanged 会返回 false)。
    RelayoutForImagesIfNeeded(hwnd, state);
    SyncScrollBar(hwnd, state);

    // T42(bench 专用):首屏解码完之后,若开启了"强制全量解码",再对整份文档
    // 补一次 UpdateVisibleRange,把首屏之外的图片也解码一遍,近似"滚到底"的
    // 内存读数;只在 benchForceFullDecode 且尚未触发过首帧上报时执行一次,
    // 不影响正常交互场景下的虚拟化行为。
    if (state->benchForceFullDecode && !state->firstPresentDone && state->layout) {
        state->layout->UpdateVisibleRange(0.0f, state->layout->TotalHeight(), *state->fonts,
                                          state->residency);
        RelayoutForImagesIfNeeded(hwnd, state);
    }

    // T37/T38:把查找命中集合与查找条/提示条打包成只读视图交给渲染层;
    // 两者都没有时传 nullptr,渲染层零额外开销。
    ShellOverlay overlay{};
    const ShellOverlay* overlayPtr = nullptr;
    if (state->find || state->statusMessage) {
        if (state->find) {
            Span<const Match> matches = state->find->Matches();
            overlay.matches = matches.data;
            overlay.matchCount = matches.len;
            overlay.currentMatch = state->find->CurrentIndex();
            overlay.findBarVisible = state->find->Visible();
            overlay.findQuery = state->find->Query();
        } else {
            overlay.currentMatch = kInvalidIndex;
        }
        overlay.doc = state->doc;
        overlay.statusMessage = state->statusMessage;
        overlayPtr = &overlay;
    }
    // 内容整体向下推 kContentPaddingDip 实现"上边距":RenderFrame 内部各 DrawXxx
    // 早就在算 `g.top - scrollY`,传一个减去内边距的 scrollY 即等效于内容下移。
    float effectiveScrollY = state->scrollY - kContentPaddingDip;
    bool presented = state->renderer->RenderFrame(hwnd, *state->layout, effectiveScrollY,
                                                   kContentPaddingDip, overlayPtr);

    if (presented && !state->firstPresentDone) {
        state->firstPresentDone = true;
        if (state->onFirstPresent) state->onFirstPresent(state->callbackUserData);
    }
}

// 客户区尺寸变化:同步渲染目标尺寸、按新视口宽度重跑布局(不重解析),
// 并把滚动偏移重新夹到新的合法区间(窗口变高时文档可能已不足以滚那么远)。
void OnSize(HWND hwnd, WindowState* state, UINT32 width, UINT32 height) {
    if (!state) return;

    if (state->renderer) {
        state->renderer->OnResize(width, height);
    }
    if (state->layout && state->doc) {
        // 视口宽度变化只重跑布局,不重新解析(架构 §5/§9)。布局用 DIP,故先按 DPI 换算,
        // 换行宽度再收窄掉左右内边距(ContentWidthDip)。
        float scale = DipScaleOf(hwnd);
        float fontScale = state->fonts ? state->fonts->Scale() : 1.0f;
        float contentWidth = ContentWidthDip(static_cast<float>(width) / scale);
        state->layout->Relayout(*state->doc, contentWidth, fontScale, state->images);
        // 滚动范围夹取同样要用"可用视口高度"(收窄掉上下内边距),口径与
        // ClampScrollOffset 的其余调用点一致,滚到底才会正确留出底部内边距空白。
        float usableViewportHeight = UsableViewportHeightDip(static_cast<float>(height) / scale);
        state->scrollY = ClampScrollOffset(state->scrollY, state->layout->TotalHeight(),
                                           usableViewportHeight);
        // 窗口尺寸变化必然改变滚动条的 nMax/nPage,即使 scrollY 数值没变也要同步。
        SyncScrollBar(hwnd, state);
    }
}

// T29:字号缩放变更后的统一收尾。调用前调用方须已经调过
// FontSubsystem::ZoomIn()/ZoomOut()/ResetZoom() 之一,使新档位生效
// (释放旧 IDWriteTextFormat 并重建);这里负责:记住当前视口顶部块 ->
// 释放全部 IDWriteTextLayout 并按新字号重排 -> 用同一个块下标的新 top
// 恢复滚动位置("按块下标恢复,不按像素")-> 刷新虚拟化范围并请求重绘。
void ApplyZoomChange(HWND hwnd, WindowState* state) {
    if (!state || !state->layout || !state->doc || !state->fonts) return;

    u32 topBlock = FindTopBlockIndex(*state->layout, state->scrollY);

    state->layout->Relayout(*state->doc, ViewportWidthOf(hwnd), state->fonts->Scale(),
                            state->images);

    float usableViewportHeight = UsableViewportHeightOf(hwnd);
    float newScrollY =
        (topBlock < state->layout->BlockCount()) ? state->layout->Geometry(topBlock).top : 0.0f;
    state->scrollY =
        ClampScrollOffset(newScrollY, state->layout->TotalHeight(), usableViewportHeight);
    state->layout->UpdateVisibleRange(state->scrollY, state->scrollY + usableViewportHeight,
                                      *state->fonts, state->residency);
    SyncScrollBar(hwnd, state);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// DPI 变化:先应用系统建议的新窗口矩形,再通知渲染器按新 DPI 重建 D2D 资源。
void OnDpiChanged(HWND hwnd, WindowState* state, UINT newDpi, const RECT* suggested) {
    if (suggested) {
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (state && state->renderer) {
        state->renderer->OnDpiChanged(static_cast<float>(newDpi));
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

// 主窗口过程:绘制、尺寸/DPI 变化、滚轮与键盘滚动、Ctrl+W / Esc 关闭。
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        // 创建时把调用方传入的 WindowState 绑定到窗口上,后续消息直接取用。
        const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    WindowState* state = StateOf(hwnd);

    switch (msg) {
    case WM_PAINT:
        PaintOnce(hwnd, state);
        ValidateRect(hwnd, nullptr);
        return 0;

    case WM_SIZE:
        OnSize(hwnd, state, LOWORD(lparam), HIWORD(lparam));
        return 0;

    case WM_DPICHANGED:
        OnDpiChanged(hwnd, state, LOWORD(wparam), reinterpret_cast<const RECT*>(lparam));
        return 0;

    case WM_LBUTTONDOWN: {
        // T35/T36/T36b:统一走命中测试 —— 链接走链接行为,图片走"打开原图"
        // (网络图未下载时是 T34 的"点击加载",见 OnImageClicked)。
        if (state && state->layout) {
            HitResult hit = HitTestAtClientPoint(hwnd, state, GET_X_LPARAM(lparam),
                                                  GET_Y_LPARAM(lparam));
            if (hit.kind == HitKind::Link) {
                OnLinkClicked(hwnd, state, hit.linkTargetIdx);
                return 0;
            }
            const ImageBox* box = ImageBoxOfHit(*state->layout, hit);
            if (box) {
                OnImageClicked(hwnd, state, *box);
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_SETCURSOR: {
        // T35:鼠标移到链接或可点击的图片/占位块上时给手型光标。
        if (state && state->layout && LOWORD(lparam) == HTCLIENT) {
            POINT pt{};
            if (GetCursorPos(&pt) && ScreenToClient(hwnd, &pt)) {
                if (ShouldUseHandCursor(HitTestAtClientPoint(hwnd, state, pt.x, pt.y))) {
                    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));  // IDC_HAND
                    return TRUE;
                }
            }
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_CHAR: {
        // T37:查找条可见时,可打印字符进查询串、退格删末尾,增量输入即时重搜。
        if (state && state->find && state->find->Visible()) {
            wchar_t ch = static_cast<wchar_t>(wparam);
            bool changed = (ch == 0x08) ? state->find->Backspace() : state->find->AppendChar(ch);
            if (changed) RerunFind(hwnd, state);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case kWmRemoteImageDone:
        OnRemoteImageDone(hwnd, state, reinterpret_cast<RemoteImageResult*>(lparam));
        return 0;

    case WM_MOUSEWHEEL: {
        // T29:Ctrl+滚轮缩放,不与普通滚动共享同一分支。
        bool ctrlDown = (GET_KEYSTATE_WPARAM(wparam) & MK_CONTROL) != 0;
        if (ctrlDown && state && state->fonts) {
            int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            if (delta > 0) state->fonts->ZoomIn();
            else if (delta < 0) state->fonts->ZoomOut();
            ApplyZoomChange(hwnd, state);
            return 0;
        }
        if (state && state->layout) {
            float newY = ScrollByWheel(state->scrollY, GET_WHEEL_DELTA_WPARAM(wparam),
                                       state->layout->TotalHeight(), UsableViewportHeightOf(hwnd));
            SetScrollY(hwnd, state, newY);
        }
        return 0;
    }

    case WM_VSCROLL: {
        // 原生垂直滚动条(WS_VSCROLL)拖动/点击箭头/点击滑槽的统一入口。
        if (state && state->layout) {
            int code = LOWORD(wparam);
            if (IsThumbScrollCode(code)) {
                // MSDN 明确建议:WM_VSCROLL 的 HIWORD(wParam) 只有 16 位,文档高度
                // 一旦超过 65535 DIP 就会截断,拖动滑块/松手时改用 GetScrollInfo 的
                // SIF_TRACKPOS 读取完整精度的滑块位置。
                SCROLLINFO si{};
                si.cbSize = sizeof(SCROLLINFO);
                si.fMask = SIF_TRACKPOS;
                float target = state->scrollY;
                if (GetScrollInfo(hwnd, SB_VERT, &si)) {
                    target = static_cast<float>(si.nTrackPos);
                }
                SetScrollY(hwnd, state, target);
            } else {
                ScrollCommand command;
                if (ScrollCommandFromScrollBarCode(code, &command)) {
                    float newY = ApplyScrollCommand(state->scrollY, command,
                                                    state->layout->TotalHeight(),
                                                    UsableViewportHeightOf(hwnd));
                    SetScrollY(hwnd, state, newY);
                }
            }
        }
        return 0;
    }

    case WM_KEYDOWN: {
        bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        // T37:Esc 先关查找条(查找条没开时才是"关窗口")。
        if (wparam == VK_ESCAPE && state && state->find && state->find->Visible()) {
            state->find->Close();
            state->statusMessage = nullptr;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        // Ctrl+W / Esc:关闭当前窗口(每个文件一个独立窗口,关掉即退出本进程)。
        if (wparam == VK_ESCAPE || (ctrlDown && wparam == 'W')) {
            DestroyWindow(hwnd);
            return 0;
        }
        // T37:Ctrl+F 打开查找条。
        if (ctrlDown && wparam == 'F' && state && state->find) {
            state->find->Open();
            state->statusMessage = nullptr;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        // T38:Enter / Shift+Enter / F3 / Shift+F3 在命中之间前后跳转。
        if (state && state->find && state->find->Visible() && wparam == VK_RETURN) {
            StepFind(hwnd, state, !shiftDown);
            return 0;
        }
        if (state && state->find && wparam == VK_F3) {
            StepFind(hwnd, state, !shiftDown);
            return 0;
        }
        // T29:Ctrl+=/Ctrl+-/Ctrl+0 字号缩放(放大/缩小一档/复位到 1.0)。
        if (ctrlDown && state && state->fonts &&
            (wparam == VK_OEM_PLUS || wparam == VK_OEM_MINUS || wparam == '0')) {
            if (wparam == VK_OEM_PLUS) state->fonts->ZoomIn();
            else if (wparam == VK_OEM_MINUS) state->fonts->ZoomOut();
            else state->fonts->ResetZoom();
            ApplyZoomChange(hwnd, state);
            return 0;
        }
        ScrollCommand command = ScrollCommand::Home;
        if (!ctrlDown && state && state->layout &&
            ScrollCommandFromVirtualKey(wparam, &command)) {
            float newY = ApplyScrollCommand(state->scrollY, command,
                                            state->layout->TotalHeight(),
                                            UsableViewportHeightOf(hwnd));
            SetScrollY(hwnd, state, newY);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

}  // namespace

bool EnablePerMonitorV2DpiAwareness() {
    // 选择代码方式而非 manifest:不需要改动构建脚本,且能按系统能力优雅退化。
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return false;

    using SetContextFn = BOOL(WINAPI*)(HANDLE);
    SetContextFn setContext = reinterpret_cast<SetContextFn>(
        reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
    if (!setContext) return false;  // Win10 1703 以前:退化为系统 DPI 感知

    return setContext(reinterpret_cast<HANDLE>(kDpiAwarenessContextPerMonitorV2)) != FALSE;
}

bool RegisterMainWindowClass(HINSTANCE instance) {
    if (g_classRegistered) return true;

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClassName;
    // 标准箭头光标(IDC_ARROW 的资源序号 32512;工程未定义 UNICODE 宏,
    // 这里显式用宽字符版本的资源 ID 以匹配 LoadCursorW)。
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    // 背景刷取主题背景色(当前浅色主题 = COLOR_WINDOW,与 Renderer 的清屏色一致),
    // 避免窗口首次显示到首帧绘制之间出现与主题不符的白闪(架构 §6)。
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    g_classRegistered = RegisterClassW(&wc) != 0;
    return g_classRegistered;
}

HWND CreateMainWindow(HINSTANCE instance, const wchar_t* title, WindowState* state) {
    if (!state) return nullptr;
    state->scrollY = 0.0f;
    state->firstPresentDone = false;

    // 标准 Windows 标题栏(WS_OVERLAPPEDWINDOW),不自绘(裁决 #9)。
    // WS_VSCROLL:原生右侧滚动条,外观完全交给系统主题,不自绘。
    HWND hwnd = CreateWindowExW(
        0, kWindowClassName, title, WS_OVERLAPPEDWINDOW | WS_VSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT, kInitialWidthDip, kInitialHeightDip,
        nullptr, nullptr, instance, state);
    if (!hwnd) return nullptr;

    // 窗口创建成功、显示之前触发一次回调(T14 性能埋点用,为空时零开销)。
    if (state->onWindowCreated) state->onWindowCreated(state->callbackUserData);

    // Per-Monitor V2 下窗口尺寸是物理像素,按实际所在显示器的 DPI 放大初始尺寸,
    // 让高 DPI 屏上的初始窗口与 100% 缩放时视觉大小一致。
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    GetDpiForWindowFn getDpi = user32 ? reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow"))) : nullptr;
    if (getDpi) {
        UINT dpi = getDpi(hwnd);
        if (dpi != 0 && dpi != static_cast<UINT>(kBaselineDpi)) {
            int width = MulDiv(kInitialWidthDip, static_cast<int>(dpi), kBaselineDpi);
            int height = MulDiv(kInitialHeightDip, static_cast<int>(dpi), kBaselineDpi);
            SetWindowPos(hwnd, nullptr, 0, 0, width, height,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    // 保险起见显式同步一次滚动条:WM_SIZE 通常会在窗口创建/显示过程中触发并
    // 顺带同步(见 OnSize),这里再补一次是为了在极端情况下(比如 WM_SIZE 没有
    // 如预期触发)也不会出现"滚动条还没配置好"的窗口。
    SyncScrollBar(hwnd, state);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}

float ClientWidthDip(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    return static_cast<float>(rc.right - rc.left) / DipScaleOf(hwnd);
}

float ClientHeightDip(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    return static_cast<float>(rc.bottom - rc.top) / DipScaleOf(hwnd);
}

int RunMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

}  // namespace mdvn
