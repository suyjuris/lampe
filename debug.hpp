#pragma once

#include "Buffer.hpp"

namespace jup {

// An output stream for debugging purposes
struct Debug_ostream {
    std::ostream& out;
    Buffer buf;
    
    Debug_ostream(std::ostream& out): out{out} {}

    template <typename... Args>
    Debug_ostream& printf(c_str fmt, Args const&... args) {
        buf.reserve(256);
        while (true) {
            int count = std::snprintf(buf.data(), buf.capacity(), fmt, args...);
            assert(count > 0);
            if (count < buf.capacity()) break;
            buf.reserve(count);
        }
        out << buf.data();
        return *this;
    }

};

inline void operator, (Debug_ostream& out, int) {
    out.out << std::endl;
}

template <typename T>
Debug_ostream& operator< (Debug_ostream& out, T const& obj) {
    out.out << obj << ' '; return out;
}
inline Debug_ostream& operator< (Debug_ostream& out, double d) {
    return out.printf(".2le ", d);
}
inline Debug_ostream& operator< (Debug_ostream& out, float f) {
    return out < (double)f;
}
inline Debug_ostream& operator< (Debug_ostream& out, u8 n) {
    return out < (int)n;
}

template <typename Range>
Debug_ostream& operator<= (Debug_ostream& out, Range const& r) {
    out < "{size =" < r.size() < ";";
    for (auto i = r.begin(); i != r.end(); ++i) {
        out < *i;
    }
    return out;
}


extern Debug_ostream jdbg;

} /* end of namespace jup */
