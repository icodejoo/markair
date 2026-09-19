// Bug A 覆盖测试:等宽(代码块)回退链必须含中文族名。
//
// 背景:裁决 #10 原文把等宽回退链写成纯西文(Cascadia Mono → Consolas →
// Courier New),代码块里的中文(中文注释/中文字符串/mermaid 节点名等)
// 一个字形都匹配不到,DirectWrite 全部画成方块(tofu)。2026-09-17 修订:
// 在西文等宽族之后追加正文已加载的那三个中文族兜底。
//
// 这里只读 FontSubsystem::MonoFallbackFamilies() 暴露的常量族名表,
// 不创建 DirectWrite 工厂、不做任何字体枚举。
//
// 不修改 tests/test_font.cpp(M0 基线文件),单独起一个新文件。
#include "markair_test.h"
#include "../src/text/font.h"

using markair::FontSubsystem;
using markair::u32;

namespace {

// 宽字符串相等比较(不引入 <cwchar>,与项目"自己写极简工具"的风格一致)。
bool WEquals(const wchar_t* a, const wchar_t* b) {
    if (!a || !b) return false;
    u32 i = 0;
    while (a[i] != 0 && a[i] == b[i]) ++i;
    return a[i] == b[i];
}

// 回退链里是否存在某个族名。
bool ChainContains(const wchar_t* const* chain, u32 count, const wchar_t* family) {
    for (u32 i = 0; i < count; ++i) {
        if (WEquals(chain[i], family)) return true;
    }
    return false;
}

// 某个族名在回退链里的下标,不存在时返回 count。
u32 ChainIndexOf(const wchar_t* const* chain, u32 count, const wchar_t* family) {
    for (u32 i = 0; i < count; ++i) {
        if (WEquals(chain[i], family)) return i;
    }
    return count;
}

}  // namespace

// 用例 1:等宽回退链必须同时含西文等宽族和中文族——缺了中文族,代码块里的
// 中文就是方块;缺了西文等宽族,代码就不是等宽观感了。
MARKAIR_TEST(FontFallback_MonoChainContainsCjkFamilies) {
    u32 count = 0;
    const wchar_t* const* chain = FontSubsystem::MonoFallbackFamilies(&count);
    MARKAIR_CHECK(chain != nullptr);
    MARKAIR_CHECK(count >= 5u);

    MARKAIR_CHECK(ChainContains(chain, count, L"Consolas"));
    MARKAIR_CHECK(ChainContains(chain, count, L"Courier New"));
    MARKAIR_CHECK(ChainContains(chain, count, L"Microsoft YaHei UI"));
    MARKAIR_CHECK(ChainContains(chain, count, L"Microsoft YaHei"));
    MARKAIR_CHECK(ChainContains(chain, count, L"SimSun"));
}

// 用例 2:优先级必须是"西文等宽在前、中文兜底在后"——否则西文代码会被
// 中文字体的非等宽字形接走,代码块失去等宽对齐。
MARKAIR_TEST(FontFallback_MonoChainPrefersWesternMonoBeforeCjk) {
    u32 count = 0;
    const wchar_t* const* chain = FontSubsystem::MonoFallbackFamilies(&count);
    MARKAIR_CHECK(chain != nullptr);

    u32 consolas = ChainIndexOf(chain, count, L"Consolas");
    u32 courier = ChainIndexOf(chain, count, L"Courier New");
    u32 yahei = ChainIndexOf(chain, count, L"Microsoft YaHei UI");
    u32 simsun = ChainIndexOf(chain, count, L"SimSun");

    MARKAIR_CHECK(consolas < courier);
    MARKAIR_CHECK(courier < yahei);
    MARKAIR_CHECK(yahei < simsun);
}

// 用例 3:等宽主族仍是裁决 #10 的 Cascadia Mono,追加中文兜底不改主族。
MARKAIR_TEST(FontFallback_MonoPrimaryFamilyUnchanged) {
    MARKAIR_CHECK(WEquals(FontSubsystem::MonoFamilyName(), L"Cascadia Mono"));
}
