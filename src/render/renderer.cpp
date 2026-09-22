#include "renderer.h"

#include <cstring>
#include <cwchar>
#include <cstdio>

#include "../assets/data_uri.h"
#include "../assets/svg_decoder.h"
#include "../doc/file_map.h"
#include "../hl/lexer.h"
#include "../shell/button.h"
#include "../shell/folder_scan.h"
#include "../shell/sidebar.h"
#include "../util/str.h"

namespace markair {

namespace {

// 分割线右侧留白(DIP),与 layout.cpp 里代码块背景的右侧留白取相同量级,
// 视觉上让分割线/代码块背景右边缘对齐。
constexpr float kThematicBreakRightMarginDip = 16.0f;

// 背景色:主题背景(浅色),与窗口类的 GDI 背景色保持一致,避免首帧白闪
// (架构 §6,已在窗口类里落地;这里只是让 D2D 清屏色跟 GDI 背景一致)。
// TextDrawLeft/TextDrawTop 现在是 layout.h 里的共享函数,渲染(这里)与
// 命中测试(shell/hit_test.cpp)必须用同一份实现,不能各自维护一份——
// 之前两边各写一份、hit_test.cpp 漏加 textPad 导致点击位置与实际文字错位
// 一份内边距,是真实 bug,详见 layout.h 那两个函数的头注释。

// 代码高亮区圆角半径(DIP),对齐 GitHub Primer 代码块的 6px 圆角。
constexpr float kCodeBlockCornerRadiusDip = 6.0f;

// 代码块边框线宽(DIP),对齐 GitHub 代码块的 1px 描边(颜色见 Palette::codeBorder)。
constexpr float kCodeBlockBorderWidthDip = 1.0f;

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
constexpr float kOverlayBarMaxWidthDip = 520.0f; // 状态提示条最大宽度,超长文字自行截断

// 查找条几何(2026-09-19 改版)。数值必须与 shell/find_bar.h 的同名常量保持
// 一致——render 不反向 include shell 头文件,这里只重复几个纯数字,真正的
// "唯一权威定义"(含 Edit 控件矩形换算)在 shell 侧,window.cpp 按那份常量
// 摆放原生 Edit 子窗口。
constexpr float kFindBarPaddingXDip = 10.0f;
constexpr float kFindBarPrefixWidthDip = 44.0f;
constexpr float kFindBarEditWidthDip = 150.0f;
constexpr float kFindBarGapDip = 8.0f;
constexpr float kFindBarStatusNavGapDip = 4.0f;   // 须与 shell/find_bar.h 的同名常量一致
constexpr float kFindBarStatusWidthDip = 50.0f;
constexpr float kFindBarNavButtonWidthDip = 22.0f;
constexpr float kFindBarCloseButtonWidthDip = 22.0f;  // 须与 shell/find_bar.h 的同名常量一致
constexpr float kFindBarHeightDip = 26.0f;
constexpr float kFindBarMarginDip = 10.0f;
constexpr float kFindBarFontSizeDip = 13.0f;
constexpr float kFindBarCornerRadiusDip = 6.0f;
constexpr float kFindBarWidthDip = kFindBarPaddingXDip * 2.0f + kFindBarPrefixWidthDip +
                                    kFindBarEditWidthDip + kFindBarGapDip + kFindBarStatusWidthDip +
                                    kFindBarStatusNavGapDip + kFindBarNavButtonWidthDip * 2.0f +
                                    kFindBarGapDip + kFindBarCloseButtonWidthDip;

// 单个查找命中最多跨几行:HitTestTextRange 的输出上限,超出部分不画(极端长命中)。
constexpr UINT32 kMaxFindHitMetrics = 16;

// 叠加层条文字的栈上缓冲长度,以及"查找条 + 提示条"同时出现时第二条的下移量。
constexpr u32 kMaxOverlayBarChars = 256;
constexpr float kOverlayBarStackStepDip = 34.0f;

// T63 大纲侧栏几何常量。数值必须与 shell/outline_panel.h 里的同名常量保持一致
// ——render 层不反向 include shell 目录下的头文件(架构约束,单向依赖
// shell -> render),这里只重复几个纯数字,真正的"唯一权威定义"在 shell 侧,
// window.cpp 传进来的坐标(outlineScrollY 等)都是按 shell 那份常量算出来的。
// 宽度本身 T63b 起可拖拽调整,不再是常量,由 overlay_->outlinePanelWidthDip
// 传入,这里只留行高/缩进/内边距这些仍然固定的几何量。
constexpr float kOutlineItemHeightDip = 28.0f;
constexpr float kOutlineIndentStepDip = 14.0f;
constexpr float kOutlinePanelPaddingDip = 10.0f;
constexpr float kOutlineRowFontSizeDip = 13.0f;

// 自绘滚动条几何常量(方案A)。数值必须与 shell/scrollbar.h 的同名常量保持
// 一致——render 层不反向 include shell 目录下的头文件,这里只重复几个纯
// 数字,与 T63 大纲侧栏常量同一约束(见上方注释)。
constexpr float kScrollbarWidthDip = 6.0f;
constexpr float kScrollbarCornerRadiusDip = 3.0f;
constexpr float kScrollbarMarginDip = 4.0f;
constexpr float kScrollbarMinThumbHeightDip = 24.0f;
constexpr u32 kMaxOutlineTitleChars = 256;

// 底部操作栏几何常量。数值必须与 shell/bottom_bar.h 里的同名常量保持一致
// ——同一条"render 不反向 include shell"的约束(见上面大纲侧栏的注释),
// 这里只重复几个纯数字,真正的"唯一权威定义"(含命中测试/文字标签)在 shell 侧。
// 新布局(2026-09-18):栏高塞不下"图标+文字"两行(2026-09-19 由 32px 再收窄
// 到 24px,同一条理由更适用),图标改为在整条栏
// 高度内垂直居中,不再画常驻文字标签;左侧 5 个按钮固定宽度紧贴排列,右侧最右边是历史记录按钮,中间是状态文字区。
constexpr float kBottomBarHeightDip = 24.0f;
constexpr u32 kBottomBarButtonCount = 10;
constexpr u32 kBottomBarLeftButtonCount = 8;
// 右侧固定图标位下标(与 shell/bottom_bar.h::BottomBarButton 保持一致)。
// CopyPath 仅当前有打开文档时存在,不存在时不占位、不参与命中/绘制。
constexpr u32 kBottomBarCopyPathIndex = 8;
constexpr u32 kBottomBarHistoryIndex = 9;
constexpr float kBottomBarButtonWidthDip = kBottomBarHeightDip;  // 正方形按钮,与栏高相等
constexpr float kBottomBarIconSizeDip = 14.0f;   // 图标绘制区正方形边长(右侧固定按钮群)
// 左侧 8 个按钮整体比右侧固定按钮群小 2px,
// 视觉上更轻——2026-09-19 新增查找按钮时一并调整。
constexpr float kBottomBarLeftIconSizeDip = kBottomBarIconSizeDip - 2.0f;
constexpr float kBottomBarIconStrokeWidthDip = 1.4f;
constexpr float kBottomBarStatusFontSizeDip = 11.0f;    // 右侧状态文字字号
constexpr float kBottomBarStatusPaddingDip = 10.0f;     // 状态文字右侧留白
constexpr u64 kBottomBarBytesPerMb = 1024ull * 1024ull; // 1MB 对应字节数,KB/MB 切换阈值

// 悬浮提示气泡几何常量(2026-09-18 新增,取代常驻文字标签)。
constexpr float kBottomBarTooltipFontSizeDip = 12.0f;
constexpr float kBottomBarTooltipPaddingDip = 6.0f;    // 气泡文字左右各留白
constexpr float kBottomBarTooltipHeightDip = 22.0f;
constexpr float kBottomBarTooltipGapDip = 4.0f;        // 气泡底边与底部栏顶边的间隙

// 10 个按钮从左到右的悬浮提示文案,下标须与 shell/bottom_bar.h 的
// BottomBarButton 枚举顺序一一对应。数值/文案必须与那边的 kBottomBarLabels
// 保持一致——同一条"render 不反向 include shell"的约束。
constexpr const wchar_t* kBottomBarTooltipLabels[kBottomBarButtonCount] = {
    L"文件列表", L"大纲", L"打开文件", L"打开文件夹", L"主题", L"缩小", L"放大", L"查找", L"复制当前文件路径", L"历史",
};

// 文件夹侧栏条目 hover 完整路径提示气泡几何常量:与底部栏提示气泡
// (kBottomBarTooltip*)同一套画法,区别是宽度按内容自适应、超出宽度自动换行
// (显示完整绝对路径,不允许像正文里那样省略号截断)。
constexpr float kFolderTooltipFontSizeDip = 12.0f;
constexpr float kFolderTooltipPaddingDip = 8.0f;   // 气泡文字四周留白
constexpr float kFolderTooltipMaxWidthDip = 320.0f; // 超过这个宽度自动换行
constexpr float kFolderTooltipGapDip = 6.0f;        // 气泡左边距、以及气泡与所在行的竖直间隙
constexpr float kFolderTooltipScreenMarginDip = 4.0f; // 气泡与屏幕边缘的最小间距

// 欢迎屏几何/文案常量。按钮尺寸/位置数值必须与 shell/welcome_screen.h 的
// 同名常量保持一致——同一条"render 不反向 include shell"的约束,这里只
// 重复几个纯数字,真正的"唯一权威定义"(含命中测试)在 shell 侧。
constexpr float kWelcomeButtonWidthDip = 140.0f;
constexpr float kWelcomeButtonHeightDip = 32.0f;
constexpr float kWelcomeButtonGapDip = 16.0f;
constexpr float kWelcomeButtonTopRatio = 0.46f;
constexpr float kWelcomeButtonCornerRadiusDip = 8.0f;
constexpr float kWelcomeButtonLabelFontSizeDip = 14.0f;    // 按钮文案字号
constexpr float kWelcomeHeadingFontSizeScale = 1.8f;   // 与正文一级标题同一档缩放(kHeadingScale[1])
constexpr float kWelcomeHeadingGapAboveButtonDip = 24.0f;  // 标题底边与按钮顶边的间距
constexpr float kWelcomeIconSizeDip = 14.0f;               // 文件夹图标外接正方形边长
constexpr float kWelcomeIconLabelGapDip = 8.0f;            // 图标与文字标签的横向间距
constexpr const wchar_t kWelcomeHeadingText[] = L"Welcome To Markair";
constexpr const wchar_t kWelcomeButtonLabel[] = L"打开文件";
constexpr const wchar_t kWelcomeFolderButtonLabel[] = L"打开文件夹";

// 标题级别 -> 缩进量,口径与 markair::OutlineItemIndentDip 一致。
float OutlineRowIndentDip(u8 level) {
    u8 step = (level >= 1) ? static_cast<u8>(level - 1) : 0;
    if (step > 5) step = 5;
    return kOutlineIndentStepDip * static_cast<float>(step);
}

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

// 拼出查找条右侧的状态文字(2026-09-19 改版:查询串本身由原生 Edit 控件
// 显示,这里只拼状态区那一小段):固定"当前序号/命中总数"格式,零命中
// (含还没搜过/搜过但没命中两种情况,不再区分)统一显示"0/0",不再出现
// "回车搜索"/"无匹配"这类文案(与 images/7.png 的参考样式对齐)。
u32 ComposeFindBarStatusText(const ShellOverlay* overlay, wchar_t* buf, u32 cap) {
    u32 total = overlay->matchCount;
    u32 ordinal = (total == 0 || overlay->currentMatch == kInvalidIndex) ? 0u
                                                                          : overlay->currentMatch + 1u;
    u32 cursor = AppendDecimal(buf, 0, cap, ordinal);
    cursor = AppendLiteral(buf, cursor, cap, L"/");
    cursor = AppendDecimal(buf, cursor, cap, total);
    buf[cursor] = 0;
    return cursor;
}

// 把文件大小格式化成状态区文字:< 1MB 显示 KB,否则显示 MB,保留 1 位小数
// (如 "856.0 KB" / "2.3 MB")。口径必须与 shell/bottom_bar.h 的同名函数保持
// 一致——同一条"render 不反向 include shell"的约束,这里独立实现一份。
u32 AppendFormattedFileSize(wchar_t* buf, u32 cursor, u32 cap, u64 sizeBytes) {
    bool useMb = sizeBytes >= kBottomBarBytesPerMb;
    float kb = static_cast<float>(sizeBytes) / 1024.0f;
    float value = useMb ? kb / 1024.0f : kb;
    i32 tenths = static_cast<i32>(value * 10.0f + 0.5f);
    cursor = AppendDecimal(buf, cursor, cap, static_cast<u32>(tenths / 10));
    cursor = AppendLiteral(buf, cursor, cap, L".");
    cursor = AppendDecimal(buf, cursor, cap, static_cast<u32>(tenths % 10));
    cursor = AppendLiteral(buf, cursor, cap, useMb ? L" MB" : L" KB");
    return cursor;
}

// 拼出底部栏状态区的一行文字:"<路径> (<大小>)"。
u32 ComposeBottomBarStatusText(const wchar_t* documentPath, u64 documentSizeBytes,
                                wchar_t* buf, u32 cap) {
    u32 cursor = AppendLiteral(buf, 0, cap, documentPath);
    cursor = AppendLiteral(buf, cursor, cap, L" (");
    cursor = AppendFormattedFileSize(buf, cursor, cap, documentSizeBytes);
    cursor = AppendLiteral(buf, cursor, cap, L")");
    buf[cursor] = 0;
    return cursor;
}

}  // namespace

const wchar_t* ImagePlaceholderText(ImageStatus status) {
    switch (status) {
    case ImageStatus::Ok:              return L"";
    case ImageStatus::NotLoaded:       return L"图片加载中";
    case ImageStatus::Failed:          return L"图片加载失败";
    case ImageStatus::Unsupported:     return L"不支持的图片格式";
    case ImageStatus::TooLarge:        return L"图片过大,未加载";
    case ImageStatus::RemoteNotLoaded: return L"网络图片,点击加载";
    }
    return L"图片加载失败";
}

const wchar_t* DownsampledBadgeText() { return L"查看原图"; }

namespace {

// 某个状态是否是"终态失败":不要每帧重试解码(损坏文件、格式不支持、超限)。
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

    ID2D1RenderTarget* target = renderer_ ? renderer_->Target() : nullptr;
    DecodedImage decoded{nullptr, 0, 0, ImageStatus::Failed, false};
    bool isSvg = IsSvgImageRef(box.href);  // SVG 走 lunasvg 离线栅格化,不进 WIC

    if (box.kind == LinkTargetKind::DataUri) {
        if (!scratch_) return ImageStatus::Failed;
        scratch_->Reset();  // data: URI 的解码缓冲只需活到本次解码结束
        DataUriPayload payload = ParseDataUri(box.href, scratch_);
        if (!payload.valid || payload.len == 0) {
            cache_->Put(box.href, nullptr, 0, 0, ImageStatus::Failed, false);
            return ImageStatus::Failed;
        }
        decoded = isSvg ? DecodeSvgFromMemory(payload.bytes, payload.len, target)
                         : decoder_.DecodeFromMemory(payload.bytes, payload.len, target);
    } else if (box.kind == LinkTargetKind::External) {
        // 网络图片:只有已经下载过原始字节才解码,否则停在"点击加载"占位(T34)。
        u32 rawLen = 0;
        const u8* raw = cache_->FindRemoteBytes(box.href, &rawLen);
        if (!raw) {
            cache_->Put(box.href, nullptr, 0, 0, ImageStatus::RemoteNotLoaded, false);
            return ImageStatus::RemoteNotLoaded;
        }
        decoded = isSvg ? DecodeSvgFromMemory(raw, rawLen, target)
                         : decoder_.DecodeFromMemory(raw, rawLen, target);
    } else if (box.kind == LinkTargetKind::RelativePath) {
        wchar_t path[MAX_PATH * 2]{};
        if (!BuildLocalImagePath(box.href, docDir_, path, MAX_PATH * 2)) {
            cache_->Put(box.href, nullptr, 0, 0, ImageStatus::Failed, false);
            return ImageStatus::Failed;
        }
        if (isSvg) {
            // SVG 走只读文件映射零拷贝取字节,再喂给 lunasvg——不复用 WIC 那条
            // CreateDecoderFromFilename 路径(WIC 本身不认识 svg)。
            FileMap fm;
            if (fm.Open(path) == FileMapError::None) {
                StrSlice content = fm.Data();
                decoded = DecodeSvgFromMemory(content.data, content.len, target);
                fm.Close();
            }
        } else {
            decoded = decoder_.DecodeFromFile(path, target);
        }
    } else {
        cache_->Put(box.href, nullptr, 0, 0, ImageStatus::Failed, false);
        return ImageStatus::Failed;
    }

    cache_->Put(box.href, decoded.bitmap, decoded.width, decoded.height, decoded.status,
                decoded.wasDownsampled, decoded.originalWidth, decoded.originalHeight);
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
    : zoomInIconGeometry_(nullptr), zoomOutIconGeometry_(nullptr), factory_(nullptr),
      fonts_(nullptr), images_(nullptr), target_(nullptr), dpi_(0.0f),
      overlay_(nullptr), frameViewportHeight_(0.0f), palette_(&kLightPalette),
      outlineScratchInited_(false) {}

// 析构时释放渲染目标本体;工厂/字体子系统均不归本对象所有,不在此释放。
// 放大/缩小图标几何挂在 factory_(设备无关资源),本对象持有引用,这里一并释放。
Renderer::~Renderer() {
    ReleaseRenderTarget();
    if (zoomInIconGeometry_) zoomInIconGeometry_->Release();
    if (zoomOutIconGeometry_) zoomOutIconGeometry_->Release();
}

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
                                ID2D1SolidColorBrush* tableGridBrush,
                                ID2D1SolidColorBrush* tableZebraBrush,
                                ID2D1SolidColorBrush* tableRowHoverBrush,
                                u32 hoverRow) {
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

    // 表体斑马纹(奇数行)+ 悬浮行高亮:必须画在网格线之前——D2D 后画的盖住
    // 先画的,网格线的 DrawLine 调用留在下面,自然留在最上层不被盖住。
    // hoverRow 命中的行在斑马纹之上再叠一次,是刻意的优先级(悬浮态更显眼)。
    for (u32 r = g.tableHeadRowCount; r + 1 < g.tableRowTops.len; ++r) {
        u32 bodyRow = r - g.tableHeadRowCount;
        float rowTop = g.tableRowTops[r] - scrollY;
        float rowBottom = g.tableRowTops[r + 1] - scrollY;
        if (tableZebraBrush && (bodyRow % 2 == 1)) {
            target_->FillRectangle(D2D1::RectF(left, rowTop, right, rowBottom), tableZebraBrush);
        }
        if (tableRowHoverBrush && bodyRow == hoverRow) {
            target_->FillRectangle(D2D1::RectF(left, rowTop, right, rowBottom), tableRowHoverBrush);
        }
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
            // T76:整条 run 已经滚出屏幕就直接跳过。下面那次 DrawTextLayout 画的是
            // **整份** layout(只靠裁剪把可见部分留下来),代价与裁剪框大小无关,
            // 所以屏幕外的 run 一个都不能白画。
            if (clipRect.bottom < 0.0f || clipRect.top > frameViewportHeight_) continue;
            // 裁剪到该 link range 的矩形后整体重画一次 layout——裁剪区之外的部分
            // 不可见,效果上等价于"只给这段文字换色",但不需要 SetDrawingEffect
            // 与自定义 TextRenderer(T24 明确避免的成本)。
            target_->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            target_->DrawTextLayout(D2D1::Point2F(drawLeft, drawTop), g.textLayout, linkBrush,
                                     D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            target_->PopAxisAlignedClip();
        }
    }
}

void Renderer::DrawCodeHighlights(const BlockGeometry& g, float scrollY,
                                    ID2D1SolidColorBrush* const* hlBrushes) {
    if (!g.textLayout || g.codeHighlights.len == 0 || !hlBrushes) return;

    constexpr UINT32 kMaxHitTestMetrics = 8;  // 单个 token 通常不跨行,8 条足够兜底
    float drawLeft = TextDrawLeft(g);
    float drawTop = TextDrawTop(g) - scrollY;
    for (u32 i = 0; i < g.codeHighlights.len; ++i) {
        const CodeHighlightRun& run = g.codeHighlights[i];
        ID2D1SolidColorBrush* brush = hlBrushes[run.tokenType];
        if (!brush) continue;

        DWRITE_HIT_TEST_METRICS metrics[kMaxHitTestMetrics];
        UINT32 actualCount = 0;
        HRESULT hr = g.textLayout->HitTestTextRange(
            run.textPosition, run.textLength, drawLeft, drawTop,
            metrics, kMaxHitTestMetrics, &actualCount);
        if (FAILED(hr)) continue;

        UINT32 count = actualCount < kMaxHitTestMetrics ? actualCount : kMaxHitTestMetrics;
        for (UINT32 m = 0; m < count; ++m) {
            D2D1_RECT_F clipRect = D2D1::RectF(
                metrics[m].left, metrics[m].top,
                metrics[m].left + metrics[m].width, metrics[m].top + metrics[m].height);
            // T76:理由同 DrawLinkOverlays —— 屏幕外的 token 跳过,否则一个大代码块
            // 里成百上千个 token 每个都要整份 layout 重画一次,而其中绝大多数根本
            // 不在视口里(BENCH-C 实测这一项就占了每帧的大头)。
            if (clipRect.bottom < 0.0f || clipRect.top > frameViewportHeight_) continue;
            // 裁剪到该 token range 的矩形后整体重画一次 layout——与 DrawLinkOverlays
            // 同一手法,不引入 SetDrawingEffect 与自定义 TextRenderer(T24/T53 同一约束)。
            target_->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            target_->DrawTextLayout(D2D1::Point2F(drawLeft, drawTop), g.textLayout, brush,
                                     D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
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
                             layout, textBrush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    layout->Release();
}

void Renderer::DrawFindBar(float targetWidth, const wchar_t* statusText, u32 statusTextLen,
                            ID2D1SolidColorBrush* bgBrush, ID2D1SolidColorBrush* textBrush) {
    if (!target_ || !fonts_ || !bgBrush) return;

    // 几何口径必须跟 shell/find_bar.h::ComputeFindBarLayout 算出同一个矩形,
    // 窗口才会把原生 Edit 控件摆在这条背景条中间正确的位置——两边独立算,
    // 用的是完全相同的公式与常量(见上面 kFindBar* 常量的注释)。
    float right = targetWidth - kFindBarMarginDip;
    float left = right - kFindBarWidthDip;
    if (left < kFindBarMarginDip) left = kFindBarMarginDip;
    float top = kFindBarMarginDip;

    D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(
        D2D1::RectF(left, top, left + kFindBarWidthDip, top + kFindBarHeightDip),
        kFindBarCornerRadiusDip, kFindBarCornerRadiusDip);
    target_->FillRoundedRectangle(rounded, bgBrush);

    if (!textBrush) return;

    // "查找: " 前缀,固定在左侧;中间的 kFindBarEditWidthDip 那一段留白,
    // 原生 Edit 子窗口盖在上面画查询串本身与光标。
    const wchar_t prefix[] = L"查找:";
    IDWriteTextLayout* prefixLayout = fonts_->CreateTextLayout(
        prefix, WideLength(prefix), FontRole::Body, kFindBarPrefixWidthDip, kFindBarHeightDip);
    if (prefixLayout) {
        prefixLayout->SetFontSize(kFindBarFontSizeDip, DWRITE_TEXT_RANGE{0, WideLength(prefix)});
        prefixLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        target_->DrawTextLayout(D2D1::Point2F(left + kFindBarPaddingXDip, top), prefixLayout,
                                textBrush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        prefixLayout->Release();
    }

    float statusLeft = left + kFindBarPaddingXDip + kFindBarPrefixWidthDip + kFindBarEditWidthDip +
                        kFindBarGapDip;
    if (statusText && statusTextLen > 0) {
        IDWriteTextLayout* statusLayout = fonts_->CreateTextLayout(
            statusText, statusTextLen, FontRole::Body, kFindBarStatusWidthDip, kFindBarHeightDip);
        if (statusLayout) {
            statusLayout->SetFontSize(kFindBarFontSizeDip, DWRITE_TEXT_RANGE{0, statusTextLen});
            statusLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            // 靠右贴紧箭头按钮画("0/0"这类短文案默认左对齐会在 50px 状态栏宽度
            // 里留一大截空白,视觉上和后面的按钮离得远),不改布局宽度/命中
            // 几何,只调文字在自己框内的水平对齐。
            statusLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            target_->DrawTextLayout(D2D1::Point2F(statusLeft, top), statusLayout, textBrush,
                                    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            statusLayout->Release();
        }
    }

    // "上一个"/"下一个"箭头按钮(几何口径必须与 shell/find_bar.h::
    // ComputeFindBarLayout 的 prevLeft/nextLeft 完全一致,否则画的箭头位置
    // 会跟 window.cpp 里做点击命中测试用的矩形错位)。用两条折线画 ^/v 形,
    // 不额外引入图标字体或 PathGeometry。
    float prevLeft = statusLeft + kFindBarStatusWidthDip + kFindBarStatusNavGapDip;
    float nextLeft = prevLeft + kFindBarNavButtonWidthDip;
    float chevronHalfW = kFindBarNavButtonWidthDip * 0.18f;
    float chevronHalfH = kFindBarNavButtonWidthDip * 0.14f;
    float chevronStroke = 1.4f;
    // 上一个:朝上的 "^"。
    {
        float cx = prevLeft + kFindBarNavButtonWidthDip * 0.5f;
        float cy = top + kFindBarHeightDip * 0.5f;
        target_->DrawLine(D2D1::Point2F(cx - chevronHalfW, cy + chevronHalfH),
                          D2D1::Point2F(cx, cy - chevronHalfH), textBrush, chevronStroke);
        target_->DrawLine(D2D1::Point2F(cx, cy - chevronHalfH),
                          D2D1::Point2F(cx + chevronHalfW, cy + chevronHalfH), textBrush,
                          chevronStroke);
    }
    // 下一个:朝下的 "v"。
    {
        float cx = nextLeft + kFindBarNavButtonWidthDip * 0.5f;
        float cy = top + kFindBarHeightDip * 0.5f;
        target_->DrawLine(D2D1::Point2F(cx - chevronHalfW, cy - chevronHalfH),
                          D2D1::Point2F(cx, cy + chevronHalfH), textBrush, chevronStroke);
        target_->DrawLine(D2D1::Point2F(cx, cy + chevronHalfH),
                          D2D1::Point2F(cx + chevronHalfW, cy - chevronHalfH), textBrush,
                          chevronStroke);
    }
    // 关闭按钮:最右侧一个 "X"(点击关闭整条查找条,与 Esc 同效果)。
    {
        float closeLeft = nextLeft + kFindBarNavButtonWidthDip + kFindBarGapDip;
        float cx = closeLeft + kFindBarCloseButtonWidthDip * 0.5f;
        float cy = top + kFindBarHeightDip * 0.5f;
        float half = kFindBarCloseButtonWidthDip * 0.18f;
        target_->DrawLine(D2D1::Point2F(cx - half, cy - half), D2D1::Point2F(cx + half, cy + half),
                          textBrush, chevronStroke);
        target_->DrawLine(D2D1::Point2F(cx - half, cy + half), D2D1::Point2F(cx + half, cy - half),
                          textBrush, chevronStroke);
    }
}

void Renderer::DrawOutlineOverlayMask(float targetWidth, float targetHeight,
                                       ID2D1SolidColorBrush* maskBrush) {
    // 与 DrawOutlinePanel 同一个"侧栏是否打开"判断依据,侧栏关闭时本函数
    // 直接返回,不产生任何额外绘制(维持 T63"关闭时开销为 0"的设计)。
    // 注意:不能拿 outlineItemCount == 0 当"侧栏未打开"的判据——大纲为空的
    // 文档(或欢迎屏空状态)打开侧栏时 itemCount 恒为 0,之前误把这种情况当
    // "侧栏关闭"直接跳过,导致点击大纲按钮蒙层/侧栏都不出现(真实 bug)。
    if (!overlay_) return;
    if (!maskBrush || !target_) return;

    float animProgress = overlay_->outlineAnimProgress;
    if (animProgress <= 0.0001f) return;

    // 只盖侧栏当前可见矩形之外的正文区域,侧栏本身随后单独画(DrawOutlinePanel),
    // 不会被这层蒙层盖住。滑动动画期间,蒙层从侧栏当前可见右边缘开始向右铺满。
    //
    // Only cover the text area outside the visible sliding outline panel.
    float visibleWidth = overlay_->outlinePanelWidthDip * animProgress;
    if (visibleWidth < 0.0f) visibleWidth = 0.0f;
    if (visibleWidth > overlay_->outlinePanelWidthDip) visibleWidth = overlay_->outlinePanelWidthDip;

    D2D1_RECT_F maskRect =
        D2D1::RectF(visibleWidth, 0.0f, targetWidth, targetHeight);
    target_->FillRectangle(maskRect, maskBrush);
}

// 自绘滚动条滑块(方案A,见类头文件注释):按视口/内容尺寸算出滑块矩形,
// 内容不超过一屏时不画。几何公式与 shell/scrollbar.h::CalcScrollbarMetrics
// 保持一致(纯数字,那边可单测;这里只负责按结果画一个直角轨道与圆角滑块)。
void Renderer::DrawScrollbar(float viewportWidth, float viewportHeight, float totalHeight,
                              float scrollY, ID2D1SolidColorBrush* trackBrush,
                              ID2D1SolidColorBrush* thumbBrush) {
    if (!target_ || !thumbBrush) return;
    if (viewportHeight <= 0.0f || totalHeight <= viewportHeight) return;

    float right = viewportWidth - kScrollbarMarginDip;
    float left = right - kScrollbarWidthDip;

    // 轨道:常驻显示,标示"这一整条都是可滚动范围",直角矩形铺满整个视口高度(背景无圆角)。
    //
    // Track: permanently visible straight rectangle without rounded corners.
    if (trackBrush) {
        D2D1_RECT_F trackRect = D2D1::RectF(left, 0.0f, right, viewportHeight);
        target_->FillRectangle(trackRect, trackBrush);
    }

    float thumbHeight = viewportHeight * (viewportHeight / totalHeight);
    if (thumbHeight < kScrollbarMinThumbHeightDip) thumbHeight = kScrollbarMinThumbHeightDip;
    if (thumbHeight > viewportHeight) thumbHeight = viewportHeight;

    float maxScrollY = totalHeight - viewportHeight;
    float maxThumbTop = viewportHeight - thumbHeight;
    float ratio = (maxScrollY > 0.0f) ? (scrollY / maxScrollY) : 0.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    float thumbTop = ratio * maxThumbTop;

    D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(
        D2D1::RectF(left, thumbTop, right, thumbTop + thumbHeight),
        kScrollbarCornerRadiusDip, kScrollbarCornerRadiusDip);
    target_->FillRoundedRectangle(rounded, thumbBrush);
}

void Renderer::DrawOutlinePanel(float targetHeight,
                                ID2D1SolidColorBrush* bgBrush, ID2D1SolidColorBrush* textBrush,
                                ID2D1SolidColorBrush* highlightBgBrush,
                                ID2D1SolidColorBrush* highlightTextBrush,
                                ID2D1SolidColorBrush* scrollbarTrackBrush,
                                ID2D1SolidColorBrush* scrollbarThumbBrush) {
    // 同上:不能拿 itemCount == 0 当"侧栏未打开"判据(空大纲/欢迎屏空状态)。
    // overlay_->doc 同理放宽——itemCount == 0 时下面循环体不会执行,不会
    // 解引用 doc,doc 为空并不妨碍画出空侧栏的背景/边框。
    if (!overlay_) return;
    if (!fonts_ || !bgBrush || !textBrush || !target_) return;

    float animProgress = overlay_->outlineAnimProgress;
    if (animProgress <= 0.0001f) return;

    // 悬浮覆盖:侧栏浮在正文左侧上方,不改变正文视口宽度,几何与主内容布局
    // 完全无关(不读取任何 BlockGeometry 几何字段),开关侧栏因此是纯重绘。
    // T63b:宽度可拖拽调整,用外壳层传来的实际当前宽度。
    float panelWidth = overlay_->outlinePanelWidthDip;

    // Apply horizontal translation for slide drawer animation.
    //
    // 为抽屉式侧栏滑动动画施加水平平移变换。
    float slideOffset = 0.0f;
    if (animProgress < 1.0f) {
        slideOffset = (animProgress - 1.0f) * panelWidth;
        target_->SetTransform(D2D1::Matrix3x2F::Translation(slideOffset, 0.0f));
    }

    D2D1_RECT_F panelRect = D2D1::RectF(0.0f, 0.0f, panelWidth, targetHeight);
    target_->FillRectangle(panelRect, bgBrush);

    // 侧栏右边框:与 DrawHistoryPanel 左边框同一手法,分隔侧栏与蒙层区域,
    // 缺了这条线侧栏右边缘会跟蒙层的纯色背景糊在一起(真实 bug)。
    if (highlightBgBrush) {
        target_->DrawLine(D2D1::Point2F(panelWidth, 0.0f), D2D1::Point2F(panelWidth, targetHeight),
                          highlightBgBrush, 1.0f);
    }

    // 标题原文的拼接/UTF-16 转换缓冲,惰性 Init(见头文件字段注释),每帧开头
    // 整体 Reset 复用同一块地址空间——这是唯一被本函数使用的 Arena,侧栏从未
    // 打开过时本函数永远不会被调用,自然也不会走到这里。
    if (!outlineScratchInited_) outlineScratchInited_ = outlineScratch_.Init(1 * 1024 * 1024);
    outlineScratch_.Reset();

    // 大纲为空(当前文档没有标题,或欢迎屏空状态):整片区域居中画一行提示,
    // 不进入下面的逐行循环(itemCount 为 0,循环体本就不会执行,这里只是补
    // 一行视觉反馈)——与 DrawHistoryPanel 的"No Records"同一手法。
    if (overlay_->outlineItemCount == 0) {
        static const wchar_t kNoOutlineText[] = L"No Outline";
        u32 noOutlineLen = static_cast<u32>(wcslen(kNoOutlineText));
        float maxTextWidth = panelWidth - kOutlinePanelPaddingDip * 2.0f;
        IDWriteTextLayout* emptyLayout = fonts_->CreateTextLayout(
            kNoOutlineText, noOutlineLen, FontRole::Body, maxTextWidth, targetHeight);
        if (emptyLayout) {
            emptyLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            emptyLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            target_->DrawTextLayout(D2D1::Point2F(kOutlinePanelPaddingDip, 0.0f),
                                    emptyLayout, textBrush,
                                    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            emptyLayout->Release();
        }
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        return;
    }

    for (u32 i = 0; i < overlay_->outlineItemCount; ++i) {
        const OutlineItem& item = overlay_->outlineItems[i];
        float rowTop = kOutlineItemHeightDip * static_cast<float>(i) - overlay_->outlineScrollY;
        if (rowTop + kOutlineItemHeightDip < 0.0f || rowTop > targetHeight) continue;  // 侧栏自身裁剪:只画可见行

        bool highlighted = (i == overlay_->outlineCurrentItem);
        if (highlighted && highlightBgBrush) {
            target_->FillRectangle(
                D2D1::RectF(0.0f, rowTop, panelWidth, rowTop + kOutlineItemHeightDip),
                highlightBgBrush);
        }

        if (item.blockIdx >= overlay_->doc->blocks.Size()) continue;  // 防御:块下标越界跳过
        const Block& b = overlay_->doc->blocks[item.blockIdx];

        // 拼接该块全部 inline run 的源字节 -> UTF-16,与 doc/search.cpp 的
        // BuildBlockText 同一手法(未跨模块复用,量很小,不值得为此破坏
        // search.cpp 的匿名命名空间封装)。
        u32 totalBytes = 0;
        for (u32 k = 0; k < b.inlineCount; ++k) {
            totalBytes += overlay_->doc->inlines[b.firstInlineIdx + k].textLen;
        }
        if (totalBytes == 0) continue;
        char* byteBuf = static_cast<char*>(outlineScratch_.Alloc(totalBytes, 1));
        if (!byteBuf) continue;
        u32 cursor = 0;
        for (u32 k = 0; k < b.inlineCount; ++k) {
            const Inline& in = overlay_->doc->inlines[b.firstInlineIdx + k];
            if (in.textLen == 0) continue;
            memcpy(byteBuf + cursor, InlineTextBytes(in, *overlay_->doc), in.textLen);
            cursor += in.textLen;
        }
        Utf16Slice wide = Utf8ToUtf16(StrSlice{byteBuf, cursor}, &outlineScratch_);
        if (wide.len == 0) continue;

        float indent = OutlineRowIndentDip(item.level);
        float maxTextWidth = panelWidth - kOutlinePanelPaddingDip * 2.0f - indent;
        if (maxTextWidth <= 0.0f) continue;

        // 超长标题截断成省略号:先量整段,放得下就直接画;放不下二分查出
        // 能塞下的最大前缀长度(与 markair::TruncateOutlineTitle 同一口径,
        // 这里不跨 shell/render 反向 include,直接用 fonts_ 现场量)。
        u32 useLen = wide.len;
        if (useLen > kMaxOutlineTitleChars) useLen = kMaxOutlineTitleChars;
        IDWriteTextLayout* probe =
            fonts_->CreateTextLayout(wide.data, useLen, FontRole::Body, 8192.0f, 64.0f);
        if (!probe) continue;
        probe->SetFontSize(kOutlineRowFontSizeDip, DWRITE_TEXT_RANGE{0, useLen});
        DWRITE_TEXT_METRICS metrics{};
        bool fits = SUCCEEDED(probe->GetMetrics(&metrics)) &&
                    metrics.widthIncludingTrailingWhitespace <= maxTextWidth;
        probe->Release();

        wchar_t rowBuf[kMaxOutlineTitleChars + 4];
        u32 rowLen;
        if (fits) {
            rowLen = useLen;
            for (u32 c = 0; c < rowLen; ++c) rowBuf[c] = wide.data[c];
        } else {
            u32 lo = 0, hi = useLen;
            while (lo < hi) {
                u32 mid = lo + (hi - lo + 1) / 2;
                IDWriteTextLayout* t =
                    fonts_->CreateTextLayout(wide.data, mid, FontRole::Body, 8192.0f, 64.0f);
                bool ok = false;
                if (t) {
                    t->SetFontSize(kOutlineRowFontSizeDip, DWRITE_TEXT_RANGE{0, mid});
                    DWRITE_TEXT_METRICS m{};
                    if (SUCCEEDED(t->GetMetrics(&m))) {
                        // "..." 三个字符按当前字号的等宽估算(与整体截断量级相比,
                        // 这个近似不影响可读性,避免为量一个固定字面量再建一次 layout)。
                        float ellipsisApprox = kOutlineRowFontSizeDip * 1.8f;
                        ok = (m.widthIncludingTrailingWhitespace + ellipsisApprox) <= maxTextWidth;
                    }
                    t->Release();
                }
                if (ok) lo = mid; else hi = mid - 1;
            }
            rowLen = lo;
            for (u32 c = 0; c < rowLen; ++c) rowBuf[c] = wide.data[c];
            rowBuf[rowLen++] = L'.';
            rowBuf[rowLen++] = L'.';
            rowBuf[rowLen++] = L'.';
        }
        if (rowLen == 0) continue;

        IDWriteTextLayout* rowLayout =
            fonts_->CreateTextLayout(rowBuf, rowLen, FontRole::Body, maxTextWidth, kOutlineItemHeightDip);
        if (!rowLayout) continue;
        rowLayout->SetFontSize(kOutlineRowFontSizeDip, DWRITE_TEXT_RANGE{0, rowLen});
        float textTop = rowTop + (kOutlineItemHeightDip - kOutlineRowFontSizeDip - 4.0f) * 0.5f;
        target_->DrawTextLayout(D2D1::Point2F(indent + kOutlinePanelPaddingDip, textTop),
                                rowLayout,
                                (highlighted && highlightTextBrush) ? highlightTextBrush : textBrush,
                                D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        rowLayout->Release();
    }

    // 侧栏自身的滚动条(方案A):内容总高按条目数换算,与 OutlinePanel 里
    // ClampScrollOffset 用的口径一致(shell/outline_panel.h::OutlinePanelContentHeightDip)。
    float contentHeight = kOutlineItemHeightDip * static_cast<float>(overlay_->outlineItemCount);
    DrawScrollbar(panelWidth, targetHeight, contentHeight, overlay_->outlineScrollY,
                  scrollbarTrackBrush, scrollbarThumbBrush);

    if (slideOffset != 0.0f) {
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
    }
}

void Renderer::DrawHistoryOverlayMask(float targetWidth, float targetHeight,
                                      ID2D1SolidColorBrush* maskBrush) {
    // 2026-09-19 修复:历史记录为空(count==0)时也要能打开侧栏显示"No Records"
    // 提示,不能在这里直接 return——之前把"entries 指针为空"(功能未接入)
    // 和"entries 非空但条目数为 0"(功能已接入,只是暂无记录)混为一谈,
    // 导致空历史时点击按钮毫无视觉反应。只在指针真正为空时才当作功能未接入。
    if (!overlay_ || !overlay_->historyEntries) return;
    if (!maskBrush || !target_) return;

    float animProgress = overlay_->historyAnimProgress;
    if (animProgress <= 0.0001f) return;

    float visibleWidth = overlay_->historyPanelWidthDip * animProgress;
    if (visibleWidth < 0.0f) visibleWidth = 0.0f;
    if (visibleWidth > overlay_->historyPanelWidthDip) visibleWidth = overlay_->historyPanelWidthDip;

    D2D1_RECT_F maskRect = D2D1::RectF(0.0f, 0.0f, targetWidth - visibleWidth, targetHeight);
    target_->FillRectangle(maskRect, maskBrush);
}

// 侧栏行内"关闭"按钮图标(X 两条交叉线)。userData 是 textBrush。
void Renderer::PaintSidebarCloseIcon(void* renderCtx, const ButtonRectDip& rect, void* userData) {
    Renderer* self = static_cast<Renderer*>(renderCtx);
    ID2D1SolidColorBrush* brush = static_cast<ID2D1SolidColorBrush*>(userData);
    if (!self || !self->target_ || !brush) return;
    float pad = kSidebarCloseButtonGlyphPaddingDip;
    self->target_->DrawLine(D2D1::Point2F(rect.left + pad, rect.top + pad),
                            D2D1::Point2F(rect.right - pad, rect.bottom - pad), brush, 1.4f);
    self->target_->DrawLine(D2D1::Point2F(rect.right - pad, rect.top + pad),
                            D2D1::Point2F(rect.left + pad, rect.bottom - pad), brush, 1.4f);
}

// 侧栏行内"打开所在文件夹"按钮图标(文件夹矩形轮廓 + 左上角标签)。
void Renderer::PaintSidebarFolderIcon(void* renderCtx, const ButtonRectDip& rect, void* userData) {
    Renderer* self = static_cast<Renderer*>(renderCtx);
    ID2D1SolidColorBrush* brush = static_cast<ID2D1SolidColorBrush*>(userData);
    if (!self || !self->target_ || !brush) return;
    ID2D1HwndRenderTarget* target_ = self->target_;
    float pad = kSidebarCloseButtonGlyphPaddingDip;
    float bodyLeft = rect.left + pad;
    float bodyRight = rect.right - pad;
    float bodyTop = rect.top + pad + 2.0f;
    float bodyBottom = rect.bottom - pad;
    target_->DrawRectangle(D2D1::RectF(bodyLeft, bodyTop, bodyRight, bodyBottom), brush, 1.2f);
    float tabWidth = (bodyRight - bodyLeft) * 0.45f;
    target_->DrawLine(D2D1::Point2F(bodyLeft, bodyTop),
                      D2D1::Point2F(bodyLeft + tabWidth * 0.5f, bodyTop - 2.5f), brush, 1.2f);
    target_->DrawLine(D2D1::Point2F(bodyLeft + tabWidth * 0.5f, bodyTop - 2.5f),
                      D2D1::Point2F(bodyLeft + tabWidth, bodyTop), brush, 1.2f);
}

// 侧栏行内"新窗口打开"按钮图标(小方框 + 右上角引出箭头)。
void Renderer::PaintSidebarNewWindowIcon(void* renderCtx, const ButtonRectDip& rect, void* userData) {
    Renderer* self = static_cast<Renderer*>(renderCtx);
    ID2D1SolidColorBrush* brush = static_cast<ID2D1SolidColorBrush*>(userData);
    if (!self || !self->target_ || !brush) return;
    ID2D1HwndRenderTarget* target_ = self->target_;
    float pad = kSidebarCloseButtonGlyphPaddingDip;
    float bodyLeft = rect.left + pad;
    float bodyRight = rect.right - pad - 2.5f;
    float bodyTop = rect.top + pad + 2.5f;
    float bodyBottom = rect.bottom - pad;
    target_->DrawRectangle(D2D1::RectF(bodyLeft, bodyTop, bodyRight, bodyBottom), brush, 1.2f);
    D2D1_POINT_2F arrowTip = D2D1::Point2F(rect.right - pad, rect.top + pad);
    D2D1_POINT_2F arrowBase = D2D1::Point2F(bodyRight - 0.5f, bodyTop + 3.0f);
    target_->DrawLine(arrowBase, arrowTip, brush, 1.2f);
    target_->DrawLine(arrowTip, D2D1::Point2F(arrowTip.x - 3.2f, arrowTip.y), brush, 1.2f);
    target_->DrawLine(arrowTip, D2D1::Point2F(arrowTip.x, arrowTip.y + 3.2f), brush, 1.2f);
}

void Renderer::DrawSidebarListItem(const SidebarListItemParams& params,
                                  ID2D1SolidColorBrush* textBrush,
                                  ID2D1SolidColorBrush* highlightBgBrush,
                                  ID2D1SolidColorBrush* currentItemBrush,
                                  ID2D1SolidColorBrush* buttonBgBrush) {
    if (!target_) return;

    // 1. 行背景（当前激活项或鼠标悬浮项圆角底色）
    D2D1_RECT_F rowRect = D2D1::RectF(
        params.rowLeft + kSidebarPanelPaddingDip * 0.5f,
        params.rowTop + 1.0f,
        params.rowLeft + params.rowWidth - kSidebarPanelPaddingDip * 0.5f,
        params.rowTop + params.rowHeight - 1.0f);
    if (params.isCurrent && currentItemBrush) {
        target_->FillRoundedRectangle(D2D1::RoundedRect(rowRect, 4.0f, 4.0f), currentItemBrush);
    } else if (params.isHover && highlightBgBrush) {
        target_->FillRoundedRectangle(D2D1::RoundedRect(rowRect, 4.0f, 4.0f), highlightBgBrush);
    }

    // 2. 文字排版与 DirectWrite 原生 Trimming 省略号截断
    if (params.text && params.textLen > 0 && fonts_ && textBrush) {
        float maxTextWidth = params.rowWidth - kSidebarPanelPaddingDip * 2.0f;
        if (maxTextWidth > 0.0f) {
            IDWriteTextLayout* textLayout = fonts_->CreateTextLayout(
                params.text, params.textLen, FontRole::Body, maxTextWidth, params.rowHeight);
            if (textLayout) {
                textLayout->SetFontSize(kSidebarRowFontSizeDip, DWRITE_TEXT_RANGE{0, params.textLen});
                textLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                IDWriteInlineObject* ellipsisSign = nullptr;
                if (fonts_->Factory() &&
                    SUCCEEDED(fonts_->Factory()->CreateEllipsisTrimmingSign(textLayout, &ellipsisSign))) {
                    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                    textLayout->SetTrimming(&trimming, ellipsisSign);
                    ellipsisSign->Release();
                }
                float textTop = params.rowTop + (params.rowHeight - kSidebarRowFontSizeDip - 4.0f) * 0.5f;
                target_->DrawTextLayout(D2D1::Point2F(params.rowLeft + kSidebarPanelPaddingDip, textTop),
                                        textLayout, textBrush,
                                        D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
                textLayout->Release();
            }
        }
    }

    // 3. 悬浮操作按钮与底色遮罩
    if (params.isHover && textBrush && params.buttonMask != kSidebarItemBtnNone) {
        float rowRight = params.rowLeft + params.rowWidth;
        float rowCenterY = params.rowTop + params.rowHeight * 0.5f;
        // 排布算法(第 slotIndex 个槽位的几何中心在哪)是本系统自己的
        // "从右向左分槽位"公式——与 sidebar.h::SidebarItemButtonSlotRectDip
        // 同一口径(那边是给 window.cpp 命中测试用的 shell 层版本,这里是
        // render 层自己的绘制坐标,两者允许独立存在)。算出中心点后统一交给
        // IconButtonRectDip 得到最终矩形,不再手写 left = right - size。
        auto slotCenterX = [&](u32 slotIndex) {
            float right = rowRight - kSidebarCloseButtonMarginDip -
                          static_cast<float>(slotIndex) * (kSidebarCloseButtonSizeDip + kSidebarFolderButtonGapDip);
            return right - kSidebarCloseButtonSizeDip * 0.5f;
        };

        bool hasClose = (params.buttonMask & kSidebarItemBtnClose) != 0;
        bool hasFolder = (params.buttonMask & kSidebarItemBtnFolder) != 0;
        bool hasNewWindow = (params.buttonMask & kSidebarItemBtnNewWindow) != 0;

        IconButton closeBtn{kSidebarCloseButtonSizeDip, 0.0f, L"关闭", &Renderer::PaintSidebarCloseIcon, nullptr};
        IconButton folderBtn{kSidebarCloseButtonSizeDip, 0.0f, L"打开所在文件夹", &Renderer::PaintSidebarFolderIcon,
                             nullptr};
        IconButton newWindowBtn{kSidebarCloseButtonSizeDip, 0.0f, L"新窗口打开", &Renderer::PaintSidebarNewWindowIcon,
                                nullptr};
        ButtonRectDip closeRect{}, folderRect{}, newWindowRect{};
        float coverLeft = rowRight;

        // 槽位按"关闭 -> 文件夹 -> 新窗口"的优先级从右向左依次分配,只有
        // 存在的按钮才占用一个槽位——与 sidebar.h 里各按钮矩形计算函数
        // (SidebarCloseButtonLocalRectDip 等,均固定引用槽位 0/1)的口径一致。
        u32 slot = 0;
        if (hasClose) {
            closeRect = IconButtonRectDip(closeBtn, slotCenterX(slot++), rowCenterY);
            coverLeft = closeRect.left;
        }
        if (hasFolder) {
            folderRect = IconButtonRectDip(folderBtn, slotCenterX(slot++), rowCenterY);
            coverLeft = folderRect.left;
        }
        if (hasNewWindow) {
            newWindowRect = IconButtonRectDip(newWindowBtn, slotCenterX(slot++), rowCenterY);
            coverLeft = newWindowRect.left;
        }

        // 遮罩底色：覆盖右侧按钮区域下方的文字，避免文字与矢量图标重叠
        if (buttonBgBrush) {
            target_->FillRectangle(
                D2D1::RectF(coverLeft - 4.0f, params.rowTop,
                            rowRight - kSidebarPanelPaddingDip * 0.5f,
                            params.rowTop + params.rowHeight),
                buttonBgBrush);
        }

        // 图标内容经各自的 paintIcon 回调画出(hover 遮罩/矩形几何已经由
        // Button 抽象承载,这里只负责把回调接上)。
        if (hasClose && closeBtn.paintIcon) closeBtn.paintIcon(this, closeRect, textBrush);
        if (hasFolder && folderBtn.paintIcon) folderBtn.paintIcon(this, folderRect, textBrush);
        if (hasNewWindow && newWindowBtn.paintIcon) newWindowBtn.paintIcon(this, newWindowRect, textBrush);
    }
}

void Renderer::DrawHistoryPanel(float targetWidth, float targetHeight,
                                ID2D1SolidColorBrush* bgBrush, ID2D1SolidColorBrush* textBrush,
                                ID2D1SolidColorBrush* highlightBgBrush,
                                ID2D1SolidColorBrush* buttonBgBrush,
                                ID2D1SolidColorBrush* scrollbarTrackBrush,
                                ID2D1SolidColorBrush* scrollbarThumbBrush) {
    // 同上:count==0 不再直接 return,空历史也要能画出侧栏骨架 + "No Records"。
    if (!overlay_ || !overlay_->historyEntries) return;
    if (!fonts_ || !bgBrush || !textBrush || !target_) return;

    float animProgress = overlay_->historyAnimProgress;
    if (animProgress <= 0.0001f) return;

    float panelWidth = overlay_->historyPanelWidthDip;
    float slideOffset = 0.0f;
    if (animProgress < 1.0f) {
        slideOffset = (1.0f - animProgress) * panelWidth;
    }

    target_->SetTransform(D2D1::Matrix3x2F::Translation(slideOffset + (targetWidth - panelWidth), 0.0f));

    D2D1_RECT_F panelRect = D2D1::RectF(0.0f, 0.0f, panelWidth, targetHeight);
    target_->FillRectangle(panelRect, bgBrush);

    if (highlightBgBrush) {
        target_->DrawLine(D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(0.0f, targetHeight),
                          highlightBgBrush, 1.0f);
    }

    // 标题栏("历史记录"),占据顶部 kSidebarHeaderHeightDip 高度——这段高度
    // 同样是 SidebarHitTestItem 命中测试跳过的区域,行必须从这个高度之后
    // 开始画,否则视觉行位置与命中判定的行位置会错位一段距离。
    {
        static const wchar_t kHistoryHeaderText[] = L"历史记录";
        u32 headerLen = static_cast<u32>(wcslen(kHistoryHeaderText));
        IDWriteTextLayout* headerLayout = fonts_->CreateTextLayout(
            kHistoryHeaderText, headerLen, FontRole::Body,
            panelWidth - kSidebarPanelPaddingDip * 2.0f, kSidebarHeaderHeightDip);
        if (headerLayout) {
            headerLayout->SetFontSize(kSidebarHeaderFontSizeDip, DWRITE_TEXT_RANGE{0, headerLen});
            float textTop = (kSidebarHeaderHeightDip - kSidebarHeaderFontSizeDip - 4.0f) * 0.5f;
            target_->DrawTextLayout(D2D1::Point2F(kSidebarPanelPaddingDip, textTop),
                                    headerLayout, textBrush,
                                    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            headerLayout->Release();
        }
        target_->DrawLine(D2D1::Point2F(0.0f, kSidebarHeaderHeightDip),
                          D2D1::Point2F(panelWidth, kSidebarHeaderHeightDip),
                          textBrush, 1.0f);
    }

    // 文案永远用满宽度——关闭/文件夹按钮只在 hover 时无背景色地悬浮在文案
    // 之上,不为它们预留固定空间(否则未 hover 的行会白白截断文案)。
    float maxTextWidth = panelWidth - kSidebarPanelPaddingDip * 2.0f;

    // 历史记录为空:侧栏标题栏之下的整片区域居中画一行提示,不进入下面的
    // 逐行循环(count 为 0,循环体本就不会执行,这里只是补一行视觉反馈)。
    if (overlay_->historyItemCount == 0) {
        static const wchar_t kNoRecordsText[] = L"No Records";
        u32 noRecordsLen = static_cast<u32>(wcslen(kNoRecordsText));
        float emptyAreaTop = kSidebarHeaderHeightDip;
        float emptyAreaHeight = targetHeight - emptyAreaTop;
        IDWriteTextLayout* emptyLayout = fonts_->CreateTextLayout(
            kNoRecordsText, noRecordsLen, FontRole::Body, maxTextWidth, emptyAreaHeight);
        if (emptyLayout) {
            emptyLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            emptyLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            target_->DrawTextLayout(D2D1::Point2F(kSidebarPanelPaddingDip, emptyAreaTop),
                                    emptyLayout, textBrush,
                                    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            emptyLayout->Release();
        }
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        return;
    }

    for (u32 i = 0; i < overlay_->historyItemCount; ++i) {
        float rowTop = kSidebarHeaderHeightDip + kSidebarRowHeightDip * static_cast<float>(i) -
                       overlay_->historyScrollY;
        if (rowTop + kSidebarRowHeightDip < kSidebarHeaderHeightDip || rowTop > targetHeight) continue;

        const wchar_t* path = overlay_->historyEntries[i].path;
        u32 pathLen = WideLength(path);

        SidebarListItemParams itemParams{};
        itemParams.rowLeft = 0.0f;
        itemParams.rowTop = rowTop;
        itemParams.rowWidth = panelWidth;
        itemParams.rowHeight = kSidebarRowHeightDip;
        itemParams.text = path;
        itemParams.textLen = pathLen;
        itemParams.isCurrent = false;
        itemParams.isHover = (i == overlay_->historyHoverItem);
        itemParams.buttonMask = kSidebarItemBtnClose | kSidebarItemBtnFolder;

        DrawSidebarListItem(itemParams, textBrush, highlightBgBrush, nullptr, buttonBgBrush);
    }

    float contentHeight = kSidebarHeaderHeightDip +
                          kSidebarRowHeightDip * static_cast<float>(overlay_->historyItemCount);
    DrawScrollbar(panelWidth, targetHeight, contentHeight, overlay_->historyScrollY,
                  scrollbarTrackBrush, scrollbarThumbBrush);

    target_->SetTransform(D2D1::Matrix3x2F::Identity());
}

void Renderer::DrawEditBoxBorder(D2D1_RECT_F rect, ID2D1SolidColorBrush* borderBrush,
                                  EditBoxBorderStyle style, float strokeWidth) {
    if (!target_ || !borderBrush) return;
    if (style == EditBoxBorderStyle::Borderless) {
        // 无边框:不画任何描边/下划线,靠输入框自身背景色跟容器融为一体。
        return;
    }
    if (style == EditBoxBorderStyle::UnderlineOnly) {
        target_->DrawLine(D2D1::Point2F(rect.left, rect.bottom),
                          D2D1::Point2F(rect.right, rect.bottom), borderBrush, strokeWidth);
        return;
    }
    D2D1_ROUNDED_RECT rounded{rect, 4.0f, 4.0f};
    target_->DrawRoundedRectangle(rounded, borderBrush, strokeWidth);
}

// 侧栏头部"打开根目录文件夹"按钮图标:与行内文件夹图标同一个视觉家族,
// 但热区更大(24dip)、图标视觉本身缩小并做了形状比例微调——保留独立的
// 绘制函数,不套用 PaintSidebarFolderIcon(避免为了共用而扭曲既有视觉)。
void Renderer::PaintSidebarHeaderFolderIcon(void* renderCtx, const ButtonRectDip& rect, void* userData) {
    Renderer* self = static_cast<Renderer*>(renderCtx);
    ID2D1SolidColorBrush* textBrush = static_cast<ID2D1SolidColorBrush*>(userData);
    if (!self || !self->target_ || !textBrush) return;
    ID2D1HwndRenderTarget* target_ = self->target_;
    // 按钮热区仍是24x24，图标视觉再缩小到12px((24-12)/2=6.0)，在按钮内居中
    float hPad = 6.0f;
    float hBodyLeft = rect.left + hPad;
    float hBodyRight = rect.right - hPad;
    // 标签凸起偏移量按图标缩小比例(12/14)在上一轮2.1f基础上继续等比例收窄，保持形状比例不变
    float hBodyTop = rect.top + hPad + 1.8f;
    float hBodyBottom = rect.bottom - hPad;
    target_->DrawRectangle(D2D1::RectF(hBodyLeft, hBodyTop, hBodyRight, hBodyBottom), textBrush, 1.2f);
    float hTabWidth = (hBodyRight - hBodyLeft) * 0.45f;
    target_->DrawLine(D2D1::Point2F(hBodyLeft, hBodyTop),
                      D2D1::Point2F(hBodyLeft + hTabWidth * 0.5f, hBodyTop - 1.8f), textBrush, 1.2f);
    target_->DrawLine(D2D1::Point2F(hBodyLeft + hTabWidth * 0.5f, hBodyTop - 1.8f),
                      D2D1::Point2F(hBodyLeft + hTabWidth, hBodyTop), textBrush, 1.2f);
}

void Renderer::DrawFolderPanel(float targetWidth, float targetHeight,
                               ID2D1SolidColorBrush* bgBrush, ID2D1SolidColorBrush* textBrush,
                               ID2D1SolidColorBrush* highlightBgBrush,
                               ID2D1SolidColorBrush* currentItemBrush,
                               ID2D1SolidColorBrush* buttonBgBrush,
                               ID2D1SolidColorBrush* scrollbarTrackBrush,
                               ID2D1SolidColorBrush* scrollbarThumbBrush,
                               ID2D1SolidColorBrush* tooltipBgBrush,
                               ID2D1SolidColorBrush* tooltipTextBrush) {
    // 注意:folderEntries 为空文件夹时可能是 nullptr,不能拿它当"侧栏是否
    // 打开"的判据,否则空文件夹侧栏本身也画不出来(真实 bug 教训)。
    if (!overlay_) return;
    if (!fonts_ || !bgBrush || !textBrush || !target_) return;

    float animProgress = overlay_->folderAnimProgress;
    if (animProgress <= 0.0001f) return;

    // 挤压模式的常驻面板,不是盖住一切的悬浮抽屉,高度要跟正文一样让出
    // 底部栏那一条,否则侧栏自己的背景/列表会把底部栏(唯一能收起它的
    // 入口)盖在下面。这里之后的 targetHeight 全部指"侧栏可用高度"。
    targetHeight -= kBottomBarHeightDip;
    if (targetHeight < 0.0f) targetHeight = 0.0f;

    float panelWidth = overlay_->folderPanelWidthDip;
    float slideOffset = 0.0f;
    if (animProgress < 1.0f) {
        slideOffset = (animProgress - 1.0f) * panelWidth;
    }

    target_->SetTransform(D2D1::Matrix3x2F::Translation(slideOffset, 0.0f));

    // 侧栏面板底色与右边框
    D2D1_RECT_F panelRect = D2D1::RectF(0.0f, 0.0f, panelWidth, targetHeight);
    target_->FillRectangle(panelRect, bgBrush);

    if (highlightBgBrush) {
        target_->DrawLine(D2D1::Point2F(panelWidth, 0.0f), D2D1::Point2F(panelWidth, targetHeight),
                          highlightBgBrush, 1.0f);
    }

    u32 displayCount = overlay_->folderFilteredIndices ? overlay_->folderFilteredCount : overlay_->folderEntryCount;

    // 面板内所有图标按钮(头部按钮 + 行内按钮)共用的悬浮标题气泡状态:谁悬浮
    // 记谁的矩形/文案,任意时刻至多一个按钮悬浮(悬浮态互斥,见 window.cpp),
    // 真正的绘制统一放到本函数最后(裁剪区域之外、行列表画完之后)一次性画,
    // 保证气泡永远在本面板最上层,不会被后画的过滤框/行列表盖住。
    bool hoverButtonValid = false;
    SidebarRectDip hoverButtonRect{};
    const wchar_t* hoverButtonLabel = nullptr;

    // 顶部工具栏（分两行，总高度 kFolderSidebarHeaderHeightDip = 72.0f）
    // 第一行：文件夹标题 + 右侧"打开所在根目录"文件夹按钮 (0 ~ 36.0f)
    {
        wchar_t headerText[128];
        const wchar_t* rootName = (overlay_->folderRootName && overlay_->folderRootName[0] != 0)
                                      ? overlay_->folderRootName
                                      : L"文档列表";
        if (overlay_->folderFilteredIndices && overlay_->folderFilteredCount < overlay_->folderEntryCount) {
            swprintf_s(headerText, L"文件夹: %s (%u/%u)", rootName, overlay_->folderFilteredCount, overlay_->folderEntryCount);
        } else {
            swprintf_s(headerText, L"文件夹: %s (%u)", rootName, overlay_->folderEntryCount);
        }
        u32 headerLen = static_cast<u32>(wcslen(headerText));
        float titleMaxWidth = panelWidth - kSidebarPanelPaddingDip - 28.0f;
        IDWriteTextLayout* headerLayout = fonts_->CreateTextLayout(
            headerText, headerLen, FontRole::Body,
            titleMaxWidth > 10.0f ? titleMaxWidth : 10.0f, kFolderHeaderRow1HeightDip);
        if (headerLayout) {
            headerLayout->SetFontSize(kSidebarHeaderFontSizeDip, DWRITE_TEXT_RANGE{0, headerLen});
            headerLayout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_RANGE{0, headerLen});
            headerLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            IDWriteInlineObject* headerEllipsis = nullptr;
            if (fonts_->Factory() &&
                SUCCEEDED(fonts_->Factory()->CreateEllipsisTrimmingSign(headerLayout, &headerEllipsis))) {
                DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                headerLayout->SetTrimming(&trimming, headerEllipsis);
                headerEllipsis->Release();
            }
            float textTop = (kFolderHeaderRow1HeightDip - kSidebarHeaderFontSizeDip - 4.0f) * 0.5f;
            target_->DrawTextLayout(D2D1::Point2F(kSidebarPanelPaddingDip, textTop),
                                    headerLayout, textBrush,
                                    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            headerLayout->Release();
        }

        // 第一行右侧"打开根目录"文件夹按钮:矩形几何来自 sidebar.h 的
        // SidebarFolderHeaderButtonLocalRectDip(该函数内部已经统一经
        // IconButtonRectDip 构造),这里只需要构造一个 IconButton 把
        // paintIcon 接上,图标内容通过回调画出。
        IconButton headerBtn{kSidebarFolderHeaderButtonSizeDip, 0.0f, L"打开根目录文件夹",
                             &Renderer::PaintSidebarHeaderFolderIcon, nullptr};
        SidebarRectDip headerBtnRectRaw = SidebarFolderHeaderButtonLocalRectDip(panelWidth);
        ButtonRectDip headerBtnRect{headerBtnRectRaw.left, headerBtnRectRaw.top, headerBtnRectRaw.right,
                                    headerBtnRectRaw.bottom};
        if (overlay_->folderHeaderButtonHover && highlightBgBrush) {
            target_->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(headerBtnRect.left, headerBtnRect.top,
                                              headerBtnRect.right, headerBtnRect.bottom),
                                  4.0f, 4.0f),
                highlightBgBrush);
        }
        if (headerBtn.paintIcon) headerBtn.paintIcon(this, headerBtnRect, textBrush);

        // 头部按钮的悬浮标题气泡不在这里直接画:面板后面还要画过滤框/行列表,
        // 画早了会被后画的内容盖住(实测 bug)。这里只记录悬浮态与矩形,
        // 交给下面统一的"悬浮气泡最后画一次"逻辑(与行内按钮共用同一套
        // hoverButtonValid/hoverButtonRect/hoverButtonLabel,谁悬浮记谁的,
        // 任意时刻至多一个按钮悬浮),保证气泡永远画在本面板最上层。
        if (overlay_->folderHeaderButtonHover) {
            hoverButtonValid = true;
            hoverButtonRect = SidebarRectDip{headerBtnRect.left, headerBtnRect.top, headerBtnRect.right,
                                             headerBtnRect.bottom};
            hoverButtonLabel = L"打开根目录文件夹";
        }
    }

    // 第二行：过滤输入框外框背景 (36.0f ~ 72.0f)
    {
        SidebarRectDip inputRect = SidebarFolderFilterInputLocalRectDip(panelWidth);
        D2D1_ROUNDED_RECT roundedBox{
            D2D1::RectF(inputRect.left, inputRect.top, inputRect.right, inputRect.bottom),
            4.0f, 4.0f
        };
        // 改为无边框样式:不画任何描边/下划线,靠输入框背景色跟侧栏面板融为一体
        DrawEditBoxBorder(roundedBox.rect, textBrush, EditBoxBorderStyle::Borderless, 0.8f);

        // 抽屉正在动画中或 Win32 原生输入框未显示时，绘制占位/当前文字以保证视觉连续平滑
        if (animProgress < 0.999f) {
            const wchar_t* qText = (overlay_->folderFilterQuery && overlay_->folderFilterQuery[0] != 0)
                                       ? overlay_->folderFilterQuery
                                       : L"过滤文件...";
            u32 qLen = static_cast<u32>(wcslen(qText));
            IDWriteTextLayout* qLayout = fonts_->CreateTextLayout(
                qText, qLen, FontRole::Body, inputRect.right - inputRect.left - 12.0f, inputRect.bottom - inputRect.top);
            if (qLayout) {
                qLayout->SetFontSize(kSidebarRowFontSizeDip, DWRITE_TEXT_RANGE{0, qLen});
                float qTop = inputRect.top + (inputRect.bottom - inputRect.top - kSidebarRowFontSizeDip - 4.0f) * 0.5f;
                target_->DrawTextLayout(D2D1::Point2F(inputRect.left + 6.0f, qTop), qLayout, textBrush,
                                        D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
                qLayout->Release();
            }
        }

        // 第二行底部与列表内容区的分割线
        target_->DrawLine(D2D1::Point2F(0.0f, kFolderSidebarHeaderHeightDip),
                          D2D1::Point2F(panelWidth, kFolderSidebarHeaderHeightDip),
                          textBrush, 0.8f);
    }

    float maxTextWidth = panelWidth - kSidebarPanelPaddingDip * 2.0f;

    if (displayCount == 0) {
        const wchar_t* emptyText = (overlay_->folderEntryCount == 0) ? L"未找到 Markdown 文件" : L"无匹配文件";
        u32 emptyLen = static_cast<u32>(wcslen(emptyText));
        float emptyAreaTop = kFolderSidebarHeaderHeightDip;
        float emptyAreaHeight = targetHeight - emptyAreaTop;
        IDWriteTextLayout* emptyLayout = fonts_->CreateTextLayout(
            emptyText, emptyLen, FontRole::Body, maxTextWidth, emptyAreaHeight);
        if (emptyLayout) {
            emptyLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            emptyLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            target_->DrawTextLayout(D2D1::Point2F(kSidebarPanelPaddingDip, emptyAreaTop),
                                    emptyLayout, textBrush,
                                    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            emptyLayout->Release();
        }
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        return;
    }

    // 裁剪区域：行只在标题栏下到视口底部之间可见
    D2D1_RECT_F clipRect = D2D1::RectF(0.0f, kFolderSidebarHeaderHeightDip, panelWidth, targetHeight);
    target_->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_ALIASED);

    // hover 行的完整路径提示气泡要画在裁剪区域之外(否则会被行列表的裁剪矩形
    // 截断),这里先记下 hover 行的几何与全路径文本，等 PopAxisAlignedClip 后
    // 再画。
    bool hoverRowValid = false;
    float hoverRowTop = 0.0f;
    const wchar_t* hoverFullPath = nullptr;
    u32 hoverFullPathLen = 0;

    // 行内"打开所在文件夹"/"新窗口打开"按钮的悬浮标题小气泡同样要画在裁剪
    // 区域之外——hoverButtonValid/Rect/Label 是函数开头就声明的共享状态
    // (头部按钮悬浮时可能已经设过一次),这里按需覆盖成当前 hover 行的按钮。
    float baseTop = kFolderSidebarHeaderHeightDip - overlay_->folderScrollY;
    for (u32 i = 0; i < displayCount; ++i) {
        float rowTop = baseTop + static_cast<float>(i) * kSidebarRowHeightDip;
        float rowBottom = rowTop + kSidebarRowHeightDip;
        if (rowBottom < kFolderSidebarHeaderHeightDip || rowTop > targetHeight) continue;

        u32 realIdx = overlay_->folderFilteredIndices ? overlay_->folderFilteredIndices[i] : i;
        bool isCurrent = (realIdx == overlay_->folderCurrentItem);
        bool isHover = (i == overlay_->folderHoverItem);

        const FolderEntry& entry = overlay_->folderEntries[realIdx];
        u32 pathLen = static_cast<u32>(wcslen(entry.relPath));

        SidebarListItemParams itemParams{};
        itemParams.rowLeft = 0.0f;
        itemParams.rowTop = rowTop;
        itemParams.rowWidth = panelWidth;
        itemParams.rowHeight = kSidebarRowHeightDip;
        itemParams.text = entry.relPath;
        itemParams.textLen = pathLen;
        itemParams.isCurrent = isCurrent;
        itemParams.isHover = isHover;
        itemParams.buttonMask = kSidebarItemBtnFolder | kSidebarItemBtnNewWindow;

        DrawSidebarListItem(itemParams, textBrush, highlightBgBrush, currentItemBrush, buttonBgBrush);

        if (isHover) {
            hoverRowValid = true;
            hoverRowTop = rowTop;
            hoverFullPath = entry.fullPath;
            hoverFullPathLen = static_cast<u32>(wcslen(entry.fullPath));

            if (overlay_->folderItemNewWindowButtonHover) {
                hoverButtonValid = true;
                hoverButtonRect = SidebarFolderItemNewWindowButtonLocalRectDip(
                    panelWidth, i, overlay_->folderScrollY);
                hoverButtonLabel = L"新窗口打开";
            } else if (overlay_->folderItemFolderButtonHover) {
                hoverButtonValid = true;
                hoverButtonRect =
                    SidebarFolderItemButtonLocalRectDip(panelWidth, i, overlay_->folderScrollY);
                hoverButtonLabel = L"打开所在文件夹";
            }
        }
    }

    target_->PopAxisAlignedClip();

    // hover 行完整绝对路径提示气泡：自适应宽度(不超过 kFolderTooltipMaxWidthDip)、
    // 超宽自动换行，不做单行省略号截断——与正文里省略号截断的路径展示区分开。
    // 气泡底色/文字色改用 tooltipBgBrush/tooltipTextBrush(与窗口内提示条
    // overlayBar 共用同一套"主题反差半透明深色气泡"配色)而不是侧栏自己的
    // bgBrush/textBrush——侧栏底色下的气泡几乎融进背景,起不到提示效果。
    if (hoverRowValid && fonts_ && tooltipBgBrush && tooltipTextBrush && hoverFullPathLen > 0) {
        IDWriteTextLayout* tipLayout = fonts_->CreateTextLayout(
            hoverFullPath, hoverFullPathLen, FontRole::Body, kFolderTooltipMaxWidthDip, 400.0f);
        if (tipLayout) {
            tipLayout->SetFontSize(kFolderTooltipFontSizeDip, DWRITE_TEXT_RANGE{0, hoverFullPathLen});
            DWRITE_TEXT_METRICS metrics{};
            tipLayout->GetMetrics(&metrics);
            float bubbleWidth = metrics.width + kFolderTooltipPaddingDip * 2.0f;
            float bubbleHeight = metrics.height + kFolderTooltipPaddingDip * 2.0f;

            // 水平方向:气泡左边对齐列表项的水平中点(而不是贴左边),看起来
            // 像是从行中间"弹出来"的;超出面板宽度也没关系——挤压模式下
            // 侧栏右边就是正文,不像悬浮蒙层那样怕盖住别的东西,只需要不
            // 越过屏幕右边缘。
            // 水平方向的左右边距不对称(左边贴 0,右边留屏幕边距),不是
            // ClampTooltipLeftDip 覆盖的"左右同一 margin"情形,这里保留原样
            // 两步钳制。
            float bubbleLeft = panelWidth * 0.5f;
            if (bubbleLeft + bubbleWidth > targetWidth - kFolderTooltipScreenMarginDip) {
                bubbleLeft = targetWidth - kFolderTooltipScreenMarginDip - bubbleWidth;
            }
            if (bubbleLeft < 0.0f) bubbleLeft = 0.0f;

            // 竖直方向出现在该行上方,与行之间留一条小缝隙;若行本身太靠近
            // 顶部、上方放不下,退化成显示在行下方,保证气泡始终完整可见。
            TooltipVerticalDip bubbleV = TooltipBubbleVerticalDip(
                hoverRowTop, hoverRowTop + kSidebarRowHeightDip, bubbleHeight, kFolderTooltipGapDip,
                kFolderSidebarHeaderHeightDip, targetHeight - kFolderTooltipScreenMarginDip);
            float bubbleTop = bubbleV.top;

            D2D1_ROUNDED_RECT bubble = D2D1::RoundedRect(
                D2D1::RectF(bubbleLeft, bubbleTop, bubbleLeft + bubbleWidth, bubbleTop + bubbleHeight),
                4.0f, 4.0f);
            target_->FillRoundedRectangle(bubble, tooltipBgBrush);
            target_->DrawTextLayout(
                D2D1::Point2F(bubbleLeft + kFolderTooltipPaddingDip, bubbleTop + kFolderTooltipPaddingDip),
                tipLayout, tooltipTextBrush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            tipLayout->Release();
        }
    }

    // 行内"打开所在文件夹"/"新窗口打开"按钮的悬浮标题小气泡——单行、固定
    // 高度,画法与底部栏图标的悬浮提示(kBottomBarTooltip*)同一套,只是
    // 位置换成按钮正上方、水平居中于按钮。
    if (hoverButtonValid && fonts_ && tooltipBgBrush && tooltipTextBrush && hoverButtonLabel) {
        u32 labelLen = WideLength(hoverButtonLabel);
        IDWriteTextLayout* btnTipLayout =
            fonts_->CreateTextLayout(hoverButtonLabel, labelLen, FontRole::Body, 200.0f,
                                     kBottomBarTooltipHeightDip);
        if (btnTipLayout) {
            btnTipLayout->SetFontSize(kBottomBarTooltipFontSizeDip, DWRITE_TEXT_RANGE{0, labelLen});
            DWRITE_TEXT_METRICS metrics{};
            btnTipLayout->GetMetrics(&metrics);
            float bubbleWidth = metrics.width + kBottomBarTooltipPaddingDip * 2.0f;
            float buttonCenterX = (hoverButtonRect.left + hoverButtonRect.right) * 0.5f;
            float bubbleLeft = ClampTooltipLeftDip(buttonCenterX - bubbleWidth * 0.5f, bubbleWidth,
                                                    targetWidth, kFolderTooltipScreenMarginDip);
            // 按钮太靠近顶部、上方放不下时,退化到按钮下方(与行完整路径气泡
            // 同一套 TooltipBubbleVerticalDip 定位逻辑)。
            TooltipVerticalDip bubbleV =
                TooltipBubbleVerticalDip(hoverButtonRect.top, hoverButtonRect.bottom,
                                         kBottomBarTooltipHeightDip, kBottomBarTooltipGapDip,
                                         kFolderSidebarHeaderHeightDip);
            float bubbleTop = bubbleV.top;
            float bubbleBottom = bubbleV.bottom;
            D2D1_ROUNDED_RECT bubble = D2D1::RoundedRect(
                D2D1::RectF(bubbleLeft, bubbleTop, bubbleLeft + bubbleWidth, bubbleBottom), 4.0f, 4.0f);
            target_->FillRoundedRectangle(bubble, tooltipBgBrush);
            target_->DrawTextLayout(
                D2D1::Point2F(bubbleLeft + kBottomBarTooltipPaddingDip,
                              bubbleTop + (kBottomBarTooltipHeightDip - metrics.height) * 0.5f),
                btnTipLayout, tooltipTextBrush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            btnTipLayout->Release();
        }
    }

    float contentHeight = kFolderSidebarHeaderHeightDip +
                          kSidebarRowHeightDip * static_cast<float>(displayCount);
    DrawScrollbar(panelWidth, targetHeight, contentHeight, overlay_->folderScrollY,
                  scrollbarTrackBrush, scrollbarThumbBrush);

    target_->SetTransform(D2D1::Matrix3x2F::Identity());
}

// 按给定 SVG 路径数据(viewBox 0 0 24 24,全直线段,无曲线)构建"放大"/"缩小"
// 图标的填充几何。三个子轮廓(字母 "A" 外形 + "A" 内部三角镂空 + 右下角
// "+"/"-" 号)按 SVG 原始点序原样搬入,镂空子轮廓的绕向与外轮廓相反,
// nonzero 缠绕规则下天然抠出镂空,不需要额外布尔运算。
ID2D1PathGeometry* Renderer::BuildZoomFontIconGeometry(bool zoomIn) {
    if (!factory_) return nullptr;
    ID2D1PathGeometry* geo = nullptr;
    if (FAILED(factory_->CreatePathGeometry(&geo)) || !geo) return nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(geo->Open(&sink)) || !sink) {
        geo->Release();
        return nullptr;
    }
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);

    auto figure = [&](const D2D1_POINT_2F* pts, u32 count) {
        sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLines(pts + 1, count - 1);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    };

    if (zoomIn) {
        const D2D1_POINT_2F outerA[] = {
            {1.0f, 19.0f},    {6.25f, 5.0f},   {8.75f, 5.0f},    {14.0f, 19.0f},
            {11.6f, 19.0f},   {10.325f, 15.425f}, {4.675f, 15.425f}, {3.4f, 19.0f},
        };
        figure(outerA, static_cast<u32>(sizeof(outerA) / sizeof(outerA[0])));

        const D2D1_POINT_2F holeA[] = {
            {5.4f, 13.4f}, {9.6f, 13.4f}, {7.55f, 7.6f}, {7.45f, 7.6f},
        };
        figure(holeA, static_cast<u32>(sizeof(holeA) / sizeof(holeA[0])));

        const D2D1_POINT_2F plus[] = {
            {18.0f, 16.0f}, {18.0f, 13.0f}, {15.0f, 13.0f}, {15.0f, 11.0f},
            {18.0f, 11.0f}, {18.0f, 8.0f},  {20.0f, 8.0f},  {20.0f, 11.0f},
            {23.0f, 11.0f}, {23.0f, 13.0f}, {20.0f, 13.0f}, {20.0f, 16.0f},
        };
        figure(plus, static_cast<u32>(sizeof(plus) / sizeof(plus[0])));
    } else {
        const D2D1_POINT_2F outerA[] = {
            {0.99f, 19.0f}, {3.41f, 19.0f}, {4.68f, 15.42f}, {10.33f, 15.42f},
            {11.59f, 19.0f}, {14.01f, 19.0f}, {8.75f, 5.0f}, {6.25f, 5.0f},
        };
        figure(outerA, static_cast<u32>(sizeof(outerA) / sizeof(outerA[0])));

        const D2D1_POINT_2F holeA[] = {
            {5.41f, 13.39f}, {7.44f, 7.6f}, {7.56f, 7.6f}, {9.59f, 13.39f},
        };
        figure(holeA, static_cast<u32>(sizeof(holeA) / sizeof(holeA[0])));

        const D2D1_POINT_2F minus[] = {
            {23.0f, 11.0f}, {23.0f, 13.0f}, {15.0f, 13.0f}, {15.0f, 11.0f},
        };
        figure(minus, static_cast<u32>(sizeof(minus) / sizeof(minus[0])));
    }

    sink->Close();
    sink->Release();
    return geo;
}

// 传给 Renderer::PaintBottomBarIcon 的 userData:底部栏图标画法按下标分派
// (原先内联在 DrawBottomBar 里的一整套 switch),需要知道画的是第几个按钮、
// 复制路径按钮是否处于"已复制"态、以及两支画笔。
struct BottomBarIconPaintCtx {
    u32 index;
    bool pathCopied;
    ID2D1SolidColorBrush* iconBrush;
    ID2D1SolidColorBrush* bgBrush;
};

// 底部栏各按钮图标内容(手绘矢量图形,零图标字体零位图)。renderCtx 固定是
// 发起绘制的 Renderer* this;rect 是 IconButtonRectDip 算出的按钮矩形——
// 图标本身按原有视觉再从矩形中心收缩一圈画(与迁移前逐像素一致)。
void Renderer::PaintBottomBarIcon(void* renderCtx, const ButtonRectDip& rect, void* userData) {
    Renderer* self = static_cast<Renderer*>(renderCtx);
    auto* ctx = static_cast<BottomBarIconPaintCtx*>(userData);
    if (!self || !self->target_ || !ctx || !ctx->iconBrush) return;

    ID2D1HwndRenderTarget* target_ = self->target_;  // 复用下面搬过来的原始代码,变量名保持一致
    ID2D1SolidColorBrush* iconBrush = ctx->iconBrush;
    ID2D1SolidColorBrush* bgBrush = ctx->bgBrush;
    bool pathCopied = ctx->pathCopied;
    u32 i = ctx->index;
    float centerX = (rect.left + rect.right) * 0.5f;
    float iconCenterY = (rect.top + rect.bottom) * 0.5f;
    // 左侧 8 个按钮用略小一档的图标尺寸(kBottomBarLeftIconSizeDip),
    // 右侧固定按钮群(CopyPath/History)保持原尺寸——两组独立计算 half。
    float half = (i < kBottomBarLeftButtonCount ? kBottomBarLeftIconSizeDip
                                                  : kBottomBarIconSizeDip) * 0.5f;

    switch (static_cast<int>(i)) {
    case 0: {  // 文件列表: List 图标(三行: 左边实心小圆点 + 右边短横线)
        float dotR = 1.0f;
        float lineLeft = centerX - half * 0.15f;
        float lineRight = centerX + half * 0.75f;
        float dotX = centerX - half * 0.55f;
        float lineTop = iconCenterY - half;
        for (int row = 0; row < 3; ++row) {
            float y = lineTop + kBottomBarLeftIconSizeDip * (0.25f + 0.3f * row);
            D2D1_ELLIPSE dot{D2D1::Point2F(dotX, y), dotR, dotR};
            target_->FillEllipse(dot, iconBrush);
            target_->DrawLine(D2D1::Point2F(lineLeft, y),
                              D2D1::Point2F(lineRight, y), iconBrush,
                              kBottomBarIconStrokeWidthDip);
        }
        break;
    }
    case 1: {  // 大纲:三条横线(列表/层级图标)
        float w = half * 1.6f;
        float lineTop = iconCenterY - half;
        for (int line = 0; line < 3; ++line) {
            float y = lineTop + kBottomBarLeftIconSizeDip * (0.25f + 0.3f * line);
            target_->DrawLine(D2D1::Point2F(centerX - w * 0.5f, y),
                              D2D1::Point2F(centerX + w * 0.5f, y), iconBrush,
                              kBottomBarIconStrokeWidthDip);
        }
        break;
    }
    case 2: {  // 打开文件:带折角的文档矩形
        float w = half * 1.3f, h = half * 1.7f;
        float fold = w * 0.35f;  // 右上角折角大小
        D2D1_POINT_2F pts[5] = {
            {centerX - w * 0.5f, iconCenterY - h * 0.5f},
            {centerX + w * 0.5f - fold, iconCenterY - h * 0.5f},
            {centerX + w * 0.5f, iconCenterY - h * 0.5f + fold},
            {centerX + w * 0.5f, iconCenterY + h * 0.5f},
            {centerX - w * 0.5f, iconCenterY + h * 0.5f},
        };
        for (int p = 0; p < 5; ++p) {
            D2D1_POINT_2F next = pts[(p + 1) % 5];
            target_->DrawLine(pts[p], next, iconBrush, kBottomBarIconStrokeWidthDip);
        }
        // 折角内的斜线,呼应"折角"的视觉细节。
        target_->DrawLine(D2D1::Point2F(centerX + w * 0.5f - fold, iconCenterY - h * 0.5f),
                          D2D1::Point2F(centerX + w * 0.5f, iconCenterY - h * 0.5f + fold),
                          iconBrush, kBottomBarIconStrokeWidthDip);
        // 文档内两条短横线代表正文。
        target_->DrawLine(D2D1::Point2F(centerX - w * 0.25f, iconCenterY + h * 0.05f),
                          D2D1::Point2F(centerX + w * 0.2f, iconCenterY + h * 0.05f),
                          iconBrush, kBottomBarIconStrokeWidthDip);
        target_->DrawLine(D2D1::Point2F(centerX - w * 0.25f, iconCenterY + h * 0.3f),
                          D2D1::Point2F(centerX + w * 0.2f, iconCenterY + h * 0.3f),
                          iconBrush, kBottomBarIconStrokeWidthDip);
        break;
    }
    case 3: {  // 打开文件夹:文件夹轮廓(矩形 + 顶部小凸起)
        float w = half * 1.7f, h = half * 1.2f;
        D2D1_RECT_F body =
            D2D1::RectF(centerX - w * 0.5f, iconCenterY - h * 0.3f,
                        centerX + w * 0.5f, iconCenterY + h * 0.7f);
        target_->DrawRectangle(body, iconBrush, kBottomBarIconStrokeWidthDip);
        D2D1_RECT_F tab = D2D1::RectF(body.left, body.top - h * 0.35f,
                                       body.left + w * 0.4f, body.top);
        target_->DrawRectangle(tab, iconBrush, kBottomBarIconStrokeWidthDip);
        break;
    }
    case 4: {  // 主题:太阳(圆 + 8条短射线,包含顶部)
        float r = half * 0.45f;
        D2D1_ELLIPSE sun{D2D1::Point2F(centerX, iconCenterY), r, r};
        target_->DrawEllipse(sun, iconBrush, kBottomBarIconStrokeWidthDip);
        float rayLen = half * 0.32f;
        const float kDiag = 0.7071f;
        D2D1_POINT_2F dirs[8] = {
            {0.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, {1.0f, 0.0f},
            {kDiag, -kDiag}, {-kDiag, -kDiag}, {kDiag, kDiag}, {-kDiag, kDiag},
        };
        for (int d = 0; d < 8; ++d) {
            float sx = centerX + dirs[d].x * (r + 1.5f);
            float sy = iconCenterY + dirs[d].y * (r + 1.5f);
            float ex = centerX + dirs[d].x * (r + 1.5f + rayLen);
            float ey = iconCenterY + dirs[d].y * (r + 1.5f + rayLen);
            target_->DrawLine(D2D1::Point2F(sx, sy), D2D1::Point2F(ex, ey), iconBrush,
                              kBottomBarIconStrokeWidthDip);
        }
        break;
    }
    case 5:    // 缩小:给定 SVG("A" + 右下角 "-")的精确复刻几何
    case 6: {  // 放大:给定 SVG("A" + 右下角 "+")的精确复刻几何,
               // 首次用到才建、按角色缓存复用(见 zoomInIconGeometry_ /
               // zoomOutIconGeometry_ 的注释)。
        bool isZoomIn = (i == 6);
        ID2D1PathGeometry*& cached = isZoomIn ? self->zoomInIconGeometry_ : self->zoomOutIconGeometry_;
        if (!cached) cached = self->BuildZoomFontIconGeometry(isZoomIn);
        if (cached && iconBrush) {
            // 原 SVG viewBox 是 24x24,换算到本图标的正方形绘制区(用
            // half*2 而不是固定常量,自动跟随左侧图标缩小 2px 的尺寸)。
            float iconBoxSize = half * 2.0f * 1.35f;
            float svgScale = iconBoxSize / 24.0f;
            float iconLeft = centerX - iconBoxSize * 0.5f;
            float iconTop = iconCenterY - iconBoxSize * 0.5f;
            target_->SetTransform(D2D1::Matrix3x2F::Scale(svgScale, svgScale) *
                                  D2D1::Matrix3x2F::Translation(iconLeft, iconTop));
            target_->FillGeometry(cached, iconBrush);
            target_->SetTransform(D2D1::Matrix3x2F::Identity());
        }
        break;
    }
    case 7: {  // 查找:放大镜(圆 + 右下角短柄),点击唤起查找条
        float r = half * 0.55f;
        D2D1_POINT_2F center = D2D1::Point2F(centerX - half * 0.15f, iconCenterY - half * 0.15f);
        D2D1_ELLIPSE lens{center, r, r};
        target_->DrawEllipse(lens, iconBrush, kBottomBarIconStrokeWidthDip);
        const float kDiag = 0.7071f;
        D2D1_POINT_2F handleStart{center.x + r * kDiag, center.y + r * kDiag};
        D2D1_POINT_2F handleEnd{centerX + half * 0.55f, iconCenterY + half * 0.55f};
        target_->DrawLine(handleStart, handleEnd, iconBrush,
                          kBottomBarIconStrokeWidthDip * 1.2f);
        break;
    }
    case kBottomBarCopyPathIndex: {
        if (pathCopied) {
            // 短暂的"已复制"成功态:一个对勾,与代码块复制按钮的
            // 反馈图案同一画法(两段折线),只是复用底部栏的单色
            // iconBrush,不额外引入绿色刷子。
            D2D1_POINT_2F p1 = D2D1::Point2F(centerX - half * 0.5f, iconCenterY);
            D2D1_POINT_2F p2 = D2D1::Point2F(centerX - half * 0.1f, iconCenterY + half * 0.4f);
            D2D1_POINT_2F p3 = D2D1::Point2F(centerX + half * 0.55f, iconCenterY - half * 0.4f);
            target_->DrawLine(p1, p2, iconBrush, kBottomBarIconStrokeWidthDip * 1.3f);
            target_->DrawLine(p2, p3, iconBrush, kBottomBarIconStrokeWidthDip * 1.3f);
        } else {
            // "复制"图标:与代码块复制按钮同一份画法(DrawCopySheetsGlyph),
            // 前纸用底部栏背景色填实,保证两处图标视觉一致。
            float iconSize = kBottomBarIconSizeDip;
            self->DrawCopySheetsGlyph(centerX - iconSize * 0.5f, iconCenterY - iconSize * 0.5f,
                                iconSize, iconBrush, bgBrush);
        }
        break;
    }
    case kBottomBarHistoryIndex:
    default: {  // 历史记录:时钟图标(圆圈 + 12点/3点表针)
        float r = half * 0.65f;
        D2D1_ELLIPSE clockCircle{D2D1::Point2F(centerX, iconCenterY), r, r};
        target_->DrawEllipse(clockCircle, iconBrush, kBottomBarIconStrokeWidthDip);
        target_->DrawLine(D2D1::Point2F(centerX, iconCenterY),
                          D2D1::Point2F(centerX, iconCenterY - r * 0.5f), iconBrush,
                          kBottomBarIconStrokeWidthDip);
        target_->DrawLine(D2D1::Point2F(centerX, iconCenterY),
                          D2D1::Point2F(centerX + r * 0.45f, iconCenterY), iconBrush,
                          kBottomBarIconStrokeWidthDip);
        break;
    }
    }
}

// 底部栏第 index 个按钮的左边界(DIP)——DrawBottomBar 的图标排布循环与
// DrawBottomBarTooltip 的气泡定位共用同一份三段式布局公式(左侧定宽网格 /
// CopyPath / History 贴右),避免两处独立维护、按钮布局一改就悄悄错位。
float BottomBarButtonLeftDip(u32 index, float targetWidth) {
    if (index < kBottomBarLeftButtonCount) {
        return kBottomBarButtonWidthDip * static_cast<float>(index);
    }
    if (index == kBottomBarCopyPathIndex) {
        return targetWidth - kBottomBarButtonWidthDip * 2.0f;
    }
    return targetWidth - kBottomBarButtonWidthDip;  // History,始终贴最右
}

// 底部操作栏(2026-09-18 改版:左图标 + 右状态):固定占据客户区底部一条
// 32px 高的带,左侧 5 个固定宽度的纯图标按钮紧贴左边排列,右侧最右边是历史按钮,
// 中间是状态文字(当前文档路径 + 大小)。
void Renderer::DrawBottomBar(float targetWidth, float targetHeight,
                              ID2D1SolidColorBrush* bgBrush, ID2D1SolidColorBrush* iconBrush,
                              ID2D1SolidColorBrush* textBrush, ID2D1SolidColorBrush* dividerBrush,
                              const wchar_t* documentPath, u64 documentSizeBytes,
                              bool pathCopied) {
    if (!target_ || !bgBrush) return;
    bool hasDocument = documentPath && documentPath[0] != L'\0';

    float barTop = targetHeight - kBottomBarHeightDip;
    D2D1_RECT_F barRect = D2D1::RectF(0.0f, barTop, targetWidth, targetHeight);
    target_->FillRectangle(barRect, bgBrush);

    // 底部栏顶边 1px 深色分割线,区分内容区与底部操作栏(与滚动条分隔线复用
    // 同一 dividerBrush,同一条"1px 分割线"口径)。
    if (dividerBrush) {
        target_->DrawLine(D2D1::Point2F(0.0f, barTop), D2D1::Point2F(targetWidth, barTop),
                          dividerBrush, 1.0f);
    }

    float btnW = kBottomBarButtonWidthDip;
    float iconCenterY = barTop + kBottomBarHeightDip * 0.5f;  // 图标整体垂直居中于栏高度内

    for (u32 i = 0; i < kBottomBarButtonCount; ++i) {
        // CopyPath 仅当前有打开文档时存在——未打开文档时整个跳过(不占位、
        // 不画、不留分隔线),该区域让给中间的状态文字区。
        if (i == kBottomBarCopyPathIndex && !hasDocument) continue;
        bool isHistory = (i == kBottomBarHistoryIndex);

        float left = BottomBarButtonLeftDip(i, targetWidth);
        float centerX = left + btnW * 0.5f;

        // 分隔线画在:左侧图标按钮之间、状态文字区与右侧固定按钮群之间的
        // 第一条分界、以及(CopyPath 存在时)CopyPath 与 History 之间。
        if (dividerBrush) {
            bool dividerHere = (i > 0 && i < kBottomBarLeftButtonCount) ||
                               (i == kBottomBarLeftButtonCount && hasDocument) || isHistory;
            if (dividerHere) {
                target_->DrawLine(D2D1::Point2F(left, barTop + 4.0f),
                                  D2D1::Point2F(left, targetHeight - 4.0f), dividerBrush,
                                  1.0f);
            }
        }

        if (iconBrush) {
            // "这是一个按钮"这件事(矩形几何 + 图标绘制入口)交给 IconButton
            // 承载:排布算法(上面算出的 centerX/iconCenterY)仍是底部栏自己的,
            // 但最终矩形经 IconButtonRectDip 得到,图标内容经 paintIcon
            // 回调(PaintBottomBarIcon,见文件末尾实现)画出,不再由这里直接
            // 内联一整套 switch。
            IconButton btn{btnW, 0.0f, kBottomBarTooltipLabels[i], &Renderer::PaintBottomBarIcon, nullptr};
            ButtonRectDip iconRect = IconButtonRectDip(btn, centerX, iconCenterY);
            BottomBarIconPaintCtx ctx{i, pathCopied, iconBrush, bgBrush};
            if (btn.paintIcon) btn.paintIcon(this, iconRect, &ctx);
        }
    }

    // 右侧状态区:显示当前文档路径 + 大小
    if (fonts_ && textBrush && documentPath && documentPath[0] != L'\0') {
        wchar_t statusText[MAX_PATH + 40];
        u32 textLen = ComposeBottomBarStatusText(documentPath, documentSizeBytes, statusText,
                                                  MAX_PATH + 40);
        float statusAreaLeft = btnW * static_cast<float>(kBottomBarLeftButtonCount);
        // hasDocument 为 true 时 CopyPath 按钮占用一个额外的右侧位置。
        float statusAreaRight = targetWidth - btnW * (hasDocument ? 2.0f : 1.0f);
        float statusAreaWidth = statusAreaRight - statusAreaLeft - kBottomBarStatusPaddingDip;
        if (statusAreaWidth > 0.0f) {
            IDWriteTextLayout* statusLayout = fonts_->CreateTextLayout(
                statusText, textLen, FontRole::Body, statusAreaWidth, kBottomBarHeightDip);
            if (statusLayout) {
                statusLayout->SetFontSize(kBottomBarStatusFontSizeDip, DWRITE_TEXT_RANGE{0, textLen});
                statusLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                statusLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                target_->DrawTextLayout(D2D1::Point2F(statusAreaLeft, barTop), statusLayout, textBrush,
                                        D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
                statusLayout->Release();
            }
        }
    }
}

void Renderer::DrawBottomBarTooltip(float targetWidth, float targetHeight,
                                     ID2D1SolidColorBrush* bgBrush, ID2D1SolidColorBrush* textBrush,
                                     u32 hoverButtonIndex, bool hasDocument) {
    if (!target_ || !fonts_ || !bgBrush || !textBrush) return;
    if (hoverButtonIndex >= kBottomBarButtonCount) return;
    if (hoverButtonIndex == kBottomBarCopyPathIndex && !hasDocument) return;

    float barTop = targetHeight - kBottomBarHeightDip;
    float buttonCenterX = BottomBarButtonLeftDip(hoverButtonIndex, targetWidth) +
                          kBottomBarButtonWidthDip * 0.5f;

    const wchar_t* label = kBottomBarTooltipLabels[hoverButtonIndex];
    u32 labelLen = WideLength(label);
    IDWriteTextLayout* tipLayout =
        fonts_->CreateTextLayout(label, labelLen, FontRole::Body, 200.0f, kBottomBarTooltipHeightDip);
    if (!tipLayout) return;
    tipLayout->SetFontSize(kBottomBarTooltipFontSizeDip, DWRITE_TEXT_RANGE{0, labelLen});
    DWRITE_TEXT_METRICS metrics{};
    tipLayout->GetMetrics(&metrics);
    float bubbleWidth = metrics.width + kBottomBarTooltipPaddingDip * 2.0f;
    float bubbleLeft = ClampTooltipLeftDip(buttonCenterX - bubbleWidth * 0.5f, bubbleWidth, targetWidth, 4.0f);
    // 底部栏气泡永远画在栏顶边上方,不会像侧栏按钮气泡那样退化到锚点下方
    // (minTopDip 传一个极小值,TooltipBubbleVerticalDip 的下方退化分支永远
    // 不会触发,行为与原实现一致)。
    TooltipVerticalDip bubbleV = TooltipBubbleVerticalDip(
        barTop, barTop, kBottomBarTooltipHeightDip, kBottomBarTooltipGapDip, -1e9f);
    D2D1_ROUNDED_RECT bubble = D2D1::RoundedRect(
        D2D1::RectF(bubbleLeft, bubbleV.top, bubbleLeft + bubbleWidth, bubbleV.bottom), 4.0f, 4.0f);
    target_->FillRoundedRectangle(bubble, bgBrush);
    target_->DrawTextLayout(
        D2D1::Point2F(bubbleLeft + kBottomBarTooltipPaddingDip,
                      bubbleV.top + (kBottomBarTooltipHeightDip - metrics.height) * 0.5f),
        tipLayout, textBrush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    tipLayout->Release();
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

// 传给 Renderer::PaintCodeCopySheetsIcon 的 userData。
struct CodeCopyIconPaintCtx {
    ID2D1SolidColorBrush* strokeBrush;
    ID2D1SolidColorBrush* paperBrush;
};

// 代码块复制按钮默认态图标:两张叠压纸(复用 DrawCopySheetsGlyph,与底部栏
// "复制路径"按钮的默认图标同一份画法,保证两处视觉一致)。
void Renderer::PaintCodeCopySheetsIcon(void* renderCtx, const ButtonRectDip& rect, void* userData) {
    Renderer* self = static_cast<Renderer*>(renderCtx);
    auto* ctx = static_cast<CodeCopyIconPaintCtx*>(userData);
    if (!self || !ctx) return;
    self->DrawCopySheetsGlyph(rect.left, rect.top, rect.Width(), ctx->strokeBrush, ctx->paperBrush);
}

void Renderer::DrawCodeCopyButton(const BlockGeometry& g, u32 blockIndex, float scrollY,
                                   ID2D1SolidColorBrush* iconBrush,
                                   ID2D1SolidColorBrush* hoverBgBrush,
                                   ID2D1SolidColorBrush* paperBrush,
                                   ID2D1SolidColorBrush* doneBrush) {
    if (g.codeCopyButton.width <= 0.0f) return;

    bool hovered = overlay_ && overlay_->copyButtonHoverBlock == blockIndex;
    bool copied = overlay_ && overlay_->copyButtonCopiedBlock == blockIndex;

    // 矩形几何:排布算法(x/y/width/height)来自文档布局引擎(与其它 4 套
    // 按钮系统"各自算锚点"同一个道理,只是这里的锚点是代码块在文档流里的
    // 位置,由 layout 层算出),算出锚点/尺寸之后统一交给 Button 抽象的
    // ButtonRectFromCenterDip 得到最终矩形。
    Button btn{g.codeCopyButton.width, g.codeCopyButton.height, g.codeCopyButton.width * kCopyButtonCornerRadiusRatio,
               nullptr, nullptr, &Renderer::PaintCodeCopySheetsIcon, nullptr};
    float centerX = g.codeCopyButton.x + g.codeCopyButton.width * 0.5f;
    float centerY = (g.codeCopyButton.y - scrollY) + g.codeCopyButton.height * 0.5f;
    ButtonRectDip rect = ButtonRectFromCenterDip(btn, centerX, centerY);
    float left = rect.left;
    float top = rect.top;
    float size = rect.Width();
    D2D1_RECT_F box = D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom);
    D2D1_ROUNDED_RECT roundedBox = D2D1::RoundedRect(box, btn.radius, btn.radius);
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

    if (btn.paintIcon) {
        CodeCopyIconPaintCtx ctx{iconBrush, paperBrush};
        btn.paintIcon(this, rect, &ctx);
    }
}

// "复制"图标本体(T45,2026-09-19 抽成公共函数供底部栏复制路径按钮复用):
// 两张叠压的圆角纸——后面那张只露出左上一角,前面那张先用纸面色填实再描边,
// 叠压关系因此清晰可辨(纯几何,不依赖任何字体字形)。
void Renderer::DrawCopySheetsGlyph(float left, float top, float size,
                                    ID2D1SolidColorBrush* strokeBrush,
                                    ID2D1SolidColorBrush* paperBrush) {
    if (!target_ || !strokeBrush || size <= 0.0f) return;

    float stroke = size * kCopyStrokeWidthRatio;
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

    target_->DrawRoundedRectangle(backSheet, strokeBrush, stroke);
    if (paperBrush) target_->FillRoundedRectangle(frontSheet, paperBrush);
    target_->DrawRoundedRectangle(frontSheet, strokeBrush, stroke);
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
    target_->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), labelLayout, markerBrush,
                             D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
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
    target_->DrawTextLayout(D2D1::Point2F(g.indent, g.top - scrollY), labelLayout, textBrush,
                             D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
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
        D2D1::Point2F(rect.left + kImagePlaceholderTextPaddingDip, textTop), layout, textBrush,
        D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
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
                            layout, badgeTextBrush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
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

void Renderer::DrawSelectionHighlights(const BlockGeometry& g, u32 blockIndex, float scrollY,
                                        ID2D1SolidColorBrush* fillBrush) {
    if (!overlay_ || !overlay_->selectionActive || !fillBrush) return;
    if (!g.textLayout) return;
    if (blockIndex < overlay_->selStartBlock || blockIndex > overlay_->selEndBlock) return;

    // DWRITE_TEXT_METRICS 没有直接的"文本总长度"字段,借道 HitTestPoint 打在
    // 一个远超文本范围的坐标上(必落在最后一个字符的尾随边),取
    // textPosition + length 反推出整块文本的 UTF-16 长度。
    u32 blockLen = 0xFFFFFFFFu;
    {
        BOOL trailing = FALSE;
        BOOL inside = FALSE;
        DWRITE_HIT_TEST_METRICS endMetrics{};
        if (SUCCEEDED(g.textLayout->HitTestPoint(1.0e6f, 1.0e6f, &trailing, &inside, &endMetrics))) {
            blockLen = endMetrics.textPosition + endMetrics.length;
        }
    }

    // 起止块各自裁到选区在本块内的偏移,中间块整块高亮(见类头注释)。
    u32 position = (blockIndex == overlay_->selStartBlock) ? overlay_->selStartOffset : 0;
    u32 end = (blockIndex == overlay_->selEndBlock) ? overlay_->selEndOffset : blockLen;
    if (end <= position) return;
    u32 length = end - position;

    DWRITE_HIT_TEST_METRICS hitMetrics[kMaxFindHitMetrics];
    UINT32 actualCount = 0;
    HRESULT hr = g.textLayout->HitTestTextRange(position, length, TextDrawLeft(g),
                                                 TextDrawTop(g) - scrollY, hitMetrics,
                                                 kMaxFindHitMetrics, &actualCount);
    if (FAILED(hr)) return;

    UINT32 count = actualCount < kMaxFindHitMetrics ? actualCount : kMaxFindHitMetrics;
    for (UINT32 k = 0; k < count; ++k) {
        target_->FillRectangle(
            D2D1::RectF(hitMetrics[k].left, hitMetrics[k].top,
                        hitMetrics[k].left + hitMetrics[k].width,
                        hitMetrics[k].top + hitMetrics[k].height),
            fillBrush);
    }
}

void Renderer::DrawBlock(const BlockGeometry& g, u32 blockIndex, float scrollY, float targetWidth,
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
                          ID2D1SolidColorBrush* const* hlBrushes) {
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
        // 1px 描边(对齐 GitHub 代码块的细边框),画在填色之后避免被盖住。
        if (codeBorderBrush) {
            target_->DrawRoundedRectangle(roundedRect, codeBorderBrush, kCodeBlockBorderWidthDip);
        }
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

    // 表格网格线/表头背景/斑马纹/悬浮行高亮:只在 Table 容器块自身画一次,覆盖整张表。
    // hoverRow 从 overlay_ 里读——只有当前块就是鼠标悬浮所在的那张表时才生效。
    if (g.type == BlockType::Table) {
        u32 hoverRow = kInvalidIndex;
        if (overlay_ && overlay_->hoverTableBlock == blockIndex) hoverRow = overlay_->hoverTableRow;
        DrawTableChrome(g, scrollY, tableHeaderBrush, tableGridBrush, tableZebraBrush,
                         tableRowHoverBrush, hoverRow);
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
        // 拖选高亮(T80)与查找命中高亮(T38)都画在文本**之前**,于是底色在
        // 文字下方,文字颜色不受影响。选区在下、查找命中在上,查找命中更
        // 显眼(更常用于"定位"),视觉优先级更高。
        DrawSelectionHighlights(g, blockIndex, scrollY, selectionBrush);
        DrawFindHighlights(g, blockIndex, scrollY, findHighlightBrush, findCurrentBrush);
        target_->DrawTextLayout(D2D1::Point2F(TextDrawLeft(g), TextDrawTop(g) - scrollY),
                                  g.textLayout, textBrush,
                                  D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        // 链接着色(T24):在正文之上叠加一次裁剪重绘,见 DrawLinkOverlays 注释。
        DrawLinkOverlays(g, scrollY, linkBrush);
        // 代码语法着色(T53):同一手法,只在该块有 codeHighlights 时才生效
        // (未识别语言/非代码块恒为空,函数内部也会再判一次)。
        DrawCodeHighlights(g, scrollY, hlBrushes);
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

// 画欢迎屏(未打开任何文档时,取代正文 DrawBlock 循环,详见 renderer.h 的
// 声明处注释)。
// 欢迎屏"打开文件"按钮图标:单文档文件图标(带折角矩形效果)。rect 是
// 图标应绘制在其中的矩形区域(contentLeft/iconTop 起、kWelcomeIconSizeDip
// 见方),userData 是文字/图标共用的 textBrush。
void Renderer::PaintWelcomeFileIcon(void* renderCtx, const ButtonRectDip& rect, void* userData) {
    Renderer* self = static_cast<Renderer*>(renderCtx);
    ID2D1SolidColorBrush* textBrush = static_cast<ID2D1SolidColorBrush*>(userData);
    if (!self || !self->target_ || !textBrush) return;
    ID2D1HwndRenderTarget* target_ = self->target_;
    float contentLeft = rect.left;
    float iconTop = rect.top;
    float size = rect.Width();
    D2D1_RECT_F docRect = D2D1::RectF(contentLeft + 1.0f, iconTop, contentLeft + size - 1.0f, iconTop + size);
    target_->DrawRoundedRectangle(D2D1::RoundedRect(docRect, 1.5f, 1.5f), textBrush, 1.2f);
    target_->DrawLine(D2D1::Point2F(contentLeft + 4.0f, iconTop + 5.0f),
                      D2D1::Point2F(contentLeft + size - 4.0f, iconTop + 5.0f), textBrush, 1.0f);
    target_->DrawLine(D2D1::Point2F(contentLeft + 4.0f, iconTop + 9.0f),
                      D2D1::Point2F(contentLeft + size - 4.0f, iconTop + 9.0f), textBrush, 1.0f);
}

// 欢迎屏"打开文件夹"按钮图标:文件夹轮廓(带顶端标签)。
void Renderer::PaintWelcomeFolderIcon(void* renderCtx, const ButtonRectDip& rect, void* userData) {
    Renderer* self = static_cast<Renderer*>(renderCtx);
    ID2D1SolidColorBrush* textBrush = static_cast<ID2D1SolidColorBrush*>(userData);
    if (!self || !self->target_ || !textBrush) return;
    ID2D1HwndRenderTarget* target_ = self->target_;
    float contentLeft = rect.left;
    float iconTop = rect.top;
    float size = rect.Width();
    float tabWidth = size * 0.46f;
    float tabHeight = size * 0.20f;
    D2D1_RECT_F tabRect = D2D1::RectF(contentLeft, iconTop, contentLeft + tabWidth, iconTop + tabHeight);
    target_->FillRoundedRectangle(D2D1::RoundedRect(tabRect, 1.5f, 1.5f), textBrush);

    float bodyTop = iconTop + tabHeight * 0.7f;
    D2D1_RECT_F bodyRect = D2D1::RectF(contentLeft, bodyTop, contentLeft + size, iconTop + size);
    target_->FillRoundedRectangle(D2D1::RoundedRect(bodyRect, 2.0f, 2.0f), textBrush);
}

void Renderer::DrawWelcomeScreen(float targetWidth, float targetHeight,
                                  bool buttonHover, bool folderButtonHover,
                                  ID2D1SolidColorBrush* textBrush,
                                  ID2D1SolidColorBrush* buttonFillBrush,
                                  ID2D1SolidColorBrush* buttonHoverFillBrush,
                                  ID2D1SolidColorBrush* buttonBorderBrush) {
    float buttonTop = targetHeight * kWelcomeButtonTopRatio;
    float totalButtonsWidth = kWelcomeButtonWidthDip * 2.0f + kWelcomeButtonGapDip;
    float startX = (targetWidth - totalButtonsWidth) * 0.5f;
    float buttonCenterY = buttonTop + kWelcomeButtonHeightDip * 0.5f;

    float fileButtonCenterX = startX + kWelcomeButtonWidthDip * 0.5f;
    float folderButtonCenterX = startX + kWelcomeButtonWidthDip + kWelcomeButtonGapDip + kWelcomeButtonWidthDip * 0.5f;

    // 标题:与正文一级标题同一套字号与加粗
    constexpr u32 kHeadingLen = sizeof(kWelcomeHeadingText) / sizeof(wchar_t) - 1;
    if (fonts_ && textBrush) {
        IDWriteTextLayout* headingLayout = fonts_->CreateTextLayout(
            kWelcomeHeadingText, kHeadingLen, FontRole::Body, targetWidth, buttonTop);
        if (headingLayout) {
            IDWriteTextFormat* fmt = fonts_->GetTextFormat(FontRole::Body);
            float baseFontSize = fmt ? fmt->GetFontSize() : 16.0f;
            DWRITE_TEXT_RANGE fullRange{0, kHeadingLen};
            headingLayout->SetFontSize(baseFontSize * kWelcomeHeadingFontSizeScale, fullRange);
            headingLayout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, fullRange);
            headingLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

            DWRITE_TEXT_METRICS metrics{};
            headingLayout->GetMetrics(&metrics);
            float headingTop = buttonTop - kWelcomeHeadingGapAboveButtonDip - metrics.height;
            if (headingTop < 0.0f) headingTop = 0.0f;
            target_->DrawTextLayout(D2D1::Point2F(0.0f, headingTop), headingLayout, textBrush,
                                      D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            headingLayout->Release();
        }
    }

    // isFolder 决定这个按钮的图标画法(paintIcon)——两枚按钮各自构造一个
    // Button 值(label/title/paintIcon 都填好),最终矩形统一经
    // ButtonRectFromCenterDip 得到,不再各自手写 left/right 展开式。
    auto drawOneButton = [&](float centerX, bool isHover, const Button& btn) {
        ButtonRectDip rect = ButtonRectFromCenterDip(btn, centerX, buttonCenterY);
        D2D1_RECT_F buttonRect = D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom);
        D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(buttonRect, btn.radius, btn.radius);
        ID2D1SolidColorBrush* fillBrush = isHover ? buttonHoverFillBrush : buttonFillBrush;
        if (fillBrush) target_->FillRoundedRectangle(rounded, fillBrush);
        if (buttonBorderBrush) target_->DrawRoundedRectangle(rounded, buttonBorderBrush, 1.0f);

        u32 labelLen = static_cast<u32>(wcslen(btn.label));
        IDWriteTextLayout* labelLayout = nullptr;
        float labelWidth = 0.0f;
        float labelHeight = 0.0f;
        if (fonts_) {
            labelLayout = fonts_->CreateTextLayout(btn.label, labelLen, FontRole::Body,
                                                   kWelcomeButtonWidthDip, kWelcomeButtonHeightDip);
            if (labelLayout) {
                labelLayout->SetFontSize(kWelcomeButtonLabelFontSizeDip * fonts_->Scale(),
                                          DWRITE_TEXT_RANGE{0, labelLen});
                DWRITE_TEXT_METRICS metrics{};
                labelLayout->GetMetrics(&metrics);
                labelWidth = metrics.width;
                labelHeight = metrics.height;
            }
        }

        float contentWidth = kWelcomeIconSizeDip + (labelLayout ? kWelcomeIconLabelGapDip + labelWidth : 0.0f);
        float contentLeft = rect.left + (kWelcomeButtonWidthDip - contentWidth) * 0.5f;
        float iconTop = buttonTop + (kWelcomeButtonHeightDip - kWelcomeIconSizeDip) * 0.5f;

        if (textBrush && btn.paintIcon) {
            ButtonRectDip iconRect{contentLeft, iconTop, contentLeft + kWelcomeIconSizeDip,
                                    iconTop + kWelcomeIconSizeDip};
            btn.paintIcon(this, iconRect, textBrush);
        }

        if (labelLayout) {
            float labelLeft = contentLeft + kWelcomeIconSizeDip + kWelcomeIconLabelGapDip;
            float labelTop = buttonTop + (kWelcomeButtonHeightDip - labelHeight) * 0.5f;
            if (textBrush) {
                target_->DrawTextLayout(D2D1::Point2F(labelLeft, labelTop), labelLayout, textBrush,
                                          D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            }
            labelLayout->Release();
        }
    };

    Button fileBtn{kWelcomeButtonWidthDip, kWelcomeButtonHeightDip, kWelcomeButtonCornerRadiusDip,
                    kWelcomeButtonLabel, kWelcomeButtonLabel, &Renderer::PaintWelcomeFileIcon, nullptr};
    Button folderBtn{kWelcomeButtonWidthDip, kWelcomeButtonHeightDip, kWelcomeButtonCornerRadiusDip,
                      kWelcomeFolderButtonLabel, kWelcomeFolderButtonLabel, &Renderer::PaintWelcomeFolderIcon,
                      nullptr};

    // 绘制 1：打开文件按钮
    drawOneButton(fileButtonCenterX, buttonHover, fileBtn);
    // 绘制 2：打开文件夹按钮
    drawOneButton(folderButtonCenterX, folderButtonHover, folderBtn);
}

bool Renderer::RenderFrame(HWND hwnd, const BlockLayoutEngine& layout, float scrollY,
                            float leftPaddingDip, const ShellOverlay* overlay,
                            bool mainScrollbarActive, const wchar_t* documentPath,
                            u64 documentSizeBytes, u32 bottomBarHoverButtonIndex,
                            bool bottomBarPathCopied, bool welcomeScreen,
                            bool welcomeButtonHover, bool welcomeFolderButtonHover) {
    if (!EnsureRenderTarget(hwnd)) return false;

    // 叠加层视图只在本帧内有效,画完立刻置空,避免留下悬空引用。
    overlay_ = overlay;

    ID2D1SolidColorBrush* textBrush = nullptr;
    ID2D1SolidColorBrush* quoteBrush = nullptr;
    ID2D1SolidColorBrush* codeBgBrush = nullptr;
    ID2D1SolidColorBrush* codeBorderBrush = nullptr;
    ID2D1SolidColorBrush* hrBrush = nullptr;
    ID2D1SolidColorBrush* linkBrush = nullptr;
    ID2D1SolidColorBrush* tableHeaderBrush = nullptr;
    ID2D1SolidColorBrush* tableGridBrush = nullptr;
    ID2D1SolidColorBrush* tableZebraBrush = nullptr;
    ID2D1SolidColorBrush* tableRowHoverBrush = nullptr;
    ID2D1SolidColorBrush* checkboxBorderBrush = nullptr;
    ID2D1SolidColorBrush* checkboxCheckBrush = nullptr;
    ID2D1SolidColorBrush* placeholderBgBrush = nullptr;
    ID2D1SolidColorBrush* placeholderBorderBrush = nullptr;
    ID2D1SolidColorBrush* badgeBgBrush = nullptr;
    ID2D1SolidColorBrush* badgeTextBrush = nullptr;
    ID2D1SolidColorBrush* findHighlightBrush = nullptr;
    ID2D1SolidColorBrush* findCurrentBrush = nullptr;
    ID2D1SolidColorBrush* selectionBrush = nullptr;
    ID2D1SolidColorBrush* overlayBarBgBrush = nullptr;
    ID2D1SolidColorBrush* overlayBarTextBrush = nullptr;
    ID2D1SolidColorBrush* findBarBgBrush = nullptr;
    ID2D1SolidColorBrush* findBarTextBrush = nullptr;
    ID2D1SolidColorBrush* outlineHighlightBgBrush = nullptr;
    ID2D1SolidColorBrush* outlineHighlightTextBrush = nullptr;
    ID2D1SolidColorBrush* outlineOverlayMaskBrush = nullptr;
    ID2D1SolidColorBrush* historyRowButtonBgBrush = nullptr;
    // 大纲侧栏底色改跟正文背景同色(浅色主题白底黑字、深色主题黑底白字),
    // 不再借用查找条那套固定深色浮出条配色——蒙层已经把侧栏之外的正文
    // 压暗,侧栏本身用主题背景色天然就能在视觉上分离出来。
    ID2D1SolidColorBrush* outlinePanelBgBrush = nullptr;
    ID2D1SolidColorBrush* copyIconBrush = nullptr;
    ID2D1SolidColorBrush* copyHoverBgBrush = nullptr;
    ID2D1SolidColorBrush* copyPaperBrush = nullptr;
    ID2D1SolidColorBrush* copyDoneBrush = nullptr;
    ID2D1SolidColorBrush* scrollbarTrackIdleBrush = nullptr;
    ID2D1SolidColorBrush* scrollbarTrackActiveBrush = nullptr;
    ID2D1SolidColorBrush* scrollbarThumbIdleBrush = nullptr;
    ID2D1SolidColorBrush* scrollbarThumbActiveBrush = nullptr;
    // T53:代码语法着色的 7 支画笔,下标与 hl/lexer.h::TokenType 取值一一对应。
    ID2D1SolidColorBrush* hlBrushes[7] = {nullptr, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, nullptr};
    target_->CreateSolidColorBrush(palette_->text, &textBrush);
    target_->CreateSolidColorBrush(palette_->quoteBar, &quoteBrush);
    target_->CreateSolidColorBrush(palette_->codeBackground, &codeBgBrush);
    target_->CreateSolidColorBrush(palette_->codeBorder, &codeBorderBrush);
    target_->CreateSolidColorBrush(palette_->thematicBreak, &hrBrush);
    target_->CreateSolidColorBrush(palette_->link, &linkBrush);
    target_->CreateSolidColorBrush(palette_->tableHeaderBackground, &tableHeaderBrush);
    target_->CreateSolidColorBrush(palette_->tableGrid, &tableGridBrush);
    target_->CreateSolidColorBrush(palette_->tableZebraBackground, &tableZebraBrush);
    target_->CreateSolidColorBrush(palette_->tableRowHoverBackground, &tableRowHoverBrush);
    target_->CreateSolidColorBrush(palette_->checkboxBorder, &checkboxBorderBrush);
    target_->CreateSolidColorBrush(palette_->checkboxCheck, &checkboxCheckBrush);
    target_->CreateSolidColorBrush(palette_->imagePlaceholderBackground, &placeholderBgBrush);
    target_->CreateSolidColorBrush(palette_->imagePlaceholderBorder, &placeholderBorderBrush);
    target_->CreateSolidColorBrush(palette_->downsampledBadgeBackground, &badgeBgBrush);
    target_->CreateSolidColorBrush(palette_->downsampledBadgeText, &badgeTextBrush);
    target_->CreateSolidColorBrush(palette_->findHighlight, &findHighlightBrush);
    target_->CreateSolidColorBrush(palette_->findCurrentHighlight, &findCurrentBrush);
    target_->CreateSolidColorBrush(palette_->selectionHighlight, &selectionBrush);
    target_->CreateSolidColorBrush(palette_->overlayBarBackground, &overlayBarBgBrush);
    target_->CreateSolidColorBrush(palette_->overlayBarText, &overlayBarTextBrush);
    target_->CreateSolidColorBrush(palette_->findBarBackground, &findBarBgBrush);
    target_->CreateSolidColorBrush(palette_->findBarText, &findBarTextBrush);
    target_->CreateSolidColorBrush(palette_->outlineHighlightBackground, &outlineHighlightBgBrush);
    target_->CreateSolidColorBrush(palette_->outlineHighlightText, &outlineHighlightTextBrush);
    target_->CreateSolidColorBrush(palette_->historyRowButtonBackground, &historyRowButtonBgBrush);

    // Fade-in / fade-out alpha animation for outline & history & folder mask overlay.
    //
    // 大纲/历史/文件夹侧栏共用同一个蒙层画笔,透明度必须覆盖"当前所有用到它的
    // 侧栏"的动画进度——以后再新增第四个用这个画笔的侧栏时,也要记得在这里加一行,
    // 否则会重演"只开某个侧栏时蒙层恒透明"的漏算 bug。
    D2D1_COLOR_F maskColor = palette_->outlineOverlayMaskBackground;
    float animProgress = 0.0f;
    // 不能再拿 itemCount > 0 当"侧栏打开"判据(空大纲/空历史/欢迎屏空状态
    // 打开侧栏时 itemCount 恒为 0)——outlineAnimProgress/historyAnimProgress
    // 本身在侧栏关闭时已经是 0(见 window.cpp 组装 overlay 处),直接读它们
    // 就够了,itemCount 只影响画不画条目,不该影响蒙层透明度。
    if (overlay_ && overlay_->outlineAnimProgress > animProgress) {
        animProgress = overlay_->outlineAnimProgress;
    }
    if (overlay_ && overlay_->historyAnimProgress > animProgress) {
        animProgress = overlay_->historyAnimProgress;
    }
    // 文件夹侧栏动画进度此前漏算,导致单独打开文件夹侧栏时蒙层恒为全透明。
    if (overlay_ && overlay_->folderAnimProgress > animProgress) {
        animProgress = overlay_->folderAnimProgress;
    }
    if (animProgress < 0.0f) animProgress = 0.0f;
    if (animProgress > 1.0f) animProgress = 1.0f;
    maskColor.a *= animProgress;
    target_->CreateSolidColorBrush(maskColor, &outlineOverlayMaskBrush);

    target_->CreateSolidColorBrush(palette_->background, &outlinePanelBgBrush);
    target_->CreateSolidColorBrush(palette_->codeCopyIcon, &copyIconBrush);
    target_->CreateSolidColorBrush(palette_->codeCopyHoverBackground, &copyHoverBgBrush);
    target_->CreateSolidColorBrush(palette_->codeCopyPaper, &copyPaperBrush);
    target_->CreateSolidColorBrush(palette_->codeCopyDone, &copyDoneBrush);
    target_->CreateSolidColorBrush(palette_->scrollbarTrackIdle, &scrollbarTrackIdleBrush);
    target_->CreateSolidColorBrush(palette_->scrollbarTrackActive, &scrollbarTrackActiveBrush);
    target_->CreateSolidColorBrush(palette_->scrollbarThumbIdle, &scrollbarThumbIdleBrush);
    target_->CreateSolidColorBrush(palette_->scrollbarThumbActive, &scrollbarThumbActiveBrush);
    target_->CreateSolidColorBrush(palette_->hlKeyword, &hlBrushes[kTokenKeyword]);
    target_->CreateSolidColorBrush(palette_->hlString, &hlBrushes[kTokenString]);
    target_->CreateSolidColorBrush(palette_->hlNumber, &hlBrushes[kTokenNumber]);
    target_->CreateSolidColorBrush(palette_->hlComment, &hlBrushes[kTokenComment]);
    target_->CreateSolidColorBrush(palette_->hlPunct, &hlBrushes[kTokenPunct]);
    target_->CreateSolidColorBrush(palette_->hlBuiltin, &hlBrushes[kTokenBuiltin]);
    target_->CreateSolidColorBrush(palette_->hlOther, &hlBrushes[kTokenOther]);

    D2D1_SIZE_F targetSize = target_->GetSize();
    frameViewportHeight_ = targetSize.height;
    // 文件夹侧栏是挤压模式(唯一一个):展开时正文要整体右移让出这块空间,
    // 而不是被悬浮蒙层盖住。挤压宽度与外壳层(window.cpp/main.cpp)重排布局
    // 用的 FolderSqueezeWidthDip 同一口径(面板宽度 * 动画进度)。
    float folderSqueezeDip = 0.0f;
    if (overlay_ && overlay_->folderAnimProgress > 0.0f) {
        folderSqueezeDip = SidebarSqueezeWidthDip(overlay_->folderPanelWidthDip, overlay_->folderAnimProgress);
    }
    // 正文可用宽度:客户区宽度收窄掉左右内边距(各 leftPaddingDip)与文件夹
    // 侧栏挤压宽度——下面画分割线/脚注分隔线的右边界要按这个来算,否则会画到
    // 内边距/侧栏区域的空白里去。
    float contentWidth = targetSize.width - 2.0f * leftPaddingDip - folderSqueezeDip;
    if (contentWidth < 0.0f) contentWidth = 0.0f;

    target_->BeginDraw();
    target_->Clear(palette_->background);

    // 左内边距:水平方向整体平移 leftPaddingDip + 文件夹侧栏挤压宽度,每个
    // DrawXxx 已经在用 g.indent 当 x 坐标画东西,不用逐个改。垂直方向的内边距
    // 由调用方传入的 scrollY(已经减去过内边距)实现,这里不用再叠一次。仅对
    // "文档内容"生效——叠加层(查找条/提示条)画之前会恢复成 Identity,不跟着
    // 这个平移走。
    target_->SetTransform(D2D1::Matrix3x2F::Translation(leftPaddingDip + folderSqueezeDip, 0.0f));

    // T76:视口裁剪。此前这个循环对**全部**块无条件调 DrawBlock —— 屏幕外的
    // 代码块背景/引用竖线/分割线/表格网格线照样会提交给 D2D(只有正文文字因为
    // textLayout 被虚拟化淘汰成 nullptr 而侥幸跳过),于是首帧耗时与文档总块数
    // 成正比而不是与可见块数成正比:10 MB 语料(33299 块)实测首帧绘制段
    // 1516 ms,占首屏总耗时的 96%。裁剪范围取"可见 ± 1 屏",与 layout 的
    // UpdateVisibleRange 用同一条边界,既不会误裁掉任何有 textLayout 的块,
    // 也给块内那几 DIP 的背景/竖线外扩留足了余量。
    const float cullMargin = targetSize.height;
    const float cullTop = scrollY - cullMargin;
    const float cullBottom = scrollY + targetSize.height + cullMargin;

    if (welcomeScreen) {
        // 欢迎屏(当前没有已加载文档):不走正常的 DrawBlock 循环,改画
        // "Welcome to markair" 标题 + "打开文件"按钮。按钮底色/描边复用代码块
        // 复制按钮同一套画笔槽位(codeBgBrush/copyHoverBgBrush/codeBorderBrush),
        // 不新增 Palette 槽位。
        DrawWelcomeScreen(targetSize.width, targetSize.height, welcomeButtonHover,
                          welcomeFolderButtonHover, textBrush,
                          codeBgBrush, copyHoverBgBrush, codeBorderBrush);
    } else {
        u32 blockCount = layout.BlockCount();
        for (u32 i = 0; i < blockCount; ++i) {
            const BlockGeometry& g = layout.Geometry(i);
            if (g.bottom < cullTop || g.top > cullBottom) continue;
            DrawBlock(g, i, scrollY, contentWidth,
                      textBrush, quoteBrush, codeBgBrush, codeBorderBrush, hrBrush, linkBrush,
                      tableHeaderBrush, tableGridBrush, tableZebraBrush, tableRowHoverBrush,
                      checkboxBorderBrush, checkboxCheckBrush,
                      placeholderBgBrush, placeholderBorderBrush, badgeBgBrush, badgeTextBrush,
                      findHighlightBrush, findCurrentBrush, selectionBrush,
                      copyIconBrush, copyHoverBgBrush, copyPaperBrush, copyDoneBrush,
                      hlBrushes);
        }
    }

    // 叠加层(查找条/窗口内提示)不随内容平移——先恢复 Identity 变换。
    target_->SetTransform(D2D1::Matrix3x2F::Identity());

    // 大纲或历史侧栏打开时正文整块被蒙层盖住、也不可滚动，此时正文滚动条不画。
    bool outlineOpen = overlay_ && overlay_->outlineItems && overlay_->outlineItemCount > 0 &&
                       overlay_->outlineAnimProgress > 0.0001f;
    bool historyOpen = overlay_ && overlay_->historyEntries && overlay_->historyItemCount > 0 &&
                       overlay_->historyAnimProgress > 0.0001f;

    // 正文滚动条(方案A,替代原生 WS_VSCROLL):内容超过一屏就画,不依赖
    // overlay_ 是否为空(除了上面这条"侧栏打开时隐藏"的例外)。scrollY/
    // leftPaddingDip 已经是外壳层减去过 kContentPaddingDip 的"有效滚动偏移",
    // 这里加回来换算成真实滚动偏移;视口高度同理用客户区高度减去两份内边距
    // 换算(与外壳层 UsableViewportHeightDip 同一口径,不重复 include shell 头文件)。
    if (!welcomeScreen && !outlineOpen && !historyOpen) {
        float scrollbarHeight = targetSize.height - kBottomBarHeightDip;
        if (scrollbarHeight < 0.0f) scrollbarHeight = 0.0f;
        // 滚动条背景不跟随鼠标悬浮高亮 (保持常驻 Idle 颜色)。
        //
        // Scrollbar track background does not highlight on mouse hover (remains idle color).
        ID2D1SolidColorBrush* trackBrush = scrollbarTrackIdleBrush;
        ID2D1SolidColorBrush* thumbBrush =
            mainScrollbarActive ? scrollbarThumbActiveBrush : scrollbarThumbIdleBrush;
        DrawScrollbar(targetSize.width, scrollbarHeight, layout.TotalHeight(),
                      scrollY + leftPaddingDip, trackBrush, thumbBrush);
    }

    // 正文顶部分割线(y=0,紧贴系统标题栏下沿),复用底部栏同一份分割线颜色,
    // 不新增调色板字段。画在正文/滚动条之后、蒙层与查找条/侧栏之前——
    // 侧栏蒙层/查找条打开时盖住这条线是合理的(蒙层本该盖住正文所有内容)。
    {
        ID2D1SolidColorBrush* topDividerBrush = nullptr;
        target_->CreateSolidColorBrush(palette_->bottomBarDivider, &topDividerBrush);
        if (topDividerBrush) {
            target_->DrawLine(D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(targetSize.width, 0.0f),
                              topDividerBrush, 1.0f);
            topDividerBrush->Release();
        }
    }

    // 底部操作栏(常驻,不依赖 overlay_ 是否为空——它不是可选叠加层)。画在
    // 正文/滚动条之后、蒙层与查找条/侧栏之前,这样大纲/历史侧栏打开时蒙层能盖住
    // 底部栏(裁决:蒙层应完整遮住正文可交互区域,底部栏也不例外)。
    // bottomBarBgBrush/bottomBarTextBrush 的生命周期要跨过下面的侧栏叠加层
    // 一直留到函数末尾——悬浮提示气泡(DrawBottomBarTooltip)由下面那段 z 序
    // 分发决定什么时候画,可能排在所有侧栏之后,这两支笔那时才用得上。
    ID2D1SolidColorBrush* bottomBarBgBrush = nullptr;
    ID2D1SolidColorBrush* bottomBarTextBrush = nullptr;
    {
        ID2D1SolidColorBrush* bottomBarIconBrush = nullptr;
        ID2D1SolidColorBrush* bottomBarDividerBrush = nullptr;
        target_->CreateSolidColorBrush(palette_->bottomBarBackground, &bottomBarBgBrush);
        target_->CreateSolidColorBrush(palette_->bottomBarIcon, &bottomBarIconBrush);
        target_->CreateSolidColorBrush(palette_->bottomBarText, &bottomBarTextBrush);
        target_->CreateSolidColorBrush(palette_->bottomBarDivider, &bottomBarDividerBrush);
        DrawBottomBar(targetSize.width, targetSize.height, bottomBarBgBrush, bottomBarIconBrush,
                      bottomBarTextBrush, bottomBarDividerBrush, documentPath, documentSizeBytes,
                      bottomBarPathCopied);
        if (bottomBarIconBrush) bottomBarIconBrush->Release();
        if (bottomBarDividerBrush) bottomBarDividerBrush->Release();
    }

    // 叠加层最后画,浮在正文/底部栏之上:查找条(T37)与窗口内提示(T36)。
    if (overlay_) {
        float topOffset = 0.0f;
        if (overlay_->findBarVisible) {
            wchar_t status[kMaxOverlayBarChars];
            u32 len = ComposeFindBarStatusText(overlay_, status, kMaxOverlayBarChars);
            DrawFindBar(targetSize.width, status, len, findBarBgBrush, findBarTextBrush);
            topOffset = kOverlayBarStackStepDip;
        }
        if (overlay_->statusMessage && overlay_->statusMessage[0] != 0) {
            DrawOverlayBar(targetSize.width, overlay_->statusMessage,
                           WideLength(overlay_->statusMessage), overlayBarBgBrush,
                           overlayBarTextBrush, topOffset);
        }
    }

    // 可互相遮挡的 4 个图层按动态 z 序绘制。之所以不再写死"大纲→历史→文件夹
    // →气泡"的调用顺序:三个侧栏现在可以同时打开、在屏幕上真实重叠,谁该压在
    // 上面取决于"谁最近被激活",这个信息只有外壳层知道(ShellOverlay 里的
    // 那四个 z 值)。条目数最多 4,直接开栈上定长数组 + 插入排序,渲染路径
    // 不做任何堆分配。每个侧栏的蒙层与面板是一个整体,同一个 case 里连着画。
    enum LayerKind : u32 { kLayerOutline, kLayerHistory, kLayerFolder, kLayerBottomBarTooltip };
    struct ZLayer {
        u32 z;
        u32 kind;
    };
    ZLayer layers[4];
    u32 layerCount = 0;
    // 入选条件与各 DrawXxx 自己的提前返回判据保持一致(动画进度 > 0.0001f),
    // 免得给一个当帧根本不画的图层白排一次序。
    if (overlay_ && overlay_->outlineAnimProgress > 0.0001f) {
        layers[layerCount++] = ZLayer{overlay_->outlineZOrder, kLayerOutline};
    }
    if (overlay_ && overlay_->historyAnimProgress > 0.0001f) {
        layers[layerCount++] = ZLayer{overlay_->historyZOrder, kLayerHistory};
    }
    if (overlay_ && overlay_->folderAnimProgress > 0.0001f) {
        layers[layerCount++] = ZLayer{overlay_->folderZOrder, kLayerFolder};
    }
    // 气泡不依赖 overlay_(底部栏本身就是常驻的,没有任何侧栏时也要能弹提示),
    // z 值缺省为 0,单独悬浮时它是唯一图层,顺序无所谓。
    bool hasDocument = documentPath && documentPath[0] != L'\0';
    if (bottomBarHoverButtonIndex < kBottomBarButtonCount) {
        u32 tooltipZ = overlay_ ? overlay_->bottomBarTooltipZOrder : 0u;
        layers[layerCount++] = ZLayer{tooltipZ, kLayerBottomBarTooltip};
    }
    for (u32 i = 1; i < layerCount; ++i) {
        ZLayer key = layers[i];
        u32 j = i;
        while (j > 0 && layers[j - 1].z > key.z) {
            layers[j] = layers[j - 1];
            --j;
        }
        layers[j] = key;
    }
    for (u32 i = 0; i < layerCount; ++i) {
        switch (layers[i].kind) {
            case kLayerOutline: {
                // 蒙层压暗侧栏之外的正文,再画侧栏本体——这两步之间的先后是
                // 固定的(蒙层在下),跟着整个图层一起在 z 序里移动。
                DrawOutlineOverlayMask(targetSize.width, targetSize.height, outlineOverlayMaskBrush);
                bool outlineScrollbarActive = overlay_->outlineScrollbarActive;
                DrawOutlinePanel(targetSize.height, outlinePanelBgBrush, textBrush,
                                 outlineHighlightBgBrush, outlineHighlightTextBrush,
                                 scrollbarTrackIdleBrush,
                                 outlineScrollbarActive ? scrollbarThumbActiveBrush
                                                        : scrollbarThumbIdleBrush);
                break;
            }
            case kLayerHistory: {
                DrawHistoryOverlayMask(targetSize.width, targetSize.height, outlineOverlayMaskBrush);
                bool historyScrollbarActive = overlay_->historyScrollbarActive;
                DrawHistoryPanel(targetSize.width, targetSize.height, outlinePanelBgBrush, textBrush,
                                 outlineHighlightBgBrush, historyRowButtonBgBrush,
                                 scrollbarTrackIdleBrush,
                                 historyScrollbarActive ? scrollbarThumbActiveBrush
                                                        : scrollbarThumbIdleBrush);
                break;
            }
            case kLayerFolder: {
                // 挤压模式,不画悬浮蒙层——正文宽度已经在外壳层按
                // FolderSqueezeWidthDip 收窄让出这块空间,见 DrawFolderPanel 注释。
                bool folderScrollbarActive = overlay_->folderScrollbarActive;
                DrawFolderPanel(targetSize.width, targetSize.height, outlinePanelBgBrush, textBrush,
                                outlineHighlightBgBrush, copyHoverBgBrush,
                                historyRowButtonBgBrush,
                                scrollbarTrackIdleBrush,
                                folderScrollbarActive ? scrollbarThumbActiveBrush
                                                      : scrollbarThumbIdleBrush,
                                overlayBarBgBrush, overlayBarTextBrush);
                break;
            }
            case kLayerBottomBarTooltip:
                DrawBottomBarTooltip(targetSize.width, targetSize.height, bottomBarBgBrush,
                                     bottomBarTextBrush, bottomBarHoverButtonIndex, hasDocument);
                break;
            default:
                break;
        }
    }
    if (bottomBarBgBrush) bottomBarBgBrush->Release();
    if (bottomBarTextBrush) bottomBarTextBrush->Release();

    if (textBrush) textBrush->Release();
    if (quoteBrush) quoteBrush->Release();
    if (codeBgBrush) codeBgBrush->Release();
    if (codeBorderBrush) codeBorderBrush->Release();
    if (hrBrush) hrBrush->Release();
    if (linkBrush) linkBrush->Release();
    if (tableHeaderBrush) tableHeaderBrush->Release();
    if (tableGridBrush) tableGridBrush->Release();
    if (tableZebraBrush) tableZebraBrush->Release();
    if (tableRowHoverBrush) tableRowHoverBrush->Release();
    if (checkboxBorderBrush) checkboxBorderBrush->Release();
    if (checkboxCheckBrush) checkboxCheckBrush->Release();
    if (placeholderBgBrush) placeholderBgBrush->Release();
    if (placeholderBorderBrush) placeholderBorderBrush->Release();
    if (badgeBgBrush) badgeBgBrush->Release();
    if (badgeTextBrush) badgeTextBrush->Release();
    if (findHighlightBrush) findHighlightBrush->Release();
    if (findCurrentBrush) findCurrentBrush->Release();
    if (selectionBrush) selectionBrush->Release();
    if (overlayBarBgBrush) overlayBarBgBrush->Release();
    if (overlayBarTextBrush) overlayBarTextBrush->Release();
    if (findBarBgBrush) findBarBgBrush->Release();
    if (findBarTextBrush) findBarTextBrush->Release();
    if (outlineHighlightBgBrush) outlineHighlightBgBrush->Release();
    if (historyRowButtonBgBrush) historyRowButtonBgBrush->Release();
    if (outlineHighlightTextBrush) outlineHighlightTextBrush->Release();
    if (outlineOverlayMaskBrush) outlineOverlayMaskBrush->Release();
    if (outlinePanelBgBrush) outlinePanelBgBrush->Release();
    if (copyIconBrush) copyIconBrush->Release();
    if (copyHoverBgBrush) copyHoverBgBrush->Release();
    if (copyPaperBrush) copyPaperBrush->Release();
    if (copyDoneBrush) copyDoneBrush->Release();
    if (scrollbarTrackIdleBrush) scrollbarTrackIdleBrush->Release();
    if (scrollbarTrackActiveBrush) scrollbarTrackActiveBrush->Release();
    if (scrollbarThumbIdleBrush) scrollbarThumbIdleBrush->Release();
    if (scrollbarThumbActiveBrush) scrollbarThumbActiveBrush->Release();
    for (u32 i = 0; i < 7; ++i) {
        if (hlBrushes[i]) hlBrushes[i]->Release();
    }

    overlay_ = nullptr;  // 本帧结束,不再持有外壳层传进来的视图

    HRESULT hr = target_->EndDraw();
    if (ShouldRecreateRenderTarget(hr)) {
        // 设备丢失:释放旧渲染目标,下次 RenderFrame 调用会惰性重建(架构 §9)。
        // 传入的 layout 不受影响 —— IDWriteTextLayout 不绑定具体渲染目标实例。
        ReleaseRenderTarget();
    }
    return SUCCEEDED(hr);
}

}  // namespace markair
