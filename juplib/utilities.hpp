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

struct Rng {
    constexpr static u64 init = 0xd1620b2a7a243d4bull;
    u64 rand_state = init;

    /**
     * Return a random u64
     */
    u64 rand();

    /**
     * Return the result of a Bernoulli-Experiment with success rate perbyte/256
     */
    bool gen_bool(u8 perbyte = 128);

    /**
     * Generate a random value in [0, max)
     */
    u64 gen_uni(u64 max);
    
    /**
     * Return an exponentially distributed value, with parameter lambda = perbyte/256. Slow.
     */
    u8 gen_exp(u8 perbyte);

    template <typename T>
    T const* choose_weighted(T const* ptr, int count) {
        if (count == 0) return nullptr;
        u64 sum = 0;
        for (auto const& i: Array_view<T> {ptr, count}) {
            sum += i.rating;
        }
        u64 x = gen_uni(sum);
        for (auto const& i: Array_view<T> {ptr, count}) {
            if (x < i.rating) return &i;
            x -= i.rating;
        }
        assert(false);
    }
};

template <typename Partial_viewer>
struct Partial_view_iterator {
    using difference_type = std::ptrdiff_t;
    using value_type = typename Partial_viewer::value_type;
    using pointer    = typename Partial_viewer::value_type*;
    using reference  = typename Partial_viewer::value_type&;
    using iterator_category = std::random_access_iterator_tag;
    
    using data_type = typename Partial_viewer::data_type;
    using _Self = Partial_view_iterator<Partial_viewer>;

    Partial_view_iterator(data_type* ptr): ptr{ptr} {}
    
    data_type* ptr;
    reference operator*  () const { return  Partial_viewer::view(*ptr); }
    pointer   operator-> () const { return &Partial_viewer::view(*ptr); }
    reference operator[] (difference_type n) const { return Partial_viewer::view(ptr[n]); }
    
    _Self& operator++ ()    { ++ptr; return *this; }
    _Self  operator++ (int) { _Self r = *this; ++ptr; return r; }
    _Self& operator-- ()    { --ptr; return *this; }
    _Self  operator-- (int) { _Self r = *this; --ptr; return r; }

    _Self& operator+= (difference_type n) { ptr += n; return *this; }
    _Self& operator-= (difference_type n) { ptr -= n; return *this; }
    _Self operator+ (difference_type n) const { _Self r = *this; return r += n; }
    _Self operator- (difference_type n) const { _Self r = *this; return r -= n; }
    
    bool operator== (_Self const o) const { return ptr == o.ptr; }
    bool operator!= (_Self const o) const { return ptr != o.ptr; }
    bool operator<  (_Self const o) const { return ptr <  o.ptr; }
    bool operator>  (_Self const o) const { return ptr >  o.ptr; }
    bool operator<= (_Self const o) const { return ptr <= o.ptr; }
    bool operator>= (_Self const o) const { return ptr >= o.ptr; }
};

template <typename Partial_viewer> Partial_view_iterator<Partial_viewer> operator+ (
    typename Partial_view_iterator<Partial_viewer>::difference_type n,
    Partial_view_iterator<Partial_viewer> p
) { return p + n; }
template <typename Partial_viewer> Partial_view_iterator<Partial_viewer> operator- (
    typename Partial_view_iterator<Partial_viewer>::difference_type n,
    Partial_view_iterator<Partial_viewer> p
) { return p - n; }

template <typename Partial_viewer>
struct Partial_view_range {
    using It = Partial_view_iterator<Partial_viewer>;

    template <typename T, int n>
    Partial_view_range(T (&range)[n]): _begin{std::begin(range)}, _end{std::end(range)} {}
    
    template <typename Range>
    Partial_view_range(Range range): _begin{std::begin(range)}, _end{std::end(range)} {}
    
    It _begin, _end;
    It begin() const { return _begin; }
    It end()   const { return _end;   }
};

} /* end of namespace jup */
