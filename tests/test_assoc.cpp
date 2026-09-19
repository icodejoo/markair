// T58 覆盖测试:文件关联"要写/删哪些键"这一纯数据描述。真实
// RegSetValueExW/RegDeleteTreeW 调用不在这里做,留给未来人工验收(T61)。
#include "markair_test.h"
#include "../src/shell/assoc.h"

#include <cstring>

using markair::AssocDeleteEntry;
using markair::AssocRegValue;
using markair::BuildAssocUninstallPlan;
using markair::BuildAssocWritePlan;
using markair::kAssocDeleteCount;
using markair::kAssocProgId;
using markair::kAssocProgIdWriteCount;
using markair::kAssocWriteCount;
using markair::kAssociatedExtensionCount;
using markair::kAssociatedExtensions;
using markair::ShouldRemoveExtensionKey;
using markair::u32;

namespace {

bool Contains(const wchar_t* haystack, const wchar_t* needle) {
    return wcsstr(haystack, needle) != nullptr;
}

}  // namespace

// 用例 1:扩展名清单恰好是 5 个,且是裁决 #2 附带问题 B 指定的那五个。
MARKAIR_TEST(Assoc_ExtensionListIsExactlyFive) {
    static_assert(kAssociatedExtensionCount == 5u, "扩展名必须恰好是五个");
    MARKAIR_CHECK(wcscmp(kAssociatedExtensions[0], L".md") == 0);
    MARKAIR_CHECK(wcscmp(kAssociatedExtensions[1], L".markdown") == 0);
    MARKAIR_CHECK(wcscmp(kAssociatedExtensions[2], L".mdown") == 0);
    MARKAIR_CHECK(wcscmp(kAssociatedExtensions[3], L".mkd") == 0);
    MARKAIR_CHECK(wcscmp(kAssociatedExtensions[4], L".mdtext") == 0);
}

// 用例 2:注册计划恰好是 1 个 ProgID 子树(3 条值)+ 5 条 OpenWithProgids,共 8 条。
MARKAIR_TEST(Assoc_WritePlanHasExactlyOneProgIdAndFiveOpenWithProgids) {
    static_assert(kAssocWriteCount == 8u, "1 个 ProgID(3 条)+ 5 条 OpenWithProgids = 8");
    static_assert(kAssocProgIdWriteCount == 3u, "ProgID 子树恰好 3 条值");

    AssocRegValue writes[kAssocWriteCount];
    u32 n = BuildAssocWritePlan(L"C:\\markair.exe", L"markair Markdown 文档", writes);
    MARKAIR_CHECK_EQ(n, kAssocWriteCount);

    // 前 3 条都挂在唯一的 ProgID "markair.md" 子树下(默认值/DefaultIcon/shell open command)。
    u32 progIdCount = 0;
    u32 openWithCount = 0;
    for (u32 i = 0; i < n; ++i) {
        if (wcsncmp(writes[i].subKey, L"markair.md", 7) == 0) {
            ++progIdCount;
        }
        if (wcsstr(writes[i].subKey, L"OpenWithProgids") != nullptr) {
            ++openWithCount;
            // OpenWithProgids 的值名必须是唯一的 ProgID,数据为空(REG_NONE 语义)。
            MARKAIR_CHECK(wcscmp(writes[i].valueName, kAssocProgId) == 0);
            MARKAIR_CHECK(writes[i].data[0] == L'\0');
        }
    }
    MARKAIR_CHECK_EQ(progIdCount, kAssocProgIdWriteCount);
    MARKAIR_CHECK_EQ(openWithCount, kAssociatedExtensionCount);
}

// 用例 3:五条 OpenWithProgids 分别挂在五个扩展名下,一一对应,不多不少。
MARKAIR_TEST(Assoc_WritePlanOpenWithProgidsCoverAllFiveExtensions) {
    AssocRegValue writes[kAssocWriteCount];
    u32 n = BuildAssocWritePlan(L"C:\\markair.exe", L"markair Markdown 文档", writes);

    for (u32 e = 0; e < kAssociatedExtensionCount; ++e) {
        wchar_t expected[64];
        swprintf_s(expected, L"%s\\OpenWithProgids", kAssociatedExtensions[e]);

        bool found = false;
        for (u32 i = 0; i < n; ++i) {
            if (wcscmp(writes[i].subKey, expected) == 0) {
                found = true;
                break;
            }
        }
        MARKAIR_CHECK(found);
    }
}

// 用例 4:ProgID 的三条值内容正确——显示名、DefaultIcon、shell\open\command。
MARKAIR_TEST(Assoc_ProgIdValuesContainExePathAndDisplayName) {
    AssocRegValue writes[kAssocWriteCount];
    u32 n = BuildAssocWritePlan(L"C:\\markair.exe", L"markair Markdown 文档", writes);
    (void)n;

    MARKAIR_CHECK(wcscmp(writes[0].subKey, kAssocProgId) == 0);
    MARKAIR_CHECK(writes[0].valueName == nullptr);
    MARKAIR_CHECK(wcscmp(writes[0].data, L"markair Markdown 文档") == 0);

    MARKAIR_CHECK(wcscmp(writes[1].subKey, L"markair.md\\DefaultIcon") == 0);
    MARKAIR_CHECK(Contains(writes[1].data, L"C:\\markair.exe"));

    MARKAIR_CHECK(wcscmp(writes[2].subKey, L"markair.md\\shell\\open\\command") == 0);
    MARKAIR_CHECK(Contains(writes[2].data, L"C:\\markair.exe"));
    MARKAIR_CHECK(Contains(writes[2].data, L"%1"));
}

// 用例 5:卸载计划恰好是 1 个 ProgID 子树删除 + 5 条 OpenWithProgids 值删除,共 6 条。
MARKAIR_TEST(Assoc_UninstallPlanHasExactlyOneProgIdTreeAndFiveOpenWithProgids) {
    static_assert(kAssocDeleteCount == 6u, "1 个 ProgID 子树删除 + 5 条 OpenWithProgids = 6");

    AssocDeleteEntry deletes[kAssocDeleteCount];
    u32 n = BuildAssocUninstallPlan(deletes);
    MARKAIR_CHECK_EQ(n, kAssocDeleteCount);

    // 第一条:删除 ProgID 整棵子树(valueName == nullptr 表示 RegDeleteTreeW)。
    MARKAIR_CHECK(wcscmp(deletes[0].subKey, kAssocProgId) == 0);
    MARKAIR_CHECK(deletes[0].valueName == nullptr);

    u32 openWithDeleteCount = 0;
    for (u32 i = 1; i < n; ++i) {
        MARKAIR_CHECK(wcsstr(deletes[i].subKey, L"OpenWithProgids") != nullptr);
        MARKAIR_CHECK(deletes[i].valueName != nullptr);
        MARKAIR_CHECK(wcscmp(deletes[i].valueName, kAssocProgId) == 0);
        ++openWithDeleteCount;
    }
    MARKAIR_CHECK_EQ(openWithDeleteCount, kAssociatedExtensionCount);
}

// 用例 6:卸载集合与注册集合的扩展名覆盖一一对应(同一组五个扩展名)。
MARKAIR_TEST(Assoc_UninstallSetCorrespondsToRegisterSet) {
    AssocRegValue writes[kAssocWriteCount];
    BuildAssocWritePlan(L"C:\\markair.exe", L"markair Markdown 文档", writes);

    AssocDeleteEntry deletes[kAssocDeleteCount];
    BuildAssocUninstallPlan(deletes);

    for (u32 e = 0; e < kAssociatedExtensionCount; ++e) {
        wchar_t expected[64];
        swprintf_s(expected, L"%s\\OpenWithProgids", kAssociatedExtensions[e]);

        bool inWrites = false;
        for (u32 i = 0; i < kAssocWriteCount; ++i) {
            if (wcscmp(writes[i].subKey, expected) == 0) inWrites = true;
        }
        bool inDeletes = false;
        for (u32 i = 0; i < kAssocDeleteCount; ++i) {
            if (wcscmp(deletes[i].subKey, expected) == 0) inDeletes = true;
        }
        MARKAIR_CHECK(inWrites);
        MARKAIR_CHECK(inDeletes);
    }
}

// 用例 7:ShouldRemoveExtensionKey 的四种组合——只有两个条件同时成立才删。
MARKAIR_TEST(Assoc_ShouldRemoveExtensionKeyRequiresBothConditions) {
    MARKAIR_CHECK(ShouldRemoveExtensionKey(true, true) == true);
    MARKAIR_CHECK(ShouldRemoveExtensionKey(true, false) == false);
    MARKAIR_CHECK(ShouldRemoveExtensionKey(false, true) == false);
    MARKAIR_CHECK(ShouldRemoveExtensionKey(false, false) == false);
}
