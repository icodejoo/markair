// mdvn 定长整数类型别名,统一命名风格(u8/u32...),避免各处重复 typedef。
#pragma once

#include <cstdint>

namespace mdvn {

using u8 = std::uint8_t;    // 无符号 8 位整数
using u16 = std::uint16_t;  // 无符号 16 位整数
using u32 = std::uint32_t;  // 无符号 32 位整数
using u64 = std::uint64_t;  // 无符号 64 位整数

using i32 = std::int32_t;   // 有符号 32 位整数
using i64 = std::int64_t;   // 有符号 64 位整数

} // namespace mdvn
