// mdvn 底部操作栏(新需求,M3 完成后新增):常驻显示,左侧 5 个纯图标按钮
// (字体放大/字体缩小/主题切换/打开文档/打开大纲),右侧是状态文字区
// (当前文档路径 + 大小),不是按钮,不参与命中测试。
//
// 与 outline_panel.h 的"指针为空即不存在"不同——本栏默认展示且不可关闭,
// 因此不做成惰性构造的可选指针,布局关系是"挤压"(正文视口高度在窗口创建/
// resize 时减去本栏固定高度,不是像大纲侧栏那样运行期动态浮动覆盖)。
//
// 本文件不 include <windows.h>,几何计算/命中测试/文件大小格式化全部是纯
// 数字/字符串函数,可以直接链进 mdvn_tests.exe 单测(见 tests/test_bottom_bar.cpp)。
#pragma once

#include "../util/types.h"

namespace mdvn {

// 底部栏固定高度(DIP),随 DPI 缩放(调用方按需再乘 DPI 缩放系数),
// 不随字号缩放档位变化——按钮布局与正文字号是两件独立的事。32px 高度画不下
// "图标+文字"两行,因此图标不再带常驻文字标签,文字改成悬浮提示(见调用方)。
constexpr float kBottomBarHeightDip = 32.0f;

// 底部栏按钮总数,固定 5 个,不做可配置项。
constexpr u32 kBottomBarButtonCount = 5;

// 每个图标按钮的固定宽度(DIP),取值等于栏高度,做成正方形点击区——
// 与"整条宽度等分"不同,按钮不再随窗口宽度拉伸,多出的宽度让给右侧状态区。
constexpr float kBottomBarButtonWidthDip = kBottomBarHeightDip;

// 状态区右侧留白(DIP),避免文字紧贴窗口边缘。
constexpr float kBottomBarStatusPaddingDip = 10.0f;

// 1 MB 对应的字节数,文件大小格式化按这个阈值切换 KB/MB 单位。
constexpr u64 kBottomBarBytesPerMb = 1024ull * 1024ull;
constexpr float kBottomBarBytesPerKb = 1024.0f;

/**
 * 底部栏 5 个按钮的语义下标,与 `kBottomBarButtonCount` 长度、从左到右的
 * 排列顺序一一对应。`None` 表示"未命中任何按钮"(含落在右侧状态区的情况)。
 */
enum class BottomBarButton : u32 {
    ZoomIn = 0,   // 字体放大
    ZoomOut = 1,  // 字体缩小
    Theme = 2,    // 主题切换
    OpenDoc = 3,  // 打开文档
    Outline = 4,  // 打开/关闭大纲
    None = kBottomBarButtonCount,
};

// 5 个按钮从左到右的文字标签,下标须与 `BottomBarButton` 枚举顺序一一对应。
// 常驻标签取消后,这份文案仍在用——渲染层已不画标签文字,鼠标悬浮提示
// (window.cpp)与本文件的单测共用同一份,避免重复定义两份一样的字符串数组。
constexpr const wchar_t* kBottomBarLabels[kBottomBarButtonCount] = {
    L"放大", L"缩小", L"主题", L"打开", L"大纲",
};

// 一个按钮在客户区坐标系里的矩形(DIP)。
struct BottomBarButtonRect {
    float left;
    float top;
    float right;
    float bottom;
};

/**
 * 底部栏第 `index` 个按钮的矩形(DIP):固定宽度 `kBottomBarButtonWidthDip`,
 * 从左边起顺序排列、紧贴左侧,纵向铺满底部栏高度。右侧剩余宽度是状态区,
 * 不由本函数计算(状态区不是按钮)。纯几何函数。
 * @param index 按钮下标,须 < kBottomBarButtonCount(调用方保证)。
 * @param clientHeightDip 客户区高度(DIP)。
 * @return 该按钮的矩形。
 * @example auto r = mdvn::BottomBarButtonRectDip(0, 600.0f);
 */
inline BottomBarButtonRect BottomBarButtonRectDip(u32 index, float clientHeightDip) {
    float top = clientHeightDip - kBottomBarHeightDip;
    return BottomBarButtonRect{
        kBottomBarButtonWidthDip * static_cast<float>(index), top,
        kBottomBarButtonWidthDip * static_cast<float>(index + 1), clientHeightDip,
    };
}

/**
 * 判断一个点(客户区坐标,DIP)是否落在底部栏区域内——命中测试短路入口
 * (与大纲侧栏 `IsPointInOutlinePanel` 同一用法),防止底部栏区域内的点击
 * 被误判为文本拖选起点或正文命中。本判断覆盖整条栏(按钮区 + 状态区)。
 * @param clientHeightDip 客户区高度(DIP)。
 * @param pointYDip 点的纵坐标(DIP)。
 * @return 落在底部栏区域内返回 true。
 * @example bool inBar = mdvn::IsPointInBottomBar(600.0f, 590.0f); // true
 */
inline bool IsPointInBottomBar(float clientHeightDip, float pointYDip) {
    return pointYDip >= clientHeightDip - kBottomBarHeightDip;
}

/**
 * 命中测试:客户区坐标(DIP)落在哪个底部栏按钮上。调用方应先用
 * `IsPointInBottomBar` 判断纵坐标是否落在栏内(本函数只按横坐标分段,
 * 不重复判断纵坐标)。只在左侧 `5 * kBottomBarButtonWidthDip` 范围内按列
 * 命中,超出这个范围(落在右侧状态区)返回 `BottomBarButton::None`——状态区
 * 不是按钮,不应该被当成"最后一个按钮"钳制进去。
 * @param clientWidthDip 客户区宽度(DIP)。
 * @param pointXDip 点的横坐标(DIP)。
 * @return 命中的按钮;落在按钮区之外(含 `clientWidthDip <= 0`)返回
 *         `BottomBarButton::None`。
 * @example auto btn = mdvn::HitTestBottomBar(800.0f, 40.0f); // ZoomOut
 */
inline BottomBarButton HitTestBottomBar(float clientWidthDip, float pointXDip) {
    if (clientWidthDip <= 0.0f) return BottomBarButton::None;
    if (pointXDip < 0.0f) return BottomBarButton::None;
    float buttonsWidth = kBottomBarButtonWidthDip * static_cast<float>(kBottomBarButtonCount);
    if (pointXDip >= buttonsWidth) return BottomBarButton::None;  // 落在右侧状态区
    i32 idx = static_cast<i32>(pointXDip / kBottomBarButtonWidthDip);
    if (idx < 0) idx = 0;
    if (idx >= static_cast<i32>(kBottomBarButtonCount)) idx = kBottomBarButtonCount - 1;
    return static_cast<BottomBarButton>(idx);
}

/**
 * 把文件大小格式化成状态区展示用的文字:小于 1MB 显示 KB,否则显示 MB,
 * 都保留 1 位小数(如 "856.0 KB" / "2.3 MB")。纯字符串函数,不做本地化。
 * @param sizeBytes 文件字节数。
 * @param out 输出缓冲区,以 '\0' 结尾。
 * @param outCap 输出缓冲区容量(含结尾符),调用方保证 >= 16。
 * @example wchar_t buf[32]; mdvn::FormatBottomBarFileSize(2415919, buf, 32); // "2.3 MB"
 */
inline void FormatBottomBarFileSize(u64 sizeBytes, wchar_t* out, u32 outCap) {
    if (!out || outCap == 0) return;
    float kb = static_cast<float>(sizeBytes) / kBottomBarBytesPerKb;
    bool useMb = sizeBytes >= kBottomBarBytesPerMb;
    float value = useMb ? kb / kBottomBarBytesPerKb : kb;
    // 手写格式化(不用 CRT 的 swprintf 浮点格式化以外的花样):四舍五入到 1 位
    // 小数,再拼上单位——与本项目其余手写字符串拼接风格(见 window.cpp
    // CopyTruncatedPath)一致,避免引入本文件不需要的依赖。
    i32 tenths = static_cast<i32>(value * 10.0f + 0.5f);
    i32 whole = tenths / 10;
    i32 frac = tenths % 10;

    u32 pos = 0;
    // 整数部分(至少一位)。
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

}  // namespace mdvn
