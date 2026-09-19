// 进程内共享单例:多窗口改造(每文件一进程 -> 进程内多顶层窗口)后，
// DirectWrite Factory/文本格式/中文回退链不再随窗口重复初始化，
// 全部窗口共用同一份 FontSubsystem 实例。
#pragma once

#include "../text/font.h"

namespace markair {

/**
 * 取得进程内唯一的字体子系统实例(首次调用时构造，之后一直存活到进程退出)。
 *
 * 注意:`FontSubsystem::zoomIndex_` 等缩放状态原先语义是"每个窗口/进程各自
 * 一份"，共享单例后缩放变成全局——任意窗口缩放会影响所有已开窗口。这是本次
 * 多窗口改造明确评估并接受的行为变化，不是遗留缺陷。
 *
 * @return 共享的 FontSubsystem 引用，调用方仍需自行调用一次 Init()。
 * @example markair::FontSubsystem& fonts = markair::SharedFontSubsystem();
 */
FontSubsystem& SharedFontSubsystem();

}  // namespace markair
