// markair 有两处原生 Win32 EDIT 子窗口(查找条输入框、文件夹侧栏过滤输入框):
// 各自要做"创建+子类化+关主题"、"按 DPI/字号缩放重定位+重建字体"这两组
// 完全一样的事，之前各写一份、容易改一处漏一处(比如只给查找条修了关主题
// 的 bug，过滤框没跟着改)。这里把这两组公共步骤收成两个小函数，两个调用
// 点各自传自己的参数(子类化回调、控件 id、几何、字号)，子类化回调本身
// (Enter/Esc/F3 等按键转发逻辑两处差异较大)仍由调用方各自实现、作为参数
// 传进来，不强行合并。
//
// 边框视觉(EditBoxBorderStyle)是本次新增的能力：过去两处输入框各画各的
// 容器矩形，没有统一的"完整边框/仅下边框"概念——这里先把这两种视觉定义
// 成一个枚举，交给 renderer.cpp 里的绘制函数按枚举取值选择画法，本文件
// 只放纯声明，不接触任何 D2D 句柄。
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace markair {

// 输入框容器的边框视觉方案，由 D2D 渲染层(renderer.cpp)按取值选择画法。
//
// Bordered:      四周完整边框(圆角矩形描边)。
// UnderlineOnly: 仅底部一条下划线，其余三边不画线。
// Borderless:    不画任何描边/下划线，纯靠输入框自身背景色跟容器融为一体。
enum class EditBoxBorderStyle {
    Bordered,
    UnderlineOnly,
    Borderless,
};

/**
 * 创建一个原生 EDIT 子窗口并完成子类化、关闭视觉主题这两步公共初始化。
 *
 * 关主题的原因:主题化的 EDIT 在深色背景下会忽略 WM_CTLCOLOREDIT 里
 * SetTextColor 设的文字色，固定按主题引擎自己的浅色方案画黑字——这是
 * 已知的 Win32 坑，禁用主题后才会真正采用经典消息路径的自定义颜色。
 *
 * @param parent 父窗口句柄。
 * @param instance 模块实例句柄，透传给 CreateWindowExW。
 * @param controlId 子窗口控件 id(WM_COMMAND/WM_CTLCOLOREDIT 里用于区分来源)。
 * @param subclassProc 子类化后接管的窗口过程，原窗口过程保存进 GWLP_USERDATA。
 * @return 创建成功返回新窗口句柄，创建失败返回 nullptr(不弹窗，调用方决定如何处理)。
 * @example
 *   HWND h = markair::CreateSubclassedEditBox(hwnd, instance, kFindEditControlId,
 *                                              FindEditSubclassProc);
 */
HWND CreateSubclassedEditBox(HWND parent, HINSTANCE instance, int controlId,
                              WNDPROC subclassProc);

/**
 * 按给定的 DIP 矩形与当前 DPI 缩放系数重新定位 EDIT 子窗口，并在缩放系数
 * 变化时按新字号重建字体(避免每次 WM_SIZE 都重建 GDI 字体对象)。
 *
 * @param editHwnd 要重定位的 EDIT 子窗口句柄，为空时静默返回。
 * @param scale 当前 DPI 缩放系数(相对 96 DPI 基准)。
 * @param xDip/yDip/wDip/hDip 目标矩形，DIP 单位，内部按 scale 换算成物理像素。
 * @param fontSizeDip 字体大小(DIP)，用于换算成实际像素高度构造 LOGFONTW。
 * @param fontScaleCache 调用方持有的"上次生效缩放系数"缓存(如
 *        WindowState::findEditFontScale)，仅当与当前 scale 不同才重建字体，
 *        重建后写回本次的 scale。
 * @param marginLeftDip/marginRightDip 文字左右内边距(DIP)，通过 EM_SETMARGINS
 *        实现——窗口矩形本身铺满调用方传入的容器矩形，不再用缩小窗口矩形的
 *        方式留白，避免视觉边框跨越整个容器、而 EDIT 背景只占中间一小块、
 *        两侧露出面板底色的问题。默认 0 表示不设内边距。
 * @example
 *   markair::RepositionEditBox(state->findEditHwnd, scale, layout.editLeft,
 *       layout.editTop, layout.editWidth, layout.editHeight,
 *       kFindBarFontSizeDip, &state->findEditFontScale);
 */
void RepositionEditBox(HWND editHwnd, float scale, float xDip, float yDip, float wDip,
                        float hDip, float fontSizeDip, float* fontScaleCache,
                        float marginLeftDip = 0.0f, float marginRightDip = 0.0f);

}  // namespace markair
