# markair 根 CMake 工程：仅支持 MSVC，不引入任何包管理器（vcpkg/conan）。
cmake_minimum_required(VERSION 3.20)

project(markair CXX C)

# 本项目硬性要求 MSVC 工具链（COM/ABI 假设、编译选项均针对 MSVC），非 MSVC 直接报错。
if(NOT MSVC)
    message(FATAL_ERROR "markair 仅支持 MSVC 工具链构建，请使用 Visual Studio 2022 生成器。")
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

# 主代码统一使用的编译期硬性约束：禁异常、禁 RTTI、最高警告级别、源码/执行
# 字符集固定 UTF-8。third_party（如 md4c）单独设置选项，不受此变量影响。
#
# /utf-8（2026-09-19 追加，修 GitHub Actions CI 实测失败）：本仓库源码含
# 大量中文字符串字面量（如 tests/test_ini.cpp 里 L"微软雅黑"）。MSVC 默认
# 按"当前系统 ANSI 代码页"解释源码里的窄/宽字符字面量，而不是按源文件的
# 实际 UTF-8 编码——开发者本机（zh-CN 代码页 936/GBK 或已启用系统级
# UTF-8）恰好能凑对，但 GitHub Actions windows-latest runner 默认是英文
# 代码页(1252)，同一份 UTF-8 源码在那边编译出来的宽字符字面量字节值就
# 不对了，实测表现为 wcscmp 断言失败（值与"运行时按 UTF-8 显式解码 ini
# 文件字节"得到的结果对不上）。/utf-8 强制"源码字符集"与"执行字符集"
# 都按 UTF-8 解释，与源文件实际编码一致，消除这个机器相关的差异。
set(MARKAIR_COMMON_COMPILE_OPTIONS
    /W4       # 主代码要求零警告
    /EHs-c-   # 禁用 C++ 异常
    /GR-      # 禁用 RTTI
    /utf-8    # 源码/执行字符集固定 UTF-8,不依赖编译机的系统代码页
)

# Release 专属的体积/速度优化选项：内存占用优先于运行速度（用户裁决），
# /O1 是 MSVC 现行"体积最小化"预设(等价旧版 /Os，MSVC 文档已建议用 /O1 替代
# 单独的 /Os)，与 /O2(速度优先)互斥，二选一；/GL /Gy /Gw 三项本身就是
# 体积相关优化(整程序优化/函数级链接/消除重复只读数据)，继续保留。
#
# Release compile options: memory footprint takes priority over raw speed
# (per user decision). /O1 is MSVC's current "minimize size" preset
# (the documented replacement for standalone /Os, mutually exclusive with
# /O2). /GL /Gy /Gw stay unchanged — they already are size-related.
set(MARKAIR_RELEASE_COMPILE_OPTIONS
    /O1 /GL /Gy /Gw
)

# Release 链接期优化：剔除未引用符号、合并等价函数、跨模块优化。
# /OPT:ICF=3(2026-09-19 体积调研追加):多轮迭代折叠等价函数,比默认单轮
# /OPT:ICF 多省几 KB；代价是不同函数可能共享同一地址，本项目不比较函数
# 指针，无副作用。/EMITTOOLVERSIONINFO:NO:去掉链接器写进 exe 的工具版本
# 记录节，省几百字节到几 KB，零风险。/FILEALIGN:512:PE 文件对齐从默认
# 512(已是最小值)——显式声明留痕，实际已是默认值，不改变输出，仅避免
# 未来 CMake/工具链默认值漂移。
set(MARKAIR_RELEASE_LINK_OPTIONS
    /OPT:REF /OPT:ICF=3 /LTCG /EMITTOOLVERSIONINFO:NO /FILEALIGN:512
)

# 延迟加载的系统 DLL（T34 验收项）：网络图片用的 WinHTTP 只在真正发起请求时
# 才加载 —— 不含网络图片的文档、或 load_remote_images=0（用户手动关闭）时，
# 模块列表里不应出现 winhttp.dll。2026-09-19 裁决把 load_remote_images 默认值
# 反转为 1，因此"默认不出现"这条验收口径现在只在无网络图片的文档下成立；
# 含网络图片的文档默认会自动发起下载，winhttp.dll 会按预期加载，这是功能
# 生效的正常表现，不是延迟加载失效。/DELAYLOAD 需要配合链接 delayimp.lib
# （在 src/CMakeLists.txt 里链接）。
#
# 为什么没有 /DELAYLOAD:windowscodecs.dll（T30 验收项的等价实现）：
# markair 访问 WIC 的唯一途径是 `CoCreateInstance(CLSID_WICImagingFactory, ...)`，
# 完全不引用 windowscodecs.dll 的任何导出符号，因此 exe 的导入表里根本没有这个
# DLL，加上 /DELAYLOAD 只会得到 LNK4199「未找到来自 windowscodecs.dll 的导入」。
# COM 运行期激活本身就比延迟加载更晚、更惰性：不解码图片就不会 CoCreateInstance，
# 也就不会把 windowscodecs.dll 映射进进程 —— 验收要的「无图文档模块列表里不出现
# windowscodecs.dll」由此成立，且比 /DELAYLOAD 更强（连导入表条目都没有）。
# shell32.dll(2026-09-17 深挖内存追加):`ShellExecuteW`(外链跳转、点本地/
# 临时文件图片调用系统看图软件,见 src/shell/navigate.cpp)只在用户真正点击
# 链接/图片时才会被调用,但 shell32 是静态导入,进程启动时无条件加载——
# 实测它会连带拉起 windows.storage.dll(~7.8MB)、comctl32.dll(~2.7MB)等一整条
# Shell 命名空间基础设施,而 markair 自己从不使用其中任何 Shell UI/命名空间功能,
# 纯粹是 ShellExecuteW 这一个入口点带来的"连坐"加载。延迟加载后,不点击任何
# 外链/图片的会话里,模块列表不应出现这几个 DLL。
# dwmapi.dll(T48 标题栏深浅色):`DwmSetWindowAttribute` 只是切一个窗口属性,
# 换来的是"进程启动就无条件静态加载一个 DLL"这个代价,与 shell32 同理不划算。
# 注意它和 winhttp/shell32 不同:本条会在窗口创建时(而非用户交互后)就调用,
# 所以 /DELAYLOAD 在这里只做到"不提前于导入表解析阶段加载、按需触发"，
# dwmapi.dll 实际仍会在启动早期出现在模块列表里——这是预期行为，不是延迟
# 加载没生效，验收时如实记录这个模块增量即可。
# uxtheme.dll(T48 追加,2026-09-17 用户真机反馈补充:原生滚动条没跟着标题栏
# 变深色):`SetWindowTheme` 让 WS_VSCROLL 滚动条切换到系统的深色控件主题,
# 与 dwmapi 同理在窗口创建时就调用,/DELAYLOAD 同样只做到不提前于导入表
# 解析阶段加载。
set(MARKAIR_DELAYLOAD_LINK_OPTIONS
    /DELAYLOAD:winhttp.dll
    /DELAYLOAD:shell32.dll
    /DELAYLOAD:dwmapi.dll
    /DELAYLOAD:uxtheme.dll
)

# md4c 是 vendored 的第三方源码（third_party/md4c/），不在该目录下新增任何文件，
# 直接在这里把 md4c.c 编译成静态库，并单独关闭 /W4（上游代码在 /W4 下有警告，
# 属于预期情况，不修改 third_party 源码去掩盖）。
add_library(md4c STATIC third_party/md4c/md4c.c)
target_include_directories(md4c PUBLIC third_party/md4c)
target_compile_options(md4c PRIVATE /W3 /wd4204 /wd4706)

# lunasvg + plutovg 是 vendored 的第三方源码（third_party/lunasvg/，含内嵌的
# plutovg 子模块源码），用于 T?? SVG 图片解码支持（裁决：SVG 图片走 lunasvg
# 离线栅格化成 RGBA 再进现有 ImageCache/ID2D1Bitmap 管线，不新增 D3D 设备，
# 不改动「默认软件渲染」架构裁决）。上游代码在 /W4 下有警告，单独关闭，不修改
# third_party 源码去掩盖；STATIC 构建需要 *_BUILD_STATIC 宏关掉 dllexport。
#
# Release 也要套 MARKAIR_RELEASE_COMPILE_OPTIONS（2026-09-19 CI 实测追加）：
# 这两个 target 最初漏加了 /O1 /GL /Gy /Gw，导致 Release 构建时 plutovg 里
# 未被实际调用的代码(stb_image/stb_image_write 的 PNG/JPEG 编解码、
# stb_truetype 字形栅格化——markair 只用 lunasvg 画 shape/path 类 SVG，不需要
# `<image>` 元素内嵌位图与 `<text>` 字体渲染这两块)没有 /Gy 逐函数拆分,
# 最终链接时 /OPT:REF 也就没法按函数粒度剔除,整份未用代码原样留在 exe 里，
# 实测把体积从 ~312KB 顶到 ~817KB，超过 M2 体积硬性门禁(基线+80KB)。补上
# 这两行后交给现有的 /OPT:REF /OPT:ICF /LTCG 链接期裁剪去处理，不手改
# vendored 源码本体。
add_library(plutovg STATIC
    third_party/lunasvg/plutovg/source/plutovg-blend.c
    third_party/lunasvg/plutovg/source/plutovg-canvas.c
    third_party/lunasvg/plutovg/source/plutovg-font.c
    third_party/lunasvg/plutovg/source/plutovg-ft-math.c
    third_party/lunasvg/plutovg/source/plutovg-ft-raster.c
    third_party/lunasvg/plutovg/source/plutovg-ft-stroker.c
    third_party/lunasvg/plutovg/source/plutovg-matrix.c
    third_party/lunasvg/plutovg/source/plutovg-paint.c
    third_party/lunasvg/plutovg/source/plutovg-path.c
    third_party/lunasvg/plutovg/source/plutovg-rasterize.c
    third_party/lunasvg/plutovg/source/plutovg-surface.c
)
target_include_directories(plutovg PUBLIC third_party/lunasvg/plutovg/include)
target_compile_definitions(plutovg PUBLIC PLUTOVG_BUILD_STATIC)
# stb_image(plutovg-surface.c 内嵌)按格式关闭未用到的解码器,纯宏开关,不动
# vendor 源码本体:markair 的 SVG `<image>` 场景只保留 PNG/JPEG 两种最常见的
# 内嵌位图格式,BMP/PSD/TGA/GIF/HDR/PIC/PNM 全部关掉(2026-09-19 体积调研)。
target_compile_definitions(plutovg PRIVATE
    STBI_NO_BMP STBI_NO_PSD STBI_NO_TGA STBI_NO_GIF
    STBI_NO_HDR STBI_NO_PIC STBI_NO_PNM STBI_NO_LINEAR STBI_NO_FAILURE_STRINGS)
target_compile_options(plutovg PRIVATE /W3 /wd4996 /wd4244 /wd4267 /wd4100
    $<$<CONFIG:Release>:${MARKAIR_RELEASE_COMPILE_OPTIONS}>)

add_library(lunasvg STATIC
    third_party/lunasvg/source/graphics.cpp
    third_party/lunasvg/source/lunasvg.cpp
    third_party/lunasvg/source/svgelement.cpp
    third_party/lunasvg/source/svggeometryelement.cpp
    third_party/lunasvg/source/svglayoutstate.cpp
    third_party/lunasvg/source/svgpaintelement.cpp
    third_party/lunasvg/source/svgparser.cpp
    third_party/lunasvg/source/svgproperty.cpp
    third_party/lunasvg/source/svgrenderstate.cpp
    third_party/lunasvg/source/svgtextelement.cpp
)
target_include_directories(lunasvg PUBLIC third_party/lunasvg/include
                                    PRIVATE third_party/lunasvg/source)
target_compile_definitions(lunasvg PUBLIC LUNASVG_BUILD_STATIC)
target_link_libraries(lunasvg PUBLIC plutovg)
target_compile_options(lunasvg PRIVATE /W3 /wd4996 /wd4244 /wd4267 /wd4100
    $<$<CONFIG:Release>:${MARKAIR_RELEASE_COMPILE_OPTIONS}>)

add_subdirectory(src)
add_subdirectory(tests)
