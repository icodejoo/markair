// markair 底部操作栏:常驻显示,左侧 8 个纯图标按钮
// (文件列表/大纲/打开文件/打开文件夹/主题切换/字体缩小/字体放大/查找),
// 中间是状态文字区,最右侧固定贴右的是复制路径与历史记录按钮。
#pragma once

#include "../util/types.h"
#include "button.h"

namespace markair {

// 底部栏固定高度(DIP),随 DPI 缩放(调用方按需再乘 DPI 缩放系数)。
constexpr float kBottomBarHeightDip = 24.0f;

// 底部栏左侧按钮数量与总按钮数。
constexpr u32 kBottomBarLeftButtonCount = 8;
constexpr u32 kBottomBarButtonCount = 10;  // 8 个在左侧 + 复制路径 + 历史,均在最右侧

// "复制路径"按钮距最右侧历史按钮的间距(DIP)——两者都固定贴右,不随中间
// 状态文字区宽度变化;仅当前有打开文件(currentDocumentPath 非空)时才
// 参与布局/绘制/命中测试,否则整段区域让给状态文字区。

// 每个图标按钮的固定宽度(DIP),取值等于栏高度,做成正方形点击区。
constexpr float kBottomBarButtonWidthDip = kBottomBarHeightDip;

// 状态区右侧留白(DIP)。
constexpr float kBottomBarStatusPaddingDip = 10.0f;

// 1 MB 对应的字节数,文件大小格式化按这个阈值切换 KB/MB 单位。
constexpr u64 kBottomBarBytesPerMb = 1024ull * 1024ull;
constexpr float kBottomBarBytesPerKb = 1024.0f;

/**
 * Semantic indices of bottom bar buttons.
 *
 * 底部栏按钮的语义枚举。
 */
enum class BottomBarButton : u32 {
    FileList = 0,   // 打开/关闭文件列表侧栏 (左侧抽屉)
    Outline = 1,    // 打开/关闭大纲侧栏
    OpenDoc = 2,    // 打开文件
    OpenFolder = 3, // 打开文件夹
    Theme = 4,      // 主题切换
    ZoomOut = 5,    // 字体缩小
    ZoomIn = 6,     // 字体放大
    Find = 7,       // 打开查找条(放大镜,与 Ctrl+F 同一路径)
    CopyPath = 8,   // 复制当前文档全路径(紧贴历史按钮左侧;未打开文档时不存在)
    History = 9,    // 历史记录（位于底部栏最右侧）
    None = kBottomBarButtonCount,
};

// 按钮文字标签数组。
constexpr const wchar_t* kBottomBarLabels[kBottomBarButtonCount] = {
    L"文件列表", L"大纲", L"打开文件", L"打开文件夹", L"主题", L"缩小", L"放大", L"查找", L"复制当前文件路径", L"历史",
};

// 一个按钮在客户区坐标系里的矩形(DIP)。
struct BottomBarButtonRect {
    float left;
    float top;
    float right;
    float bottom;
};

/**
 * Calculate button rectangle in DIP based on index and client dimensions. Pure function.
 *
 * 根据按钮下标与客户区尺寸计算按钮矩形（DIP）。纯几何函数。
 *
 * @param index Button index (< kBottomBarButtonCount).
 *
 *   按钮下标（< kBottomBarButtonCount）。
 *
 * @param clientWidthDip Client width in DIP.
 *
 *   客户区宽度（DIP）。
 *
 * @param clientHeightDip Client height in DIP.
 *
 *   客户区高度（DIP）。
 *
 * @return Rectangle of the requested button in DIP. For CopyPath, this is
 *         its rectangle when visible — callers must separately check
 *         whether a document is open before using it.
 *
 *   该按钮的矩形（DIP）。对 CopyPath 而言，这是它可见时的矩形——调用方须
 *   自行先判断当前是否有文档打开。
 */
inline BottomBarButtonRect BottomBarButtonRectDip(u32 index, float clientWidthDip, float clientHeightDip) {
    float top = clientHeightDip - kBottomBarHeightDip;
    float bottom = clientHeightDip;

    // 排布算法(每个按钮的锚点在哪)仍然是本系统自己的:左侧 8 个按钮定宽
    // 网格从左往右排,History/CopyPath 固定贴右——这部分本来就允许 5 套
    // 按钮系统各不相同(网格 vs 并排 vs 从右向左分槽位)。算出 left/right/
    // top/bottom 之后,统一交给 Button 的矩形函数(ButtonRectFromCenterDip)
    // 从几何中心点重新构造一遍——这一步与直接返回 {left,top,right,bottom}
    // 数值上恒等(中心点就是这四个数算出来的),但确保"这是一个按钮"这件事
    // 真的经过 Button 抽象,而不是绕过它。
    float left, right;
    if (index == static_cast<u32>(BottomBarButton::History)) {
        right = clientWidthDip;
        left = right - kBottomBarButtonWidthDip;
        if (left < 0.0f) left = 0.0f;
    } else if (index == static_cast<u32>(BottomBarButton::CopyPath)) {
        // 紧贴历史按钮左侧,同样固定宽度的正方形按钮区。
        float historyLeft = clientWidthDip - kBottomBarButtonWidthDip;
        right = historyLeft;
        left = right - kBottomBarButtonWidthDip;
        if (left < 0.0f) left = 0.0f;
    } else {
        left = kBottomBarButtonWidthDip * static_cast<float>(index);
        right = kBottomBarButtonWidthDip * static_cast<float>(index + 1);
    }

    Button btn{right - left, bottom - top, 0.0f, kBottomBarLabels[index], kBottomBarLabels[index],
               nullptr, nullptr};
    ButtonRectDip r = ButtonRectFromCenterDip(btn, (left + right) * 0.5f, (top + bottom) * 0.5f);
    return BottomBarButtonRect{r.left, r.top, r.right, r.bottom};
}

/**
 * Check if a mouse coordinate Y falls inside the bottom bar. Pure function.
 *
 * 判断纵坐标是否落在底部栏高度范围内。纯函数。
 *
 * @param clientHeightDip Client height in DIP.
 *
 *   客户区高度（DIP）。
 *
 * @param pointYDip Mouse Y coordinate in DIP.
 *
 *   鼠标纵坐标（DIP）。
 *
 * @return True if point Y is within bottom bar, false otherwise.
 *
 *   落在底部栏高度内返回 true，否则返回 false。
 */
inline bool IsPointInBottomBar(float clientHeightDip, float pointYDip) {
    return pointYDip >= clientHeightDip - kBottomBarHeightDip;
}

/**
 * Hit-test mouse X coordinate against bottom bar buttons. Pure function.
 *
 * 根据横坐标命中测试点击落在哪个底部栏按钮上。纯函数。
 *
 * @param clientWidthDip Client width in DIP.
 *
 *   客户区宽度（DIP）。
 *
 * @param pointXDip Mouse X coordinate in DIP.
 *
 *   鼠标横坐标（DIP）。
 *
 * @param hasDocument Whether a document is currently open. The CopyPath
 *        button only exists (and is only hit) when true; when false, that
 *        screen area falls through to the middle status area (None).
 *
 *   当前是否有文档打开。CopyPath 按钮只在为 true 时存在(才可能被命中);
 *   为 false 时该区域退化为中间状态文本区(命中返回 None)。
 *
 * @return Hit BottomBarButton, or BottomBarButton::None if in middle status area.
 *
 *   命中的按钮；若落在中间状态文本区返回 BottomBarButton::None。
 */
inline BottomBarButton HitTestBottomBar(float clientWidthDip, float pointXDip, bool hasDocument) {
    if (clientWidthDip <= 0.0f || pointXDip < 0.0f || pointXDip >= clientWidthDip) {
        return BottomBarButton::None;
    }
    // HitTestBottomBar 本来就只判定横坐标(纵坐标是否落在栏高度内由调用方
    // 另外用 IsPointInBottomBar 判过)——这里把 clientHeightDip 固定取
    // kBottomBarHeightDip(令 top=0/bottom=kBottomBarHeightDip),pointYDip
    // 固定取栏纵向中点,这样就能直接喂给 PointInButtonRectDip 做统一命中
    // 测试,不再手写一遍 `x>=left && x<right`。
    const float pointYDip = kBottomBarHeightDip * 0.5f;

    // Check rightmost History button first
    BottomBarButtonRect historyRect =
        BottomBarButtonRectDip(static_cast<u32>(BottomBarButton::History), clientWidthDip, kBottomBarHeightDip);
    if (historyRect.right > historyRect.left &&
        PointInButtonRectDip(ButtonRectDip{historyRect.left, historyRect.top, historyRect.right, historyRect.bottom},
                              pointXDip, pointYDip)) {
        return BottomBarButton::History;
    }

    // CopyPath sits immediately left of History, only when a document is open.
    if (hasDocument) {
        BottomBarButtonRect copyRect =
            BottomBarButtonRectDip(static_cast<u32>(BottomBarButton::CopyPath), clientWidthDip, kBottomBarHeightDip);
        if (copyRect.right > copyRect.left &&
            PointInButtonRectDip(ButtonRectDip{copyRect.left, copyRect.top, copyRect.right, copyRect.bottom},
                                  pointXDip, pointYDip)) {
            return BottomBarButton::CopyPath;
        }
    }

    // Check left buttons (0..4)
    float leftButtonsWidth = kBottomBarButtonWidthDip * static_cast<float>(kBottomBarLeftButtonCount);
    if (pointXDip < leftButtonsWidth) {
        i32 idx = static_cast<i32>(pointXDip / kBottomBarButtonWidthDip);
        if (idx < 0) idx = 0;
        if (idx >= static_cast<i32>(kBottomBarLeftButtonCount)) {
            idx = static_cast<i32>(kBottomBarLeftButtonCount - 1);
        }
        return static_cast<BottomBarButton>(idx);
    }

    return BottomBarButton::None;
}

/**
 * 判断底部栏"文件列表"按钮当前是否可用（纯函数）。
 * 有文档打开，或已有文件夹上下文（例如穿透扫描过一个文件夹，即使其中
 * 有多个文件而没有自动打开任何一个）时可用；两者都没有（如冷启动且无
 * 历史记录的欢迎屏）时禁用——点了也没有目录/文件可看。
 *
 * @param hasDocument 当前是否有文档打开。
 * @param hasFolderContext 是否已有文件夹上下文（如 folderRootPath 非空）。
 * @return 可用返回 true。
 * @example bool enabled = markair::IsBottomBarFileListEnabled(hasDoc, state->folderRootPath[0] != 0);
 */
inline bool IsBottomBarFileListEnabled(bool hasDocument, bool hasFolderContext) {
    return hasDocument || hasFolderContext;
}

/**
 * 判断底部栏"大纲"按钮当前是否可用（纯函数）。
 * 大纲内容来自当前文档的标题结构，没有打开任何文档时没有内容可提取，
 * 禁用。
 *
 * @param hasDocument 当前是否有文档打开。
 * @return 可用返回 true。
 * @example bool enabled = markair::IsBottomBarOutlineEnabled(hasDoc);
 */
inline bool IsBottomBarOutlineEnabled(bool hasDocument) {
    return hasDocument;
}

/**
 * Format file size in bytes to human-readable string (e.g. "856.0 KB" / "2.3 MB"). Pure function.
 *
 * 将文件字节大小格式化为易读字符串（如 "856.0 KB" 或 "2.3 MB"）。纯函数。
 *
 * @param sizeBytes File size in bytes.
 *
 *   文件字节数。
 *
 * @param out Destination wide character buffer.
 *
 *   输出的宽字符缓冲区。
 *
 * @param outCap Capacity of destination buffer in characters.
 *
 *   目标缓冲区的字符容量。
 */
inline void FormatBottomBarFileSize(u64 sizeBytes, wchar_t* out, u32 outCap) {
    if (!out || outCap == 0) return;
    float kb = static_cast<float>(sizeBytes) / kBottomBarBytesPerKb;
    bool useMb = sizeBytes >= kBottomBarBytesPerMb;
    float value = useMb ? kb / kBottomBarBytesPerKb : kb;

    i32 tenths = static_cast<i32>(value * 10.0f + 0.5f);
    i32 whole = tenths / 10;
    i32 frac = tenths % 10;

    u32 pos = 0;
    wchar_t digits[16];
    u32 digitCount = 0;
    i32 w = whole;
    if (w == 0) {
        digits[digitCount++] = L'0';
    } else {
        while (w > 0 && digitCount < 16) {
            digits[digitCount++] = static_cast<wchar_t>(L'0' + (w % 10));
            w /= 10;
        }
    }
    for (u32 i = 0; i < digitCount && pos + 1 < outCap; ++i) {
        out[pos++] = digits[digitCount - 1 - i];
    }
    if (pos + 1 < outCap) out[pos++] = L'.';
    if (pos + 1 < outCap) out[pos++] = static_cast<wchar_t>(L'0' + frac);
    if (pos + 1 < outCap) out[pos++] = L' ';
    const wchar_t* unit = useMb ? L"MB" : L"KB";
    for (u32 i = 0; unit[i] != L'\0' && pos + 1 < outCap; ++i) out[pos++] = unit[i];
    out[pos] = L'\0';
}

}  // namespace markair
