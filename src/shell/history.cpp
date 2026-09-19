#include "history.h"

namespace markair {

namespace {

// 环形缓冲栈的通用实现:PushRaw 满了从栈底挤掉最旧一条;PopRaw 从栈顶
// 弹出;后退栈/前进栈共用同一套逻辑,只是各自传入自己的计数/起点/数组。
void PushRaw(u32* count, u32* start, HistoryEntry* entries,
             const wchar_t* path, float scrollY) {
    u32 writeIndex;
    if (*count < kHistoryCapacity) {
        writeIndex = (*start + *count) % kHistoryCapacity;
        *count += 1;
    } else {
        // 满了:栈底(*start 指向的最旧一条)被新条目复用,起点后移一格,
        // 新条目自然成为新的栈顶,计数保持 kHistoryCapacity 不变。
        writeIndex = *start;
        *start = (*start + 1) % kHistoryCapacity;
    }
    HistoryEntry& slot = entries[writeIndex];
    slot.path[0] = L'\0';
    if (path) {
        u32 i = 0;
        for (; path[i] != L'\0' && i + 1 < kHistoryPathCapacity; ++i) slot.path[i] = path[i];
        slot.path[i] = L'\0';
    }
    slot.scrollY = scrollY;
}

bool PopRaw(u32* count, u32* start, HistoryEntry* entries, HistoryEntry* out) {
    if (*count == 0) return false;
    u32 topIndex = (*start + *count - 1) % kHistoryCapacity;
    if (out) *out = entries[topIndex];
    *count -= 1;
    return true;
}

}  // namespace

History::History()
    : backCount_(0), backStart_(0), forwardCount_(0), forwardStart_(0) {}

void History::PushNavigation(const wchar_t* path, float scrollY) {
    PushRaw(&backCount_, &backStart_, backEntries_, path, scrollY);
    // 新导航必须清空前进栈——刚离开的这条路径与之前"后退过又前进回来"的
    // 分支已经不再相关。
    forwardCount_ = 0;
    forwardStart_ = 0;
}

bool History::PopBack(HistoryEntry* out) {
    return PopRaw(&backCount_, &backStart_, backEntries_, out);
}

bool History::PopForward(HistoryEntry* out) {
    return PopRaw(&forwardCount_, &forwardStart_, forwardEntries_, out);
}

void History::PushForwardRaw(const wchar_t* path, float scrollY) {
    PushRaw(&forwardCount_, &forwardStart_, forwardEntries_, path, scrollY);
}

void History::PushBackRaw(const wchar_t* path, float scrollY) {
    PushRaw(&backCount_, &backStart_, backEntries_, path, scrollY);
}

}  // namespace markair
