// markair 文件关联注册/卸载核心(T58)。只写 HKEY_CURRENT_USER\Software\Classes,
// 绝不碰 HKLM(需要管理员权限,与"绿色单 exe、双击即用"冲突),绝不触碰
// UserChoice(篡改它是恶意软件行为,且会被系统重置)。
//
// "要写/删哪些键"这件事被拆成纯函数(BuildAssocWritePlan / BuildAssocUninstallPlan /
// ShouldRemoveExtensionKey),可以脱离真实注册表单测(见 tests/test_assoc.cpp)；
// 真正调用 RegSetValueExW/RegDeleteTreeW 等 Win32 API 的只是薄薄一层
// (RegisterFileAssociations / UnregisterFileAssociations),不参与自动化测试,
// 留给未来人工验收(T61)。
//
// 本文件是一次性动作模块,不涉及 CLI 接入(--register/--unregister 是 T59)。
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../util/types.h"

namespace markair {

// 关联的五个 Markdown 扩展名,唯一定义处——T59 的 CLI、T61 的验收脚本、
// 卸载路径全部从这里派生,不得各写一份 [裁决 #2 附带问题 B]。
constexpr const wchar_t* kAssociatedExtensions[] = {
    L".md", L".markdown", L".mdown", L".mkd", L".mdtext",
};
constexpr u32 kAssociatedExtensionCount = 5;

// 唯一的 ProgID,五个扩展名共用(不为每个扩展名各建一个 ProgID)。
constexpr const wchar_t* kAssocProgId = L"markair.md";

/**
 * 一条要写入 HKCU\Software\Classes 下的注册表值。
 * `subKey` 是相对 `HKCU\Software\Classes\` 的子键路径(不含该前缀)。
 */
struct AssocRegValue {
    const wchar_t* subKey;     // 子键路径,如 L"markair.md" 或 L".md\\OpenWithProgids"
    const wchar_t* valueName;  // nullptr = 该键的默认值;OpenWithProgids 用值名 = ProgID 名
    const wchar_t* data;       // 字符串数据;OpenWithProgids 写空值(REG_NONE),这里传 L""
};

// ProgID 子树的 3 条值(默认值 / DefaultIcon / shell\open\command)+
// 5 条 OpenWithProgids,是 BuildAssocWritePlan 恒定的输出条数。
constexpr u32 kAssocProgIdWriteCount = 3;
constexpr u32 kAssocWriteCount = kAssocProgIdWriteCount + kAssociatedExtensionCount;

/**
 * 生成注册文件关联要写入的全部键值(纯函数,不触碰真实注册表)。
 * 输出内容借用内部静态缓冲区拼出的 DefaultIcon/command 字符串,单线程、
 * 非重入使用即可(与项目"单线程"的整体约束一致);调用方应在下一次调用
 * BuildAssocWritePlan 之前用完本次结果。
 * @param exePath 主程序完整路径,用于拼 `"<exe>",0` 与 `"<exe>" "%1"`。
 * @param displayName ProgID 默认值(打开方式列表里展示的名字)。
 * @param out 输出缓冲区,长度必须 >= kAssocWriteCount(8)。
 * @return 写入条目数,恒为 kAssocWriteCount。
 * @example
 *   markair::AssocRegValue writes[markair::kAssocWriteCount];
 *   markair::BuildAssocWritePlan(L"C:\\markair.exe", L"markair Markdown 文档", writes);
 */
u32 BuildAssocWritePlan(const wchar_t* exePath, const wchar_t* displayName, AssocRegValue out[]);

/**
 * 一条卸载时要删除的注册表条目。`valueName == nullptr` 表示删除整个子键
 * (`RegDeleteTreeW`);否则只删这一个值(`RegDeleteKeyValueW`)。
 */
struct AssocDeleteEntry {
    const wchar_t* subKey;
    const wchar_t* valueName;
};

// ProgID 子树整体删除(1 条)+ 5 条 OpenWithProgids 值删除,与
// BuildAssocWritePlan 的写入集合一一对应。
constexpr u32 kAssocDeleteCount = 1 + kAssociatedExtensionCount;

/**
 * 生成卸载文件关联时必须删除的键/值集合(纯函数)。是否额外删除某个扩展名
 * 键本身由 ShouldRemoveExtensionKey 另行判断,不在这个集合里——本函数只
 * 覆盖"注册时写了什么,卸载时就删什么"这条确定性对应关系。
 * @param out 输出缓冲区,长度必须 >= kAssocDeleteCount(6)。
 * @return 删除条目数,恒为 kAssocDeleteCount。
 * @example
 *   markair::AssocDeleteEntry deletes[markair::kAssocDeleteCount];
 *   markair::BuildAssocUninstallPlan(deletes);
 */
u32 BuildAssocUninstallPlan(AssocDeleteEntry out[]);

/**
 * 判断卸载时是否应该额外删掉某个扩展名键本身(而不只是它下面那条
 * `OpenWithProgids\markair.md` 值)。
 *
 * 两个条件同时成立才删:① 删掉本程序那条 OpenWithProgids 值之后,
 * `OpenWithProgids` 子键已经没有其它值/子键(`openWithProgidsEmptyAfterDelete`);
 * ② 该扩展名键除了这个 `OpenWithProgids` 子键之外,没有任何其它值/子键
 * (`extensionKeyHasNoOtherContent`)—— 这等价于"该扩展名键在我们写入前就是
 * 空的,或者是本程序创建的",两种情形下删除都不会破坏别的程序的关联,
 * 因为真被别的程序占用的扩展名键在①②检查时至少会有一项不成立。
 * 绝不删除本程序注册前就已存在且带内容的扩展名键。
 * @param openWithProgidsEmptyAfterDelete 删掉本程序的值之后 OpenWithProgids 是否已空。
 * @param extensionKeyHasNoOtherContent 扩展名键除 OpenWithProgids 外是否没有其它内容。
 * @return true 表示应该删除该扩展名键(及其下的 OpenWithProgids 子键)。
 * @example markair::ShouldRemoveExtensionKey(true, true);  // true
 */
bool ShouldRemoveExtensionKey(bool openWithProgidsEmptyAfterDelete, bool extensionKeyHasNoOtherContent);

/**
 * 真正把 BuildAssocWritePlan 的结果写进 `HKCU\Software\Classes`,写完后调用
 * `SHChangeNotify(SHCNE_ASSOCCHANGED, ...)` 通知外壳刷新。只写 HKCU。
 * @param exePath 主程序完整路径。
 * @param displayName ProgID 默认值(展示名)。
 * @return 全部写入成功返回 true;任意一步失败返回 false(注册是幂等操作,
 *         失败后可整体重试,不做部分回滚)。
 * @example markair::RegisterFileAssociations(L"C:\\markair.exe", L"markair Markdown 文档");
 */
bool RegisterFileAssociations(const wchar_t* exePath, const wchar_t* displayName);

/**
 * 真正卸载文件关联:删掉 `markair.md` 整棵子树 + 五条 `OpenWithProgids` 值,
 * 并对每个扩展名键按 `ShouldRemoveExtensionKey` 的判断额外清理空壳键。
 * 绝不触碰 `UserChoice`,绝不删除注册前就已存在且带内容的扩展名键。
 * @return 全部删除步骤(键本不存在也视为成功)都无致命错误返回 true。
 * @example markair::UnregisterFileAssociations();
 */
bool UnregisterFileAssociations();

}  // namespace markair
