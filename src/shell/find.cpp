#include "find.h"

#include "../util/str.h"

namespace mdvn {

FindSession::FindSession(Arena* results, Arena* scratch)
    : results_(results), scratch_(scratch), matches_(results), queryLen_(0),
      current_(kInvalidIndex), visible_(false) {
    query_[0] = 0;
}

void FindSession::Open() { visible_ = true; }

void FindSession::Close() {
    visible_ = false;
    queryLen_ = 0;
    query_[0] = 0;
    current_ = kInvalidIndex;
    // Vec 没有 Clear 接口;重新绑定同一个 Arena 即可让命中集合归零
    // (与 layout.cpp / navigate.cpp 同样的手法)。
    matches_ = Vec<Match>(results_);
    if (results_) results_->Reset();
}

bool FindSession::AppendChar(wchar_t ch) {
    if (ch < 0x20) return false;           // 控制字符(退格/回车/Esc 等)不进查询串
    if (queryLen_ >= kMaxFindQueryChars) return false;
    query_[queryLen_++] = ch;
    query_[queryLen_] = 0;
    return true;
}

bool FindSession::Backspace() {
    if (queryLen_ == 0) return false;
    query_[--queryLen_] = 0;
    return true;
}

u32 FindSession::Rerun(const Document& doc) {
    current_ = kInvalidIndex;
    if (!results_ || !scratch_) return 0;

    // 整体 Reset 后重建:命中集合不做增量维护,增量输入时重搜一次即可
    // (一次全文扫描本来就在 50ms 预算内)。
    results_->Reset();
    matches_ = Vec<Match>(results_);
    if (queryLen_ == 0) return 0;

    // 查询串是 UTF-16(来自 WM_CHAR),文档文本是 UTF-8,先转一次再搜。
    StrSlice needle = Utf16ToUtf8(Utf16Slice{query_, queryLen_}, results_);
    if (needle.len == 0) return 0;

    u32 count = SearchDocument(doc, needle, scratch_, &matches_);
    if (count > 0) current_ = 0;
    return count;
}

Span<const Match> FindSession::Matches() const {
    return Span<const Match>{matches_.Data(), matches_.Size()};
}

const Match* FindSession::CurrentMatch() const {
    if (current_ == kInvalidIndex || current_ >= matches_.Size()) return nullptr;
    return matches_.Data() + current_;
}

bool FindSession::GoNext() {
    u32 count = matches_.Size();
    if (count == 0) return false;
    current_ = (current_ == kInvalidIndex) ? 0 : (current_ + 1) % count;  // 到末尾环绕
    return true;
}

bool FindSession::GoPrev() {
    u32 count = matches_.Size();
    if (count == 0) return false;
    current_ = (current_ == kInvalidIndex || current_ == 0) ? (count - 1) : (current_ - 1);
    return true;
}

}  // namespace mdvn
