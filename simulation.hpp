#pragma once

#include "buffer.hpp"
#include "flat_data.hpp"
#include "graph.hpp"
#include "objects.hpp"
#include "statistics.hpp"

namespace jup {

constexpr int number_of_agents = 16;
constexpr int planning_max_tasks = 4;

struct Task {
    // This struct must assume a default value on zero-initialization!
    
    enum Type: u8 {
        NONE = 0, BUY_ITEM, CRAFT_ITEM, CRAFT_ASSIST, DELIVER_ITEM, CHARGE, VISIT
    };
    
    u8 type;
    u8 where;
    Item_stack item;
    union {
        u16 job_id;
        u8 crafter_id;
    };
    u8 state;
};

struct Task_result {
    // This struct must assume a default value on zero-initialization!
    
    enum Error_code: u8 {
        SUCCESS = 0, OUT_OF_BATTERY, CRAFT_NO_ITEM, CRAFT_NO_TOOL, NO_CRAFTER_FOUND,
        NOT_IN_INVENTORY, NOT_VALID_FOR_JOB, NO_SUCH_JOB
    };
    u8 time;
    u8 err;
    Item_stack err_arg;
};

class World {
public:
    World(Simulation const& s0, Graph const* graph, Buffer* containing);
    void update(Simulation const& s, u8 id, Buffer* containing);

	u8 team;
	u16 seed_capital;
	u16 steps;
	Flat_array<Item> items;
    Flat_array<Role> roles;
    Graph const* graph;
};

struct Job_item {
    u16 job_id;
    Item_stack item;

    bool operator== (Job_item o) const {
        return job_id == o.job_id and item == o.item;
    }
};

struct Bookkeeping {
    // Keep in mind to add any arrays here into Situation::register_arr as well
    Flat_array<Job_item> delivered;

    void add_item_to_job(u16 job, Item_stack item, Diff_flat_arrays* diff);
};

union Task_slot {
    Task task;
    Task_result result;
};

struct Strategy {
    Task_slot m_tasks[number_of_agents * planning_max_tasks] = {0};

    Task_slot& task(u8 agent, u8 index) {
        assert(0 <= agent and agent < number_of_agents);
        assert(0 <= index and index < planning_max_tasks);
        return m_tasks[index * number_of_agents + agent];
    }
    Task_slot const& task(u8 agent, u8 index) const {
        assert(0 <= agent and agent < number_of_agents);
        assert(0 <= index and index < planning_max_tasks);
        return m_tasks[index * number_of_agents + agent];
    }
};

struct Params {
    u8 craft_max_wait = 20;
    u8 shop_assume_duration = 30;
};

struct Self_sim: Self {
    // @Incomplete: Care abount initialization
    u8 task_index;
    u8 task_state;
    u8 task_sleep;
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

    Bookkeeping book;
    
    auto& self(u8 agent) {
        assert(0 <= agent and agent < number_of_agents);
        return selves[agent];
    }
    auto& task(u8 agent) {
        assert(0 <= agent and agent < number_of_agents);
        return strategy.task(agent, selves[agent].task_index);
    }
    auto const& self(u8 agent) const {
        assert(0 <= agent and agent < number_of_agents);
        return selves[agent];
    }
    auto const& task(u8 agent) const {
        assert(0 <= agent and agent < number_of_agents);
        return strategy.task(agent, selves[agent].task_index);
    }

    Situation() {}
    Situation(Percept const& p0, Bookkeeping const* book_old /* = nullptr */, Buffer* containing);
    void update(Percept const& p, u8 id, Buffer* containing);
    void register_arr(Diff_flat_arrays* diff);
    
    void get_action(World const& world, Situation const& old, u8 agent, Buffer* into, Diff_flat_arrays* diff);

    void agent_goto_nl(World const& world, u8 agent, u8 target_id);
    void task_update(World const& world, u8 agent, Diff_flat_arrays* diff);

    bool agent_goto(u8 where, u8 agent, Buffer* into);

    Pos find_pos(u8 id) const;
};

class Simulation_state {
public:
    World* world;
    Diff_flat_arrays diff;
    
    int orig_offset, orig_size;
    int sit_offset;

    Simulation_state() {}
    Simulation_state(World* world, Buffer* sit_buffer, int sit_offset, int sit_size) {
        init(world, sit_buffer, sit_offset, sit_size);
    }

    void init(World* world, Buffer* sit_buffer, int sit_offset, int sit_size);
    void reset();
    
    auto& buf()  { return *diff.container; }
    auto& sit()  { return diff.container->get<Situation>(sit_offset ); }
    auto& orig() { return diff.container->get<Situation>(orig_offset); }
    
    void add_charging(u8 agent, u8 before);
    void fast_forward(int max_step);
    void fix_errors(int max_step, int max_iterations = 16);

    void remove_task(u8 agent, u8 index);
    void add_item_for(u8 for_agent, u8 for_index, Item_stack for_item, bool for_tool);

    // from https://en.wikipedia.org/wiki/Xorshift
    constexpr static u64 rand_state_init = 0xd1620b2a7a243d4bull;
    u64 rand_state = rand_state_init;
    u64 jrand() {
        u64 x = rand_state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        rand_state = rand_state;
        return x * 0x2545f4914f6cdd1dull;
    }
};

template <typename T>
T const& get_by_id(Flat_array<T> const& arr, u8 id) {
    for (T const& i: arr) {
        if (i.id == id) return i;
    }
    assert(false);
}
template <typename T>
T const* find_by_id(Flat_array<T> const& arr, u8 id) {
    for (T const& i: arr) {
        if (i.id == id) return &i;
    }
    assert(false);
}
template <typename T>
T& get_by_id(Flat_array<T>& arr, u8 id) {
    for (T& i: arr) {
        if (i.id == id) return i;
    }
    assert(false);
}
template <typename T>
T* find_by_id(Flat_array<T>& arr, u8 id) {
    for (T& i: arr) {
        if (i.id == id) return &i;
    }
    assert(false);
}

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
