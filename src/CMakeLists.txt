# mdvn 主程序目标：把窗口 + D2D + DirectWrite + Arena + md4c 链接到一起。
add_executable(mdvn WIN32
    app/main.cpp
    app/bench.cpp
    util/arena.cpp
    util/cmdline.cpp
    util/str.cpp
    util/ini.cpp
    doc/file_map.cpp
    doc/encoding.cpp
    doc/parser.cpp
    doc/attr.cpp
    doc/front_matter.cpp
    doc/search.cpp
    assets/image.cpp
    assets/data_uri.cpp
    assets/cache.cpp
    assets/remote.cpp
    text/font.cpp
    layout/layout.cpp
    layout/table.cpp
    render/renderer.cpp
    shell/window.cpp
    shell/clipboard.cpp
    shell/navigate.cpp
    shell/hit_test.cpp
    shell/find.cpp
)

# ole32:WIC 工厂通过 CoCreateInstance 惰性激活(T30),不直接链接 windowscodecs.lib
# ——不引用它的任何导出符号,exe 导入表里就没有这个 DLL,比 /DELAYLOAD 更彻底。
# winhttp:网络图片(T34),走 /DELAYLOAD,需要 delayimp.lib 提供延迟加载桩。
# pathcch:T36 的相对 .md 链接用 PathCchCombineEx / PathCchCanonicalize 做路径规范化。
target_link_libraries(mdvn PRIVATE
    md4c d2d1 dwrite imm32 shell32 psapi ole32 winhttp pathcch delayimp)

# 主代码在所有配置下都要求 /W4 零警告；Release 追加体积/速度优化选项。
target_compile_options(mdvn PRIVATE
    ${MDVN_COMMON_COMPILE_OPTIONS}
    $<$<CONFIG:Release>:${MDVN_RELEASE_COMPILE_OPTIONS}>
)

# 延迟加载(T30/T34 的验收项):无图文档不加载 windowscodecs.dll,
# load_remote_images=0 且不点击时不加载 winhttp.dll。
target_link_options(mdvn PRIVATE
    ${MDVN_DELAYLOAD_LINK_OPTIONS}
    $<$<CONFIG:Release>:${MDVN_RELEASE_LINK_OPTIONS}>
)
