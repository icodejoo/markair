// markair 欢迎屏(未打开任何文档时的静态引导页,取代示例 markdown):
// 只提供"打开文件"按钮的几何计算与命中测试两个纯函数,不含任何绘制逻辑——
// 绘制交给 render/renderer.cpp 的 DrawWelcomeScreen(几何常量在那边各自
// 复制一份字面量,与底部栏/大纲侧栏的既有约定一致,render 不反向 include
// shell 头文件)。
#pragma once

#include "button.h"

namespace markair {

// 欢迎屏"打开文件"按钮尺寸(DIP):足够宽大醒目,不易误触到旁边区域。
constexpr float kWelcomeButtonWidthDip = 140.0f;
constexpr float kWelcomeButtonHeightDip = 32.0f;

// 按钮顶部相对客户区高度的位置系数:水平居中,垂直位置固定在标题下方一段
// 距离处(用比例而不是绝对像素,窗口拉伸时按钮位置跟着按比例走)。
constexpr float kWelcomeButtonTopRatio = 0.46f;

// 按钮圆角半径(DIP)。render 层(renderer.cpp)按既有约定不反向 include
// shell 头文件,自己复制了一份同名字面量,数值须保持一致。
constexpr float kWelcomeButtonCornerRadiusDip = 8.0f;

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
    // 借用统一的 Button 几何函数(width/height 固定,几何中心点由本函数算出),
    // 不再手写一遍等价的 centerX ± width*0.5 展开式。
    Button btn{kWelcomeButtonWidthDip, kWelcomeButtonHeightDip, kWelcomeButtonCornerRadiusDip,
               L"打开文件", L"打开文件", nullptr, nullptr};
    ButtonRectDip r = ButtonRectFromCenterDip(btn, centerX, top + kWelcomeButtonHeightDip * 0.5f);
    return WelcomeButtonRect{r.left, r.top, r.right, r.bottom};
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
    return PointInRectDip(rect.left, rect.top, rect.right, rect.bottom, xDip, yDip);
}

// 欢迎屏双按钮并排间距(DIP)。
constexpr float kWelcomeButtonGapDip = 16.0f;

/**
 * 欢迎屏双按钮布局结果(打开文件 + 打开文件夹)。
 */
struct WelcomeButtonPair {
    WelcomeButtonRect fileButton;
    WelcomeButtonRect folderButton;
};

/**
 * 欢迎屏按钮命中测试结果枚举。
 */
enum class WelcomeButtonHit {
    None,
    OpenFile,
    OpenFolder,
};

/**
 * 计算欢迎屏并排的两个按钮(打开文件、打开文件夹)的矩形(DIP)。纯几何函数。
 *
 * @param clientWidthDip 客户区宽度(DIP)。
 * @param clientHeightDip 客户区高度(DIP)。
 * @return 包含文件按钮和文件夹按钮的矩形对。
 */
inline WelcomeButtonPair WelcomeButtonPairDip(float clientWidthDip, float clientHeightDip) {
    float totalWidth = kWelcomeButtonWidthDip * 2.0f + kWelcomeButtonGapDip;
    float startX = (clientWidthDip - totalWidth) * 0.5f;
    float top = clientHeightDip * kWelcomeButtonTopRatio;
    float centerY = top + kWelcomeButtonHeightDip * 0.5f;

    Button fileBtn{kWelcomeButtonWidthDip, kWelcomeButtonHeightDip, kWelcomeButtonCornerRadiusDip,
                    L"打开文件", L"打开文件", nullptr, nullptr};
    Button folderBtn{kWelcomeButtonWidthDip, kWelcomeButtonHeightDip, kWelcomeButtonCornerRadiusDip,
                      L"打开文件夹", L"打开文件夹", nullptr, nullptr};
    ButtonRectDip fileRect = ButtonRectFromCenterDip(fileBtn, startX + kWelcomeButtonWidthDip * 0.5f, centerY);
    ButtonRectDip folderRect = ButtonRectFromCenterDip(
        folderBtn, startX + kWelcomeButtonWidthDip + kWelcomeButtonGapDip + kWelcomeButtonWidthDip * 0.5f, centerY);

    WelcomeButtonPair pair{};
    pair.fileButton = WelcomeButtonRect{fileRect.left, fileRect.top, fileRect.right, fileRect.bottom};
    pair.folderButton = WelcomeButtonRect{folderRect.left, folderRect.top, folderRect.right, folderRect.bottom};
    return pair;
}

/**
 * 欢迎屏双按钮命中测试。纯函数。
 *
 * @param clientWidthDip 客户区宽度(DIP)。
 * @param clientHeightDip 客户区高度(DIP)。
 * @param xDip 鼠标横坐标(DIP)。
 * @param yDip 鼠标纵坐标(DIP)。
 * @return 命中的按钮类型(WelcomeButtonHit)。
 */
inline WelcomeButtonHit HitTestWelcomeButtons(float clientWidthDip, float clientHeightDip, float xDip, float yDip) {
    auto pair = WelcomeButtonPairDip(clientWidthDip, clientHeightDip);
    if (IsPointInWelcomeButton(pair.fileButton, xDip, yDip)) return WelcomeButtonHit::OpenFile;
    if (IsPointInWelcomeButton(pair.folderButton, xDip, yDip)) return WelcomeButtonHit::OpenFolder;
    return WelcomeButtonHit::None;
}

}  // namespace markair
