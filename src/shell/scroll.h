// mdvn 的滚动位置计算(T12):把"滚轮增量 / 翻页 / 首尾跳转 -> 新的滚动偏移"
// 这部分纯数字逻辑从窗口过程里剥离出来,不依赖 HWND、不 include <windows.h>,
// 因此可以直接链进 mdvn_tests.exe 做单元测试(见 tests/test_scroll.cpp)。
//
// 约定:滚动偏移 scrollY 的单位是 DIP,0 表示文档顶部对齐视口顶部,
// 取值恒被夹在 [0, MaxScrollOffset()] 区间内。
#pragma once

namespace mdvn {

// 一次标准滚轮刻度的增量(等同 Win32 的 WHEEL_DELTA;这里自定义常量以免本头文件依赖 windows.h)。
constexpr int kWheelDeltaUnit = 120;

// 一次滚轮刻度滚动的行数,取 Windows 默认值 3 行(不读注册表,避免启动期额外 I/O)。
constexpr int kWheelScrollLines = 3;

// 滚动计算用的标称行高(DIP),与 layout 模块正文行高保持一致。
constexpr float kScrollLineHeightDip = 20.0f;

/**
 * 键盘触发的滚动动作,供 `ApplyScrollCommand` 分发。
 * 与具体虚拟键码解耦,便于脱离 Win32 做单元测试。
 */
enum class ScrollCommand {
    LineUp,     // 向上一行
    LineDown,   // 向下一行
    PageUp,     // 向上一整屏(PageUp)
    PageDown,   // 向下一整屏(PageDown)
    Home,       // 跳到文档顶部
    End,        // 跳到文档底部
};

/**
 * 计算允许的最大滚动偏移。
 * @param totalHeight 文档总高度(DIP)。
 * @param viewportHeight 视口高度(DIP)。
 * @return `max(0, totalHeight - viewportHeight)`;文档比视口短时返回 0。
 * @example float maxY = mdvn::MaxScrollOffset(layout.TotalHeight(), 600.0f); // 文档 1000 时得 400
 */
inline float MaxScrollOffset(float totalHeight, float viewportHeight) {
    float maxScroll = totalHeight - viewportHeight;
    return maxScroll > 0.0f ? maxScroll : 0.0f;
}

/**
 * 把任意滚动偏移夹到合法区间 `[0, MaxScrollOffset(...)]`。
 * @param scrollY 待夹取的滚动偏移(DIP),允许为负或超界。
 * @param totalHeight 文档总高度(DIP)。
 * @param viewportHeight 视口高度(DIP)。
 * @return 夹取后的合法滚动偏移(DIP)。
 * @example float y = mdvn::ClampScrollOffset(-50.0f, 1000.0f, 600.0f); // 得 0
 */
inline float ClampScrollOffset(float scrollY, float totalHeight, float viewportHeight) {
    float maxScroll = MaxScrollOffset(totalHeight, viewportHeight);
    if (scrollY < 0.0f) return 0.0f;
    if (scrollY > maxScroll) return maxScroll;
    return scrollY;
}

/**
 * 按滚轮增量算出新的滚动偏移并夹到合法区间。
 * 遵循 Win32 约定:`wheelDelta` 为正表示滚轮前推(内容向下移、scrollY 减小)。
 *
 * @param scrollY 当前滚动偏移(DIP)。
 * @param wheelDelta `GET_WHEEL_DELTA_WPARAM(wparam)` 取到的原始增量,一刻度为 ±120。
 * @param totalHeight 文档总高度(DIP)。
 * @param viewportHeight 视口高度(DIP)。
 * @return 夹取后的新滚动偏移(DIP)。
 * @example float y = mdvn::ScrollByWheel(0.0f, -120, 1000.0f, 600.0f); // 向下滚一刻度,得 60
 */
inline float ScrollByWheel(float scrollY, int wheelDelta, float totalHeight, float viewportHeight) {
    float notches = static_cast<float>(wheelDelta) / static_cast<float>(kWheelDeltaUnit);
    float deltaDip = notches * static_cast<float>(kWheelScrollLines) * kScrollLineHeightDip;
    return ClampScrollOffset(scrollY - deltaDip, totalHeight, viewportHeight);
}

/**
 * 按键盘滚动动作算出新的滚动偏移并夹到合法区间。
 * 翻页按整个视口高度跳转,Home/End 直接跳到文档首/尾。
 *
 * @param scrollY 当前滚动偏移(DIP)。
 * @param command 要执行的滚动动作。
 * @param totalHeight 文档总高度(DIP)。
 * @param viewportHeight 视口高度(DIP)。
 * @return 夹取后的新滚动偏移(DIP)。
 * @example float y = mdvn::ApplyScrollCommand(0.0f, mdvn::ScrollCommand::End, 1000.0f, 600.0f); // 得 400
 */
inline float ApplyScrollCommand(float scrollY, ScrollCommand command,
                                float totalHeight, float viewportHeight) {
    switch (command) {
    case ScrollCommand::LineUp:
        return ClampScrollOffset(scrollY - kScrollLineHeightDip, totalHeight, viewportHeight);
    case ScrollCommand::LineDown:
        return ClampScrollOffset(scrollY + kScrollLineHeightDip, totalHeight, viewportHeight);
    case ScrollCommand::PageUp:
        return ClampScrollOffset(scrollY - viewportHeight, totalHeight, viewportHeight);
    case ScrollCommand::PageDown:
        return ClampScrollOffset(scrollY + viewportHeight, totalHeight, viewportHeight);
    case ScrollCommand::Home:
        return 0.0f;
    case ScrollCommand::End:
        return MaxScrollOffset(totalHeight, viewportHeight);
    }
    return ClampScrollOffset(scrollY, totalHeight, viewportHeight);
}

}  // namespace mdvn
