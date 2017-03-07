#pragma once

#define SOFTASSERT

/**
 * Needed for CancelSynchronousIo
 */
#define _WIN32_WINNT _WIN32_WINNT_VISTA

#define BARRIER "--------------------------------"

#ifndef SOFTASSERT
#include <cassert>
#else
void assert(bool expr);
#endif

#include <cstdint>
#include <ostream>

namespace jup {
// Standard integer types
using s64 = std::int64_t;
using u64 = std::uint64_t;
using s32 = std::int32_t;
using u32 = std::uint32_t;
using s16 = std::int16_t;
using u16 = std::uint16_t;
using s8 = std::int8_t;
using u8 = std::uint8_t;

// Zero terminated, read-only string
using c_str = char const*;

// Narrow a value, asserting that the conversion is valid.
template <typename T, typename R>
inline void narrow(T& into, R from) {
	into = static_cast<T>(from);
	assert(static_cast<R>(into) == from and (into > 0) == (from > 0));
}


// Use these facilities for general output. They may redirect into a logfile later on.
extern std::ostream& jout;
extern std::ostream& jerr;
using std::endl;


/**
 * While the program is being shut down, this is set to 1. Inhibits the printing
 * of some error messages.
 */
extern bool program_closing;

// for debugging
void debug_break();

} /* end of namespace jup */
