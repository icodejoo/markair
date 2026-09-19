// T81:许可文件一致性回归——防止手抄版本号出错。
// 校验 THIRD-PARTY-NOTICES.md 里写的 md4c commit hash 与
// third_party/md4c/VERSION.txt 里的实地记录逐字一致,以及根 LICENSE
// 的版权行确实是裁决 #7①拍板的写法。
// 本文件只读取仓库内的静态文本文件做字符串比对,用 fopen 足够;
// 关掉 CRT 的 fopen 弃用警告,保持 /W4 零警告。
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "markair_test.h"

#include <cstdio>
#include <cstring>

namespace {

// 把整个文件读进一段以 '\0' 结尾的缓冲区,失败返回 false。
bool ReadWholeFile(const char* path, char* buf, size_t bufSize) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    size_t n = std::fread(buf, 1, bufSize - 1, f);
    std::fclose(f);
    buf[n] = '\0';
    return true;
}

// 从 VERSION.txt 内容里取出 "commit: " 后面那一整行(不含换行)。
// 结果写进 out,返回是否找到。
bool ExtractCommitLine(const char* content, char* out, size_t outSize) {
    const char* p = std::strstr(content, "commit: ");
    if (!p) return false;
    p += std::strlen("commit: ");
    const char* end = p;
    while (*end != '\0' && *end != '\r' && *end != '\n') ++end;
    size_t len = static_cast<size_t>(end - p);
    if (len >= outSize) len = outSize - 1;
    std::memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

}  // namespace

// 用例:VERSION.txt 里的 commit hash 必须逐字出现在 THIRD-PARTY-NOTICES.md 中,
// 防止版本号被手抄错。
MARKAIR_TEST(Licensing_ThirdPartyNoticesMatchesVersionTxt) {
    static char versionContent[4096];
    static char noticesContent[16384];

    bool okVersion = ReadWholeFile(MARKAIR_REPO_ROOT_DIR "/third_party/md4c/VERSION.txt",
                                    versionContent, sizeof(versionContent));
    MARKAIR_CHECK(okVersion);
    if (!okVersion) return;

    bool okNotices = ReadWholeFile(MARKAIR_REPO_ROOT_DIR "/THIRD-PARTY-NOTICES.md",
                                    noticesContent, sizeof(noticesContent));
    MARKAIR_CHECK(okNotices);
    if (!okNotices) return;

    char commitHash[256];
    bool okExtract = ExtractCommitLine(versionContent, commitHash, sizeof(commitHash));
    MARKAIR_CHECK(okExtract);
    if (!okExtract) return;

    MARKAIR_CHECK(std::strstr(noticesContent, commitHash) != nullptr);
}

// 用例:根 LICENSE 的版权行必须是裁决 #7①拍板的 "Copyright (c) 2026 cassian"。
MARKAIR_TEST(Licensing_RootLicenseHasDecidedCopyrightLine) {
    static char licenseContent[8192];
    bool ok = ReadWholeFile(MARKAIR_REPO_ROOT_DIR "/LICENSE", licenseContent, sizeof(licenseContent));
    MARKAIR_CHECK(ok);
    if (!ok) return;
    MARKAIR_CHECK(std::strstr(licenseContent, "Copyright (c) 2026 cassian") != nullptr);
}

// 用例:THIRD-PARTY-NOTICES.md 必须引用 third_party/md4c/LICENSE 的许可全文
// (以 md4c 上游版权行 "Martin Mit" 前缀出现为判据,避免只写引用路径不抄全文)。
MARKAIR_TEST(Licensing_ThirdPartyNoticesEmbedsMd4cLicenseText) {
    static char noticesContent[16384];
    bool ok = ReadWholeFile(MARKAIR_REPO_ROOT_DIR "/THIRD-PARTY-NOTICES.md",
                             noticesContent, sizeof(noticesContent));
    MARKAIR_CHECK(ok);
    if (!ok) return;
    MARKAIR_CHECK(std::strstr(noticesContent, "Martin Mit") != nullptr);
}
