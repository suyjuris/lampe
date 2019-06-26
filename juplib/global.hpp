
// lampe general headers
#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>   
#include <list>
#include <memory>
#include <mutex>
#include <ostream>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// pugixml headers
#include "pugixml.hpp"

// win32 libraries
#ifdef JUP_WINDOWS

#include <io.h>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

// Needed for CancelSynchronousIo
#undef _WIN32_WINNT
#define _WIN32_WINNT _WIN32_WINNT_VISTA

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef __USE_W32_SOCKETS
#define __USE_W32_SOCKETS
#endif

#endif // JUP_WINDOWS

#ifdef NDEBUG

#define assert(expr) (__builtin_expect(not (expr), 0) ? __builtin_unreachable() : (void)0)
#define assert_errno(expr) assert(expr)
#define assert_win(expr) assert(expr)

#else

#define assert(expr) ((expr) ? (void)0 : ::jup::_assert_fail(#expr, __FILE__, __LINE__))
#define assert_errno(expr) ((expr) ? (void)0 : ::jup::_assert_errno_fail(#expr, __FILE__, __LINE__))
#define assert_win(expr) ((expr) ? (void)0 : ::jup::_assert_win_fail(#expr, __FILE__, __LINE__))

#endif

#define __JUP_UNIQUE_NAME1(x, y) x##y
#define __JUP_UNIQUE_NAME2(x, y) __JUP_UNIQUE_NAME1(x, y)
#define JUP_UNIQUE_NAME(x) __JUP_UNIQUE_NAME2(x, __COUNTER__)

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

// Custom assertions, prints stack trace
[[noreturn]] void _assert_fail(char const* expr_str, char const* file, int line);
[[noreturn]] void _assert_errno_fail(char const* expr_str, char const* file, int line);
[[noreturn]] void _assert_win_fail(char const* expr_str, char const* file, int line);

// Zero terminated, read-only string
using c_str = char const*;

// Prints the error nicely into the console
void err_msg(char const* msg, int code = 0);

// Narrow a value, asserting that the conversion is valid.
template <typename T, typename R>
inline void narrow(T& into, R from) {
	into = static_cast<T>(from);
	assert(static_cast<R>(into) == from and (into > 0) == (from > 0));
}
template <typename T, typename R>
inline T narrow(R from) {
	T result = static_cast<T>(from);
	assert(static_cast<R>(result) == from and (result > 0) == (from > 0));
    return result;
}

// Initialized the termination procedure. Call at the start of program.
void init_signals();

// Closes the program
[[noreturn]] void die(bool show_stacktrace = true);
[[noreturn]] void die(char const* msg, int code = 0, bool show_stacktrace = true);

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
