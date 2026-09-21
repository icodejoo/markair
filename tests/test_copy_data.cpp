// 单实例 IPC(WM_COPYDATA)消息结构校验/解析的单元测试。纯函数,不需要
// 真实窗口/消息循环,见 src/shell/copy_data.h 顶部注释。
#include "markair_test.h"
#include "../src/shell/copy_data.h"

using markair::kCopyDataMagic;
using markair::TryParseCopyDataPath;

namespace {

// 构造一份"合法"的 COPYDATASTRUCT:cbData 按 (字符数+1)*sizeof(wchar_t) 算,
// 与 main.cpp 里 wWinMain 次实例中继发送时的构造方式保持一致。
COPYDATASTRUCT MakeCopyData(const wchar_t* path, size_t charCountIncludingNul, ULONG_PTR magic) {
    COPYDATASTRUCT cds{};
    cds.dwData = magic;
    cds.cbData = static_cast<DWORD>(charCountIncludingNul * sizeof(wchar_t));
    cds.lpData = const_cast<wchar_t*>(path);
    return cds;
}

}  // namespace

// 用例:合法输入 —— 魔数正确、以 null 结尾、长度正常,解析成功且路径内容一致。
MARKAIR_TEST(CopyData_ValidPathParsesSuccessfully) {
    const wchar_t path[] = L"C:\\docs\\readme.md";
    COPYDATASTRUCT cds = MakeCopyData(path, wcslen(path) + 1, kCopyDataMagic);
    const wchar_t* out = nullptr;
    MARKAIR_CHECK(TryParseCopyDataPath(&cds, &out));
    MARKAIR_CHECK(out == path);
}

// 用例:cds 为空指针。
MARKAIR_TEST(CopyData_NullStructRejected) {
    const wchar_t* out = nullptr;
    MARKAIR_CHECK(!TryParseCopyDataPath(nullptr, &out));
}

// 用例:魔数不匹配 —— 其它程序/意外消息误发的 WM_COPYDATA。
MARKAIR_TEST(CopyData_WrongMagicRejected) {
    const wchar_t path[] = L"C:\\docs\\readme.md";
    COPYDATASTRUCT cds = MakeCopyData(path, wcslen(path) + 1, 0x12345678);
    const wchar_t* out = nullptr;
    MARKAIR_CHECK(!TryParseCopyDataPath(&cds, &out));
}

// 用例:cbData 为 0。
MARKAIR_TEST(CopyData_ZeroSizeRejected) {
    COPYDATASTRUCT cds{};
    cds.dwData = kCopyDataMagic;
    cds.cbData = 0;
    wchar_t buf[4] = L"x";
    cds.lpData = buf;
    const wchar_t* out = nullptr;
    MARKAIR_CHECK(!TryParseCopyDataPath(&cds, &out));
}

// 用例:lpData 为空指针。
MARKAIR_TEST(CopyData_NullDataRejected) {
    COPYDATASTRUCT cds{};
    cds.dwData = kCopyDataMagic;
    cds.cbData = 8;
    cds.lpData = nullptr;
    const wchar_t* out = nullptr;
    MARKAIR_CHECK(!TryParseCopyDataPath(&cds, &out));
}

// 用例:cbData 不是 sizeof(wchar_t) 的整数倍 —— 畸形缓冲区。
MARKAIR_TEST(CopyData_MisalignedSizeRejected) {
    const wchar_t path[] = L"a.md";
    COPYDATASTRUCT cds = MakeCopyData(path, wcslen(path) + 1, kCopyDataMagic);
    cds.cbData -= 1;  // 破坏对齐
    const wchar_t* out = nullptr;
    MARKAIR_CHECK(!TryParseCopyDataPath(&cds, &out));
}

// 用例:字符串未以 L'\0' 结尾 —— 对端没按协议在末尾放终止符。
MARKAIR_TEST(CopyData_NotNulTerminatedRejected) {
    const wchar_t path[] = {L'a', L'.', L'm', L'd'};  // 没有结尾 \0
    COPYDATASTRUCT cds = MakeCopyData(path, 4, kCopyDataMagic);
    const wchar_t* out = nullptr;
    MARKAIR_CHECK(!TryParseCopyDataPath(&cds, &out));
}

// 用例:空字符串(只有一个 \0)—— 路径不能为空。
MARKAIR_TEST(CopyData_EmptyPathRejected) {
    const wchar_t path[] = L"";
    COPYDATASTRUCT cds = MakeCopyData(path, 1, kCopyDataMagic);
    const wchar_t* out = nullptr;
    MARKAIR_CHECK(!TryParseCopyDataPath(&cds, &out));
}

// 用例:路径长度超过 MAX_PATH —— 防御性拒绝超长/畸形输入。
MARKAIR_TEST(CopyData_OverlongPathRejected) {
    wchar_t path[MAX_PATH + 32];
    for (int i = 0; i < MAX_PATH + 10; ++i) path[i] = L'a';
    path[MAX_PATH + 10] = L'\0';
    COPYDATASTRUCT cds = MakeCopyData(path, wcslen(path) + 1, kCopyDataMagic);
    const wchar_t* out = nullptr;
    MARKAIR_CHECK(!TryParseCopyDataPath(&cds, &out));
}

// 用例:cbData 里包含结尾 \0 之后的多余字节(仍然合法)——只要末尾字符是
// \0 且实际有效路径长度不超过 MAX_PATH,不因为缓冲区略大于最小需要而拒绝。
MARKAIR_TEST(CopyData_TrailingNulAtBufferEndIsFine) {
    const wchar_t path[] = L"b.md";
    COPYDATASTRUCT cds = MakeCopyData(path, wcslen(path) + 1, kCopyDataMagic);
    const wchar_t* out = nullptr;
    MARKAIR_CHECK(TryParseCopyDataPath(&cds, &out));
    MARKAIR_CHECK_EQ(wcslen(out), wcslen(path));
}
