#pragma once

#include "agent.hpp"
#include "array.hpp"
#include "buffer.hpp"
#include "flat_data.hpp"
#include "graph.hpp"
#include "objects.hpp"
#include "statistics.hpp"
#include "utilities.hpp"

namespace jup {

constexpr int number_of_agents = agents_per_team;
constexpr int planning_max_tasks = 4;

constexpr u8 craft_max_wait = 15;
constexpr u8 shop_assume_duration = 30;

constexpr u8 fast_forward_steps = 80;
constexpr u8 fixer_iterations = 20;
constexpr u8 max_idle_time = 10;

constexpr float price_shop_factor = 1.25f / 1.25f;
constexpr u16   price_craft_val   = 125;

constexpr u8    rate_additem_involved  = 30;
constexpr u8    rate_additem_carryall  = 5;
constexpr float rate_additem_idlescale = 0.125f;
constexpr u8    rate_additem_inventory = 90;
constexpr u8    rate_additem_shop      = 10;
constexpr u8    rate_additem_crafting  = 0;
constexpr u8    rate_additem_fatten    = 30;

constexpr u8    rate_job_started = 90;
constexpr float rate_job_cost    = 1.1f;
constexpr float rate_job_havefac = 0.5f;
constexpr float rate_job_profit  = 0.05f;

constexpr u8 fixer_it_limit = 10;


struct Task {
    // This struct must assume a default value on zero-initialization!

    enum Type: u8 {
        NONE = 0, BUY_ITEM, CRAFT_ITEM, CRAFT_ASSIST, DELIVER_ITEM, CHARGE, VISIT
    };

    struct Craft_id_t {
        u8 id, check;
        bool operator== (Craft_id_t o) const { return id == o.id and check == o.check; }
        bool operator!= (Craft_id_t o) const { return not (*this == o); }
    };
    
    u8 type;
    u8 where;
    Item_stack item;
    union {
        u16 job_id;
        Craft_id_t crafter;
    };
    u8 cnt;
    u8 fixer_it;
};

struct Task_result {
    // This struct must assume a default value on zero-initialization!
    
    enum Error_code: u8 {
        SUCCESS = 0,   OUT_OF_BATTERY,   CRAFT_NO_ITEM,    CRAFT_NO_ITEM_SELF,
        CRAFT_NO_TOOL, NO_CRAFTER_FOUND, NOT_IN_INVENTORY, NOT_VALID_FOR_JOB,
        NO_SUCH_JOB,   MAX_LOAD,         ASSIST_USELESS,   DELIVERY_USELESS,
        NOT_IN_SHOP
    };
    u8 time;
    u8 err;
    Item_stack err_arg;
    u8 left;
    u8 load;
};

struct Shop_limit {
    u8 shop;
    Item_stack item;
};

struct Item_cost {
    u8 id;
    u8 count;
    u16 sum;
    u8 craftval;

    u16 value() const { return sum / count; }
};

class World {
public:
    World(Simulation const& s0, Graph* graph, Buffer* containing);
    void update(Simulation const& s, u8 id, Buffer* containing);

    void step_init(Percept const& p0, Buffer* containing);
    void step_update(Percept const& p, u8 id, Buffer* containing);
    
	u8 team;
	u16 seed_capital;
	u16 steps;
	Flat_array<Item> items;
    Flat_array<Role> roles;
    Graph* graph;

    // Inferred knowledge
    Flat_array<Shop_limit> shop_limits;
    u16 item_costs_job = 0;
    Flat_array<Item_cost> item_costs;
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

// This can be made a union to improve performance and reduce debuggability
struct Task_slot {
    Task task;
    Task_result result;
};

struct Partial_viewer_task {
    using data_type = Task_slot const;
    using value_type = Task const;
    static Task const& view(Task_slot const& slot) { return slot.task; }
};
struct Partial_viewer_task_result {
    using data_type = Task_slot const;
    using value_type = Task_result const;
    static Task_result const& view(Task_slot const& slot) { return slot.result; }
};

struct Strategy {
    Task_slot m_tasks[number_of_agents * planning_max_tasks] = {0};

    Task_slot& task(u8 agent, u8 index) {
        assert(0 <= agent and agent < number_of_agents);
        assert(0 <= index and index < planning_max_tasks);
        return m_tasks[agent * planning_max_tasks + index];
    }
    Task_slot const& task(u8 agent, u8 index) const {
        assert(0 <= agent and agent < number_of_agents);
        assert(0 <= index and index < planning_max_tasks);
        return m_tasks[agent * planning_max_tasks + index];
    }

    Array_view<Task_slot> p_agent(u8 agent) const {
        return {&m_tasks[agent * planning_max_tasks], planning_max_tasks};
    }
    auto p_tasks()   const { return Partial_view_range<Partial_viewer_task>        {m_tasks}; }
    auto p_results() const { return Partial_view_range<Partial_viewer_task_result> {m_tasks}; }

    void insert_task(u8 agent, u8 index, Task task_) {
        for (u8 i = planning_max_tasks - 1; i > index; --i) {
            task(agent, i) = task(agent, i-1);
        }
        task(agent, index).task = task_;
    }
    Task pop_task(u8 agent, u8 index) {
        Task result = task(agent, index).task;
        for (u8 i = index; i + 1 < planning_max_tasks; ++i) {
            task(agent, i) = task(agent, i + 1);
        }
        task(agent, planning_max_tasks - 1).task = Task {};
        return result;
    }
};

struct Self_sim: Self {
    u8 task_index;
    u8 task_state;
    u8 task_sleep;
};

// Fully describes the dynamic data of the world. Effectively combines Percept's
// of all agents, as well as the current strategy
class Situation {
public:
    // Keep in mind to add any arrays here into Situation::register_arr as well
    bool initialized;
	u16 simulation_step;
	s32 team_money;
	Self_sim selves[number_of_agents] = {};
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
    auto const& self(u8 agent) const {
        assert(0 <= agent and agent < number_of_agents);
        return selves[agent];
    }
    auto& task(u8 agent) {
        assert(0 <= agent and agent < number_of_agents);
        return strategy.task(agent, selves[agent].task_index);
    }
    auto const& task(u8 agent) const {
        assert(0 <= agent and agent < number_of_agents);
        return strategy.task(agent, selves[agent].task_index);
    }
    u8 last_time(u8 agent) const {
        if (selves[agent].task_index == 0) {
            return 0;
        } else {
            return strategy.task(agent, selves[agent].task_index - 1).result.time;
        }
    }

    Situation() {}
    Situation(Percept const& p0, Situation const* sit_old /* = nullptr */, Buffer* containing);
    void update(Percept const& p, u8 id, Buffer* containing);
    void register_arr(Diff_flat_arrays* diff);

    void flush_old(World const& world, Situation const& old, Diff_flat_arrays* diff);
    void get_action(World const& world, Situation const& old, u8 agent,
        Buffer* into /*= nullptr*/, Diff_flat_arrays* diff);

    void agent_goto_nl(World const& world, u8 agent, u8 target_id);
    void task_update(World const& world, u8 agent, Diff_flat_arrays* diff);

    bool agent_goto(u8 where, u8 agent, Buffer* into);

    Pos find_pos(u8 id) const;
    
    Job* find_by_id_job(u16 id, u8* type = nullptr);
    Job& get_by_id_job(u16 id, u8* type = nullptr);
};

class Simulation_state {
public:
    World* world;
    Diff_flat_arrays diff;
    Rng rng;
    
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
    void fast_forward();
    void fast_forward(int max_step);

    void fix_errors();
    void create_work();
    float rate();

    void remove_task(u8 agent, u8 index);
    void reduce_load(u8 agent, u8 index);
    void reduce_buy(u8 agent, u8 index, Item_stack arg);
    void reduce_assist(u8 agent, u8 index, Item_stack arg);
    void add_item_for(u8 for_agent, u8 for_index, Item_stack for_item, bool for_tool);
};

template <typename T, typename Id = decltype(T().id)>
T const& get_by_id(Flat_array<T> const& arr, Id id) {
    for (T const& i: arr) {
        if (i.id == id) return i;
    }
    assert(false);
}
template <typename T, typename Id = decltype(T().id)>
T const* find_by_id(Flat_array<T> const& arr, Id id) {
    for (T const& i: arr) {
        if (i.id == id) return &i;
    }
    return nullptr;
}
template <typename T, typename Id = decltype(T().id)>
T& get_by_id(Flat_array<T>& arr, Id id) {
    for (T& i: arr) {
        if (i.id == id) return i;
    }
    assert(false);
}
template <typename T, typename Id = decltype(T().id)>
T* find_by_id(Flat_array<T>& arr, Id id) {
    for (T& i: arr) {
        if (i.id == id) return &i;
    }
    return nullptr;
}

} /* end of namespace jup */
