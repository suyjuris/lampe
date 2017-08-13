#pragma once

#include "arena.hpp"
#include "buffer.hpp"

namespace jup {

/**
 * Return a pointer to a region of memory at least size in size. It must not be
 * freed and is valid until the next call to tmp_alloc_reset.
 */
void* tmp_alloc(int size);

/**
 * Free memory allocated with tmp_alloc
 */
void tmp_alloc_reset();

/**
 * Return a buffer.
 */
Buffer& tmp_alloc_buffer();

/**
 * Return the memory used for tmp_alloc. This function does not allocate memory.
 */
Arena& tmp_alloc_arena();

template <typename T>
T&& string_unpacker(T&& obj) { return obj; }
inline char const* string_unpacker(jup_str obj) { return obj.c_str(); }

/**
 * Like sprintf, but uses tmp_alloc for memory.
 */
template <typename... Args>
jup_str jup_printf(jup_str fmt, Args const&... args) {
    errno = 0;
    int size = std::snprintf(nullptr, 0, fmt.c_str(), string_unpacker(args)...);
    assert_errno(errno == 0);

    char* tmp = (char*)tmp_alloc(size + 1);
    
    errno = 0;
    std::snprintf(tmp, size + 1, fmt.c_str(), string_unpacker(args)...);
    assert_errno(errno == 0);

    return {tmp, size};
}

jup_str nice_bytes(u64 bytes);

jup_str nice_oct(Buffer_view data, bool swap = false);
jup_str nice_hex(Buffer_view data);

template <typename T>
jup_str nice_oct(T const& obj) {
    return nice_oct(Buffer_view::from_obj<T>(obj), true);
}
template <typename T>
jup_str nice_hex(T const& obj) {
    return nice_hex(Buffer_view::from_obj<T>(obj));
}

void print_wrapped(std::ostream& out, jup_str str);

extern std::ostream& jnull;

extern jup_str jup_stoi_messages[];
u8 jup_stoi(jup_str str, int* val);


} /* end of namespace jup */
