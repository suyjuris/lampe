#pragma once

#include "Buffer.hpp"
#include "objects.hpp"
#include "agent.hpp"
#include "messages.hpp"

namespace jup {

void dbg_main();

#define __get_macro(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9,\
_10, _11, _12, _13, _14, mac, ...) mac
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
#define __forall(mac, ...) __get_macro(__VA_ARGS__, __fh15,__fh14,\
__fh13, __fh12, __fh11, __fh10, __fh9, __fh8, __fh7, __fh6,\
__fh5, __fh4, __fh3, __fh2, __fh1, "fill") (mac, __VA_ARGS__)

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

struct Debug_tabulator {
	u8 n;
	Debug_tabulator() {
		n = 0;
	}
};

extern Debug_tabulator tab;

inline Debug_ostream& operator< (Debug_ostream& out, Debug_tabulator const& tab) {
	for (u8 i = 0; i < tab.n; i++) {
		out.out << "    ";
	}
	return out;
}

inline void operator, (Debug_ostream& out, u8 n) {
	do {
		out.out << std::endl;
	} while (n --> 0);
}

template <typename T, typename T2, typename T3>
inline Debug_ostream& operator< (Debug_ostream& out, Flat_array<T, T2, T3> const& fa) {
	return out <= fa;
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
#define display_var(var) < " " < #var < " = " < obj.var< "\b,"
#define display_obj(type, ...)                                          \
    out < "(" < #type < ") {" __forall(display_var, __VA_ARGS__) < "\b } "
#define display_obJ(type, id, ...)                                       \
    out < "(" < #type < ") {" < #id < " = " < (obj.id ?                  \
    get_string_from_id(obj.id).c_str() : "") \
     < "," __forall(display_var, __VA_ARGS__) < "\b } "
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

#define oP(type, id, ...)                                                   \
    inline Debug_ostream& operator< (Debug_ostream& out, type const& obj) { \
        return display_obJ(type, id, __VA_ARGS__);                          \
    }                                                                       \
    print_for_gdb(type)

oP(Item_stack, item, amount)
op(Pos, lat, lon)
oP(Product, name, assembled, volume, consumed, tools)
//op(Role, name, speed, max_battery, max_load, tools)
oP(Role, name, speed, max_battery, max_load)
op(Action, type, get_name(obj.type))
op(Simulation, id, team, seed_capital, steps, role, products)
op(Self, charge, load, last_action, last_action_result, pos, in_facility,
	f_position, route_length, items, route)
op(Team, money, jobs_taken, jobs_posted)
oP(Entity, name, team, pos, role)
oP(Facility, name, pos)
oP(Charging_station, name, pos, rate, price, slots, q_size)
oP(Dump_location, name, pos, price)
op(Shop_item, item, amount, cost, restock)
oP(Shop, name, pos, items)
op(Storage_item, item, amount, delivered)
oP(Storage, name, pos, price, totalCapacity, usedCapacity)
oP(Workshop, name, pos, price)
op(Job_item, item, amount, delivered)
oP(Job, id, storage, begin, end, items)
oP(Job_auction, id, storage, begin, end, items, fine, max_bid)
oP(Job_priced, id, storage, begin, end, items, reward)
op(Perception, deadline, id, simulation_step, self, team, entities,
	charging_stations, shops, storages, workshops, auction_jobs, priced_jobs)
op(Requirement, type, dependency, item, where, is_tool, state, id)
oP(Job_execution, job, cost, needed)
oP(Cheap_item, item, price, shop)
oP(Charging_station_static, name, pos, rate, price, slots)
op(Charging_station_dynamic, q_size)
op(Shop_item_static, item, cost, period)
op(Shop_item_dynamic, amount, restock)
op(Shop_static, name, pos, items)
op(Shop_dynamic, items)
op(Storage_static, name, pos, price, totalCapacity)
op(Storage_dynamic, usedCapacity, items)
op(Task, type, where, item, state)
oP(Entity_static, name, role)
op(Entity_dynamic, pos)
oP(Agent_static, name, role)
op(Agent_dynamic, pos, charge, load, last_action, last_action_result,
	in_facility, f_position, route_length, task, last_go, items, route)
op(Situation, deadline, simulation_step, team, agents, opponents,
	charging_stations, shops, storages, auction_jobs, priced_jobs)
op(World, simulation_id, team_id, opponent_team, seed_capital,
	max_steps, agents, opponents, roles, products, charging_stations,
	dump_locations, shops, storages, workshops)


#undef op
#undef display_obj
#undef display_var_ex
#undef display_var

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
