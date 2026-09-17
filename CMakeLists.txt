# mdvn 根 CMake 工程：仅支持 MSVC，不引入任何包管理器（vcpkg/conan）。
cmake_minimum_required(VERSION 3.20)

project(mdvn CXX C)

# 本项目硬性要求 MSVC 工具链（COM/ABI 假设、编译选项均针对 MSVC），非 MSVC 直接报错。
if(NOT MSVC)
    message(FATAL_ERROR "mdvn 仅支持 MSVC 工具链构建，请使用 Visual Studio 2022 生成器。")
endif()

# 全局 C++ 标准：C++17，且关闭编译器扩展以保证行为可预期。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 静态链接 CRT（/MT，Release 用 /MT，Debug 用 /MTd），避免依赖 vcruntime DLL。
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# CMake 给 MSVC 的默认标志里带 /EHsc，会跟我们主代码要求的 /EHs-c- 冲突并产生
# D9025 重写警告；这里去掉默认的异常处理标志，由各 target 自行决定。
foreach(_lang CXX C)
    string(REPLACE "/EHsc" "" CMAKE_${_lang}_FLAGS "${CMAKE_${_lang}_FLAGS}")
endforeach()

# 主代码统一使用的编译期硬性约束：禁异常、禁 RTTI、最高警告级别。
# third_party（如 md4c）单独设置选项，不受此变量影响。
set(MDVN_COMMON_COMPILE_OPTIONS
    /W4       # 主代码要求零警告
    /EHs-c-   # 禁用 C++ 异常
    /GR-      # 禁用 RTTI
)

# Release 专属的体积/速度优化选项：整程序优化、函数级链接、消除重复只读数据。
set(MDVN_RELEASE_COMPILE_OPTIONS
    /O2 /GL /Gy /Gw
)

# Release 链接期优化：剔除未引用符号、合并等价函数、跨模块优化。
set(MDVN_RELEASE_LINK_OPTIONS
    /OPT:REF /OPT:ICF /LTCG
)

# 延迟加载的系统 DLL（T34 验收项）：网络图片用的 WinHTTP 只在真正发起请求时
# 才加载 —— load_remote_images=0 且用户不点击加载时，模块列表里不应出现
# winhttp.dll。/DELAYLOAD 需要配合链接 delayimp.lib（在 src/CMakeLists.txt 里链接）。
#
# 为什么没有 /DELAYLOAD:windowscodecs.dll（T30 验收项的等价实现）：
# mdvn 访问 WIC 的唯一途径是 `CoCreateInstance(CLSID_WICImagingFactory, ...)`，
# 完全不引用 windowscodecs.dll 的任何导出符号，因此 exe 的导入表里根本没有这个
# DLL，加上 /DELAYLOAD 只会得到 LNK4199「未找到来自 windowscodecs.dll 的导入」。
# COM 运行期激活本身就比延迟加载更晚、更惰性：不解码图片就不会 CoCreateInstance，
# 也就不会把 windowscodecs.dll 映射进进程 —— 验收要的「无图文档模块列表里不出现
# windowscodecs.dll」由此成立，且比 /DELAYLOAD 更强（连导入表条目都没有）。
set(MDVN_DELAYLOAD_LINK_OPTIONS
    /DELAYLOAD:winhttp.dll
)

# md4c 是 vendored 的第三方源码（third_party/md4c/），不在该目录下新增任何文件，
# 直接在这里把 md4c.c 编译成静态库，并单独关闭 /W4（上游代码在 /W4 下有警告，
# 属于预期情况，不修改 third_party 源码去掩盖）。
add_library(md4c STATIC third_party/md4c/md4c.c)
target_include_directories(md4c PUBLIC third_party/md4c)
target_compile_options(md4c PRIVATE /W3 /wd4204 /wd4706)

add_subdirectory(src)
add_subdirectory(tests)
