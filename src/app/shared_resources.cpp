#include "shared_resources.h"

namespace markair {

FontSubsystem& SharedFontSubsystem() {
    // 函数局部 static:首次调用时构造(零副作用构造函数，见 font.cpp)，
    // 生命周期覆盖到进程退出，天然满足"进程内唯一一份"的要求。
    static FontSubsystem instance;
    return instance;
}

}  // namespace markair
