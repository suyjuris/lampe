#pragma once

#include "global.hpp"

#include <functional>

#include "buffer.hpp"
#include "objects.hpp"
#include "server.hpp"

namespace jup {

constexpr char const unsigned agents_per_team = 16;

struct Requirement {
    enum Type : u8 {
        GET_ITEM, BUY_ITEM, CRAFT_ITEM, CRAFT_ASSIST, VISIT, NOTHING
    };
    
    u8 type;
    // The index of the Requirement this depends on, or 0xff
    u8 dependency;
    Item_stack item;
    u8 where;
    bool is_tool;
    u8 state = 0;
    u8 id;
};

struct Job_execution {
    u16 job;
    u32 cost = 0;
    Flat_array<Requirement> needed;
};

struct Cheap_item {
    u32 price;
    u8 item;
    u8 shop;
};

struct Mothership_simple: Mothership {

	void on_sim_start(u8 agent, Simulation const& simulation, int sim_size) override;
    void pre_request_action() override;
    void pre_request_action(u8 agent, Perception const& perc, int perc_size) override;
    void on_request_action() override;
    void post_request_action(u8 agent, Buffer* into) override;

    bool agent_goto(u8 where, u8 agent, Buffer* into);
    bool get_execution_plan(Job const& job, Buffer* into);


    Buffer general_buffer;
    Buffer step_buffer;
    int sim_offsets[agents_per_team];
    int perc_offsets[agents_per_team];
    int jobexe_offset = 0;
    Requirement agent_task[agents_per_team];
    u8 agent_cs[agents_per_team];
    u8 agent_last_go[agents_per_team];
    int agent_count = 0;

    std::vector<Cheap_item> cheaps;
    int shop_visited_index = 0;

    auto& job() { return general_buffer.get<Job_execution>(jobexe_offset); }
    auto& sim(int i = 0) { return general_buffer.get<Simulation>(sim_offsets[i]); }
    auto& perc(int i = 0) { return step_buffer.get<Perception>(perc_offsets[i]); }

    int find_cheap(u8 id) {
        for (size_t i = 0; i < cheaps.size(); ++i) {
            if (cheaps[i].item == id) return i;
        }
        return -1;
    }
};

struct Charging_station_static : Facility {
	u8 rate;
	u16 price;
	u8 slots;
};

struct Charging_station_dynamic {
	u8 q_size;
};

struct Shop_item_static {
	u8 item;
	u16 cost;
    u8 period;
};

struct Shop_item_dynamic {
	u8 amount;
	u8 restock;
};

struct Shop_static : Facility {
	Flat_array<Shop_item_static> items;
};

struct Shop_dynamic {
	Flat_array<Shop_item_dynamic> items;
};

struct Storage_static : Facility {
	u8 price;
	u16 totalCapacity;
};

struct Storage_dynamic {
	u16 usedCapacity;
	Flat_array<Storage_item> items;
};

struct Task {
    enum Type : u8 {
        NONE, BUY_ITEM, CRAFT_ITEM, CRAFT_ASSIST, DELIVER_ITEM, CHARGE
    };
    
    u8 type;
    u8 where;
    Item_stack item;
    u8 state;
};

struct Entity_static {
	u8 name;
	u8 role;
};

struct Entity_dynamic {
	Pos pos;
};

struct Agent_static : Entity_static {
};

struct Agent_dynamic : Entity_dynamic {
	u16 charge;
	u16 load;
	u8 last_action;
	u8 last_action_result;
	u8 in_facility;
	u8 f_position;
	u8 route_length;
    
    Task task;
    u8 last_go;
    
	Flat_array<Item_stack> items;
	Flat_array<Pos> route;
};

struct Situation {
	u64 deadline;
	u16 simulation_step;
	Team team;

	Agent_dynamic agents[agents_per_team];
	Entity_dynamic opponents[agents_per_team];

	Flat_array<Charging_station_dynamic> charging_stations;
	Flat_array<Shop_dynamic> shops;
	Flat_array<Storage_dynamic> storages;
	Flat_array<Job_auction> auction_jobs;
	Flat_array<Job_priced> priced_jobs;
};

struct World {
	u8 simulation_id;
	u8 team_id;
	u8 opponent_team;
	u16 seed_capital;
	u16 max_steps;

	Agent_static agents[agents_per_team];
	Entity_static opponents[agents_per_team];

	Flat_array<Role> roles;
	Flat_array<Product> products;
	Flat_array<Charging_station_static> charging_stations;
	Flat_array<Dump_location> dump_locations;
	Flat_array<Shop_static> shops;
	Flat_array<Storage_static> storages;
	Flat_array<Workshop> workshops;

};

struct Mothership_complex : Mothership {
    
	void on_sim_start(u8 agent, Simulation const& simulation, int sim_size) override;
	void pre_request_action() override;
	void pre_request_action(u8 agent, Perception const& perc, int perc_size) override;
	void on_request_action() override;
	void post_request_action(u8 agent, Buffer* into) override;

    bool agent_goto(Situation const& sit, u8 where, u8 agent, Buffer* into);
    void get_agent_action(Situation const& sit, u8 agent, Buffer* into);
    void internal_simulation_step(Situation& sit);

	Buffer general_buffer;
	Buffer step_buffer;
	Buffer last_step_buffer;

	auto& world() { return general_buffer.get<World>(0); }
	auto& situation() { return step_buffer.get<Situation>(0); }
	auto& last_situation() { return last_step_buffer.get<Situation>(0); }

	template <typename T>
	T* get_by_id(u8 id) {
		return nullptr;
	}

	u32 rate_situation(Situation const& s);
};

#define gbi(T, m)\
template <>\
T * Mothership_complex::get_by_id<T>(u8 id) {\
	for (T & x : m) {\
		if (x.name == id) {\
			return &x;\
		}\
	}\
	return nullptr;\
}

gbi(Role, world().roles)
gbi(Product, world().products)
gbi(Charging_station_static, world().charging_stations)
gbi(Dump_location, world().dump_locations)
gbi(Shop_static, world().shops)
gbi(Workshop, world().workshops)

#undef gbi

void internal_simulation_step(World const& world, Situation& sit);

} /* end of namespace jup */
