# BENCH-B：mdvn 图片密集基准语料（T42）

> 本文件由脚本 + 手工共同生成，用于 T42"图片密集文档峰值内存"验收线的测量。
> 覆盖：50 张本地梯度尺寸 PNG、`data:` URI 内嵌图片（base64 与非 base64两种）、
> 网络图片 URL（验证"默认不加载"）、1 张 SVG、1 张多帧 GIF、1 张超过
> `kMaxDecodedDimension`（当前实测值 512，见 `src/assets/image.h`）的超大尺寸图。
>
> 全部图片语法均为标准 `![alt](path)`，可被 md4c 正确识别为 image 节点。

## 第 1 节：50 张本地 PNG（图标 → 中图 → 大图 → 特大图梯度）

![本地图片 img-001-icon-51x51.png](images/img-001-icon-51x51.png)

![本地图片 img-002-icon-54x54.png](images/img-002-icon-54x54.png)

![本地图片 img-003-icon-57x57.png](images/img-003-icon-57x57.png)

![本地图片 img-004-icon-60x60.png](images/img-004-icon-60x60.png)

![本地图片 img-005-icon-63x63.png](images/img-005-icon-63x63.png)

![本地图片 img-006-icon-66x66.png](images/img-006-icon-66x66.png)

![本地图片 img-007-icon-69x69.png](images/img-007-icon-69x69.png)

![本地图片 img-008-icon-72x72.png](images/img-008-icon-72x72.png)

![本地图片 img-009-icon-75x75.png](images/img-009-icon-75x75.png)

![本地图片 img-010-icon-78x78.png](images/img-010-icon-78x78.png)

![本地图片 img-011-icon-81x81.png](images/img-011-icon-81x81.png)

![本地图片 img-012-icon-84x84.png](images/img-012-icon-84x84.png)

![本地图片 img-013-icon-87x87.png](images/img-013-icon-87x87.png)

![本地图片 img-014-icon-90x90.png](images/img-014-icon-90x90.png)

![本地图片 img-015-icon-93x93.png](images/img-015-icon-93x93.png)

![本地图片 img-016-medium-225x225.png](images/img-016-medium-225x225.png)

![本地图片 img-017-medium-250x250.png](images/img-017-medium-250x250.png)

![本地图片 img-018-medium-275x275.png](images/img-018-medium-275x275.png)

![本地图片 img-019-medium-300x300.png](images/img-019-medium-300x300.png)

![本地图片 img-020-medium-325x325.png](images/img-020-medium-325x325.png)

![本地图片 img-021-medium-350x350.png](images/img-021-medium-350x350.png)

![本地图片 img-022-medium-375x375.png](images/img-022-medium-375x375.png)

![本地图片 img-023-medium-400x400.png](images/img-023-medium-400x400.png)

![本地图片 img-024-medium-425x425.png](images/img-024-medium-425x425.png)

![本地图片 img-025-medium-450x450.png](images/img-025-medium-450x450.png)

![本地图片 img-026-medium-475x475.png](images/img-026-medium-475x475.png)

![本地图片 img-027-medium-500x500.png](images/img-027-medium-500x500.png)

![本地图片 img-028-medium-525x525.png](images/img-028-medium-525x525.png)

![本地图片 img-029-medium-550x550.png](images/img-029-medium-550x550.png)

![本地图片 img-030-medium-575x575.png](images/img-030-medium-575x575.png)

![本地图片 img-031-medium-600x600.png](images/img-031-medium-600x600.png)

![本地图片 img-032-medium-625x625.png](images/img-032-medium-625x625.png)

![本地图片 img-033-medium-650x650.png](images/img-033-medium-650x650.png)

![本地图片 img-034-medium-675x675.png](images/img-034-medium-675x675.png)

![本地图片 img-035-medium-700x700.png](images/img-035-medium-700x700.png)

![本地图片 img-036-large-950x950.png](images/img-036-large-950x950.png)

![本地图片 img-037-large-1000x1000.png](images/img-037-large-1000x1000.png)

![本地图片 img-038-large-1050x1050.png](images/img-038-large-1050x1050.png)

![本地图片 img-039-large-1100x1100.png](images/img-039-large-1100x1100.png)

![本地图片 img-040-large-1150x1150.png](images/img-040-large-1150x1150.png)

![本地图片 img-041-large-1200x1200.png](images/img-041-large-1200x1200.png)

![本地图片 img-042-large-1250x1250.png](images/img-042-large-1250x1250.png)

![本地图片 img-043-large-1300x1300.png](images/img-043-large-1300x1300.png)

![本地图片 img-044-large-1350x1350.png](images/img-044-large-1350x1350.png)

![本地图片 img-045-large-1400x1400.png](images/img-045-large-1400x1400.png)

![本地图片 img-046-xlarge-1720x1720.png](images/img-046-xlarge-1720x1720.png)

![本地图片 img-047-xlarge-1840x1840.png](images/img-047-xlarge-1840x1840.png)

![本地图片 img-048-xlarge-1960x1960.png](images/img-048-xlarge-1960x1960.png)

![本地图片 img-049-xlarge-2080x2080.png](images/img-049-xlarge-2080x2080.png)

![本地图片 img-050-xlarge-2200x2200.png](images/img-050-xlarge-2200x2200.png)

## 第 2 节：`data:` URI 内嵌图片

验证 `src/assets/data_uri.cpp` 的解码路径：既覆盖 `;base64` 编码，也覆盖非
base64（URL-encoded）编码，两条分支都要能正确解析出 payload。

### 2.1 base64 编码的内嵌 PNG（取自 `img-001-icon-51x51.png`，51x51）

![data URI 内嵌 PNG（base64）](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAADMAAAAzCAYAAAA6oTAqAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAAHJSURBVGhD7ZWxagJBEIY3hXAEzIGBSIpY2goG0gg2VoHDwkDKYGVnkTbEIlUewC4v4OPkLfIaE3fP1XXzr3e33p2bZIoPbsed4/92VhWryzn9FVgmVFgmVP6HzMPNW25Q/ylwy1yPqR2N6R6Et0H9WSybHRJCKHrNaWY9ZUqzRp8We7Udp5lMa0Q9oUPJgB2atQ7U1XpCiZL0kdmbzJS66+fbZnpi7dYzDTfPwjw9FUaf6oiSxoiWxjs18vTtaci1q76biBQqQ2YdsHuFngfGSQpK4rRfXZecMqu4TyKaOOvbdXkyAxo6ntXL5VTM8Pba4FfLLKLNlVwjJ2eH1mtXXa9tGfu95ckUuGbpd0uHkgHRD4BR3/bWNRmJCrP5AYj6zmsmUbJ6rzUNVE/xlVFh84H6f9736nHKFCe9ZvpUD/0fFOHrPdmCPjcpUaZ8TJE8QixTFywTKiwTKixTNfH41Qnab5JXRFK5DBKwQX0+sExeUHAE6vWBJ1MEFN4G9flQuYwECWjQfl9qkakLlgkVlgmVYGXuPj4haK+GZeoAiUjQXg3LhIq3zNP5C6yfEpbJ4uLsMRPUdywskwUKb4P6joVlQoVlwmRO30waVGbPtoT+AAAAAElFTkSuQmCC)

### 2.2 非 base64（URL-encoded）编码的内嵌 SVG

![data URI 内嵌 SVG（url-encoded）](data:image/svg+xml,%3Csvg%20xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%20width%3D%2260%22%20height%3D%2260%22%3E%3Crect%20width%3D%2260%22%20height%3D%2260%22%20fill%3D%22%23ff9900%22%2F%3E%3C%2Fsvg%3E)

## 第 3 节：网络图片 URL（验证"默认不加载"，`load_remote_images=0`）

以下均是真实存在的公网图片地址（`src/assets/remote.cpp` 只支持 `http(s)://`
scheme），用于验证"默认不加载网络图片，只在用户点击后才发起 WinHTTP 请求"
这条已裁决行为——本次测量不要求这些地址真的能被访问到。

![GitHub 官方 octocat 头像](https://github.com/octocat.png)

![GitHub avatars 服务图片](https://avatars.githubusercontent.com/u/583231?v=4)

![Wikimedia 公共图片](https://upload.wikimedia.org/wikipedia/commons/4/47/PNG_transparency_demonstration_1.png)

## 第 4 节：SVG（预期落到 Unsupported 占位块）

`src/assets/image.h` 注释明确："SVG 直接判定为不支持，不进 WIC"，
以下引用应显示为 Unsupported 占位块，而不是崩溃或空白。

![本地 SVG](images/sample.svg)

## 第 5 节：多帧 GIF（验证 mdvn 现有 WIC 解码路径对多帧的处理）

手工拼 GIF89a 字节结构生成的 2 帧动图（8x8 像素，红底绿方块 -> 蓝底白方块，
颜色/内容确有变化），已用 .NET GDI+ 的 `Image.GetFrameCount` 独立验证为合法
的 2 帧 GIF。`src/assets/image.cpp::DecodeFirstFrame` 的实现只调用
`decoder->GetFrame(0, ...)` 后立即释放解码器（裁决，见代码注释），也就是说
mdvn 现有解码路径**只认第一帧、不枚举/不播放动画**——这是既有设计，不是本次
新发现的缺陷。

![多帧 GIF](images/animated.gif)

## 第 6 节：超过 kMaxDecodedDimension 的超大尺寸图

`kMaxDecodedDimension` 实测值为 512（`src/assets/image.h` 第 28 行），
以下 6000x6000 图片远超该阈值，用于验证降采样 + "已压缩·点击看原图"
提示标签逻辑。

![超大尺寸图（6000x6000）](images/img-999-oversized-6000x6000.png)
