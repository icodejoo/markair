# mdvn 编码规范检查清单

来源:`02-tech-stack.md` §1 硬性约束 + 裁决 #10/#11。代码评审时逐条对照,不合规不合并。

## 1. 禁用异常(`/EHs-c-`)

- [ ] 不写 `try` / `catch` / `throw`。
- [ ] 不调用任何已知会抛异常的标准库函数(如 `std::stoi`、`.at()` 越界、`new` 不加 `std::nothrow` 等)。
- [ ] 第三方/系统 API 返回错误码时,用返回值/HRESULT 判断,不假设异常会兜底。

## 2. 禁用 RTTI(`/GR-`)

- [ ] 不写 `dynamic_cast`、`typeid`。
- [ ] 多态判别改用自有的类型标签(enum tag)或虚函数分发。

## 3. 禁用 iostream / locale

- [ ] 不 `#include <iostream>` / `<fstream>` / `<sstream>` / `<locale>`。
- [ ] 调试输出/日志走 `fprintf(stderr, ...)` 或 Win32 API,不引入流式 I/O 的启动开销。

## 4. 禁用 `std::regex`

- [ ] 不 `#include <regex>`,不用 `std::regex` 系列类型。
- [ ] 需要模式匹配时手写状态机/字符扫描。

## 5. 慎用 `std::string`

- [ ] 长期持有的字符串数据优先用 `mdvn::StrSlice`(零拷贝切片)或 arena 分配的裸缓冲区。
- [ ] 仅允许在函数内部、极短生命周期的临时拼接场景下使用 `std::string`,且要问自己"能不能用切片视图替代"。

## 6. 禁止有副作用的全局构造函数

- [ ] 正式产品代码(`app/`、`util/`、`doc/`、`text/`、`layout/`、`render/`、`shell/` 等目录)不允许出现"全局/静态对象在构造函数里做实际工作"的写法(注册表、I/O、分配内存等)。
- [ ] 全局变量只允许是 POD 或普通指针的零初始化(参照 `main.cpp` 里 `g_d2dFactory` 等的写法)。
- [ ] 唯一允许的例外:`tests/mdvn_test.h` 里的测试自动注册机制,且只存在于独立的 `mdvn_tests.exe`,不链接进 `mdvn.exe`。

## 7. 禁止调用 `GetSystemFontCollection`(裁决 #10)

- [ ] 代码中不得出现 `IDWriteFactory::GetSystemFontCollection`。
- [ ] 字体枚举/查找走裁决 #10 约定的白名单机制(`src/text/font.h/.cpp`),不做全量系统字体枚举。
