// mdvn 历史前进后退(T65):`Alt+←`/`Alt+→` 在"窗口内换文档"之间跳转。
//
// 本文件不 include <windows.h>,不碰任何 Win32 API——只处理"路径 + 滚动
// 位置"这一对定长记录的入栈/出栈,口径与 T56 的 window_state.h 一致,
// 目的同样是"能脱离图形环境单测"(见 tests/test_history.cpp)。
//
// 接入点是 M1 T36 已有的 `WindowState::openDocumentInPlace`(见 window.cpp
// 的 `OnLinkClicked`):每次"在当前窗口内替换文档"成功之前,调用方把**当前**
// 文档路径 + **当前** scrollY 传给 `PushNavigation`,它会压进后退栈并清空
// 前进栈。T36 ③ 锚点跳转(同文档内滚动)同样算一次导航——它不经过
// `openDocumentInPlace`,调用方需在滚动之前单独调一次 `PushNavigation`,
// 记录方式自然是"同路径 + 不同 scrollY"两条记录。
//
// `Alt+←`:调用方先 `PopBack` 拿到目标记录,尝试打开该路径;打开失败
// (文件已不存在/已被移走)时,记录已经在 `PopBack` 里被摘掉了,调用方只需
// 在窗口内提示,不弹 MessageBox,不做任何补救性的"放回栈里";打开成功后,
// 调用方把跳转前的当前状态传给 `PushForwardRaw`(**不清空**后退栈,与
// `PushNavigation` 的语义区分开)。`Alt+→` 反之:`PopForward` + `PushBackRaw`。
//
// F5 重载(T70,本任务不实现):它重新加载的是同一份文档,不是一次导航,
// **不应该调用本文件的任何 Push* 接口**——这正是把"记录一次导航"与
// "在当前窗口内替换文档"拆成两个独立调用点(`PushNavigation` 由调用方
// 显式触发,不是 `openDocumentInPlace` 内部自动做的)的原因:F5 的实现
// 只需要照常调用 `openDocumentInPlace`,跳过 `PushNavigation` 这一步即可,
// 接口层面天然支持"重新打开同一文档但不算导航"这种调用方式。
#pragma once

#include "../util/types.h"

namespace mdvn {

// 历史栈容量上限(裁决 #7):后退栈、前进栈各自最多保留这么多条,满了
// 丢最旧的一条(栈底),不是拒绝新条目。
constexpr u32 kHistoryCapacity = 32;

// 与 Win32 的 MAX_PATH 取值一致(260),但不 include <windows.h>,直接抄一份
// 数值——历史记录只是纯数据,不需要因为一个宏去背上 Win32 依赖。
constexpr u32 kHistoryPathCapacity = 260;

/**
 * 一条历史记录:文档路径 + 该文档当时的纵向滚动偏移(DIP)。
 * 定长 POD,便于用固定大小数组做环形缓冲(约 520B/条,32 条 ≈ 17KB)。
 */
struct HistoryEntry {
    wchar_t path[kHistoryPathCapacity]; // 完整路径,以 '\0' 结尾
    float scrollY;                       // 记录时的纵向滚动偏移(DIP)
};

/**
 * 历史前进/后退栈(T65)。后退栈与前进栈各是一个容量 `kHistoryCapacity`
 * 的环形缓冲实现的栈(LIFO):push 加到栈顶,满了就从栈底(最旧的一条)
 * 挤掉一条,而不是拒绝新条目。纯数据结构,不含任何 Win32 调用,不做任何
 * 动态分配(两个栈的存储都是类内定长数组)。
 *
 * @example
 *   mdvn::History history;
 *   history.PushNavigation(L"C:\\a.md", 0.0f);   // 从 a.md 导航离开前记一笔
 *   mdvn::HistoryEntry back;
 *   if (history.PopBack(&back)) { / * 打开 back.path,恢复 back.scrollY * / }
 */
class History {
public:
    /** 两个栈均为空,不含任何有副作用的初始化。 */
    History();

    /**
     * 记一次"导航"(在当前窗口内替换文档,或同文档内的锚点跳转):把
     * 调用方传入的**跳转前**状态压进后退栈,并清空前进栈。
     *
     * @param path 跳转前的文档完整路径,以 '\0' 结尾;超过
     *        `kHistoryPathCapacity - 1` 的部分会被截断。
     * @param scrollY 跳转前的纵向滚动偏移(DIP)。
     * @example history.PushNavigation(L"C:\\a.md", 120.0f);
     */
    void PushNavigation(const wchar_t* path, float scrollY);

    /**
     * 弹出后退栈栈顶(`Alt+←` 用)。栈为空时不改变任何状态,返回 false。
     * 弹出的记录随即从栈内移除——若调用方随后发现该路径已不存在,
     * 什么都不用做(记录已经被摘掉,不需要额外的"剔除"步骤)。
     *
     * @param out 成功时写入弹出的记录;非空。
     * @return 成功弹出返回 true;栈为空返回 false。
     * @example mdvn::HistoryEntry e; if (history.PopBack(&e)) { ... }
     */
    bool PopBack(HistoryEntry* out);

    /** `Alt+→` 用,语义与 `PopBack` 对称,操作前进栈。 */
    bool PopForward(HistoryEntry* out);

    /**
     * 后退导航成功后,把"后退前的当前状态"压进前进栈——**不清空**后退栈
     * (与 `PushNavigation` 的关键区别:这不是一次新导航,只是把刚才离开的
     * 那份状态存起来供 `Alt+→` 用)。
     */
    void PushForwardRaw(const wchar_t* path, float scrollY);

    /** 前进导航成功后对称调用,压回后退栈,同样不清空前进栈。 */
    void PushBackRaw(const wchar_t* path, float scrollY);

    // 当前后退栈条目数(0 时 `Alt+←` 应无动作)。
    u32 BackCount() const { return backCount_; }

    // 当前前进栈条目数(0 时 `Alt+→` 应无动作)。
    u32 ForwardCount() const { return forwardCount_; }

private:
    HistoryEntry backEntries_[kHistoryCapacity];    // 后退栈存储
    u32 backCount_;                                  // 后退栈当前条目数
    u32 backStart_;                                  // 后退栈栈底在数组里的物理下标

    HistoryEntry forwardEntries_[kHistoryCapacity]; // 前进栈存储
    u32 forwardCount_;                               // 前进栈当前条目数
    u32 forwardStart_;                               // 前进栈栈底在数组里的物理下标
};

}  // namespace mdvn
