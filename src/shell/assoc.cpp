#include "assoc.h"

#include <shlobj.h>  // SHChangeNotify
#include <cwchar>    // swprintf_s/wcslen/wcsstr/wcscpy_s

namespace markair {

u32 BuildAssocWritePlan(const wchar_t* exePath, const wchar_t* displayName, AssocRegValue out[]) {
    // 单线程使用的静态缓冲区,拼出 DefaultIcon("<exe>",0)与 shell\open\command
    // ("<exe>" "%1")两条字符串——项目整体是单线程模型,与 main.cpp 里
    // swprintf_s 拼互斥体名字的用法口径一致。
    static wchar_t iconValue[MAX_PATH * 2 + 4];
    static wchar_t commandValue[MAX_PATH * 2 + 8];
    swprintf_s(iconValue, L"\"%s\",0", exePath);
    swprintf_s(commandValue, L"\"%s\" \"%%1\"", exePath);

    u32 n = 0;
    // ① ProgID 子树的 3 条值。
    out[n++] = AssocRegValue{kAssocProgId, nullptr, displayName};
    out[n++] = AssocRegValue{L"markair.md\\DefaultIcon", nullptr, iconValue};
    out[n++] = AssocRegValue{L"markair.md\\shell\\open\\command", nullptr, commandValue};

    // ② 五个扩展名各写一条 OpenWithProgids 空值(把自己加进候选列表)。
    static wchar_t openWithSubKeys[kAssociatedExtensionCount][32];
    for (u32 i = 0; i < kAssociatedExtensionCount; ++i) {
        swprintf_s(openWithSubKeys[i], L"%s\\OpenWithProgids", kAssociatedExtensions[i]);
        out[n++] = AssocRegValue{openWithSubKeys[i], kAssocProgId, L""};
    }
    return n;
}

u32 BuildAssocUninstallPlan(AssocDeleteEntry out[]) {
    u32 n = 0;
    // ProgID 整棵子树,RegDeleteTreeW 删除(valueName == nullptr)。
    out[n++] = AssocDeleteEntry{kAssocProgId, nullptr};

    // 五条 OpenWithProgids 值,一个都不能漏——只删 markair.md 而留下悬空引用
    // 正是"卸载有残留"最常见的形态。
    static wchar_t openWithSubKeys[kAssociatedExtensionCount][32];
    for (u32 i = 0; i < kAssociatedExtensionCount; ++i) {
        swprintf_s(openWithSubKeys[i], L"%s\\OpenWithProgids", kAssociatedExtensions[i]);
        out[n++] = AssocDeleteEntry{openWithSubKeys[i], kAssocProgId};
    }
    return n;
}

bool ShouldRemoveExtensionKey(bool openWithProgidsEmptyAfterDelete, bool extensionKeyHasNoOtherContent) {
    return openWithProgidsEmptyAfterDelete && extensionKeyHasNoOtherContent;
}

namespace {

// 写一条字符串值(REG_SZ)或空值(REG_NONE,data == nullptr/空串且非默认值时)。
// OpenWithProgids 语义上要求空值,这里统一按"data 为空字符串且非默认值"识别。
bool WriteOneValue(const AssocRegValue& v) {
    HKEY hKey = nullptr;
    // subKey 前面拼上 "Software\Classes\" 前缀。
    wchar_t fullPath[512];
    swprintf_s(fullPath, L"Software\\Classes\\%s", v.subKey);

    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, fullPath, 0, nullptr,
                                      REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (status != ERROR_SUCCESS) return false;

    bool ok;
    if (v.valueName != nullptr && v.data != nullptr && v.data[0] == L'\0') {
        // OpenWithProgids:空的 REG_NONE 值。
        status = RegSetValueExW(hKey, v.valueName, 0, REG_NONE, nullptr, 0);
        ok = status == ERROR_SUCCESS;
    } else {
        const wchar_t* data = v.data != nullptr ? v.data : L"";
        DWORD bytes = static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t));
        status = RegSetValueExW(hKey, v.valueName, 0, REG_SZ,
                                 reinterpret_cast<const BYTE*>(data), bytes);
        ok = status == ERROR_SUCCESS;
    }
    RegCloseKey(hKey);
    return ok;
}

// 删除一条值,或整个子树(valueName == nullptr)。键/值本不存在视为成功。
bool DeleteOneEntry(const AssocDeleteEntry& e) {
    wchar_t fullPath[512];
    swprintf_s(fullPath, L"Software\\Classes\\%s", e.subKey);

    if (e.valueName == nullptr) {
        LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, fullPath);
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }

    LSTATUS status = RegDeleteKeyValueW(HKEY_CURRENT_USER, fullPath, e.valueName);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

// 判断 HKCU\Software\Classes\<subKey> 是否已经没有任何子键/值(用于
// ShouldRemoveExtensionKey 的两个布尔输入)。键本身不存在也算"空"。
bool ClassesSubKeyIsEmpty(const wchar_t* subKey) {
    wchar_t fullPath[512];
    swprintf_s(fullPath, L"Software\\Classes\\%s", subKey);

    HKEY hKey = nullptr;
    LSTATUS openStatus = RegOpenKeyExW(HKEY_CURRENT_USER, fullPath, 0, KEY_READ, &hKey);
    if (openStatus != ERROR_SUCCESS) return true;  // 不存在视为空

    DWORD subKeyCount = 0;
    DWORD valueCount = 0;
    LSTATUS infoStatus = RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, &subKeyCount,
                                           nullptr, nullptr, &valueCount, nullptr, nullptr,
                                           nullptr, nullptr);
    RegCloseKey(hKey);
    if (infoStatus != ERROR_SUCCESS) return false;  // 查询失败:保守起见当作非空,不删
    return subKeyCount == 0 && valueCount == 0;
}

}  // namespace

bool RegisterFileAssociations(const wchar_t* exePath, const wchar_t* displayName) {
    AssocRegValue writes[kAssocWriteCount];
    u32 count = BuildAssocWritePlan(exePath, displayName, writes);

    bool allOk = true;
    for (u32 i = 0; i < count; ++i) {
        if (!WriteOneValue(writes[i])) allOk = false;
    }

    // 通知外壳刷新"打开方式"列表——即使某条写入失败也调用,让已成功的部分
    // 尽快生效(SHChangeNotify 本身没有失败返回值)。
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return allOk;
}

bool UnregisterFileAssociations() {
    AssocDeleteEntry deletes[kAssocDeleteCount];
    BuildAssocUninstallPlan(deletes);  // 恒返回 kAssocDeleteCount,固定长度数组已够用。

    bool allOk = true;
    // 第 0 条是 ProgID 子树,其余 kAssociatedExtensionCount 条是每个扩展名的
    // OpenWithProgids 值——顺序与 BuildAssocUninstallPlan 的生成顺序绑定。
    if (!DeleteOneEntry(deletes[0])) allOk = false;

    for (u32 i = 0; i < kAssociatedExtensionCount; ++i) {
        const AssocDeleteEntry& entry = deletes[1 + i];
        if (!DeleteOneEntry(entry)) allOk = false;

        // 判断是否需要连带删掉这个扩展名键本身。entry.subKey 形如
        // "<ext>\\OpenWithProgids",取出前缀 "<ext>" 才是扩展名键路径本身。
        wchar_t extKey[64];
        wcscpy_s(extKey, entry.subKey);
        wchar_t* sep = wcsstr(extKey, L"\\OpenWithProgids");
        if (sep != nullptr) *sep = L'\0';

        bool openWithProgidsEmpty = ClassesSubKeyIsEmpty(entry.subKey);
        bool extKeyHasNoOtherContent = ClassesSubKeyIsEmpty(extKey);
        if (ShouldRemoveExtensionKey(openWithProgidsEmpty, extKeyHasNoOtherContent)) {
            wchar_t extFullPath[512];
            swprintf_s(extFullPath, L"Software\\Classes\\%s", extKey);
            LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, extFullPath);
            if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) allOk = false;
        }
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return allOk;
}

}  // namespace markair
