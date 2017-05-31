#pragma once

#include "buffer.hpp"
#include "flat_data.hpp"
#include "objects.hpp"
#include "statistics.hpp"

namespace jup {

constexpr int number_of_agents = 16;
constexpr int planning_max_tasks = 4;

struct Task {
    enum Type : u8 {
        NONE, BUY_ITEM, CRAFT_ITEM, CRAFT_ASSIST, DELIVER_ITEM, CHARGE, VISIT
    };
    
    u8 type = NONE;
    u8 where = 0;
    Item_stack item;
    union {
        u16 job_id;
        u8 crafter_id;
    };
    u8 state = 0;
};

using World = Simulation;

struct Strategy {
    Task m_tasks[number_of_agents * planning_max_tasks];

    Task& task(u8 agent, u8 index) {
        assert(0 <= agent and agent < number_of_agents);
        assert(0 <= index and index < planning_max_tasks);
        return m_tasks[index * number_of_agents + agent];
    }
    Task const& task(u8 agent, u8 index) const {
        assert(0 <= agent and agent < number_of_agents);
        assert(0 <= index and index < planning_max_tasks);
        return m_tasks[index * number_of_agents + agent];
    }    
};

struct Self_sim: Self {
    u8 task_state;
    u8 task_nl;
};

// Fully describes the dynamic data of the world. Effectively combines Percept's
// of all agents, as well as the current strategy
class Situation {
public:
    bool initialized;
	u16 simulation_step;
	s32 team_money;
	Self_sim selves[number_of_agents];
    Strategy strategy;
	Flat_array<Entity> entities;
	Flat_array<Charging_station> charging_stations;
	Flat_array<Dump> dumps;
	Flat_array<Shop> shops;
	Flat_array<Storage> storages;
	Flat_array<Workshop> workshops;
	Flat_array<Resource_node> resource_nodes;
	Flat_array<Auction> auctions;
	Flat_array<Job> jobs;
	Flat_array<Mission> missions;
	Flat_array<Posted> posteds;

    auto& self(u8 agent) {
        assert(0 <= agent and agent < number_of_agents);
        return selves[agent];
    }
    
    Situation(Percept const& p0, Buffer* containing);
    void update(Percept const& p, u8 id, Buffer* containing);
    void register_arr(Diff_flat_arrays* diff);
    
    void get_action(World const& world, u8 agent, Buffer* into);
    void fast_forward(World const& world, Diff_flat_arrays* diff);

    bool agent_goto(u8 where, u8 agent, Buffer* into);
    u8 get_nonlinearity(World const& world, u8 agent);
    void task_update(World const& world, u8 agent, Diff_flat_arrays* diff);
};

struct Agent : Self {
	u8 name;
	u8 role_index;
	u8 team;
};

class Internal_simulation {
	struct Simulation_information {
		Flat_array<Item> items;
		Flat_array<Role> roles;
		Flat_array<Charging_station> charging_stations;
		Flat_array<Dump> dump_locations;
		Flat_array<Shop> shops;
		Flat_array<Storage> storages; // items will stay uninitialized
		Flat_array<Workshop> workshops;
		Flat_array<Auction> auction_jobs;
		Flat_array<Job> priced_jobs;
		Flat_array<Agent> agents;
		u16 seed_capital;
		u16 steps;
	};
	u8 agent_count;
	u16 step;

	Buffer sim_buffer;
	Simulation_information& d() { return sim_buffer.get<Simulation_information>(); }


	Internal_simulation(Game_statistic const& stat);

	void on_sim_start(Buffer *into); // provide Simulations
	void pre_request_action(Buffer *into); // provide Perceptions
	void post_reqest_action(Flat_list<Action> actions);

};

} /* end of namespace jup */
