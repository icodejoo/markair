# mdvn_tests 目标:独立的测试可执行文件,不链接进 mdvn.exe 主程序。
# MDVN_TEST 的全局构造函数注册手法(见 mdvn_test.h 顶部注释)只允许存在于这里。
add_executable(mdvn_tests
    main.cpp
    test_str.cpp
    test_encoding.cpp
    test_model.cpp
    test_model_gfm.cpp
    test_attr.cpp
    test_front_matter.cpp
    test_font.cpp
    test_font_fallback.cpp
    test_layout.cpp
    test_layout_codeblock.cpp
    test_list_marker.cpp
    test_code_copy.cpp
    test_synthetic_newline.cpp
    test_table.cpp
    test_renderer.cpp
    test_scroll.cpp
    test_scroll_v2.cpp
    test_bench.cpp
    test_image.cpp
    test_data_uri.cpp
    test_cache.cpp
    test_navigate.cpp
    test_hit_test.cpp
    test_search.cpp
    test_ini.cpp
    test_window_state.cpp
    test_theme.cpp
    test_theme_state.cpp
    test_languages.cpp
    test_lexer.cpp
    test_cmdline.cpp
    test_assoc.cpp
    test_corpus_smoke.cpp
    test_fuzz_smoke.cpp
    test_outline.cpp
    test_outline_panel.cpp
    ../src/app/bench.cpp
    ../src/util/arena.cpp
    ../src/util/cmdline.cpp
    ../src/util/str.cpp
    ../src/util/ini.cpp
    ../src/doc/file_map.cpp
    ../src/doc/encoding.cpp
    ../src/doc/parser.cpp
    ../src/doc/attr.cpp
    ../src/doc/front_matter.cpp
    ../src/doc/search.cpp
    ../src/doc/outline.cpp
    ../src/text/font.cpp
    ../src/layout/layout.cpp
    ../src/layout/table.cpp
    # renderer.cpp:T33 的占位文案/降采样标签文案是纯函数,值得单测;
    # Renderer 本体仍不在测试里构造(需要真实 HWND),只是把这个 TU 链进来。
    ../src/render/renderer.cpp
    ../src/render/theme.cpp
    ../src/hl/languages.cpp
    ../src/hl/lexer.cpp
    ../src/shell/theme_state.cpp
    ../src/assets/image.cpp
    ../src/assets/data_uri.cpp
    ../src/assets/cache.cpp
    ../src/assets/remote.cpp
    ../src/shell/navigate.cpp
    ../src/shell/hit_test.cpp
    ../src/shell/find.cpp
    ../src/shell/clipboard.cpp
    ../src/shell/assoc.cpp
    ../src/shell/outline_panel.cpp
)

# 测试代码同样要求 /W4 零警告,并遵循禁异常/禁 RTTI 的硬性约束。
target_compile_options(mdvn_tests PRIVATE ${MDVN_COMMON_COMPILE_OPTIONS})

# T41:test_corpus_smoke.cpp 需要知道 bench/corpus 语料目录的绝对路径——
# ctest 的默认工作目录是 mdvn_tests.exe 所在的构建目录,不是仓库根目录,
# 用 CMAKE_SOURCE_DIR 直接烘焙一份绝对路径,避免运行期猜测相对路径层数。
target_compile_definitions(mdvn_tests PRIVATE
    MDVN_CORPUS_DIR="${CMAKE_SOURCE_DIR}/bench/corpus")

# T43:test_fuzz_smoke.cpp 需要知道 bench/fuzz 语料目录的绝对路径,理由与
# MDVN_CORPUS_DIR 一致(ctest 默认工作目录不是仓库根目录)。
target_compile_definitions(mdvn_tests PRIVATE
    MDVN_FUZZ_DIR="${CMAKE_SOURCE_DIR}/bench/fuzz")

# T62:test_outline.cpp 的性能验收用例需要加载 bench/BENCH-A.md,同样的
# "ctest 工作目录不是仓库根目录"问题,烘焙一份绝对路径。
target_compile_definitions(mdvn_tests PRIVATE
    MDVN_BENCH_DIR="${CMAKE_SOURCE_DIR}/bench")

# test_model.cpp 通过 parser.cpp 间接依赖 md4c 的 md_parse；
# test_font.cpp 通过 font.cpp 依赖 DirectWrite。
# test_image.cpp 通过 CoCreateInstance 用 WIC(只需 ole32,不必链 windowscodecs.lib),
# test_navigate.cpp 需要 shell32,remote.cpp 需要 winhttp。测试目标不做 /DELAYLOAD ——
# 延迟/惰性加载是 mdvn.exe 的验收项,测试进程本来就是要去用这些库的。
# test_navigate.cpp 的 T36 路径规范化用 PathCchCombineEx,需要 pathcch。
target_link_libraries(mdvn_tests PRIVATE
    md4c dwrite psapi d2d1 ole32 winhttp shell32 pathcch)

enable_testing()
add_test(NAME mdvn_tests COMMAND mdvn_tests)
