// markair 网络图片加载(T34,2026-09-19 裁决反转为默认开启)。
//
// 开关语义(§7 "不做任何主动网络请求"原则的可配置逃生舱,而非硬性默认):
//   - `load_remote_images` 为 0 时(用户手动改 state.ini 关闭),本模块
//     **完全不触碰 WinHTTP** —— RemoteImageLoader::Request 在开关关闭时
//     第一行就返回 false,进程里所有 WinHttp* 调用都只存在于 remote.cpp
//     的工作线程函数里,而该函数只可能由 Request 成功之后启动。配合根
//     CMakeLists 的 /DELAYLOAD:winhttp.dll,关闭时不点击加载就不会加载
//     winhttp.dll。
//   - 默认(1)时,文档里的网络图片会在首屏之后自动发起下载,同样走
//     独立 IO 线程 + `PostMessage` 回主线程(架构 §8),不阻塞首帧。
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../util/str.h"
#include "../util/types.h"

namespace markair {

/** 网络图片下载完成时投递给主窗口的消息。wParam 恒为 0,lParam 是 RemoteImageResult*。 */
constexpr UINT kWmRemoteImageDone = WM_APP + 11;

/** 单张网络图片的下载上限(与单图压缩字节上限一致),超出即判失败。 */
constexpr u32 kMaxRemoteImageBytes = 16u * 1024u * 1024u;

/**
 * 一次网络图片请求的结果,由工作线程在进程堆上分配、随消息转交主线程;
 * 主线程用完后必须调用 ReleaseRemoteImageResult 释放。
 */
struct RemoteImageResult {
    StrSlice url;   // 发起请求时的 URL(零拷贝指向调用方保证存活的缓冲,通常是 LinkTarget::href)
    u8* bytes;      // 下载到的原始字节;失败时为 nullptr
    u32 len;        // 字节数;失败时为 0
    bool succeeded; // 是否成功拿到完整响应体
};

/**
 * 释放一条由 kWmRemoteImageDone 消息送达的结果(连同其字节缓冲)。
 * @param result 消息 lParam 转换来的指针,可为 nullptr(此时什么都不做)。
 * @example ReleaseRemoteImageResult(reinterpret_cast<markair::RemoteImageResult*>(lparam));
 */
void ReleaseRemoteImageResult(RemoteImageResult* result);

/**
 * 把一个 http/https URL 拆成 {是否 https, host, port, 资源路径}。纯字符串处理,
 * 不触碰 WinHTTP,可脱离网络单测。
 *
 * @param url UTF-8 的完整 URL。
 * @param outHttps 输出:是否 https(非空)。
 * @param host 输出缓冲:主机名(宽字符,以 '\0' 结尾)。
 * @param hostCap host 的容量(含结尾符)。
 * @param path 输出缓冲:资源路径(含前导 '/',含 query;为空时写 "/")。
 * @param pathCap path 的容量(含结尾符)。
 * @param outPort 输出:端口号,URL 未显式指定时按 scheme 取 80 / 443(非空)。
 * @return 解析成功返回 true;非 http/https、缺 host、缓冲区不够时返回 false。
 * @example
 *   wchar_t host[256], path[1024]; bool https = false; markair::u16 port = 0;
 *   bool ok = markair::ParseHttpUrl(url, &https, host, 256, path, 1024, &port);
 */
bool ParseHttpUrl(StrSlice url, bool* outHttps, wchar_t* host, u32 hostCap,
                  wchar_t* path, u32 pathCap, u16* outPort);

/**
 * 网络图片加载器:按开关决定是否允许发起请求,每张图片一个短命 IO 线程。
 *
 * @example
 *   markair::RemoteImageLoader loader;
 *   loader.Init(hwnd, true);                // load_remote_images = 1(默认)
 *   loader.Request(href);                   // 自动发起下载
 *   // 开关关闭(用户改 state.ini)时,Request 直接返回 false,
 *   // 用户点击占位块可无视开关补一次:
 *   loader.RequestOnUserClick(href);
 */
class RemoteImageLoader {
public:
    // 构造一个未初始化的加载器,不启动任何线程。
    RemoteImageLoader();

    // 等待尚未结束的请求线程句柄关闭(不强杀线程),不做任何网络清理。
    ~RemoteImageLoader();

    RemoteImageLoader(const RemoteImageLoader&) = delete;
    RemoteImageLoader& operator=(const RemoteImageLoader&) = delete;

    /**
     * 绑定接收完成通知的窗口与开关。
     * @param notifyWindow 完成后 PostMessage 的目标窗口,可为 nullptr(此时禁止请求)。
     * @param loadRemoteImages `state.ini` 的 load_remote_images 开关值(默认 1/true,
     *        2026-09-19 裁决反转;ini 读取是 T39 的事,这里只接受传入值,不自己读盘)。
     * @example loader.Init(hwnd, true);
     */
    void Init(HWND notifyWindow, bool loadRemoteImages);

    /** 开关是否打开(为 false 时不会有任何 WinHTTP 调用)。 */
    bool Enabled() const { return enabled_; }

    /**
     * 按开关发起一次自动加载请求(开关为 1 时首屏之后统一加载走这里)。
     * @param url 图片 URL(切片底层字节须在请求完成前保持有效)。
     * @return 开关关闭 / 未绑定窗口 / URL 非法 / 线程创建失败时返回 false,
     *         此时保证没有发生任何网络行为。
     * @example if (!loader.Request(href)) { / * 画"点击加载"占位块 * / }
     */
    bool Request(StrSlice url);

    /**
     * 用户点击占位块时,只为这一张图片发起一次请求(无视开关,这是用户显式动作)。
     * @param url 图片 URL。
     * @return 发起成功返回 true;URL 非法 / 未绑定窗口 / 线程创建失败返回 false。
     * @example loader.RequestOnUserClick(box.href);
     */
    bool RequestOnUserClick(StrSlice url);

private:
    // Request/RequestOnUserClick 共用:校验 URL 后起一个工作线程。
    bool StartRequest(StrSlice url);

    HWND notifyWindow_; // 完成通知目标窗口,不拥有
    bool enabled_;      // load_remote_images 开关
    i32 inFlight_;      // 尚未完成的请求数(仅供调试/限流,Interlocked 访问)
};

}  // namespace markair
