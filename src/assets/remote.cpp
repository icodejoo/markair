#include "remote.h"

#include <winhttp.h>

namespace mdvn {

namespace {

// 工作线程的入参:URL 拆解结果 + 回投目标窗口。整块由 HeapAlloc 分配,
// 线程结束时自行释放(不走 Arena —— 生命周期跨线程,不属于文档 Arena 的范畴)。
struct RemoteRequest {
    HWND notifyWindow;
    StrSlice url;        // 零拷贝引用调用方缓冲(文档 Arena 上的 href,活到文档关闭)
    bool https;
    u16 port;
    wchar_t host[256];
    wchar_t path[1024];
    i32* inFlight;       // 指向 RemoteImageLoader::inFlight_,完成时递减
};

// mdvn 的 User-Agent:固定短串,不暴露任何用户/机器信息。
constexpr wchar_t kUserAgent[] = L"mdvn";

// ASCII 小写折叠。
char LowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// 切片是否以某个小写 ASCII 前缀开头(大小写不敏感)。
bool StartsWithIgnoreCase(StrSlice s, const char* prefix, u32 prefixLen) {
    if (s.len < prefixLen) return false;
    for (u32 i = 0; i < prefixLen; ++i) {
        if (LowerAscii(s.data[i]) != prefix[i]) return false;
    }
    return true;
}

// 把 ASCII 字节逐个拓宽写入宽字符缓冲(URL 的 host/path 按 ASCII 处理即可;
// 非 ASCII 字节在这一步会被原样拓宽,交给 WinHTTP 自行处理)。
bool WidenAscii(const char* src, u32 len, wchar_t* dst, u32 cap) {
    if (len + 1u > cap) return false;
    for (u32 i = 0; i < len; ++i) dst[i] = static_cast<wchar_t>(static_cast<unsigned char>(src[i]));
    dst[len] = 0;
    return true;
}

// 真正的下载逻辑:同步 WinHTTP,跑在独立的 IO 线程上,不阻塞主线程。
// 本函数是整个进程里唯一出现 WinHttp* 调用的地方。
DWORD WINAPI RemoteWorker(LPVOID param) {
    RemoteRequest* req = static_cast<RemoteRequest*>(param);

    RemoteImageResult* result = static_cast<RemoteImageResult*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(RemoteImageResult)));
    if (!result) {
        if (req->inFlight) InterlockedDecrement(reinterpret_cast<LONG*>(req->inFlight));
        HeapFree(GetProcessHeap(), 0, req);
        return 0;
    }
    result->url = req->url;

    HINTERNET session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;
    if (session) {
        connection = WinHttpConnect(session, req->host, req->port, 0);
    }
    if (connection) {
        DWORD flags = req->https ? WINHTTP_FLAG_SECURE : 0u;
        request = WinHttpOpenRequest(connection, L"GET", req->path, nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    }

    bool ok = false;
    if (request &&
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (WinHttpQueryHeaders(request,
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                                 WINHTTP_NO_HEADER_INDEX) &&
            statusCode == 200) {
            // 分块读进一个按需增长的进程堆缓冲;超过上限立刻放弃,防止巨型响应击穿内存。
            u32 capacity = 64u * 1024u;
            u8* buffer = static_cast<u8*>(HeapAlloc(GetProcessHeap(), 0, capacity));
            u32 used = 0;
            ok = buffer != nullptr;
            while (ok) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available)) { ok = false; break; }
                if (available == 0) break;  // 正常结束
                if (used + available > kMaxRemoteImageBytes) { ok = false; break; }
                if (used + available > capacity) {
                    u32 newCapacity = capacity;
                    while (newCapacity < used + available) newCapacity *= 2;
                    u8* grown = static_cast<u8*>(
                        HeapReAlloc(GetProcessHeap(), 0, buffer, newCapacity));
                    if (!grown) { ok = false; break; }
                    buffer = grown;
                    capacity = newCapacity;
                }
                DWORD read = 0;
                if (!WinHttpReadData(request, buffer + used, available, &read)) { ok = false; break; }
                if (read == 0) break;
                used += read;
            }
            if (ok && used > 0) {
                result->bytes = buffer;
                result->len = used;
                result->succeeded = true;
            } else {
                if (buffer) HeapFree(GetProcessHeap(), 0, buffer);
                ok = false;
            }
        }
    }

    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);

    if (req->inFlight) InterlockedDecrement(reinterpret_cast<LONG*>(req->inFlight));

    // 回主线程:失败也照样投递,主线程据此把该图切到"加载失败"占位,而不是永远转圈。
    if (!PostMessageW(req->notifyWindow, kWmRemoteImageDone, 0,
                       reinterpret_cast<LPARAM>(result))) {
        ReleaseRemoteImageResult(result);  // 窗口已销毁:自行清理,不泄漏
    }
    HeapFree(GetProcessHeap(), 0, req);
    return 0;
}

}  // namespace

void ReleaseRemoteImageResult(RemoteImageResult* result) {
    if (!result) return;
    if (result->bytes) HeapFree(GetProcessHeap(), 0, result->bytes);
    HeapFree(GetProcessHeap(), 0, result);
}

bool ParseHttpUrl(StrSlice url, bool* outHttps, wchar_t* host, u32 hostCap,
                  wchar_t* path, u32 pathCap, u16* outPort) {
    if (!url.data || !outHttps || !host || !path || !outPort || hostCap == 0 || pathCap == 0) {
        return false;
    }

    bool https = false;
    u32 offset = 0;
    if (StartsWithIgnoreCase(url, "https://", 8)) {
        https = true;
        offset = 8;
    } else if (StartsWithIgnoreCase(url, "http://", 7)) {
        offset = 7;
    } else {
        return false;  // 只支持 http/https,其余 scheme 一律拒绝
    }

    // host 部分到第一个 '/'、'?' 或 '#' 为止;中间可能带 ":port"。
    u32 hostEnd = url.len;
    for (u32 i = offset; i < url.len; ++i) {
        char c = url.data[i];
        if (c == '/' || c == '?' || c == '#') { hostEnd = i; break; }
    }

    u32 colon = hostEnd;
    for (u32 i = offset; i < hostEnd; ++i) {
        if (url.data[i] == ':') { colon = i; break; }
    }

    u32 hostLen = colon - offset;
    if (hostLen == 0) return false;  // 缺 host
    if (!WidenAscii(url.data + offset, hostLen, host, hostCap)) return false;

    u32 port = https ? 443u : 80u;
    if (colon < hostEnd) {
        port = 0;
        for (u32 i = colon + 1; i < hostEnd; ++i) {
            char c = url.data[i];
            if (c < '0' || c > '9') return false;  // 非法端口
            port = port * 10u + static_cast<u32>(c - '0');
            if (port > 65535u) return false;
        }
        if (port == 0) return false;
    }

    // path 含 query,不含 fragment(#... 只对客户端有意义,不发给服务器)。
    u32 pathEnd = url.len;
    for (u32 i = hostEnd; i < url.len; ++i) {
        if (url.data[i] == '#') { pathEnd = i; break; }
    }
    if (hostEnd >= pathEnd) {
        if (pathCap < 2) return false;
        path[0] = L'/';
        path[1] = 0;
    } else if (!WidenAscii(url.data + hostEnd, pathEnd - hostEnd, path, pathCap)) {
        return false;
    }

    *outHttps = https;
    *outPort = static_cast<u16>(port);
    return true;
}

RemoteImageLoader::RemoteImageLoader() : notifyWindow_(nullptr), enabled_(false), inFlight_(0) {}

RemoteImageLoader::~RemoteImageLoader() {
    // 工作线程自带生命周期(下载完成/失败即退出并自行清理),这里不强杀、不等待:
    // 强杀会泄漏 WinHTTP 句柄,而等待会让退出被慢速网络拖住。线程发现窗口已销毁时
    // PostMessage 失败,会自行释放结果。
}

void RemoteImageLoader::Init(HWND notifyWindow, bool loadRemoteImages) {
    notifyWindow_ = notifyWindow;
    enabled_ = loadRemoteImages;
}

bool RemoteImageLoader::StartRequest(StrSlice url) {
    if (!notifyWindow_ || !url.data || url.len == 0) return false;

    RemoteRequest probe{};
    if (!ParseHttpUrl(url, &probe.https, probe.host, 256, probe.path, 1024, &probe.port)) {
        return false;  // URL 不合法:此时还没有任何 WinHTTP 调用发生
    }

    RemoteRequest* req = static_cast<RemoteRequest*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(RemoteRequest)));
    if (!req) return false;
    *req = probe;
    req->notifyWindow = notifyWindow_;
    req->url = url;
    req->inFlight = &inFlight_;

    InterlockedIncrement(reinterpret_cast<LONG*>(&inFlight_));
    HANDLE thread = CreateThread(nullptr, 0, RemoteWorker, req, 0, nullptr);
    if (!thread) {
        InterlockedDecrement(reinterpret_cast<LONG*>(&inFlight_));
        HeapFree(GetProcessHeap(), 0, req);
        return false;
    }
    CloseHandle(thread);  // 不需要 join,线程自管生命周期
    return true;
}

bool RemoteImageLoader::Request(StrSlice url) {
    // 开关关闭时在这里就返回,后面所有 WinHTTP 代码路径都不可达 ——
    // 这是"零网络行为"承诺在代码层面的唯一闸门。
    if (!enabled_) return false;
    return StartRequest(url);
}

bool RemoteImageLoader::RequestOnUserClick(StrSlice url) {
    // 用户显式点击占位块:这是明确的用户动作,不受默认关闭的开关约束(裁决 #5)。
    return StartRequest(url);
}

}  // namespace mdvn
