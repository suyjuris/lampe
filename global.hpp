#pragma once

#include <cassert>
#include <cstdint>
#include <ostream>

namespace jup {

extern std::ostream& jout;
extern std::ostream& jerr;

using s64 = std::int64_t;
using u64 = std::uint64_t;
using s32 = std::int32_t;
using u32 = std::uint32_t;
using s16 = std::int16_t;
using u16 = std::uint16_t;
using s8 = std::int8_t;
using u8 = std::uint8_t;

} /* end of namespace jup */
