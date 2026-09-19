// markair 欢迎屏(未打开任何文档时的静态引导页,取代示例 markdown):
// 只提供"打开文件"按钮的几何计算与命中测试两个纯函数,不含任何绘制逻辑——
// 绘制交给 render/renderer.cpp 的 DrawWelcomeScreen(几何常量在那边各自
// 复制一份字面量,与底部栏/大纲侧栏的既有约定一致,render 不反向 include
// shell 头文件)。
#pragma once

namespace markair {

// 欢迎屏"打开文件"按钮尺寸(DIP):足够宽大醒目,不易误触到旁边区域。
constexpr float kWelcomeButtonWidthDip = 140.0f;
constexpr float kWelcomeButtonHeightDip = 32.0f;

// 按钮顶部相对客户区高度的位置系数:水平居中,垂直位置固定在标题下方一段
// 距离处(用比例而不是绝对像素,窗口拉伸时按钮位置跟着按比例走)。
constexpr float kWelcomeButtonTopRatio = 0.46f;

// 欢迎屏按钮在客户区坐标系里的矩形(DIP)。
struct WelcomeButtonRect {
    float left;
    float top;
    float right;
    float bottom;
};

/**
 * 计算欢迎屏"打开文件"按钮的矩形(DIP)。纯几何函数,不依赖任何窗口句柄。
 *
 * @param clientWidthDip 客户区宽度(DIP)。
 * @param clientHeightDip 客户区高度(DIP)。
 * @return 按钮矩形。
 * @example auto rect = markair::WelcomeButtonRectDip(800.0f, 600.0f);
 */
inline WelcomeButtonRect WelcomeButtonRectDip(float clientWidthDip, float clientHeightDip) {
    float centerX = clientWidthDip * 0.5f;
    float top = clientHeightDip * kWelcomeButtonTopRatio;
    return WelcomeButtonRect{
        centerX - kWelcomeButtonWidthDip * 0.5f,
        top,
        centerX + kWelcomeButtonWidthDip * 0.5f,
        top + kWelcomeButtonHeightDip,
    };
}

/**
 * 判断一个点(DIP)是否落在欢迎屏按钮矩形内。纯函数。
 *
 * @param rect WelcomeButtonRectDip 算出的按钮矩形。
 * @param xDip 鼠标横坐标(DIP)。
 * @param yDip 鼠标纵坐标(DIP)。
 * @return 落在矩形内返回 true。
 * @example bool hit = markair::IsPointInWelcomeButton(rect, dipX, dipY);
 */
inline bool IsPointInWelcomeButton(const WelcomeButtonRect& rect, float xDip, float yDip) {
    return xDip >= rect.left && xDip < rect.right && yDip >= rect.top && yDip < rect.bottom;
}

}  // namespace markair
