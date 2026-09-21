// markair 按钮抽象外壳(纯头文件,纯函数 + POD 结构体,风格对齐 sidebar.h)。
//
// 本文件不是从零设计一套 UI 控件树——这个代码库是立即模式的 Win32 +
// Direct2D 渲染,没有任何带虚函数的控件体系。这里只做一件事:把底部栏
// 按钮 / 欢迎屏按钮 / 侧栏行内按钮 / 代码块复制按钮 / 侧栏头部按钮这
// 5 套原本各自实现一遍的"矩形几何 + 命中测试 + 悬浮提示气泡"收敛成
// 一套可单测的纯函数,同时给出 Button/IconButton 两个数据结构体统一描述
// 按钮的可配置属性(宽高/圆角/文案/图标)。
//
// 手绘矢量图标(底部栏的时钟/放大镜/齿轮等)不会被压成一个图标枚举——
// 图标内容通过 ButtonIconPaintFn 回调交回调用方自己画,保持原有视觉不变。
#pragma once

#include "../util/types.h"

namespace markair {

/**
 * 按钮矩形几何(DIP),5 套按钮系统统一使用的坐标结构。
 *
 * 字段含义与既有的 BottomBarButtonRect/WelcomeButtonRect/SidebarRectDip
 * 完全一致(左上右下),只是给了它们一个共同的名字,方便新写的通用几何/
 * 命中测试函数不必对每套按钮系统各写一份重载。
 */
struct ButtonRectDip {
    float left;
    float top;
    float right;
    float bottom;

    float Width() const { return right - left; }
    float Height() const { return bottom - top; }
};

/**
 * 判断一个点(DIP)是否落在矩形内。半开区间 [left, right) x [top, bottom)。
 * 纯函数。
 *
 * 这是本文件收敛的核心原语——此前底部栏/欢迎屏/侧栏三套系统各自手写过
 * 一遍等价的 `x>=l && x<r && y>=t && y<b` 比较。
 *
 * @param left 矩形左边界(DIP)。
 * @param top 矩形上边界(DIP)。
 * @param right 矩形右边界(DIP)。
 * @param bottom 矩形下边界(DIP)。
 * @param x 待判定点横坐标(DIP)。
 * @param y 待判定点纵坐标(DIP)。
 * @return 落在矩形内返回 true。
 * @example bool hit = markair::PointInRectDip(0, 0, 24, 24, 10, 10); // true
 */
inline bool PointInRectDip(float left, float top, float right, float bottom, float x, float y) {
    return x >= left && x < right && y >= top && y < bottom;
}

/**
 * 判断一个点(DIP)是否落在矩形内。闭区间 [left, right] x [top, bottom]。纯函数。
 *
 * 与 PointInRectDip 的差异仅在边界是否算命中——部分既有调用点(侧栏行内
 * 按钮/文件夹侧栏头部按钮)历史上用的是 `<=` 闭区间比较,这里原样保留
 * 这个既有口径,不在重构时顺带改成半开区间。
 *
 * @param left 矩形左边界(DIP)。
 * @param top 矩形上边界(DIP)。
 * @param right 矩形右边界(DIP)。
 * @param bottom 矩形下边界(DIP)。
 * @param x 待判定点横坐标(DIP)。
 * @param y 待判定点纵坐标(DIP)。
 * @return 落在矩形内(含边界)返回 true。
 * @example bool hit = markair::PointInRectDipInclusive(0, 0, 24, 24, 24, 24); // true
 */
inline bool PointInRectDipInclusive(float left, float top, float right, float bottom, float x, float y) {
    return x >= left && x <= right && y >= top && y <= bottom;
}

/**
 * 根据按钮矩形是否落在给定点判断命中。ButtonRectDip 版本的 PointInRectDip,
 * 方便直接传矩形而不用拆字段。纯函数。
 *
 * @param rect 按钮矩形(DIP)。
 * @param x 待判定点横坐标(DIP)。
 * @param y 待判定点纵坐标(DIP)。
 * @return 落在矩形内返回 true。
 * @example bool hit = markair::PointInButtonRectDip(rect, dipX, dipY);
 */
inline bool PointInButtonRectDip(const ButtonRectDip& rect, float x, float y) {
    return PointInRectDip(rect.left, rect.top, rect.right, rect.bottom, x, y);
}

/**
 * 图标绘制回调:在给定矩形区域内画出按钮的图标内容。渲染后端无关——
 * 调用方把自己的 D2D render target 通过 renderCtx 传入并 reinterpret_cast
 * 回具体类型,userData 用来传回自定义画法需要的额外参数(比如画刷指针)。
 * 这样底部栏那些"齿轮/放大镜/时钟"手绘矢量图标可以继续保持原有画法,不
 * 强行压缩进一个内置图元枚举里。
 *
 * @param renderCtx 调用方自定义的渲染上下文指针(如 ID2D1RenderTarget*)。
 * @param rect 图标应绘制在其中的矩形区域(DIP)。
 * @param userData 调用方自定义的附加数据指针,可为空。
 */
using ButtonIconPaintFn = void (*)(void* renderCtx, const ButtonRectDip& rect, void* userData);

/**
 * 通用按钮的可配置属性:宽高、圆角、文案、悬浮提示、图标绘制回调。
 *
 * 这是一个纯数据结构体(不是带虚函数的控件基类)——本代码库的绘制是立即
 * 模式,按钮"画出来"仍由各调用点自己在每帧调 paintIcon/画文字完成,这个
 * 结构体只负责描述"这个按钮长什么样、点哪里算命中、悬浮提示写什么"。
 *
 * @example
 *   markair::Button btn{ 140.0f, 32.0f, 4.0f, L"打开文件", L"打开文件", nullptr, nullptr };
 *   markair::ButtonRectDip rect = markair::ButtonRectFromCenterDip(btn, centerX, centerY);
 */
struct Button {
    float width;                  // 按钮宽度(DIP)
    float height;                 // 按钮高度(DIP)
    float radius;                 // 圆角半径(DIP),0 表示直角
    const wchar_t* label;         // 按钮上显示的文字,可为空(纯图标按钮)
    const wchar_t* title;         // 悬浮提示(tooltip)文案,可为空(不显示提示)
    ButtonIconPaintFn paintIcon;  // 图标绘制回调,可为空(纯文字按钮)
    void* iconUserData;           // 传给 paintIcon 的附加数据,可为空
};

/**
 * 图标按钮:在 Button 基础上"继承"——忽略 width/height,改用正方形边长
 * size(即 width == height == size)。没有 label 字段,图标按钮不带文字。
 *
 * C++ 里没有走虚函数继承,而是通过 ToButton() 转换成通用 Button 视图来
 * 复用 Button 的几何/命中测试函数,语义上等价于"IconButton 是 Button 的
 * 一种特化"。
 *
 * @example
 *   markair::IconButton btn{ 24.0f, 4.0f, L"查找", DrawFindIcon, nullptr };
 *   markair::ButtonRectDip rect = markair::IconButtonRectDip(btn, centerX, centerY);
 */
struct IconButton {
    float size;                   // 正方形边长(DIP),等价于 width == height
    float radius;                 // 圆角半径(DIP)
    const wchar_t* title;         // 悬浮提示(tooltip)文案,可为空
    ButtonIconPaintFn paintIcon;  // 图标绘制回调,可为空
    void* iconUserData;           // 传给 paintIcon 的附加数据,可为空

    /**
     * 转换成通用 Button 视图(width = height = size,label 为空)。
     * @return 等价的 Button 值。
     */
    Button ToButton() const {
        return Button{size, size, radius, nullptr, title, paintIcon, iconUserData};
    }
};

/**
 * 根据按钮的宽高与几何中心点计算按钮矩形(DIP)。纯几何函数。
 *
 * @param btn 按钮属性(取其 width/height)。
 * @param centerX 按钮几何中心横坐标(DIP)。
 * @param centerY 按钮几何中心纵坐标(DIP)。
 * @return 按钮矩形。
 * @example auto rect = markair::ButtonRectFromCenterDip(btn, 100.0f, 50.0f);
 */
inline ButtonRectDip ButtonRectFromCenterDip(const Button& btn, float centerX, float centerY) {
    return ButtonRectDip{
        centerX - btn.width * 0.5f,
        centerY - btn.height * 0.5f,
        centerX + btn.width * 0.5f,
        centerY + btn.height * 0.5f,
    };
}

/**
 * 根据图标按钮的正方形边长与几何中心点计算按钮矩形(DIP)。纯几何函数。
 *
 * @param btn 图标按钮属性(取其 size)。
 * @param centerX 按钮几何中心横坐标(DIP)。
 * @param centerY 按钮几何中心纵坐标(DIP)。
 * @return 按钮矩形(正方形)。
 * @example auto rect = markair::IconButtonRectDip(btn, 100.0f, 50.0f); // 24x24 正方形
 */
inline ButtonRectDip IconButtonRectDip(const IconButton& btn, float centerX, float centerY) {
    return ButtonRectFromCenterDip(btn.ToButton(), centerX, centerY);
}

/**
 * 悬浮提示气泡的竖直位置(DIP)。
 */
struct TooltipVerticalDip {
    float top;
    float bottom;
};

/**
 * 计算悬浮提示气泡的水平位置(DIP)——以期望的左边界为起点,钳制在
 * [marginDip, clientWidthDip - marginDip] 范围内,保证气泡不会画出屏幕外。
 * 纯函数。
 *
 * 5 套按钮系统里,气泡的水平定位策略并不完全一样(有的居中于锚点、有的
 * 左对齐于某个参照点),因此这里只收敛"钳制"这一段公共逻辑——调用方
 * 自己算出期望左边界(居中时传 `anchorCenterX - bubbleWidth * 0.5f`),
 * 再交给本函数钳到屏幕范围内。
 *
 * @param desiredLeft 未经钳制的期望气泡左边界(DIP)。
 * @param bubbleWidth 气泡宽度(DIP)。
 * @param clientWidthDip 客户区宽度(DIP)。
 * @param marginDip 气泡与屏幕左右边缘的最小间距(DIP)。
 * @return 钳制后的气泡左边界(DIP)。
 * @example float left = markair::ClampTooltipLeftDip(centerX - 40.0f, 80.0f, 800.0f, 4.0f);
 */
inline float ClampTooltipLeftDip(float desiredLeft, float bubbleWidth, float clientWidthDip, float marginDip) {
    float left = desiredLeft;
    if (left < marginDip) left = marginDip;
    if (left + bubbleWidth > clientWidthDip - marginDip) left = clientWidthDip - marginDip - bubbleWidth;
    return left;
}

/**
 * 计算悬浮提示气泡的竖直位置(DIP)——优先显示在锚点矩形上方(与锚点上边
 * 留 gapDip 间隙);若上方放不下(算出的 top < minTopDip),退化显示在
 * 锚点矩形下方;若下方又超出 maxBottomDip,再整体上移贴住 maxBottomDip。
 * 纯函数。
 *
 * 这是底部栏图标提示气泡与侧栏行内按钮悬浮提示气泡原本各自实现的同一套
 * "优先上方、放不下退化下方"定位逻辑,现在只有一份。
 *
 * @param anchorTop 锚点矩形上边界(DIP)。
 * @param anchorBottom 锚点矩形下边界(DIP)。
 * @param bubbleHeight 气泡高度(DIP)。
 * @param gapDip 气泡与锚点之间的间隙(DIP)。
 * @param minTopDip 气泡允许出现的最小 top(DIP),低于此值则退化到锚点下方。
 * @param maxBottomDip 气泡允许出现的最大 bottom(DIP),默认不限制。
 * @return 气泡的 top/bottom(DIP)。
 * @example auto v = markair::TooltipBubbleVerticalDip(100.0f, 124.0f, 22.0f, 4.0f, 40.0f);
 */
inline TooltipVerticalDip TooltipBubbleVerticalDip(float anchorTop, float anchorBottom, float bubbleHeight,
                                                    float gapDip, float minTopDip,
                                                    float maxBottomDip = 1e9f) {
    float bottom = anchorTop - gapDip;
    float top = bottom - bubbleHeight;
    if (top < minTopDip) {
        top = anchorBottom + gapDip;
        bottom = top + bubbleHeight;
    }
    if (bottom > maxBottomDip) {
        bottom = maxBottomDip;
        top = bottom - bubbleHeight;
    }
    return TooltipVerticalDip{top, bottom};
}

}  // namespace markair
