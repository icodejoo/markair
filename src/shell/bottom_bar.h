// markair 底部操作栏:常驻显示,左侧 6 个纯图标按钮
// (大纲/打开文档/主题切换/字体缩小/字体放大/查找),中间是状态文字区,最右侧是历史记录按钮。
#pragma once

#include "../util/types.h"

namespace markair {

// 底部栏固定高度(DIP),随 DPI 缩放(调用方按需再乘 DPI 缩放系数)。
constexpr float kBottomBarHeightDip = 24.0f;

// 底部栏左侧按钮数量与总按钮数。
constexpr u32 kBottomBarLeftButtonCount = 6;
constexpr u32 kBottomBarButtonCount = 8;  // 6 个在左侧 + 复制路径 + 历史,均在最右侧

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
    Outline = 0,  // 打开/关闭大纲侧栏
    OpenDoc = 1,  // 打开文档
    Theme = 2,    // 主题切换
    ZoomOut = 3,  // 字体缩小
    ZoomIn = 4,   // 字体放大
    Find = 5,     // 打开查找条(放大镜,与 Ctrl+F 同一路径)
    CopyPath = 6, // 复制当前文档全路径(紧贴历史按钮左侧;未打开文档时不存在)
    History = 7,  // 历史记录（位于底部栏最右侧）
    None = kBottomBarButtonCount,
};

// 按钮文字标签数组。
constexpr const wchar_t* kBottomBarLabels[kBottomBarButtonCount] = {
    L"大纲", L"打开", L"主题", L"缩小", L"放大", L"查找", L"复制当前文件路径", L"历史",
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

    if (index == static_cast<u32>(BottomBarButton::History)) {
        float right = clientWidthDip;
        float left = right - kBottomBarButtonWidthDip;
        if (left < 0.0f) left = 0.0f;
        return BottomBarButtonRect{left, top, right, bottom};
    }

    if (index == static_cast<u32>(BottomBarButton::CopyPath)) {
        // 紧贴历史按钮左侧,同样固定宽度的正方形按钮区。
        float historyLeft = clientWidthDip - kBottomBarButtonWidthDip;
        float right = historyLeft;
        float left = right - kBottomBarButtonWidthDip;
        if (left < 0.0f) left = 0.0f;
        return BottomBarButtonRect{left, top, right, bottom};
    }

    float left = kBottomBarButtonWidthDip * static_cast<float>(index);
    float right = kBottomBarButtonWidthDip * static_cast<float>(index + 1);
    return BottomBarButtonRect{left, top, right, bottom};
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

    // Check rightmost History button first
    float rightButtonLeft = clientWidthDip - kBottomBarButtonWidthDip;
    if (rightButtonLeft > 0.0f && pointXDip >= rightButtonLeft) {
        return BottomBarButton::History;
    }

    // CopyPath sits immediately left of History, only when a document is open.
    if (hasDocument) {
        float copyPathLeft = rightButtonLeft - kBottomBarButtonWidthDip;
        if (copyPathLeft > 0.0f && pointXDip >= copyPathLeft) {
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
