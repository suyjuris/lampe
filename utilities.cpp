
#include "utilities.hpp"
#include "system.hpp"

namespace jup {

static Arena tmp_arena;

// see header
void* tmp_alloc(int size) {
    return tmp_arena.allocate(size);
}

// see header
void tmp_alloc_reset() {
    tmp_arena.reset();
}

// see header
Arena& tmp_alloc_arena() {
    return tmp_arena;
}

static Buffer tmp_buffer;

// see header
Buffer& tmp_alloc_buffer() {
    return tmp_buffer;
}

jup_str nice_bytes(u64 bytes) {
    if (bytes < 1000) {
        return jup_printf("%d B", bytes);
    } else if ((bytes >> 10) < 1000) {
        return jup_printf("%.2f KiB", (float)bytes / 1024.f);
    } else if ((bytes >> 20) < 1000) {
        return jup_printf("%.2f MiB", (float)(bytes >> 10) / 1024.f);
    } else if ((bytes >> 30) < 1000) {
        return jup_printf("%.2f GiB", (float)(bytes >> 20) / 1024.f);
    } else {
        return jup_printf("%.2f TiB", (float)(bytes >> 30) / 1024.f);
    }
}


jup_str nice_oct(Buffer_view data, bool swap)  {
    char* tmp = (char*)tmp_alloc((data.size() * 8 + 2) / 3 + 1);

    auto get_byte = [swap, data](int i) {
        return (u8)data[swap ? data.size() -i-1 : i];
    };
    auto get_bits = [&get_byte](int i) {
        u8 val = get_byte(i/8);
        if (i%8 < 6) {
            val >>= (5 - i%8);
        } else {
            u8 oth = get_byte(i/8 + 1);
            val = (val << (i%8 - 5)) | (oth >> (13 - i%8));
        }
        return val & 7;
    };

    int i_bit = (data.size() * 8) % 3;
    int i = 0;
    bool flag = false;
    if (i_bit > 0) {
        u8 val = get_byte(0) >> (8 - i_bit);
        if (val or flag) {
            tmp[i++] = '0' + val;
            flag = true;
        }
    }
    
    for (;i_bit < data.size() * 8; i_bit += 3) {
        u8 val = get_bits(i_bit);
        if (val or flag) {
            tmp[i++] = '0' + val;
            flag = true;
        }
    }

    tmp[i] = '\0';
    return {tmp, i};
}

jup_str nice_hex(Buffer_view data) {
    char* tmp = (char*)tmp_alloc(data.size() * 2 + 1);

    for (int i = 0; i < data.size(); ++i) {
        char c1 = (u8)data[i] >> 4;
        char c2 = (u8)data[i] & 15;
        c1 = c1 < 10 ? c1 + '0' : c1 - 10 + 'a';
        c2 = c2 < 10 ? c2 + '0' : c2 - 10 + 'a';
        tmp[2*i]     = c1;
        tmp[2*i + 1] = c2;
    }
    tmp[data.size() * 2] = '\0';
    return {tmp, data.size() * 2};
}

void print_wrapped(std::ostream& out, jup_str str) {
    int width = get_terminal_width();

    if (width < 40) width = 40;

    int i = 0;
    while (i < str.size()) {
        int start = i;
        while (str[i] == ' ' and i < str.size()) ++i;
        if (i == str.size()) break;
            
        int indent = i - start;
        while (true) {
            start = i;
            while (str[i] != '\n' and i < str.size() and indent + i - start < width) ++i;

            if (i == str.size()) {
                break;
            } else if (str[i] == '\n') {
                for (int j = 0; j < indent; ++j) out.put(' ');
                out.write(str.data() + start, ++i - start);
                break;
            }
            
            while (i > start and str[i] != ' ') --i;
            if (i == start) {
                i += width - indent;
                for (int j = 0; j < indent; ++j) out.put(' ');
                out.write(str.data() + start, i - start);
            } else {
                for (int j = 0; j < indent; ++j) out.put(' ');
                out.write(str.data() + start, i++ - start);
                out.put('\n');
            }
        }
    }
}

class Dummy_streambuf: public std::streambuf {
public:
    std::streamsize xsputn (char const* str, std::streamsize n) override {
        return n;
    }
    int overflow (int c) override {
        return 1;
    }
};

class Dummy_ostream: public std::ostream {
public:
    Dummy_ostream(): std::ostream (&buffer) {}
private:
    Dummy_streambuf buffer;
};

static Dummy_ostream jnull_stream;
std::ostream& jnull = jnull_stream;


jup_str jup_stoi_messages[] = {
    /* 0 */ nullptr,
    /* 1 */ "String is empty",
    /* 2 */ "Invalid character",
    /* 3 */ "Out of range (too low)",
    /* 4 */ "Out of range (too high)"
};

u8 jup_stoi(jup_str str, s32* val) {
    assert(val);

    if (not str) return 1;
    
    bool negate = false;
    if (str[0] == '+') {
        str = {str.data() + 1, str.size() - 1};
    } else if (str[0] == '-') {
        negate = true;
        str = {str.data() + 1, str.size() - 1};
    }

    if (not str) return 1;
    
    u32 tmp = 0;
    for (char c: str) {
        if ('0' > c or c > '9') return 2;
        u32 tmp_old = tmp;
        tmp = tmp*10 + (c - '0');
        if (tmp_old > tmp) return negate ? 3 : 4;
    }
    
    if (negate) {
        tmp = -tmp;
        if (not (tmp >> 31) and tmp) {
            return 3;
        }
        *val = (s32)tmp;
        return 0;
    } else {
        if (tmp >> 31) {
            return 4;
        }
        *val = (s32)tmp;
        return 0;
    }
}

// from https://en.wikipedia.org/wiki/Xorshift
u64 Rng::rand() {
    u64 x = rand_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rand_state = x;
    return x * 0x2545f4914f6cdd1dull;
}

bool Rng::gen_bool(u8 perbyte) {
    return (rand() & 0xff) < perbyte;
}

u64 Rng::gen_uni(u64 max) {
    u64 x = rand();
    return x % max;
}

u8 Rng::gen_exp(u8 perbyte) {
    u64 x = rand();
    u64 y = std::numeric_limits<u64>::max();
    y = (y >> 8) * perbyte;

    u8 i = 0;
    while (x < y) {
        i += x > y;
        y = (y * perbyte) >> 8;
    }
    return i;
}

} /* end of namespace jup */
