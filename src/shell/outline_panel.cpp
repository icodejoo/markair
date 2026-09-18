#include "outline_panel.h"

namespace mdvn {

u32 TruncateOutlineTitle(const wchar_t* text, u32 len, float maxWidthDip,
                          const OutlineTextMeasurer& measurer,
                          wchar_t* outBuf, u32 outBufCap) {
    if (!outBuf || outBufCap == 0) return 0;
    if (!text || len == 0 || !measurer.measureWidth) {
        outBuf[0] = L'\0';
        return 0;
    }

    // 先看看整段是不是本来就放得下,放得下直接原样拷贝(截到 outBufCap-1)。
    u32 copyLen = (len < outBufCap - 1) ? len : outBufCap - 1;
    if (measurer.measureWidth(measurer.ctx, text, len) <= maxWidthDip) {
        for (u32 i = 0; i < copyLen; ++i) outBuf[i] = text[i];
        outBuf[copyLen] = L'\0';
        return copyLen;
    }

    // 放不下:二分查找能塞进 maxWidthDip 的最大前缀长度 k,拼上省略号。
    // outBufCap 至少要能放下 3 个省略号字符 + 结尾 '\0',否则连省略号都塞不下,
    // 直接退化成只输出省略号本身(仍然合法、不越界)。
    if (outBufCap < 4) {
        u32 n = 0;
        for (; n < outBufCap - 1; ++n) outBuf[n] = L'.';
        outBuf[n] = L'\0';
        return n;
    }

    constexpr wchar_t kEllipsis[] = L"...";
    u32 ellipsisLen = 3;
    u32 maxPrefix = (len < outBufCap - 1 - ellipsisLen) ? len : (outBufCap - 1 - ellipsisLen);

    // [lo, hi] 二分:lo 是已知能放下的前缀长度,hi 是已知放不下(或已到上限+1)。
    u32 lo = 0, hi = maxPrefix;
    while (lo < hi) {
        u32 mid = lo + (hi - lo + 1) / 2;  // 向上取中,避免死循环
        float w = measurer.measureWidth(measurer.ctx, text, mid) +
                  measurer.measureWidth(measurer.ctx, kEllipsis, ellipsisLen);
        if (w <= maxWidthDip) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }

    for (u32 i = 0; i < lo; ++i) outBuf[i] = text[i];
    for (u32 i = 0; i < ellipsisLen; ++i) outBuf[lo + i] = kEllipsis[i];
    outBuf[lo + ellipsisLen] = L'\0';
    return lo + ellipsisLen;
}

u32 FindCurrentOutlineItem(const float* itemTops, u32 count, float viewportTop) {
    if (!itemTops || count == 0) return kInvalidIndex;
    if (viewportTop < itemTops[0]) return kInvalidIndex;  // 裁决二选一:选"无高亮"

    // 二分查找"最后一个 itemTops[i] <= viewportTop"的下标。
    u32 lo = 0, hi = count - 1;
    while (lo < hi) {
        u32 mid = lo + (hi - lo + 1) / 2;  // 向上取中
        if (itemTops[mid] <= viewportTop) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}

}  // namespace mdvn
