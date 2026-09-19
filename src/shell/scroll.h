// markair 的滚动位置计算(T12):把"滚轮增量 / 翻页 / 首尾跳转 -> 新的滚动偏移"
// 这部分纯数字逻辑从窗口过程里剥离出来,不依赖 HWND、不 include <windows.h>,
// 因此可以直接链进 markair_tests.exe 做单元测试(见 tests/test_scroll.cpp)。
//
// 约定:滚动偏移 scrollY 的单位是 DIP,0 表示文档顶部对齐视口顶部,
// 取值恒被夹在 [0, MaxScrollOffset()] 区间内。
//
// 内容内边距(2026 追加):文档窗口上下左右统一留 12 DIP 内边距,同样是纯数字
// 计算(客户区尺寸 -> 换行宽度 / 可用视口高度),放在这里与滚动语义天然贴近——
// "可用视口高度"直接喂给下面这些 Clamp/Apply 函数当 viewportHeight 用。
#pragma once

#include "../util/types.h"

namespace markair {

// 一次标准滚轮刻度的增量(等同 Win32 的 WHEEL_DELTA;这里自定义常量以免本头文件依赖 windows.h)。
constexpr int kWheelDeltaUnit = 120;

// 一次滚轮刻度滚动的行数,取 Windows 默认值 3 行(不读注册表,避免启动期额外 I/O)。
constexpr int kWheelScrollLines = 3;

// 滚动计算用的标称行高(DIP),与 layout 模块正文行高保持一致。
constexpr float kScrollLineHeightDip = 20.0f;

// 文档窗口统一的内容内边距(DIP):上下左右各留这么多空白。
// 全项目唯一定义,换行宽度收窄、滚动范围底部留白、渲染水平平移、命中测试坐标
// 换算都引用这一个常量,不重复硬编码。
constexpr float kContentPaddingDip = 12.0f;

/**
 * 客户区宽度收窄成正文换行宽度(左右各减去一个内边距)。
 * @param clientWidthDip 客户区宽度(DIP)。
 * @return `max(0, clientWidthDip - 2 * kContentPaddingDip)`;极窄窗口下夹到 0,
 *         避免传给 `Relayout` 负数换行宽度。
 * @example float w = markair::ContentWidthDip(800.0f); // 得 776
 */
inline float ContentWidthDip(float clientWidthDip) {
    float w = clientWidthDip - 2.0f * kContentPaddingDip;
    return w > 0.0f ? w : 0.0f;
}

/**
 * 客户区高度收窄成"可用视口高度"(上下各减去一个内边距),喂给
 * `ClampScrollOffset`/`MaxScrollOffset`/`ApplyScrollCommand` 当 viewportHeight 用——
 * 这样算出来的滚动上限会比真实视口高度更大,滚到底时文档下方自然留出
 * `kContentPaddingDip` 的空白(上边距则由渲染时的 `scrollY - kContentPaddingDip`
 * 平移覆盖,不需要在这里体现)。
 * @param clientHeightDip 客户区高度(DIP)。
 * @return `max(0, clientHeightDip - 2 * kContentPaddingDip)`。
 * @example float h = markair::UsableViewportHeightDip(600.0f); // 得 576
 */
inline float UsableViewportHeightDip(float clientHeightDip) {
    float h = clientHeightDip - 2.0f * kContentPaddingDip;
    return h > 0.0f ? h : 0.0f;
}

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
 * @example float maxY = markair::MaxScrollOffset(layout.TotalHeight(), 600.0f); // 文档 1000 时得 400
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
 * @example float y = markair::ClampScrollOffset(-50.0f, 1000.0f, 600.0f); // 得 0
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
 * @example float y = markair::ScrollByWheel(0.0f, -120, 1000.0f, 600.0f); // 向下滚一刻度,得 60
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
 * @example float y = markair::ApplyScrollCommand(0.0f, markair::ScrollCommand::End, 1000.0f, 600.0f); // 得 400
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

// Win32 `WM_VSCROLL`/`WM_HSCROLL` 的请求码(`LOWORD(wParam)`),数值取自
// winuser.h 的 SB_* 宏——这里自定义常量以免本头文件依赖 windows.h。
constexpr int kSbLineUp = 0;
constexpr int kSbLineDown = 1;
constexpr int kSbPageUp = 2;
constexpr int kSbPageDown = 3;
constexpr int kSbThumbPosition = 4;
constexpr int kSbThumbTrack = 5;
constexpr int kSbTop = 6;
constexpr int kSbBottom = 7;

/**
 * 拖动滑块(松手前实时预览,或松手那一刻)是否是本次 `WM_VSCROLL` 的请求码。
 * 这两种请求码携带的是"绝对目标位置",不是相对动作,因此不进
 * `ScrollCommandFromScrollBarCode` 的映射,需要调用方另外取滑块位置处理。
 * @param code `LOWORD(wParam)`。
 * @return 是 `SB_THUMBTRACK`/`SB_THUMBPOSITION` 之一返回 true。
 * @example if (markair::IsThumbScrollCode(LOWORD(wParam))) { / * 读 nTrackPos * / }
 */
inline bool IsThumbScrollCode(int code) {
    return code == kSbThumbTrack || code == kSbThumbPosition;
}

/**
 * 把原生滚动条 `WM_VSCROLL` 的请求码映射成已有的 `ScrollCommand`,复用键盘
 * 滚动同一套语义(`SB_PAGEUP/PAGEDOWN` 对应 `PageUp/PageDown`,`SB_TOP/BOTTOM`
 * 对应 `Home/End`,`SB_LINEUP/LINEDOWN` 对应 `LineUp/LineDown`)。
 *
 * 拖动滑块(`SB_THUMBTRACK`/`SB_THUMBPOSITION`)不是相对动作,映射不到任何
 * `ScrollCommand`,返回 false——调用前应先用 `IsThumbScrollCode` 判断并走
 * 绝对定位分支(取滑块位置后直接 `ClampScrollOffset`)。`SB_ENDSCROLL` 等
 * 其余请求码同样返回 false(不产生滚动)。
 *
 * @param code `LOWORD(wParam)`。
 * @param out 命中时写入对应的 `ScrollCommand`;不命中时不修改。
 * @return 成功映射返回 true。
 * @example
 *   markair::ScrollCommand cmd;
 *   if (markair::ScrollCommandFromScrollBarCode(LOWORD(wParam), &cmd)) {
 *       float y = markair::ApplyScrollCommand(scrollY, cmd, totalHeight, viewportHeight);
 *   }
 */
inline bool ScrollCommandFromScrollBarCode(int code, ScrollCommand* out) {
    if (!out) return false;
    switch (code) {
    case kSbLineUp:   *out = ScrollCommand::LineUp;   return true;
    case kSbLineDown: *out = ScrollCommand::LineDown; return true;
    case kSbPageUp:   *out = ScrollCommand::PageUp;   return true;
    case kSbPageDown: *out = ScrollCommand::PageDown; return true;
    case kSbTop:      *out = ScrollCommand::Home;     return true;
    case kSbBottom:   *out = ScrollCommand::End;      return true;
    default:          return false;
    }
}

/**
 * T70:F5 重载后按块下标近似恢复滚动位置——把"重载前视口顶部对应的块
 * 下标"钳制到新文档的块数范围内。故意不做 diff/内容指纹匹配(文档被编辑
 * 后下标当然可能偏,但"大致回到刚才那一段"已经够用,多一份新旧文档同时
 * 驻留内存不值得),纯数字计算,不依赖 Win32/布局对象,可脱离图形环境单测。
 * @param topBlockIdx 重载前视口顶部对应的块下标。
 * @param newBlockCount 重载后新文档的块总数。
 * @return `newBlockCount == 0` 时返回 0;否则 `min(topBlockIdx, newBlockCount - 1)`。
 * @example markair::ClampReloadTopBlockIndex(5, 3); // 新文档只剩 3 块,得 2
 */
inline u32 ClampReloadTopBlockIndex(u32 topBlockIdx, u32 newBlockCount) {
    if (newBlockCount == 0) return 0;
    return (topBlockIdx < newBlockCount) ? topBlockIdx : (newBlockCount - 1);
}

}  // namespace markair
