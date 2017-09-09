#pragma once

namespace jup {

#if 0
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
    float cost = 0;
    Flat_array<Requirement, u16, u16> needed;
};

struct Cheap_item {
    u32 price;
    u8 item;
    u8 shop;

    bool operator<(Cheap_item const& other) const {
        return price < other.price;
    }
};
struct Deliver_item {
    Item_stack item;
    u8 storage;
    u16 job;
};

struct Reserved_item {
    u8 agent;
    u8 until;
    Item_stack item;
};

struct Mothership_simple: Mothership {
	void on_sim_start(u8 agent, Simulation const& simulation, int sim_size) override;
    void pre_request_action() override;
    void pre_request_action(u8 agent, Percept const& perc, int perc_size) override;
    void on_request_action() override;
    void post_request_action(u8 agent, Buffer* into) override;

    bool agent_goto(u8 where, u8 agent, Buffer* into);
    bool get_execution_plan(Job const& job, Buffer* into);
    
    Buffer general_buffer;
    Buffer step_buffer;
    Buffer old_step_buffer;
    int sim_offsets[agents_per_team];
    int perc_offsets[agents_per_team];
    int old_perc_offsets[agents_per_team];
    u16 old_job;
    int jobexe_offset = 0;
    Requirement agent_task[agents_per_team];
    u8 agent_cs[agents_per_team];
    u8 agent_last_go[agents_per_team];
    u8 agent_last_cs[agents_per_team];
    int agent_count = 0;
    u8 workshop = 0xff;
    u8 watchdog_timer = 0;
    
    std::vector<Deliver_item> delivs;
    std::vector<Cheap_item> cheaps;
    std::vector<Reserved_item> reserved_items;
    int shop_visited_index = 0;

    auto& job() { return general_buffer.get<Job_execution>(jobexe_offset); }
    auto& sim(int i = 0) { return general_buffer.get<Simulation>(sim_offsets[i]); }
    auto& perc(int i = 0) { return step_buffer.get<Percept>(perc_offsets[i]); }
    auto& old_perc(int i = 0) { return old_step_buffer.get<Percept>(old_perc_offsets[i]); }

    int find_cheap(u8 id) {
        for (size_t i = 0; i < cheaps.size(); ++i) {
            if (cheaps[i].item == id) return i;
        }
        return -1;
    }
    
    float dist_cost(Pos p1, u8 agent) {
        return p1.distr(perc(agent).self.pos) / sim(agent).role.speed * 4.5;
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
	u16 total_capacity;
};

struct Storage_dynamic {
	u16 used_capacity;
	Flat_array<Storage_item> items;
};

struct Task {
    enum Type : u8 {
        NONE, BUY_ITEM, CRAFT_ITEM, CRAFT_ASSIST, DELIVER_ITEM, CHARGE, VISIT
    };
    
    // A Task is invalid iff item.amount == 0
    // I'm sorry about that.
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
    
    Task task = {};
    u8 last_go = 0;
    
	Flat_array<Item_stack> items;
	Flat_array<Pos> route;
};

struct Situation {
	u64 deadline;
	u16 simulation_step;
	Team team;
    
    u16 current_job = 0;
    u8 crafting_fence = 0;
    
	Agent_dynamic agents[agents_per_team];
	Entity_dynamic opponents[agents_per_team];

	Flat_array<Charging_station_dynamic> charging_stations;
	Flat_array<Shop_dynamic> shops;
	Flat_array<Storage_dynamic> storages;
	Flat_array<Auction> auctions;
	Flat_array<Job_priced> jobs;

    Flat_array<Task> goals;
    
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
	Flat_array<Item> items;
	Flat_array<Charging_station_static> charging_stations;
	Flat_array<Dump> dumps;
	Flat_array<Shop_static> shops;
	Flat_array<Storage_static> storages;
	Flat_array<Workshop> workshops;

};

struct Tree {
    u32 situation;
    u32 parent;
    float rating;
    Flat_array<Tree> children;
};

struct Mothership_complex : Mothership {    
	void on_sim_start(u8 agent, Simulation const& simulation, int sim_size) override;
	void pre_request_action() override;
	void pre_request_action(u8 agent, Percept const& perc, int perc_size) override;
	void on_request_action() override;
	void post_request_action(u8 agent, Buffer* into) override;

    bool agent_goto(Situation& sit, u8 where, u8 agent, Buffer* into);
    void get_agent_action(Situation& sit, u8 agent, Buffer* into);
    void internal_simulation_step(u32 treeid);

    u8 can_agent_do(Situation const& sit, u8 agent, Task task);
    float task_heuristic(Situation const& sit, u8 agent, Task& task);
    bool refuel_if_needed(Situation& sit, u8 agent, u8 where);
    void heuristic_task_assign(Situation& sit);
    void add_req_tasks(Situation& sit, Task task);
    void add_req_tasks(Situation& sit, Item_stack is);
    
    void branch_tasks(u32 treeid, u8 agent, u8 depth);
    void branch_goals(u32 treeid, u8 depth);
    void fast_forward(u32 treeid, u8 depth);

    u32 rate_situation(Situation const& s);

    void select_diff_situation(Situation const& s);

	Buffer general_buffer;
	Buffer step_buffer;
	Buffer last_situation_buffer;
    Buffer situation_buffer;
    Diff_flat_arrays diff {&situation_buffer};
    int current_situation = -1;
    
	auto& world() { return general_buffer.get<World>(0); }
    auto& tree(u32 offset = 0) { return step_buffer.get<Tree>(offset); }

	auto& situation(u32 offset = 0) { return situation_buffer.get<Situation>(offset); }
    auto& situation(Tree const& tree) { return situation(tree.situation); }
	auto& last_situation() { return last_situation_buffer.get<Situation>(0); }

    float dist(Pos p1, Pos p2) {
        return p1.distr(p2);
    }
    float dist_cost(Pos p1, u8 agent, Situation const& sit) {
        Role const& role = world().roles[world().agents[agent].role];
        return dist(p1, sit.agents[agent].pos) / role.speed * 4.5;
    }

	template <typename T>
	T* get_by_id(u8 id) {
		return nullptr;
	}

};

#define gbi(T, m)\
template <>\
inline T * Mothership_complex::get_by_id<T>(u8 id) {\
	for (T & x : m) {\
		if (x.name == id) {\
			return &x;\
		}\
	}\
	return nullptr;\
}

gbi(Role, world().roles)
gbi(Item, world().items)
gbi(Charging_station_static, world().charging_stations)
gbi(Dump, world().dumps)
gbi(Shop_static, world().shops)
gbi(Workshop, world().workshops)

#undef gbi

template <>
inline Facility* Mothership_complex::get_by_id<Facility>(u8 id) {
    Facility * fac;
    fac = get_by_id<Charging_station_static>(id); if (fac) return fac;
    fac = get_by_id<Dump>(id);           if (fac) return fac;
    fac = get_by_id<Shop_static>(id);             if (fac) return fac;
    fac = get_by_id<Workshop>(id);                if (fac) return fac;
	return nullptr;
}

void internal_simulation_step(World const& world, Situation& sit);
#endif
} /* end of namespace jup */
