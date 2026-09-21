// markair 静默后台自动更新模块实现。
//
// 核心设计：
// 1. 启动初期(任何 D2D/窗口创建之前)执行 ApplyPendingUpdate：
//    利用 Windows 运行中 exe 允许 MoveFileExW 重命名的特性，将原 exe 改名为 .old，
//    把已下载好的新版 exe 移入原位，下一次启动即生效。
// 2. 主窗口创建后后台启动线程 BeginUpdateCheck：
//    读取 GitHub releases/latest，比较语义化版本号，若有新版则下载 exe 与 .sha256，
//    校验通过后向主窗口投递 kWmUpdateReady 消息，由主线程写盘记录待安装路径。
// 3. 零额外 DLL 依赖：复用已延迟加载的 winhttp.dll，手写标准 SHA-256 算法，无第三方库。

#include "updater.h"
#include "ini.h"
#include "version.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace markair {

namespace {

// ============================================================================
// SHA-256 算法实现 (标准 FIPS 180-4, 零外部依赖)
// ============================================================================

struct Sha256Context {
    uint32_t state[8];
    uint64_t bitCount;
    uint8_t buffer[64];
};

inline uint32_t RoTr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

inline uint32_t Sigma0(uint32_t x) {
    return RoTr(x, 2) ^ RoTr(x, 13) ^ RoTr(x, 22);
}

inline uint32_t Sigma1(uint32_t x) {
    return RoTr(x, 6) ^ RoTr(x, 11) ^ RoTr(x, 25);
}

inline uint32_t Gamma0(uint32_t x) {
    return RoTr(x, 7) ^ RoTr(x, 18) ^ (x >> 3);
}

inline uint32_t Gamma1(uint32_t x) {
    return RoTr(x, 17) ^ RoTr(x, 19) ^ (x >> 10);
}

const uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

void Sha256Init(Sha256Context* ctx) {
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->bitCount = 0;
}

void Sha256Transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               (static_cast<uint32_t>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        w[i] = Gamma1(w[i - 2]) + w[i - 7] + Gamma0(w[i - 15]) + w[i - 16];
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = h + Sigma1(e) + Ch(e, f, g) + kSha256K[i] + w[i];
        uint32_t t2 = Sigma0(a) + Maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void Sha256Update(Sha256Context* ctx, const uint8_t* data, size_t len) {
    size_t bufIndex = static_cast<size_t>((ctx->bitCount >> 3) & 0x3F);
    ctx->bitCount += static_cast<uint64_t>(len) << 3;

    size_t partLen = 64 - bufIndex;
    size_t i = 0;

    if (len >= partLen) {
        memcpy(&ctx->buffer[bufIndex], data, partLen);
        Sha256Transform(ctx->state, ctx->buffer);
        for (i = partLen; i + 63 < len; i += 64) {
            Sha256Transform(ctx->state, &data[i]);
        }
        bufIndex = 0;
    }

    if (i < len) {
        memcpy(&ctx->buffer[bufIndex], &data[i], len - i);
    }
}

void Sha256Final(Sha256Context* ctx, uint8_t out[32]) {
    uint8_t finalCount[8];
    for (int i = 0; i < 8; ++i) {
        finalCount[i] = static_cast<uint8_t>((ctx->bitCount >> ((7 - i) * 8)) & 0xFF);
    }

    uint8_t pad = 0x80;
    Sha256Update(ctx, &pad, 1);

    uint8_t zero = 0x00;
    while ((ctx->bitCount & 0x1F8) != 0x1C0) {
        Sha256Update(ctx, &zero, 1);
    }

    Sha256Update(ctx, finalCount, 8);

    for (int i = 0; i < 8; ++i) {
        out[i * 4] = static_cast<uint8_t>((ctx->state[i] >> 24) & 0xFF);
        out[i * 4 + 1] = static_cast<uint8_t>((ctx->state[i] >> 16) & 0xFF);
        out[i * 4 + 2] = static_cast<uint8_t>((ctx->state[i] >> 8) & 0xFF);
        out[i * 4 + 3] = static_cast<uint8_t>(ctx->state[i] & 0xFF);
    }
}

bool ComputeFileSha256Hex(const wchar_t* filePath, char outHex[65]) {
    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    Sha256Context ctx;
    Sha256Init(&ctx);

    constexpr DWORD kChunkSize = 64 * 1024;
    uint8_t buffer[kChunkSize];
    DWORD bytesRead = 0;

    while (ReadFile(hFile, buffer, kChunkSize, &bytesRead, nullptr) && bytesRead > 0) {
        Sha256Update(&ctx, buffer, bytesRead);
    }
    CloseHandle(hFile);

    uint8_t hash[32];
    Sha256Final(&ctx, hash);

    for (int i = 0; i < 32; ++i) {
        sprintf_s(&outHex[i * 2], 3, "%02x", hash[i]);
    }
    outHex[64] = 0;
    return true;
}

// ============================================================================
// 结构定义与辅助工具
// ============================================================================

struct UpdateResult {
    wchar_t path[MAX_PATH];
    wchar_t version[32];
};

constexpr wchar_t kUserAgent[] = L"markair-updater";
constexpr wchar_t kGithubHost[] = L"api.github.com";
constexpr wchar_t kGithubReleasePath[] = L"/repos/markair-dev/markair/releases/latest";

// ASCII 小写折叠。
char LowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// 简单 SemVer 比较: 比较 "X.Y.Z" 形式的版本号。
// 返回: 1 表示 a > b; -1 表示 a < b; 0 表示 a == b。
int CompareSemVer(const char* a, const char* b) {
    while (*a == 'v' || *a == 'V') ++a;
    while (*b == 'v' || *b == 'V') ++b;

    for (int part = 0; part < 3; ++part) {
        int valA = 0;
        while (*a >= '0' && *a <= '9') {
            valA = valA * 10 + (*a - '0');
            ++a;
        }
        if (*a == '.') ++a;

        int valB = 0;
        while (*b >= '0' && *b <= '9') {
            valB = valB * 10 + (*b - '0');
            ++b;
        }
        if (*b == '.') ++b;

        if (valA > valB) return 1;
        if (valA < valB) return -1;
    }
    return 0;
}

// 解析 URL 分解主机名、端口与相对路径。
bool CrackUrl(const wchar_t* url, bool* outHttps, wchar_t* outHost, DWORD hostCap,
              wchar_t* outPath, DWORD pathCap, INTERNET_PORT* outPort) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = outHost;
    uc.dwHostNameLength = hostCap;
    uc.lpszUrlPath = outPath;
    uc.dwUrlPathLength = pathCap;

    if (!WinHttpCrackUrl(url, 0, 0, &uc)) return false;
    *outHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    *outPort = uc.nPort;
    return true;
}

// 简单的内存 HTTP GET 请求。
bool SimpleHttpGetBuffer(const wchar_t* host, INTERNET_PORT port, bool https,
                         const wchar_t* path, char** outBuf, DWORD* outLen) {
    HINTERNET session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;

    // 网络兜底: 设置解析5s、连接5s、发送10s、接收10s短超时，断网或无服务时快速退出。
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);

    HINTERNET connection = WinHttpConnect(session, host, port, 0);
    HINTERNET request = nullptr;
    if (connection) {
        DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
        request = WinHttpOpenRequest(connection, L"GET", path, nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    }

    bool success = false;
    if (request) {
        // 配置允许自动重定向(GitHub assets 会重定向到 S3)
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

        if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD statusCode = 0;
            DWORD statusSize = sizeof(statusCode);
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                                    WINHTTP_NO_HEADER_INDEX) &&
                statusCode >= 200 && statusCode < 300) {

                DWORD totalRead = 0;
                DWORD capacity = 64 * 1024;
                char* buf = static_cast<char*>(malloc(capacity));

                if (buf) {
                    DWORD bytesRead = 0;
                    bool readOk = true;
                    while (WinHttpReadData(request, buf + totalRead, capacity - totalRead - 1, &bytesRead) &&
                           bytesRead > 0) {
                        totalRead += bytesRead;
                        if (totalRead + 32 * 1024 >= capacity) {
                            if (capacity >= 16 * 1024 * 1024) {  // 保护上限 16MB
                                readOk = false;
                                break;
                            }
                            capacity *= 2;
                            char* newBuf = static_cast<char*>(realloc(buf, capacity));
                            if (!newBuf) {
                                readOk = false;
                                break;
                            }
                            buf = newBuf;
                        }
                    }

                    if (readOk) {
                        buf[totalRead] = 0;
                        *outBuf = buf;
                        *outLen = totalRead;
                        success = true;
                    } else {
                        free(buf);
                    }
                }
            }
        }
    }

    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    return success;
}

// 将 URL 文件下载并写入本地文件。
bool DownloadUrlToFile(const wchar_t* url, const wchar_t* destPath) {
    bool https = false;
    wchar_t host[256]{};
    wchar_t path[2048]{};
    INTERNET_PORT port = 0;
    if (!CrackUrl(url, &https, host, 256, path, 2048, &port)) return false;

    HINTERNET session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;

    // 网络兜底: 设置解析5s、连接5s、发送10s、接收15s超时。
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 15000);

    HINTERNET connection = WinHttpConnect(session, host, port, 0);
    HINTERNET request = nullptr;
    if (connection) {
        DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
        request = WinHttpOpenRequest(connection, L"GET", path, nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    }

    bool success = false;
    if (request) {
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

        if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD statusCode = 0;
            DWORD statusSize = sizeof(statusCode);
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                                    WINHTTP_NO_HEADER_INDEX) &&
                statusCode >= 200 && statusCode < 300) {

                HANDLE hFile = CreateFileW(destPath, GENERIC_WRITE, 0, nullptr,
                                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hFile != INVALID_HANDLE_VALUE) {
                    constexpr DWORD kChunkSize = 64 * 1024;
                    char chunk[kChunkSize];
                    DWORD bytesRead = 0;
                    DWORD bytesWritten = 0;
                    bool writeOk = true;

                    while (WinHttpReadData(request, chunk, kChunkSize, &bytesRead) && bytesRead > 0) {
                        if (!WriteFile(hFile, chunk, bytesRead, &bytesWritten, nullptr) ||
                            bytesWritten != bytesRead) {
                            writeOk = false;
                            break;
                        }
                    }
                    CloseHandle(hFile);
                    if (writeOk) {
                        success = true;
                    } else {
                        DeleteFileW(destPath);
                    }
                }
            }
        }
    }

    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    return success;
}

// 在 JSON 文本中提取某个键的字符串值。
bool ExtractJsonStringValue(const char* json, const char* key, char* outVal, size_t outCap) {
    char needle[128];
    sprintf_s(needle, sizeof(needle), "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return false;

    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == ':') ++p;
    if (*p != '\"') return false;
    ++p;

    size_t i = 0;
    while (*p != 0 && *p != '\"' && i + 1 < outCap) {
        outVal[i++] = *p++;
    }
    outVal[i] = 0;
    return true;
}

// 在 GitHub Release JSON 中提取名为 assetName 的资源对应的 browser_download_url。
bool FindAssetDownloadUrl(const char* json, const char* assetName, wchar_t* outUrl, DWORD outCap) {
    const char* cursor = json;
    while ((cursor = strstr(cursor, "\"name\"")) != nullptr) {
        char nameVal[64]{};
        if (ExtractJsonStringValue(cursor, "name", nameVal, sizeof(nameVal))) {
            if (strcmp(nameVal, assetName) == 0) {
                char urlVal[2048]{};
                if (ExtractJsonStringValue(cursor, "browser_download_url", urlVal, sizeof(urlVal))) {
                    int wlen = MultiByteToWideChar(CP_UTF8, 0, urlVal, -1, outUrl, outCap);
                    return wlen > 0;
                }
            }
        }
        cursor += 6;
    }
    return false;
}

// 后台版本检查及更新下载线程执行体。
DWORD WINAPI UpdateWorkerThread(LPVOID param) {
    HWND notifyHwnd = reinterpret_cast<HWND>(param);

    // 1. 发起请求获取最新 release 信息
    char* releaseJson = nullptr;
    DWORD releaseJsonLen = 0;
    if (!SimpleHttpGetBuffer(kGithubHost, 443, true, kGithubReleasePath, &releaseJson, &releaseJsonLen)) {
        return 0;
    }

    // 2. 解析最新版本 tag_name
    char tagName[64]{};
    if (!ExtractJsonStringValue(releaseJson, "tag_name", tagName, sizeof(tagName))) {
        free(releaseJson);
        return 0;
    }

    // 比较版本：如果不大说明无需更新
    if (CompareSemVer(tagName, kMarkairVersion) <= 0) {
        free(releaseJson);
        return 0;
    }

    // 3. 提取 markair.exe 与 markair.exe.sha256 的下载 URL
    wchar_t exeUrl[2048]{};
    wchar_t shaUrl[2048]{};
    bool hasExe = FindAssetDownloadUrl(releaseJson, "markair.exe", exeUrl, 2048);
    bool hasSha = FindAssetDownloadUrl(releaseJson, "markair.exe.sha256", shaUrl, 2048);
    free(releaseJson);

    if (!hasExe || !hasSha) {
        return 0;
    }

    // 4. 下载 .sha256 内容并解析目标 hash
    bool shaHttps = false;
    wchar_t shaHost[256]{};
    wchar_t shaPath[2048]{};
    INTERNET_PORT shaPort = 0;
    if (!CrackUrl(shaUrl, &shaHttps, shaHost, 256, shaPath, 2048, &shaPort)) {
        return 0;
    }

    char* shaContent = nullptr;
    DWORD shaContentLen = 0;
    if (!SimpleHttpGetBuffer(shaHost, shaPort, shaHttps, shaPath, &shaContent, &shaContentLen)) {
        return 0;
    }

    char expectedHash[65]{};
    size_t hashLen = 0;
    for (DWORD i = 0; i < shaContentLen && hashLen < 64; ++i) {
        char c = LowerAscii(shaContent[i]);
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
            expectedHash[hashLen++] = c;
        } else if (hashLen > 0) {
            break;
        }
    }
    free(shaContent);

    if (hashLen != 64) {
        return 0;
    }

    // 5. 准备临时下载路径 %TEMP%\markair_update
    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    wcscat_s(tempDir, MAX_PATH, L"markair_update");
    CreateDirectoryW(tempDir, nullptr);

    wchar_t tempExePath[MAX_PATH]{};
    swprintf_s(tempExePath, L"%s\\markair_%hs.tmp", tempDir, tagName);

    // 6. 下载新版 exe
    if (!DownloadUrlToFile(exeUrl, tempExePath)) {
        return 0;
    }

    // 7. 校验 SHA-256
    char actualHash[65]{};
    if (!ComputeFileSha256Hex(tempExePath, actualHash) || strcmp(expectedHash, actualHash) != 0) {
        DeleteFileW(tempExePath);
        return 0;
    }

    // 校验通过：重命名为目标 pending 路径
    wchar_t pendingExePath[MAX_PATH]{};
    swprintf_s(pendingExePath, L"%s\\markair_%hs.exe", tempDir, tagName);
    MoveFileExW(tempExePath, pendingExePath, MOVEFILE_REPLACE_EXISTING);

    // 8. 投递消息给主窗口
    UpdateResult* res = static_cast<UpdateResult*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(UpdateResult)));
    if (res) {
        wcscpy_s(res->path, MAX_PATH, pendingExePath);
        MultiByteToWideChar(CP_UTF8, 0, tagName, -1, res->version, 32);
        PostMessageW(notifyHwnd, kWmUpdateReady, 0, reinterpret_cast<LPARAM>(res));
    }

    return 0;
}

}  // namespace

void ApplyPendingUpdate() {
    AppSettings settings;
    LoadAppSettings(&settings);

    if (settings.pendingUpdatePath[0] == 0) {
        return;
    }

    // 检查待安装文件是否存在
    DWORD attr = GetFileAttributesW(settings.pendingUpdatePath);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        settings.pendingUpdatePath[0] = 0;
        settings.pendingUpdateVersion[0] = 0;
        SaveAppSettings(settings);
        return;
    }

    wchar_t selfPath[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;

    wchar_t oldPath[MAX_PATH + 8]{};
    swprintf_s(oldPath, L"%s.old", selfPath);

    // 清理可能遗留的旧文件
    DeleteFileW(oldPath);

    // Windows 核心特性：运行中正在执行的 exe 允许被重命名
    if (!MoveFileExW(selfPath, oldPath, MOVEFILE_REPLACE_EXISTING)) {
        return;  // 重命名失败(例如无写入权限)，静默放弃
    }

    // 移入新版 exe
    if (!MoveFileExW(settings.pendingUpdatePath, selfPath, MOVEFILE_REPLACE_EXISTING)) {
        // 回滚还原
        MoveFileExW(oldPath, selfPath, 0);
        return;
    }

    // 替换成功，清空待安装记录
    settings.pendingUpdatePath[0] = 0;
    settings.pendingUpdateVersion[0] = 0;
    SaveAppSettings(settings);

    // 尽力删除旧版本备份(删不掉也没关系，下次启动继续清理)
    DeleteFileW(oldPath);
}

void BeginUpdateCheck(HWND hwnd) {
    if (!hwnd) return;

    AppSettings settings;
    LoadAppSettings(&settings);

    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    i64 nowUnix = static_cast<i64>((uli.QuadPart - 116444736000000000ULL) / 10000000ULL);

    // 24 小时 (86400秒) 内最多检查一次
    if (settings.lastUpdateCheckUnix > 0 && (nowUnix - settings.lastUpdateCheckUnix) < 86400) {
        return;
    }

    // 记录本次检查时间并保存
    settings.lastUpdateCheckUnix = nowUnix;
    SaveAppSettings(settings);

    // 启动异步工作线程
    HANDLE hThread = CreateThread(nullptr, 0, UpdateWorkerThread, reinterpret_cast<LPVOID>(hwnd), 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
    }
}

void CommitPendingUpdate(HWND hwnd, void* resultPtr) {
    (void)hwnd;
    UpdateResult* res = static_cast<UpdateResult*>(resultPtr);
    if (!res) return;

    AppSettings settings;
    LoadAppSettings(&settings);
    wcscpy_s(settings.pendingUpdatePath, MAX_PATH, res->path);
    wcscpy_s(settings.pendingUpdateVersion, 32, res->version);
    SaveAppSettings(settings);

    HeapFree(GetProcessHeap(), 0, res);
}

}  // namespace markair
