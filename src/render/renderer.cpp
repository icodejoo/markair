#include "renderer.h"

#include "../assets/data_uri.h"

namespace mdvn {

namespace {

// 分割线右侧留白(DIP),与 layout.cpp 里代码块背景的右侧留白取相同量级,
// 视觉上让分割线/代码块背景右边缘对齐。
constexpr float kThematicBreakRightMarginDip = 16.0f;

// 背景色:主题背景(浅色),与窗口类的 GDI 背景色保持一致,避免首帧白闪
// (架构 §6,已在窗口类里落地;这里只是让 D2D 清屏色跟 GDI 背景一致)。
// 文字的实际绘制 left——代码高亮区(g.textPad > 0)固定右移一份内边距,
// 让文字离背景左边框有留白;非任务列表项的列表符号(T44,g.listMarkerPad > 0)
// 同理右移让出符号/序号的位置,indent 本身不含这份偏移(见 layout.h 的
// listMarkerPad 字段注释);其余情况两者恒为 0,原样返回 g.indent。
float TextDrawLeft(const BlockGeometry& g) { return g.indent + g.textPad + g.listMarkerPad; }

// 文字的实际绘制 top,两种场景各自独立生效(互不相关,一个块不会同时是
// 表格单元格和代码块):
//  - 代码高亮区(g.textPad > 0):固定下移一份内边距,让文字离背景上边框
//    有留白。
//  - 表格单元格(g.contentHeight > 0):同一行所有单元格共用一份"行高"(取该
//    行各单元格估算高度的最大值,见 LayoutTableSubtree),但单个单元格文字的
//    真实高度(g.contentHeight,来自 GetMetrics)往往比行高小,直接从 g.top
//    起画会贴在行顶部,这里按 (行高 - 真实内容高度) / 2 算一份垂直居中偏移。
float TextDrawTop(const BlockGeometry& g) {
    float top = g.top + g.textPad;
    if (g.contentHeight <= 0.0f) return top;
    float rowHeight = g.bottom - g.top;
    float offset = (rowHeight - g.contentHeight) * 0.5f;
    return offset > 0.0f ? top + offset : top;
}

// 代码高亮区圆角半径(DIP),参考常见 Markdown 渲染器的代码块风格。
constexpr float kCodeBlockCornerRadiusDip = 4.0f;

// T27 勾选框圆角半径(DIP)。
constexpr float kCheckboxCornerRadiusDip = 3.0f;

// T33 图片占位块:圆角半径、内边距(颜色已收进 T46 的 Palette,见 theme.h)。
constexpr float kImagePlaceholderCornerRadiusDip = 6.0f;
constexpr float kImagePlaceholderTextPaddingDip = 8.0f;

// T33 标签的字号(与 T28 脚注小字号同一量级)、圆角、内边距与距图片边缘的外边距。
constexpr float kBadgeFontSizeDip = 11.0f;
constexpr float kBadgeCornerRadiusDip = 4.0f;
constexpr float kBadgePaddingXDip = 6.0f;
constexpr float kBadgePaddingYDip = 2.0f;
constexpr float kBadgeMarginDip = 6.0f;

// T33 占位块文案的排版高度上限(只是给 DWrite 一个够用的框,不截断)。
constexpr float kPlaceholderTextMaxHeightDip = 4096.0f;

// 占位块中央"图片"图标(2026-09-17 追加):零图标字体、零位图,纯 D2D 几何——
// 一个圆角方框(相框)+ 左上角一个实心圆(太阳)+ 底部两段折线(山峰),
// 是浏览器/系统最常见的"图片占位"符号,一眼可辨认(颜色见 Palette::imagePlaceholderIcon)。
constexpr float kPlaceholderIconMinBoxDip = 40.0f;  // 占位块矩形小于此高度时不画图标,只留文案(避免小图标挤爆)
constexpr float kPlaceholderIconSizeRatio = 0.32f;  // 图标边长相对 min(宽,高) 的比例
constexpr float kPlaceholderIconMinSizeDip = 18.0f;
constexpr float kPlaceholderIconMaxSizeDip = 48.0f;
constexpr float kPlaceholderIconCornerRadiusDip = 3.0f;
constexpr float kPlaceholderIconCenterYRatio = 0.38f;  // 图标中心相对占位块高度的位置,偏上给下方文案留空间
constexpr float kPlaceholderIconTextGapDip = 6.0f;     // 图标底边到文案顶边的间距

// T45 复制按钮:图标线条色(中性灰)、悬浮底色(比代码块背景更深一档的浅灰,
// 底色一出现就是明确的悬浮反馈)、图标"纸面"填充色(与页面底色一致,让两张纸
// 的叠压关系看得出来)、已复制反馈色(绿色对勾,与任务勾选框同一绿系)——
// 颜色已收进 T46 的 Palette(codeCopy* 系列槽位),这里只留几何比例常量。
// T45 复制按钮的几何比例(相对按钮边长,0~1):两张"纸"的位置与大小、圆角、
// 线宽。按钮边长由布局层决定(kCodeCopyButtonSizeDip),这里只按比例换算,
// 保证字号缩放时图标跟着等比放大。
constexpr float kCopyButtonCornerRadiusRatio = 0.20f;
constexpr float kCopyBackSheetLeftRatio = 0.20f;
constexpr float kCopyBackSheetTopRatio = 0.14f;
constexpr float kCopyBackSheetRightRatio = 0.68f;
constexpr float kCopyBackSheetBottomRatio = 0.62f;
constexpr float kCopyFrontSheetLeftRatio = 0.34f;
constexpr float kCopyFrontSheetTopRatio = 0.32f;
constexpr float kCopyFrontSheetRightRatio = 0.84f;
constexpr float kCopyFrontSheetBottomRatio = 0.86f;
constexpr float kCopySheetCornerRadiusRatio = 0.10f;
constexpr float kCopyStrokeWidthRatio = 0.075f;
// 已复制态对勾的三个折点(相对按钮边长)与线宽比例,画法同任务列表勾选框。
constexpr float kCopyCheckStrokeWidthRatio = 0.13f;

// T38 查找命中高亮:普通命中用半透明淡黄底,当前命中用半透明橙底区分。
// 只画在文本**下方**(先填底再画字),因此不改变任何文本颜色,与 T24 的链接蓝互不干扰
// (颜色见 Palette::findHighlight / findCurrentHighlight)。

// T37 查找条 / T36 窗口内提示共用的顶部浮出条样式(颜色见 Palette::overlayBarBackground/Text)。
constexpr float kOverlayBarFontSizeDip = 13.0f;
constexpr float kOverlayBarCornerRadiusDip = 6.0f;
constexpr float kOverlayBarPaddingXDip = 10.0f;
constexpr float kOverlayBarPaddingYDip = 5.0f;
constexpr float kOverlayBarMarginDip = 10.0f;   // 距客户区右上角的外边距
constexpr float kOverlayBarMaxWidthDip = 520.0f; // 查找条最大宽度,超长查询串自行截断

// 单个查找命中最多跨几行:HitTestTextRange 的输出上限,超出部分不画(极端长命中)。
constexpr UINT32 kMaxFindHitMetrics = 16;

// 叠加层条文字的栈上缓冲长度,以及"查找条 + 提示条"同时出现时第二条的下移量。
constexpr u32 kMaxOverlayBarChars = 256;
constexpr float kOverlayBarStackStepDip = 34.0f;

// 计算一个以 '\0' 结尾的宽字符串的长度(不含结尾符)。
u32 WideLength(const wchar_t* s) {
    u32 n = 0;
    while (s && s[n] != 0) ++n;
    return n;
}

// 往 buf 追加一个十进制数字串,返回推进后的游标。
u32 AppendDecimal(wchar_t* buf, u32 cursor, u32 cap, u32 value) {
    wchar_t digits[10];
    u32 n = 0;
    if (value == 0) {
        digits[n++] = L'0';
    } else {
        while (value > 0 && n < 10) {
            digits[n++] = static_cast<wchar_t>(L'0' + (value % 10));
            value /= 10;
        }
    }
    for (u32 i = 0; i < n && cursor + 1 < cap; ++i) buf[cursor++] = digits[n - 1 - i];
    return cursor;
}

// 往 buf 追加一段以 '\0' 结尾的字面量,返回推进后的游标。
u32 AppendLiteral(wchar_t* buf, u32 cursor, u32 cap, const wchar_t* s) {
    for (u32 i = 0; s && s[i] != 0 && cursor + 1 < cap; ++i) buf[cursor++] = s[i];
    return cursor;
}

// 拼出查找条上显示的一行文字:`查找: <关键词>  (3/12)`。
// 没有关键词时只显示提示语,有关键词但零命中时显示"无匹配"。
u32 ComposeFindBarText(const ShellOverlay* overlay, wchar_t* buf, u32 cap) {
    u32 cursor = AppendLiteral(buf, 0, cap, L"查找: ");
    const wchar_t* query = overlay->findQuery;
    if (!query || query[0] == 0) {
        cursor = AppendLiteral(buf, cursor, cap, L"(输入关键词,Esc 关闭)");
        buf[cursor] = 0;
        return cursor;
    }
    cursor = AppendLiteral(buf, cursor, cap, query);
    cursor = AppendLiteral(buf, cursor, cap, L"   ");
    if (overlay->matchCount == 0) {
        cursor = AppendLiteral(buf, cursor, cap, L"无匹配");
    } else {
        u32 ordinal = (overlay->currentMatch == kInvalidIndex) ? 1u : overlay->currentMatch + 1u;
        cursor = AppendDecimal(buf, cursor, cap, ordinal);
        cursor = AppendLiteral(buf, cursor, cap, L"/");
        cursor = AppendDecimal(buf, cursor, cap, overlay->matchCount);
    }
    buf[cursor] = 0;
    return cursor;
}

}  // namespace

const wchar_t* ImagePlaceholderText(ImageStatus status) {
    switch (status) {
    case ImageStatus::Ok:              return L"";
    case ImageStatus::NotLoaded:       return L"图片加载中";
    case ImageStatus::Failed:          return L"图片加载失败";
    case ImageStatus::Unsupported:     return L"不支持的图片格式(SVG)";
    case ImageStatus::TooLarge:        return L"图片过大,未加载";
    case ImageStatus::RemoteNotLoaded: return L"网络图片,点击加载";
    }
    return L"图片加载失败";
}

const wchar_t* DownsampledBadgeText() { return L"已压缩·点击看原图"; }

namespace {

// 某个状态是否是"终态失败":不要每帧重试解码(损坏文件、SVG、超限)。
bool IsTerminalFailure(ImageStatus status) {
    return status == ImageStatus::Failed || status == ImageStatus::Unsupported ||
           status == ImageStatus::TooLarge;
}

// 把 UTF-8 的相对/绝对路径拼成可用于 WIC 打开的宽字符路径。
// 返回 false 表示路径非法或超长(调用方按解码失败处理)。
bool BuildLocalImagePath(StrSlice href, const wchar_t* docDir, wchar_t* out, int outCap) {
    if (!href.data || href.len == 0) return false;

    wchar_t rel[MAX_PATH]{};
    int written = MultiByteToWideChar(CP_UTF8, 0, href.data, static_cast<int>(href.len),
                                       rel, MAX_PATH - 1);
    if (written <= 0) return false;
    rel[written] = 0;
    for (int i = 0; i < written; ++i) {
        if (rel[i] == L'/') rel[i] = L'\\';
    }

    bool absolute = (rel[0] == L'\\') || (rel[0] != 0 && rel[1] == L':');
    if (absolute || !docDir || docDir[0] == 0) {
        int i = 0;
        for (; i < written && i < outCap - 1; ++i) out[i] = rel[i];
        out[i] = 0;
        return i == written;
    }

    int cursor = 0;
    for (const wchar_t* p = docDir; *p && cursor < outCap - 1; ++p) out[cursor++] = *p;
    if (cursor > 0 && out[cursor - 1] != L'\\' && cursor < outCap - 1) out[cursor++] = L'\\';
    int i = 0;
    for (; i < written && cursor < outCap - 1; ++i) out[cursor++] = rel[i];
    out[cursor] = 0;
    return i == written;
}

}  // namespace

ImageResidencyManager::ImageResidencyManager()
    : decoder_(), renderer_(nullptr), cache_(nullptr), scratch_(nullptr), docDir_(nullptr) {}

ImageResidencyManager::~ImageResidencyManager() {}

void ImageResidencyManager::Init(Renderer* renderer, ImageCache* cache, Arena* scratch,
                                  const wchar_t* documentDirectory) {
    renderer_ = renderer;
    cache_ = cache;
    scratch_ = scratch;
    docDir_ = documentDirectory;
}

ImageStatus ImageResidencyManager::DecodeNow(const ImageBox& box) {
    if (!cache_) return ImageStatus::Failed;

    // SVG 直接判不支持,不进 WIC(裁决)——这条判断在任何 WIC 调用之前。
    if (IsSvgImageRef(box.href)) {
        cache_->Put(box.href, nullptr, 0, 0, ImageStatus::Unsupported, false);
        return ImageStatus::Unsupported;
    }

    ID2D1RenderTarget* target = renderer_ ? renderer_->Target() : nullptr;
    DecodedImage decoded{nullptr, 0, 0, ImageStatus::Failed, false};

    if (box.kind == LinkTargetKind::DataUri) {
        if (!scratch_) return ImageStatus::Failed;
        scratch_->Reset();  // data: URI 的解码缓冲只需活到本次解码结束
        DataUriPayload payload = ParseDataUri(box.href, scratch_);
        if (!payload.valid || payload.len == 0) {
            cache_->Put(box.href, nullptr, 0, 0, ImageStatus::Failed, false);
            return ImageStatus::Failed;
        }
        decoded = decoder_.DecodeFromMemory(payload.bytes, payload.len, target);
    } else if (box.kind == LinkTargetKind::External) {
        // 网络图片:只有已经下载过原始字节才解码,否则停在"点击加载"占位(T34)。
        u32 rawLen = 0;
        const u8* raw = cache_->FindRemoteBytes(box.href, &rawLen);
        if (!raw) {
            cache_->Put(box.href, nullptr, 0, 0, ImageStatus::RemoteNotLoaded, false);
            return ImageStatus::RemoteNotLoaded;
        }
        decoded = decoder_.DecodeFromMemory(raw, rawLen, target);
    } else if (box.kind == LinkTargetKind::RelativePath) {
        wchar_t path[MAX_PATH * 2]{};
        if (!BuildLocalImagePath(box.href, docDir_, path, MAX_PATH * 2)) {
            cache_->Put(box.href, nullptr, 0, 0, ImageStatus::Failed, false);
            return ImageStatus::Failed;
        }
        decoded = decoder_.DecodeFromFile(path, target);
    } else {
        cache_->Put(box.href, nullptr, 0, 0, ImageStatus::Failed, false);
        return ImageStatus::Failed;
    }

    cache_->Put(box.href, decoded.bitmap, decoded.width, decoded.height, decoded.status,
                decoded.wasDownsampled);
    return decoded.status;
}

void ImageResidencyManager::EnsureResident(const ImageBox* boxes, u32 count) {
    if (!cache_ || !boxes) return;
    for (u32 i = 0; i < count; ++i) {
        const ImageBox& box = boxes[i];
        const ImageCacheEntry* entry = cache_->Find(box.href);
        if (entry) {
            if (entry->bitmap) continue;                  // 位图还活着,无需重解
            if (IsTerminalFailure(entry->status)) continue; // 终态失败,不每帧重试
            if (entry->status == ImageStatus::RemoteNotLoaded &&
                cache_->FindRemoteBytes(box.href, nullptr) == nullptr) {
                continue;  // 网络图还没下载,等用户点击加载
            }
        }
        DecodeNow(box);
    }
}

void ImageResidencyManager::ReleaseResident(const ImageBox* boxes, u32 count) {
    if (!cache_ || !boxes) return;
    // 只丢解码位图,{width, height} 留在缓存里 —— 这是"滚回来时布局不跳动"的保证。
    for (u32 i = 0; i < count; ++i) cache_->ReleaseBitmap(boxes[i].href);
}

// 构造一个未绑定工厂、未创建渲染目标的渲染器。
Renderer::Renderer()
    : factory_(nullptr), fonts_(nullptr), images_(nullptr), target_(nullptr), dpi_(0.0f),
      overlay_(nullptr), palette_(&kLightPalette) {}

// 析构时释放渲染目标本体;工厂/字体子系统均不归本对象所有,不在此释放。
Renderer::~Renderer() { ReleaseRenderTarget(); }

void Renderer::Init(ID2D1Factory* factory, FontSubsystem* fonts, ImageCache* images) {
    factory_ = factory;
    fonts_ = fonts;
    images_ = images;
}

ID2D1RenderTarget* Renderer::Target() const { return target_; }

// 切换调色板:只改指针,不拷贝 Palette、不碰渲染目标/布局(T46,为 T49 打基础)。
void Renderer::SetPalette(const Palette* palette) {
    if (palette) palette_ = palette;
}

bool Renderer::EnsureTarget(HWND hwnd) { return EnsureRenderTarget(hwnd); }

void Renderer::OnResize(UINT32 width, UINT32 height) {
    if (target_) {
        target_->Resize(D2D1::SizeU(width, height));
    }
}

void Renderer::OnDpiChanged(float dpi) {
    dpi_ = dpi;
    // 直接整体释放渲染目标,复用 EnsureRenderTarget 的惰性重建路径,
    // 不另写一套"就地改 DPI + Resize"的逻辑。
    ReleaseRenderTarget();
}

void Renderer::ReleaseRenderTarget() {
    if (target_) {
        target_->Release();
        target_ = nullptr;
    }
    // 缓存里的 ID2D1Bitmap 是绑在刚刚释放掉的渲染目标上的,必须一并丢弃;
    // 尺寸/状态信息由 ImageCache 保留下来,重建后重新解码不会改变几何,
    // 因此不会造成滚动位置跳动。
    if (images_) images_->ReleaseAllBitmaps();
}

bool Renderer::EnsureRenderTarget(HWND hwnd) {
    if (target_ || !factory_) return target_ != nullptr;

    RECT rc{};
    GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left),
                                    static_cast<UINT32>(rc.bottom - rc.top));

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    rtProps.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;  // 默认软件渲染(架构决策,见 memory.md)
    // dpi_ 为 0 时保持 D2D 默认值(跟随系统 DPI);WM_DPICHANGED 后按新 DPI 重建。
    rtProps.dpiX = dpi_;
    rtProps.dpiY = dpi_;

    HRESULT hr = factory_->CreateHwndRenderTarget(
        rtProps, D2D1::HwndRenderTargetProperties(hwnd, size), &target_);
    if (FAILED(hr)) {
        // 创建渲染目标失败(极端情况,如系统内存不足):容错,保持 target_ 为空,
        // 调用方(RenderFrame)会静默跳过本帧绘制,不崩溃,下次绘制会重试。
        target_ = nullptr;
        return false;
    }
    return true;
}

void Renderer::DrawTableChrome(const BlockGeometry& g, float scrollY,
                                ID2D1SolidColorBrush* tableHeaderBrush,
                                ID2D1SolidColorBrush* tableGridBrush) {
    if (g.tableColWidths.len == 0 || g.tableRowTops.len < 2) return;

    float left = g.indent;
    float right = left;
    for (u32 c = 0; c < g.tableColWidths.len; ++c) right += g.tableColWidths[c];
    float top = g.tableRowTops[0] - scrollY;
    float bottom = g.tableRowTops[g.tableRowTops.len - 1] - scrollY;

    // 表头背景:先画背景,横竖网格线再叠加在上面。
    if (g.tableHeadRowCount > 0 && tableHeaderBrush) {
        u32 headEnd = g.tableHeadRowCount < g.tableRowTops.len ? g.tableHeadRowCount : g.tableRowTops.len - 1;
        float headBottom = g.tableRowTops[headEnd] - scrollY;
        target_->FillRectangle(D2D1::RectF(left, top, right, headBottom), tableHeaderBrush);
    }

    if (!tableGridBrush) return;

    // 横线:每一行边界(含表格顶/底边)。
    for (u32 r = 0; r < g.tableRowTops.len; ++r) {
        float y = g.tableRowTops[r] - scrollY;
        target_->DrawLine(D2D1::Point2F(left, y), D2D1::Point2F(right, y), tableGridBrush, 1.0f);
    }

    // 竖线:每一列边界(含左右边框)。
    float x = left;
    target_->DrawLine(D2D1::Point2F(x, top), D2D1::Point2F(x, bottom), tableGridBrush, 1.0f);
    for (u32 c = 0; c < g.tableColWidths.len; ++c) {
        x += g.tableColWidths[c];
        target_->DrawLine(D2D1::Point2F(x, top), D2D1::Point2F(x, bottom), tableGridBrush, 1.0f);
    }
}

void Renderer::DrawLinkOverlays(const BlockGeometry& g, float scrollY, ID2D1SolidColorBrush* linkBrush) {
    if (!g.textLayout || g.linkBoxes.len == 0 || !linkBrush) return;

    constexpr UINT32 kMaxHitTestMetrics = 8;  // 链接 run 通常只跨 1~2 行,8 条足够
    for (u32 i = 0; i < g.linkBoxes.len; ++i) {
        const LinkBox& lb = g.linkBoxes[i];
        DWRITE_HIT_TEST_METRICS metrics[kMaxHitTestMetrics];
        UINT32 actualCount = 0;
        float drawLeft = TextDrawLeft(g);
        float drawTop = TextDrawTop(g) - scrollY;
        HRESULT hr = g.textLayout->HitTestTextRange(
            lb.textPosition, lb.textLength, drawLeft, drawTop,
            metrics, kMaxHitTestMetrics, &actualCount);
        if (FAILED(hr)) continue;

        UINT32 count = actualCount < kMaxHitTestMetrics ? actualCount : kMaxHitTestMetrics;
        for (UINT32 m = 0; m < count; ++m) {
            D2D1_RECT_F clipRect = D2D1::RectF(
                metrics[m].left, metrics[m].top,
                metrics[m].left + metrics[m].width, metrics[m].top + metrics[m].height);
            // 裁剪到该 link range 的矩形后整体重画一次 layout——裁剪区之外的部分
            // 不可见,效果上等价于"只给这段文字换色",但不需要 SetDrawingEffect
            // 与自定义 TextRenderer(T24 明确避免的成本)。
            target_->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            target_->DrawTextLayout(D2D1::Point2F(drawLeft, drawTop), g.textLayout, linkBrush);
            target_->PopAxisAlignedClip();
        }
    }
}

void Renderer::DrawFindHighlights(const BlockGeometry& g, u32 blockIndex, float scrollY,
                                   ID2D1SolidColorBrush* fillBrush,
                                   ID2D1SolidColorBrush* currentFillBrush) {
    if (!overlay_ || !overlay_->doc || !overlay_->matches || overlay_->matchCount == 0) return;
    if (!g.textLayout || !fillBrush) return;

    for (u32 i = 0; i < overlay_->matchCount; ++i) {
        const Match& m = overlay_->matches[i];
        if (m.blockIdx < blockIndex) continue;
        if (m.blockIdx > blockIndex) break;  // 命中集合按块下标升序,越过本块即可收工

        u32 position = 0;
        u32 length = 0;
        // 命中在链接 URL / 图片 alt 上时没有可画的正文矩形,跳过(仍可跳转)。
        if (!MatchToTextRange(*overlay_->doc, m, &position, &length)) continue;

        DWRITE_HIT_TEST_METRICS metrics[kMaxFindHitMetrics];
        UINT32 actualCount = 0;
        HRESULT hr = g.textLayout->HitTestTextRange(position, length, TextDrawLeft(g), TextDrawTop(g) - scrollY,
                                                     metrics, kMaxFindHitMetrics, &actualCount);
        if (FAILED(hr)) continue;

        ID2D1SolidColorBrush* brush =
            (i == overlay_->currentMatch && currentFillBrush) ? currentFillBrush : fillBrush;
        UINT32 count = actualCount < kMaxFindHitMetrics ? actualCount : kMaxFindHitMetrics;
        for (UINT32 k = 0; k < count; ++k) {
            target_->FillRectangle(
                D2D1::RectF(metrics[k].left, metrics[k].top,
                            metrics[k].left + metrics[k].width,
                            metrics[k].top + metrics[k].height),
                brush);
        }
    }
}

void Renderer::DrawOverlayBar(float targetWidth, const wchar_t* text, u32 textLen,
                               ID2D1SolidColorBrush* bgBrush, ID2D1SolidColorBrush* textBrush,
                               float topOffset) {
    if (!fonts_ || !bgBrush || !textBrush || !text || textLen == 0) return;

    IDWriteTextLayout* layout =
        fonts_->CreateTextLayout(text, textLen, FontRole::Body, kOverlayBarMaxWidthDip, 64.0f);
    if (!layout) return;
    layout->SetFontSize(kOverlayBarFontSizeDip, DWRITE_TEXT_RANGE{0, textLen});

    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) {
        layout->Release();
        return;
    }
    float barW = metrics.widthIncludingTrailingWhitespace + kOverlayBarPaddingXDip * 2.0f;
    float barH = metrics.height + kOverlayBarPaddingYDip * 2.0f;

    // 浮在客户区右上角(不随滚动移动),窗口很窄时退化为左对齐,保证不跑出可视区。
    float right = targetWidth - kOverlayBarMarginDip;
    float left = right - barW;
    if (left < kOverlayBarMarginDip) left = kOverlayBarMarginDip;
    float top = kOverlayBarMarginDip + topOffset;

    D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(D2D1::RectF(left, top, right, top + barH),
                                                   kOverlayBarCornerRadiusDip,
                                                   kOverlayBarCornerRadiusDip);
    target_->FillRoundedRectangle(rounded, bgBrush);
    target_->DrawTextLayout(D2D1::Point2F(left + kOverlayBarPaddingXDip,
                                           top + kOverlayBarPaddingYDip),
                             layout, textBrush);
    layout->Release();
}

void Renderer::DrawTaskCheckbox(const BlockGeometry& g, float scrollY,
                                 ID2D1SolidColorBrush* borderBrush,
                                 ID2D1SolidColorBrush* checkBrush) {
    if (g.taskCheckbox.width <= 0.0f) return;

    D2D1_RECT_F rect = D2D1::RectF(
        g.taskCheckbox.x, g.taskCheckbox.y - scrollY,
        g.taskCheckbox.x + g.taskCheckbox.width,
        g.taskCheckbox.y - scrollY + g.taskCheckbox.height);
    D2D1_ROUNDED_RECT roundedRect =
        D2D1::RoundedRect(rect, kCheckboxCornerRadiusDip, kCheckboxCornerRadiusDip);

    if (borderBrush) target_->DrawRoundedRectangle(roundedRect, borderBrush, 1.5f);

    if (g.taskChecked && checkBrush) {
        float w = rect.right - rect.left;
        float h = rect.bottom - rect.top;
        // 对勾:两段折线(短的左下笔画 + 长的右上笔画),D2D 几何图元画,零字体零位图。
        D2D1_POINT_2F p1 = D2D1::Point2F(rect.left + w * 0.2f, rect.top + h * 0.55f);
        D2D1_POINT_2F p2 = D2D1::Point2F(rect.left + w * 0.42f, rect.top + h * 0.75f);
        D2D1_POINT_2F p3 = D2D1::Point2F(rect.left + w * 0.8f, rect.top + h * 0.25f);
        target_->DrawLine(p1, p2, checkBrush, 1.8f);
        target_->DrawLine(p2, p3, checkBrush, 1.8f);
    }
}

void Renderer::DrawCodeCopyButton(const BlockGeometry& g, u32 blockIndex, float scrollY,
                                   ID2D1SolidColorBrush* iconBrush,
                                   ID2D1SolidColorBrush* hoverBgBrush,
                                   ID2D1SolidColorBrush* paperBrush,
                                   ID2D1SolidColorBrush* doneBrush) {
    if (g.codeCopyButton.width <= 0.0f) return;

    bool hovered = overlay_ && overlay_->copyButtonHoverBlock == blockIndex;
    bool copied = overlay_ && overlay_->copyButtonCopiedBlock == blockIndex;

    float left = g.codeCopyButton.x;
    float top = g.codeCopyButton.y - scrollY;
    float size = g.codeCopyButton.width;
    D2D1_RECT_F box = D2D1::RectF(left, top, left + size, top + g.codeCopyButton.height);
    float buttonRadius = size * kCopyButtonCornerRadiusRatio;
    D2D1_ROUNDED_RECT roundedBox = D2D1::RoundedRect(box, buttonRadius, buttonRadius);
    float stroke = size * kCopyStrokeWidthRatio;

    if (copied) {
        // 已复制:绿色边框 + 绿色对勾,与默认/悬浮两态一眼可分。
        if (!doneBrush) return;
        target_->DrawRoundedRectangle(roundedBox, doneBrush, stroke);
        D2D1_POINT_2F p1 = D2D1::Point2F(left + size * 0.24f, top + size * 0.52f);
        D2D1_POINT_2F p2 = D2D1::Point2F(left + size * 0.44f, top + size * 0.72f);
        D2D1_POINT_2F p3 = D2D1::Point2F(left + size * 0.78f, top + size * 0.28f);
        float checkStroke = size * kCopyCheckStrokeWidthRatio;
        target_->DrawLine(p1, p2, doneBrush, checkStroke);
        target_->DrawLine(p2, p3, doneBrush, checkStroke);
        return;
    }

    // 悬浮:先铺一层浅灰圆角底,盖住代码块背景,底色本身就是悬浮反馈。
    if (hovered && hoverBgBrush) target_->FillRoundedRectangle(roundedBox, hoverBgBrush);

    if (!iconBrush) return;

    // "复制"图标 = 两张叠压的圆角纸:后面那张只露出左上一角,前面那张先用
    // 纸面色填实再描边,叠压关系因此清晰可辨(纯几何,不依赖任何字体字形)。
    float sheetRadius = size * kCopySheetCornerRadiusRatio;
    D2D1_ROUNDED_RECT backSheet = D2D1::RoundedRect(
        D2D1::RectF(left + size * kCopyBackSheetLeftRatio, top + size * kCopyBackSheetTopRatio,
                    left + size * kCopyBackSheetRightRatio,
                    top + size * kCopyBackSheetBottomRatio),
        sheetRadius, sheetRadius);
    D2D1_ROUNDED_RECT frontSheet = D2D1::RoundedRect(
        D2D1::RectF(left + size * kCopyFrontSheetLeftRatio, top + size * kCopyFrontSheetTopRatio,
                    left + size * kCopyFrontSheetRightRatio,
                    top + size * kCopyFrontSheetBottomRatio),
        sheetRadius, sheetRadius);

    target_->DrawRoundedRectangle(backSheet, iconBrush, stroke);
    if (paperBrush) target_->FillRoundedRectangle(frontSheet, paperBrush);
    target_->DrawRoundedRectangle(frontSheet, iconBrush, stroke);
}

void Renderer::DrawListMarker(const BlockGeometry& g, float scrollY, ID2D1SolidColorBrush* markerBrush) {
    if (g.listMarker.width <= 0.0f || !markerBrush) return;

    D2D1_RECT_F rect = D2D1::RectF(
        g.listMarker.x, g.listMarker.y - scrollY,
        g.listMarker.x + g.listMarker.width,
        g.listMarker.y - scrollY + g.listMarker.height);

    if (!g.listMarkerOrdered) {
        // 无序列表:三档循环——第 1 层实心圆点,第 2 层空心圆,第 3 层(及再循环)
        // 实心方块,纯 D2D 几何图元,不依赖任何字体字形(与 DrawTaskCheckbox 同一
        // 风格)。listMarkerLevel 是 1-based,减 1 再取模才对齐"第 1 层"的语义。
        u32 cyc = (g.listMarkerLevel >= 1 ? g.listMarkerLevel - 1 : 0) % 3;
        if (cyc == 2) {
            target_->FillRectangle(rect, markerBrush);
        } else {
            D2D1_ELLIPSE ellipse = D2D1::Ellipse(
                D2D1::Point2F((rect.left + rect.right) * 0.5f, (rect.top + rect.bottom) * 0.5f),
                (rect.right - rect.left) * 0.5f, (rect.bottom - rect.top) * 0.5f);
            if (cyc == 0) {
                target_->FillEllipse(ellipse, markerBrush);
            } else {
                target_->DrawEllipse(ellipse, markerBrush, 1.3f);
            }
        }
        return;
    }

    // 有序列表:数字 + 分隔符(如 "1." "2)"),复用现有 IDWriteTextLayout 管线,
    // 画法与 DrawFootnoteLabel 一致——临时创建、画完立即释放,不进虚拟化/不缓存。
    if (!fonts_) return;
    wchar_t label[16];
    wchar_t digits[10];
    u32 value = g.listMarkerOrdinal;
    u32 dn = 0;
    if (value == 0) {
        digits[dn++] = L'0';
    } else {
        while (value > 0 && dn < 10) {
            digits[dn++] = static_cast<wchar_t>(L'0' + (value % 10));
            value /= 10;
        }
    }
    u32 li = 0;
    for (u32 i = 0; i < dn; ++i) label[li++] = digits[dn - 1 - i];
    label[li++] = g.listMarkerDelim != 0 ? static_cast<wchar_t>(g.listMarkerDelim) : L'.';
    label[li] = 0;

    IDWriteTextLayout* labelLayout =
        fonts_->CreateTextLayout(label, li, FontRole::Body, rect.right - rect.left, rect.bottom - rect.top);
    if (!labelLayout) return;
    target_->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), labelLayout, markerBrush);
    labelLayout->Release();
}

void Renderer::DrawFootnoteLabel(const BlockGeometry& g, float scrollY, ID2D1SolidColorBrush* textBrush) {
    if (g.footnoteId == 0 || !fonts_ || !textBrush) return;

    wchar_t label[16];
    // 手写十进制格式化,避免为这几个字符引入 swprintf 的格式串解析开销。
    u32 value = g.footnoteId;
    wchar_t digits[10];
    u32 dn = 0;
    if (value == 0) {
        digits[dn++] = L'0';
    } else {
        while (value > 0 && dn < 10) {
            digits[dn++] = static_cast<wchar_t>(L'0' + (value % 10));
            value /= 10;
        }
    }
    u32 li = 0;
    label[li++] = L'[';
    for (u32 i = 0; i < dn; ++i) label[li++] = digits[dn - 1 - i];
    label[li++] = L']';
    label[li] = 0;

    IDWriteTextLayout* labelLayout =
        fonts_->CreateTextLayout(label, li, FontRole::Body, 64.0f, 24.0f);
    if (!labelLayout) return;
    target_->DrawTextLayout(D2D1::Point2F(g.indent, g.top - scrollY), labelLayout, textBrush);
    labelLayout->Release();
}

void Renderer::DrawImagePlaceholder(const D2D1_RECT_F& rect, const wchar_t* text, u32 textLen,
                                     ID2D1SolidColorBrush* bgBrush,
                                     ID2D1SolidColorBrush* borderBrush,
                                     ID2D1SolidColorBrush* textBrush) {
    D2D1_ROUNDED_RECT rounded =
        D2D1::RoundedRect(rect, kImagePlaceholderCornerRadiusDip, kImagePlaceholderCornerRadiusDip);
    if (bgBrush) target_->FillRoundedRectangle(rounded, bgBrush);
    if (borderBrush) target_->DrawRoundedRectangle(rounded, borderBrush, 1.0f);

    float boxHeight = rect.bottom - rect.top;
    float boxWidth = rect.right - rect.left;

    // 中央"图片"图标:矩形太小(比如行内小图标)时跳过,只留文案,避免图标挤爆占位块。
    float iconBottom = rect.top;
    if (boxHeight >= kPlaceholderIconMinBoxDip && borderBrush) {
        float iconSize = (boxWidth < boxHeight ? boxWidth : boxHeight) * kPlaceholderIconSizeRatio;
        if (iconSize < kPlaceholderIconMinSizeDip) iconSize = kPlaceholderIconMinSizeDip;
        if (iconSize > kPlaceholderIconMaxSizeDip) iconSize = kPlaceholderIconMaxSizeDip;

        float centerX = (rect.left + rect.right) * 0.5f;
        float centerY = rect.top + boxHeight * kPlaceholderIconCenterYRatio;
        float iconLeft = centerX - iconSize * 0.5f;
        float iconTop = centerY - iconSize * 0.5f;
        iconBottom = iconTop + iconSize;

        ID2D1SolidColorBrush* iconBrush = nullptr;
        target_->CreateSolidColorBrush(palette_->imagePlaceholderIcon, &iconBrush);
        if (iconBrush) {
            // 相框:圆角矩形描边。
            D2D1_RECT_F iconRect = D2D1::RectF(iconLeft, iconTop, iconLeft + iconSize, iconBottom);
            D2D1_ROUNDED_RECT iconRounded = D2D1::RoundedRect(
                iconRect, kPlaceholderIconCornerRadiusDip, kPlaceholderIconCornerRadiusDip);
            target_->DrawRoundedRectangle(iconRounded, iconBrush, 1.5f);

            // 太阳:左上角实心圆。
            D2D1_ELLIPSE sun = D2D1::Ellipse(
                D2D1::Point2F(iconLeft + iconSize * 0.28f, iconTop + iconSize * 0.28f),
                iconSize * 0.11f, iconSize * 0.11f);
            target_->FillEllipse(sun, iconBrush);

            // 山峰:两段折线,贴着相框底边。
            D2D1_POINT_2F left = D2D1::Point2F(iconLeft + iconSize * 0.16f, iconTop + iconSize * 0.78f);
            D2D1_POINT_2F peak = D2D1::Point2F(iconLeft + iconSize * 0.52f, iconTop + iconSize * 0.40f);
            D2D1_POINT_2F right = D2D1::Point2F(iconLeft + iconSize * 0.86f, iconTop + iconSize * 0.78f);
            target_->DrawLine(left, peak, iconBrush, 1.5f);
            target_->DrawLine(peak, right, iconBrush, 1.5f);

            iconBrush->Release();
        }
    }

    if (!fonts_ || !textBrush || !text || textLen == 0) return;

    // 文案画在图标下方(没画图标时就是整块居中);水平居中,垂直靠上对齐。
    float textWidth = rect.right - rect.left - kImagePlaceholderTextPaddingDip * 2.0f;
    if (textWidth < 1.0f) return;
    float textTop = iconBottom > rect.top ? iconBottom + kPlaceholderIconTextGapDip : rect.top;
    float textBoxHeight = rect.bottom - textTop;
    IDWriteTextLayout* layout =
        fonts_->CreateTextLayout(text, textLen, FontRole::Body, textWidth,
                                 textBoxHeight > 1.0f ? textBoxHeight : kPlaceholderTextMaxHeightDip);
    if (!layout) return;
    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    layout->SetParagraphAlignment(iconBottom > rect.top ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                                                         : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    target_->DrawTextLayout(
        D2D1::Point2F(rect.left + kImagePlaceholderTextPaddingDip, textTop), layout, textBrush);
    layout->Release();
}

void Renderer::DrawDownsampledBadge(const D2D1_RECT_F& imageRect,
                                     ID2D1SolidColorBrush* badgeBgBrush,
                                     ID2D1SolidColorBrush* badgeTextBrush) {
    if (!fonts_ || !badgeBgBrush || !badgeTextBrush) return;

    const wchar_t* text = DownsampledBadgeText();
    u32 len = WideLength(text);
    IDWriteTextLayout* layout =
        fonts_->CreateTextLayout(text, len, FontRole::Body, 400.0f, 64.0f);
    if (!layout) return;
    // 标签用小号字(与 T28 脚注小字号同一量级),字号设完再取实际排版尺寸,
    // 以此反推圆角背景矩形的大小,不写死文字宽度。
    layout->SetFontSize(kBadgeFontSizeDip, DWRITE_TEXT_RANGE{0, len});

    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) {
        layout->Release();
        return;
    }
    float badgeW = metrics.widthIncludingTrailingWhitespace + kBadgePaddingXDip * 2.0f;
    float badgeH = metrics.height + kBadgePaddingYDip * 2.0f;

    // 贴图片右下角内侧;图片比标签还小时退化为左对齐,保证不跑到图片外面。
    float right = imageRect.right - kBadgeMarginDip;
    float left = right - badgeW;
    if (left < imageRect.left) left = imageRect.left;
    float bottom = imageRect.bottom - kBadgeMarginDip;
    float top = bottom - badgeH;
    if (top < imageRect.top) top = imageRect.top;

    D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(D2D1::RectF(left, top, right, bottom),
                                                   kBadgeCornerRadiusDip, kBadgeCornerRadiusDip);
    target_->FillRoundedRectangle(rounded, badgeBgBrush);  // 半透明底,不遮死图片内容
    target_->DrawTextLayout(D2D1::Point2F(left + kBadgePaddingXDip, top + kBadgePaddingYDip),
                            layout, badgeTextBrush);
    layout->Release();
}

void Renderer::DrawImages(const BlockGeometry& g, float scrollY,
                           ID2D1SolidColorBrush* textBrush,
                           ID2D1SolidColorBrush* placeholderBgBrush,
                           ID2D1SolidColorBrush* placeholderBorderBrush,
                           ID2D1SolidColorBrush* badgeBgBrush,
                           ID2D1SolidColorBrush* badgeTextBrush) {
    for (u32 i = 0; i < g.imageBoxes.len; ++i) {
        const ImageBox& box = g.imageBoxes[i];
        D2D1_RECT_F rect = D2D1::RectF(box.rect.x, box.rect.y - scrollY,
                                        box.rect.x + box.rect.width,
                                        box.rect.y - scrollY + box.rect.height);

        const ImageCacheEntry* entry = images_ ? images_->Find(box.href) : nullptr;
        if (entry && entry->bitmap && entry->status == ImageStatus::Ok) {
            // 有位图:按布局算好的矩形拉伸绘制(矩形本身已按真实尺寸等比算过)。
            target_->DrawBitmap(entry->bitmap, rect, 1.0f,
                                 D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            // 降采样提示标签画在位图之上(顺序:位图 -> 标签),未降采样不画。
            if (entry->wasDownsampled) {
                DrawDownsampledBadge(rect, badgeBgBrush, badgeTextBrush);
            }
            continue;
        }

        // 无位图:五种状态共用同一个占位块绘制函数,只有文案不同。
        ImageStatus status = entry ? entry->status : box.status;
        if (status == ImageStatus::Ok) status = ImageStatus::NotLoaded;
        const wchar_t* label = ImagePlaceholderText(status);

        // 居中显示 "alt 换行 状态文案";没有 alt 时只显示状态文案。
        // 用一个栈上小缓冲拼接,不做任何堆分配(占位文案长度天然很短)。
        wchar_t composed[256];
        u32 len = 0;
        if (box.alt.data && box.alt.len > 0) {
            int altWide = MultiByteToWideChar(CP_UTF8, 0, box.alt.data,
                                               static_cast<int>(box.alt.len), composed, 200);
            if (altWide > 0) {
                len = static_cast<u32>(altWide);
                composed[len++] = L'\n';
            }
        }
        for (u32 c = 0; label[c] != 0 && len < 255; ++c) composed[len++] = label[c];
        composed[len] = 0;

        DrawImagePlaceholder(rect, composed, len, placeholderBgBrush,
                             placeholderBorderBrush, textBrush);
    }
}

void Renderer::DrawBlock(const BlockGeometry& g, u32 blockIndex, float scrollY, float targetWidth,
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
                          ID2D1SolidColorBrush* copyDoneBrush) {
    // 围栏代码块背景:先画背景,再画文本,避免文本被背景矩形盖住。
    // 4 DIP 圆角,与常见 Markdown 渲染器的代码块风格保持一致。
    if (g.type == BlockType::CodeBlock && codeBgBrush) {
        D2D1_RECT_F rect = D2D1::RectF(
            g.codeBackground.x, g.codeBackground.y - scrollY,
            g.codeBackground.x + g.codeBackground.width,
            g.codeBackground.y - scrollY + g.codeBackground.height);
        D2D1_ROUNDED_RECT roundedRect =
            D2D1::RoundedRect(rect, kCodeBlockCornerRadiusDip, kCodeBlockCornerRadiusDip);
        target_->FillRoundedRectangle(roundedRect, codeBgBrush);
    }

    // 引用块左侧竖线:D2D 几何图元填色矩形,不用任何字体字符模拟(架构 §4 第 5 条)。
    if (g.type == BlockType::BlockQuote && quoteBrush) {
        D2D1_RECT_F rect = D2D1::RectF(
            g.quoteBar.x, g.quoteBar.y - scrollY,
            g.quoteBar.x + g.quoteBar.width,
            g.quoteBar.y - scrollY + g.quoteBar.height);
        target_->FillRectangle(rect, quoteBrush);
    }

    // 分割线:同样用几何图元(DrawLine)画一条横线,不用字符模拟。
    if (g.type == BlockType::ThematicBreak && hrBrush) {
        float midY = (g.top + g.bottom) * 0.5f - scrollY;
        float rightX = targetWidth - kThematicBreakRightMarginDip;
        if (rightX < g.indent) rightX = g.indent;
        target_->DrawLine(D2D1::Point2F(g.indent, midY),
                            D2D1::Point2F(rightX, midY), hrBrush, 1.5f);
    }

    // 脚注定义区块(T28):顶部一条分隔线,与正文区分开。
    if (g.type == BlockType::FootnoteDefSection && hrBrush) {
        float y = g.top - scrollY;
        float rightX = targetWidth - kThematicBreakRightMarginDip;
        if (rightX < g.indent) rightX = g.indent;
        target_->DrawLine(D2D1::Point2F(g.indent, y), D2D1::Point2F(rightX, y), hrBrush, 1.0f);
    }

    // 表格网格线/表头背景(T26):只在 Table 容器块自身画一次,覆盖整张表。
    if (g.type == BlockType::Table) {
        DrawTableChrome(g, scrollY, tableHeaderBrush, tableGridBrush);
    }

    // 脚注定义编号标签(T28):每条脚注定义前缀 "[n]"。
    if (g.type == BlockType::FootnoteDef) {
        DrawFootnoteLabel(g, scrollY, textBrush);
    }

    // 任务列表勾选框(T27)。
    DrawTaskCheckbox(g, scrollY, checkboxBorderBrush, checkboxCheckBrush);

    // 列表符号(T44):无序圆点/空心圆/方块,有序数字序号;复用正文颜色。
    // 与上面的任务列表勾选框互斥(见 layout.cpp 里 isTaskItem 分支不写 listMarker),
    // 不会同一个 ListItem 上重复画两种前缀。
    DrawListMarker(g, scrollY, textBrush);

    // 正文/标题/代码块/表格单元格文本:虚拟化范围内才非空,直接用 DrawTextLayout。
    if (g.textLayout && textBrush) {
        // 查找命中高亮(T38)画在文本**之前**,于是底色在文字下方,文字颜色不受影响。
        DrawFindHighlights(g, blockIndex, scrollY, findHighlightBrush, findCurrentBrush);
        target_->DrawTextLayout(D2D1::Point2F(TextDrawLeft(g), TextDrawTop(g) - scrollY),
                                  g.textLayout, textBrush);
        // 链接着色(T24):在正文之上叠加一次裁剪重绘,见 DrawLinkOverlays 注释。
        DrawLinkOverlays(g, scrollY, linkBrush);
    }

    // 图片/占位块(T33):画在本块文本之后,矩形位置由布局阶段算好。
    if (g.imageBoxes.len > 0) {
        DrawImages(g, scrollY, textBrush, placeholderBgBrush, placeholderBorderBrush,
                   badgeBgBrush, badgeTextBrush);
    }

    // 代码块复制按钮(T45):最后画,浮在代码块背景与代码文字之上——命中测试
    // 里它同样最优先(见 hit_test.cpp),视觉层级与交互层级保持一致。
    if (g.type == BlockType::CodeBlock) {
        DrawCodeCopyButton(g, blockIndex, scrollY, copyIconBrush, copyHoverBgBrush,
                            copyPaperBrush, copyDoneBrush);
    }
}

bool Renderer::RenderFrame(HWND hwnd, const BlockLayoutEngine& layout, float scrollY,
                            float leftPaddingDip, const ShellOverlay* overlay) {
    if (!EnsureRenderTarget(hwnd)) return false;

    // 叠加层视图只在本帧内有效,画完立刻置空,避免留下悬空引用。
    overlay_ = overlay;

    ID2D1SolidColorBrush* textBrush = nullptr;
    ID2D1SolidColorBrush* quoteBrush = nullptr;
    ID2D1SolidColorBrush* codeBgBrush = nullptr;
    ID2D1SolidColorBrush* hrBrush = nullptr;
    ID2D1SolidColorBrush* linkBrush = nullptr;
    ID2D1SolidColorBrush* tableHeaderBrush = nullptr;
    ID2D1SolidColorBrush* tableGridBrush = nullptr;
    ID2D1SolidColorBrush* checkboxBorderBrush = nullptr;
    ID2D1SolidColorBrush* checkboxCheckBrush = nullptr;
    ID2D1SolidColorBrush* placeholderBgBrush = nullptr;
    ID2D1SolidColorBrush* placeholderBorderBrush = nullptr;
    ID2D1SolidColorBrush* badgeBgBrush = nullptr;
    ID2D1SolidColorBrush* badgeTextBrush = nullptr;
    ID2D1SolidColorBrush* findHighlightBrush = nullptr;
    ID2D1SolidColorBrush* findCurrentBrush = nullptr;
    ID2D1SolidColorBrush* overlayBarBgBrush = nullptr;
    ID2D1SolidColorBrush* overlayBarTextBrush = nullptr;
    ID2D1SolidColorBrush* copyIconBrush = nullptr;
    ID2D1SolidColorBrush* copyHoverBgBrush = nullptr;
    ID2D1SolidColorBrush* copyPaperBrush = nullptr;
    ID2D1SolidColorBrush* copyDoneBrush = nullptr;
    target_->CreateSolidColorBrush(palette_->text, &textBrush);
    target_->CreateSolidColorBrush(palette_->quoteBar, &quoteBrush);
    target_->CreateSolidColorBrush(palette_->codeBackground, &codeBgBrush);
    target_->CreateSolidColorBrush(palette_->thematicBreak, &hrBrush);
    target_->CreateSolidColorBrush(palette_->link, &linkBrush);
    target_->CreateSolidColorBrush(palette_->tableHeaderBackground, &tableHeaderBrush);
    target_->CreateSolidColorBrush(palette_->tableGrid, &tableGridBrush);
    target_->CreateSolidColorBrush(palette_->checkboxBorder, &checkboxBorderBrush);
    target_->CreateSolidColorBrush(palette_->checkboxCheck, &checkboxCheckBrush);
    target_->CreateSolidColorBrush(palette_->imagePlaceholderBackground, &placeholderBgBrush);
    target_->CreateSolidColorBrush(palette_->imagePlaceholderBorder, &placeholderBorderBrush);
    target_->CreateSolidColorBrush(palette_->downsampledBadgeBackground, &badgeBgBrush);
    target_->CreateSolidColorBrush(palette_->downsampledBadgeText, &badgeTextBrush);
    target_->CreateSolidColorBrush(palette_->findHighlight, &findHighlightBrush);
    target_->CreateSolidColorBrush(palette_->findCurrentHighlight, &findCurrentBrush);
    target_->CreateSolidColorBrush(palette_->overlayBarBackground, &overlayBarBgBrush);
    target_->CreateSolidColorBrush(palette_->overlayBarText, &overlayBarTextBrush);
    target_->CreateSolidColorBrush(palette_->codeCopyIcon, &copyIconBrush);
    target_->CreateSolidColorBrush(palette_->codeCopyHoverBackground, &copyHoverBgBrush);
    target_->CreateSolidColorBrush(palette_->codeCopyPaper, &copyPaperBrush);
    target_->CreateSolidColorBrush(palette_->codeCopyDone, &copyDoneBrush);

    D2D1_SIZE_F targetSize = target_->GetSize();
    // 正文可用宽度:客户区宽度收窄掉左右内边距(各 leftPaddingDip)——下面画
    // 分割线/脚注分隔线的右边界要按这个来算,否则会画到内边距的空白区域里去。
    float contentWidth = targetSize.width - 2.0f * leftPaddingDip;
    if (contentWidth < 0.0f) contentWidth = 0.0f;

    target_->BeginDraw();
    target_->Clear(palette_->background);

    // 左内边距:水平方向整体平移 leftPaddingDip,每个 DrawXxx 已经在用
    // g.indent 当 x 坐标画东西,不用逐个改。垂直方向的内边距由调用方传入的
    // scrollY(已经减去过内边距)实现,这里不用再叠一次。仅对"文档内容"
    // 生效——叠加层(查找条/提示条)画之前会恢复成 Identity,不跟着这个平移走。
    target_->SetTransform(D2D1::Matrix3x2F::Translation(leftPaddingDip, 0.0f));

    u32 blockCount = layout.BlockCount();
    for (u32 i = 0; i < blockCount; ++i) {
        DrawBlock(layout.Geometry(i), i, scrollY, contentWidth,
                  textBrush, quoteBrush, codeBgBrush, hrBrush, linkBrush,
                  tableHeaderBrush, tableGridBrush, checkboxBorderBrush, checkboxCheckBrush,
                  placeholderBgBrush, placeholderBorderBrush, badgeBgBrush, badgeTextBrush,
                  findHighlightBrush, findCurrentBrush,
                  copyIconBrush, copyHoverBgBrush, copyPaperBrush, copyDoneBrush);
    }

    // 叠加层(查找条/窗口内提示)不随内容平移——先恢复 Identity 变换。
    target_->SetTransform(D2D1::Matrix3x2F::Identity());

    // 叠加层最后画,浮在正文之上:查找条(T37)与窗口内提示(T36)。
    if (overlay_) {
        float topOffset = 0.0f;
        if (overlay_->findBarVisible) {
            wchar_t bar[kMaxOverlayBarChars];
            u32 len = ComposeFindBarText(overlay_, bar, kMaxOverlayBarChars);
            DrawOverlayBar(targetSize.width, bar, len, overlayBarBgBrush, overlayBarTextBrush, 0.0f);
            topOffset = kOverlayBarStackStepDip;
        }
        if (overlay_->statusMessage && overlay_->statusMessage[0] != 0) {
            DrawOverlayBar(targetSize.width, overlay_->statusMessage,
                           WideLength(overlay_->statusMessage), overlayBarBgBrush,
                           overlayBarTextBrush, topOffset);
        }
    }

    if (textBrush) textBrush->Release();
    if (quoteBrush) quoteBrush->Release();
    if (codeBgBrush) codeBgBrush->Release();
    if (hrBrush) hrBrush->Release();
    if (linkBrush) linkBrush->Release();
    if (tableHeaderBrush) tableHeaderBrush->Release();
    if (tableGridBrush) tableGridBrush->Release();
    if (checkboxBorderBrush) checkboxBorderBrush->Release();
    if (checkboxCheckBrush) checkboxCheckBrush->Release();
    if (placeholderBgBrush) placeholderBgBrush->Release();
    if (placeholderBorderBrush) placeholderBorderBrush->Release();
    if (badgeBgBrush) badgeBgBrush->Release();
    if (badgeTextBrush) badgeTextBrush->Release();
    if (findHighlightBrush) findHighlightBrush->Release();
    if (findCurrentBrush) findCurrentBrush->Release();
    if (overlayBarBgBrush) overlayBarBgBrush->Release();
    if (overlayBarTextBrush) overlayBarTextBrush->Release();
    if (copyIconBrush) copyIconBrush->Release();
    if (copyHoverBgBrush) copyHoverBgBrush->Release();
    if (copyPaperBrush) copyPaperBrush->Release();
    if (copyDoneBrush) copyDoneBrush->Release();

    overlay_ = nullptr;  // 本帧结束,不再持有外壳层传进来的视图

    HRESULT hr = target_->EndDraw();
    if (ShouldRecreateRenderTarget(hr)) {
        // 设备丢失:释放旧渲染目标,下次 RenderFrame 调用会惰性重建(架构 §9)。
        // 传入的 layout 不受影响 —— IDWriteTextLayout 不绑定具体渲染目标实例。
        ReleaseRenderTarget();
    }
    return SUCCEEDED(hr);
}

}  // namespace mdvn
