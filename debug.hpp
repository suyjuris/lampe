#pragma once

#include "array.hpp"
#include "buffer.hpp"
#include "objects.hpp"
//#include "agent.hpp"
#include "messages.hpp"

namespace jup {

void dbg_main();

#define __get_macro(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9,\
_10, _11, _12, _13, _14, _15, _16, _17, _18, mac, ...) mac
#define __fh1(mac, a) mac(a)
#define __fh2(mac, a, ...) mac(a) __fh1(mac, __VA_ARGS__)
#define __fh3(mac, a, ...) mac(a) __fh2(mac, __VA_ARGS__)
#define __fh4(mac, a, ...) mac(a) __fh3(mac, __VA_ARGS__)
#define __fh5(mac, a, ...) mac(a) __fh4(mac, __VA_ARGS__)
#define __fh6(mac, a, ...) mac(a) __fh5(mac, __VA_ARGS__)
#define __fh7(mac, a, ...) mac(a) __fh6(mac, __VA_ARGS__)
#define __fh8(mac, a, ...) mac(a) __fh7(mac, __VA_ARGS__)
#define __fh9(mac, a, ...) mac(a) __fh8(mac, __VA_ARGS__)
#define __fh10(mac, a, ...) mac(a) __fh9(mac, __VA_ARGS__)
#define __fh11(mac, a, ...) mac(a) __fh10(mac, __VA_ARGS__)
#define __fh12(mac, a, ...) mac(a) __fh11(mac, __VA_ARGS__)
#define __fh13(mac, a, ...) mac(a) __fh12(mac, __VA_ARGS__)
#define __fh14(mac, a, ...) mac(a) __fh13(mac, __VA_ARGS__)
#define __fh15(mac, a, ...) mac(a) __fh14(mac, __VA_ARGS__)
#define __fh16(mac, a, ...) mac(a) __fh15(mac, __VA_ARGS__)
#define __fh17(mac, a, ...) mac(a) __fh16(mac, __VA_ARGS__)
#define __fh18(mac, a, ...) mac(a) __fh17(mac, __VA_ARGS__)
#define __fh19(mac, a, ...) mac(a) __fh18(mac, __VA_ARGS__)
#define __forall(mac, ...) __get_macro(__VA_ARGS__, __fh19,\
__fh18, __fh17, __fh16, __fh15,__fh14, __fh13, __fh12, __fh11,\
__fh10, __fh9, __fh8, __fh7, __fh6, __fh5, __fh4, __fh3, __fh2,\
 __fh1, "fill") (mac, __VA_ARGS__)

#define __sm1(x, y, ...) y
#define __sm2(...) __sm1(__VA_ARGS__, __sm4,)
#define __sm3(...) ~, __sm5,
#define __sm4(f1, f2, x) f1(x)
#define __sm5(f1, f2, x) f2 x
#define __select(f1, f2, x) __sm2(__sm3 x)(f1, f2, x)

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
            assert(count >= 0);
            if (count < buf.capacity()) break;
            buf.reserve(count);
        }
        out << buf.data();
        return *this;
    }

};

struct Debug_tabulator {
	u8 n;
	Debug_tabulator() {
		n = 0;
	}
};

extern Debug_tabulator tab;

struct Repr {
    Buffer_view data;
};

inline Debug_ostream& operator< (Debug_ostream& out, Debug_tabulator const& tab) {
	for (u8 i = 0; i < tab.n; i++) {
		out.out << "    ";
	}
	return out;
}
inline Debug_ostream& operator< (Debug_ostream& out, Repr r) {
    out.out << '"';
	for (char c: r.data) {
        if (c == '\n') {
            out.out << "\\n";
        } else if (c == '\t') {
            out.out << "\\t";
        } else if (c == '\0') {
            out.out << "\\0";
        } else if (' ' <= c and c <= '~') {
            out.out << c;
        } else {
            out.printf("\\x%02x", c);
        }
	}
    out.out << "\" ";
	return out;
}

template <typename T> struct Hex { T const& value; };

template <typename T> auto make_hex(T const& obj) { return Hex<T> {obj}; }

template <typename T> struct Hex_fmt;
template <> struct Hex_fmt<u8>  { static constexpr c_str fmt = "0x%.2hhx"; };
template <> struct Hex_fmt<s8>  { static constexpr c_str fmt = "0x%.2hhx"; };
template <> struct Hex_fmt<u16> { static constexpr c_str fmt = "0x%.4hx"; };
template <> struct Hex_fmt<s16> { static constexpr c_str fmt = "0x%.4hx"; };
template <> struct Hex_fmt<u32> { static constexpr c_str fmt = "0x%.8x"; };
template <> struct Hex_fmt<s32> { static constexpr c_str fmt = "0x%.8x"; };
template <> struct Hex_fmt<u64> { static constexpr c_str fmt = "0x%.16I64x"; };
template <> struct Hex_fmt<s64> { static constexpr c_str fmt = "0x%.16I64x"; };
template <typename T> struct Hex_fmt<T*> { static constexpr c_str fmt = "%p"; };

template <typename T>
inline Debug_ostream& operator< (Debug_ostream& out, Hex<T> h) {
    out.printf(Hex_fmt<T>::fmt, h.value);
    out.out.put(' ');
    return out;
}

struct Id_string {
    Id_string(u8 id): id{id} {}
    u8 id;
};
struct Id_string16 {
    Id_string16(u16 id): id{id} {}
    u16 id;
};

inline Debug_ostream& operator< (Debug_ostream& out, Id_string i) {
    auto val = get_string_from_id(i.id);
    out.out.put('"');
    out.out.write(val.data(), val.size());
    out.out.put('"');
    out.out.put(' ');
	return out;
}
inline Debug_ostream& operator< (Debug_ostream& out, Id_string16 i) {
    auto val = get_string_from_id(i.id);
    out.out.put('"');
    out.out.write(val.data(), val.size());
    out.out.put('"');
    out.out.put(' ');
	return out;
}

template <typename T, T mask>
inline T apply_mask(T val) { return val & mask; }

inline void operator, (Debug_ostream& out, u8 n) {
	do {
		out.out << std::endl;
	} while (n --> 0);
}

template <typename T, typename T2, typename T3>
inline Debug_ostream& operator< (Debug_ostream& out, Flat_array<T, T2, T3> const& fa) {
	return out <= fa;
}
template <typename T>
inline Debug_ostream& operator< (Debug_ostream& out, Array<T> const& arr) {
	return out <= arr;
}
template <typename T>
inline Debug_ostream& operator< (Debug_ostream& out, Array_view<T> const& arr) {
	return out <= arr;
}
template <typename T, size_t n>
Debug_ostream& operator< (Debug_ostream& out, T const (&arr)[n]) {
	return out <= arr;
}
template <typename T>
Debug_ostream& operator< (Debug_ostream& out, T const& obj) {
	out.out << obj << ' '; return out;
}
inline Debug_ostream& operator< (Debug_ostream& out, char const* s) {
	return out.printf(s);
}
inline Debug_ostream& operator< (Debug_ostream& out, double d) {
	return out.printf("%.2elf ", d);
}
inline Debug_ostream& operator< (Debug_ostream& out, float f) {
    return out < (double)f;
}
inline Debug_ostream& operator< (Debug_ostream& out, u8 n) {
    return out < (int)n;
}

extern Debug_ostream jdbg;

// type must have between 1 and 15 elements
#define display_var1(var) < " " < #var < " = " < obj.var < "\b,"
#define display_var2(var, fmt) < " " < #var < " = " < fmt(obj.var) < "\b,"
#define display_var(var) __select(display_var1, display_var2, var)
#define display_obj(type, ...)                                          \
    out < "(" < #type < ") {" __forall(display_var, __VA_ARGS__) < "\b } "
#define print_for_gdb(type) \
    inline void print(type const& obj) __attribute__ ((used));  \
    inline void print(type const& obj) {                        \
        jup::jdbg < obj, 0;                                     \
    }
#define op(type, ...) \
    inline Debug_ostream& operator< (Debug_ostream& out, type const& obj) { \
	    return display_obj(type, __VA_ARGS__);                              \
    }                                                                       \
    print_for_gdb(type)

#define hex(x) (x, make_hex)
#define repr(x) (x, Repr)
#define id(x) (x, Id_string)
#define id16(x) (x, Id_string16)
#define mask(x, m) (x, (apply_mask<decltype(m), m>))

op(Buffer_view, hex(m_data), m_size)
op(Buffer, hex(m_data), m_size, mask(m_capacity, 0x7fffffff))

op(Item_stack, id(item), amount)
op(Pos, lat, lon)
op(Item, id(name), volume, consumed, tools)
//op(Role, name, speed, battery, load, tools)
op(Role, id(name), speed, battery, load)
op(Action, type, get_name(obj.type))
op(Simulation, id, map, team, seed_capital, steps, role, items)
op(Self, id(name), team, pos, role, charge, load, facility, action_type, action_result)
op(Entity, id(name), team, pos, role)
op(Facility, id(name), pos)
op(Charging_station, id(name), pos, rate)
op(Dump, id(name), pos)
op(Shop_item, item, amount, cost)
op(Shop, id(name), pos, restock, items)
op(Storage_item, item, amount, delivered)
op(Storage, id(name), pos, total_capacity, used_capacity, items)
op(Workshop, id(name), pos)
op(Job, id16(id), storage, start, end, required, reward)
op(Auction, id16(id), storage, start, end, required, reward, fine, max_bid)
op(Mission, id16(id), storage, start, end, required, reward, fine, max_bid)
op(Posted, id16(id), storage, start, end, required, reward)
op(Resource_node, id(name), resource, pos)
op(Percept, deadline, id, simulation_step, team_money, self, entities,
	charging_stations, dumps, shops, storages, workshops, resource_nodes,
	auctions, jobs, missions, posteds)

/*op(Requirement, type, dependency, item, where, is_tool, state, id)
op(Job_execution, job, cost, needed)
op(Cheap_item, item, price, shop)
op(Deliver_item, item, storage, job)
op(Reserved_item, agent, until, item)
op(Charging_station_static, id(name), pos, rate, price, slots)
op(Charging_station_dynamic, q_size)
op(Shop_item_static, item, cost, period)
op(Shop_item_dynamic, amount, restock)
op(Shop_static, id(name), pos, items)
op(Shop_dynamic, items)
op(Storage_static, id(name), pos, price, total_capacity)
op(Storage_dynamic, used_capacity, items)
op(Task, type, where, item, state)
op(Entity_static, id(name), role)
op(Entity_dynamic, pos)
op(Agent_static, id(name), role)
op(Agent_dynamic, pos, charge, load, last_action, last_action_result,
	in_facility, f_position, route_length, task, last_go, items, route)
op(Situation, deadline, simulation_step, team, agents, opponents,
	charging_stations, shops, storages, auctions, jobs)
op(World, simulation_id, team_id, opponent_team, seed_capital,
	max_steps, agents, opponents, roles, items, charging_stations,
	dumps, shops, storages, workshops)*/

#undef op
#undef display_obj
#undef display_var
#undef display_var1
#undef display_var2
#undef hex
#undef repr
#undef id
#undef id16
#undef mask

template <typename Range>
Debug_ostream& operator<= (Debug_ostream& out, Range const& r) {
	out < "{";
	if (std::begin(r) == std::end(r)) {
		return  out < "} ";
	}
	tab.n++;
    for (auto i = std::begin(r); i != std::end(r); ++i) {
        out < "\n" < tab < *i < "\b,";
    }
	tab.n--;
    return out < "\b \n" < tab < "} ";
}


} /* end of namespace jup */
