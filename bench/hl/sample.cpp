#include <string>
#include <iostream>

// 用原始字符串存放正则表达式模式,避免反斜杠转义(T67 语料样例)
const char* kPattern = R"(\d+\.\d+)";

// 简单的加法模板函数,含中文注释
template <typename T>
T Add(T a, T b) {
    return a + b;
}

int main() {
    std::string greeting = "你好,C++ 世界";
    std::cout << greeting << " " << kPattern << " " << Add(1, 2) << "\n";
    return 0;
}

// ---- 故意示例:以下是未闭合的块注释,单独放在文件末尾一节 ----
/* 这是故意留下的未闭合块注释,验证词法器不会把上面的正文吞掉
