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

constexpr u8 fast_forward_steps = 80;
constexpr u8 fixer_iterations = 40;
constexpr u8 optimizer_iterations = 10;
constexpr u8 max_idle_time = 10;

constexpr int inventory_size_min = 4;

constexpr float price_shop_factor = 1.25f / 1.25f;
constexpr u16   price_craft_val   = 125;

constexpr u8    rate_additem_involved  = 30;
constexpr u8    rate_additem_carryall  = 5;
constexpr float rate_additem_idlescale = 0.125f;
constexpr u8    rate_additem_inventory = 70;
constexpr u8    rate_additem_shop      = 10;
constexpr u8    rate_additem_retrieve  = 60;
constexpr u8    rate_additem_crafting  = 0;
constexpr u8    rate_additem_fatten    = 30;

constexpr u8    rate_job_started = 90;
constexpr float rate_job_cost    = 1.1f;
constexpr float rate_job_havefac = 0.5f;
constexpr float rate_job_profit  = 0.05f;

constexpr u8 fixer_it_limit = 5;

constexpr float rate_val_item  = 0.83f;
constexpr float rate_fadeoff   = 40;

constexpr float rate_error = 0.f;
constexpr float rate_idletime = 1.f;

constexpr int dist_price_fac = 1000;

constexpr int max_bets_per_step = 4;
constexpr float auction_fine_fac = 0.2f;
constexpr float auction_profit_fac = 0.8f;
constexpr float auction_profit_min = 200;

constexpr u16 recharge_rate = 7;


struct Task {
    // This struct must assume a default value on zero-initialization!

    enum Type: u8 {
        NONE = 0, BUY_ITEM, CRAFT_ITEM, CRAFT_ASSIST, DELIVER_ITEM, RETRIEVE, CHARGE, VISIT
    };

    struct Craft_id_t {
        u8 id, check;
        bool operator== (Craft_id_t o) const { return id == o.id and check == o.check; }
        bool operator!= (Craft_id_t o) const { return not (*this == o); }
    };
    
    u8 type;
    u8 where;
    u16 id;
    Item_stack item;
    union {
        u16 job_id;
        u16 craft_id; // NOTE THAT craft_id OF THE CRAFTER AND ITS id ARE NOT NECESSARILY THE SAME!
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
    void step_post(Buffer* containing);
    
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
    bool other_team_auction = false;

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
    Task_slot m_tasks[number_of_agents * planning_max_tasks] = {};
    u32 s_id = 0; // Can't call it id because of hacks in the debug.hpp implementation
    u32 parent = 0;
    u16 task_next_id = 0;

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

    void insert_task(u8 agent, u8 index, Task task_);
    Task pop_task(u8 agent, u8 index) {
        Task result = task(agent, index).task;
        for (u8 i = index; i + 1 < planning_max_tasks; ++i) {
            task(agent, i) = task(agent, i + 1);
        }
        task(agent, planning_max_tasks - 1).task = Task {};
        return result;
    }

    bool operator== (Strategy const& o) const {
        return std::memcmp(m_tasks, o.m_tasks, sizeof(m_tasks)) == 0;
    }
};

struct Self_sim: Self {
    u8 task_index;
    u8 task_state;
    u8 task_sleep;
};

struct Crafting_slot {
    enum Type: u8 {
        UNINVOLVED = 0, IDLE, EXECUTE, GIVE, USELESS, RECEIVE
    };

    u8 type;
    u8 agent;
    Item_stack item;
    u16 extra_load;
};

struct Crafting_plan {
    Crafting_slot slots[number_of_agents];
    auto& slot(u8 i) {
        assert(0 <= i and i < number_of_agents);
        return slots[i];
    }
};

struct Auction_bet {
    u16 job_id;
    u32 bet;
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

    Situation() {}
    Situation(Percept const& p0, Situation const* sit_old /* = nullptr */, Buffer* containing);
    void update(Percept const& p, u8 id, Buffer* containing);
    void register_arr(Diff_flat_arrays* diff);

    void flush_old(World const& world, Situation const& old, Diff_flat_arrays* diff);
    void moving_on(World const& world, Situation const& old, Diff_flat_arrays* diff);
    bool moving_on_one(World const& world, Situation const& old, Diff_flat_arrays* diff);
    void idle_task(World const& world, Situation const& old, u8 agent, Array<Auction_bet>* bets,
        Buffer* into);
    void get_action(World const& world, Situation const& old, u8 agent, Crafting_slot const& cs,
        Array<Auction_bet>* bets, Buffer* into);

    u16 agent_dist(World const& world, Dist_cache* dist_cache, u8 agent, u8 target_id);
    void agent_goto_nl(World const& world, Dist_cache* dist_cache, u8 agent, u8 target_id);
    void task_update(World const& world, Dist_cache* dist_cache, u8 agent, Diff_flat_arrays* diff);

    bool agent_goto(u8 where, u8 agent, Buffer* into);

    Pos find_pos(u8 id) const;
    bool is_possible_item(World const& world, u8 agent, Task_slot& t, Item_stack i, bool is_tool, bool at_all,
        Crafting_plan* plan = nullptr);
    Crafting_plan crafting_orchestrator(World const& world, u8 agent);
    Crafting_plan combined_plan(World const& world);

    Job* find_by_id_job(u16 id, u8* type = nullptr);
    Job& get_by_id_job(u16 id, u8* type = nullptr);

    void add_item_to_agent(u8 agent, Item_stack item, Diff_flat_arrays* diff);
};

class Simulation_state {
public:
    World* world;
    Diff_flat_arrays diff;
    Rng rng;
    Dist_cache dist_cache;
    
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

    bool fix_errors();
    bool create_work();
    bool optimize();
    void shuffle();
    float rate();
    void auction_bets(Array<Auction_bet>* bets);

    void remove_task(u8 agent, u8 index);
    void reduce_load(u8 agent, u8 index);
    void reduce_buy(u8 agent, u8 index, Item_stack arg);
    void reduce_assist(u8 agent, u8 index, Item_stack arg);
    void add_item_for(u8 for_agent, u8 for_index, Item_stack for_item, bool for_tool);
    bool fix_deadlock();

    u8 sim_time() { return (u8)(sit().simulation_step - orig().simulation_step); }
    int last_time(u8 agent) {
        if (sit().self(agent).task_index == 0) {
            return orig().simulation_step;
        } else {
            auto const& t = sit().strategy.task(agent, sit().self(agent).task_index - 1);
            return orig().simulation_step + t.result.time;
        }
    }
};

template <typename Range, typename T = std::remove_reference_t<decltype(*std::declval<Range>().begin())>,
    typename Id = decltype(std::declval<T>().id)>
T& get_by_id(Range& arr, Id id) {
    for (T& i: arr) {
        if (i.id == id) return i;
    }
    assert(false);
}
template <typename Range, typename T = std::remove_reference_t<decltype(*std::declval<Range>().begin())>,
    typename Id = decltype(std::declval<T>().id)>
T* find_by_id(Range& arr, Id id) {
    for (T& i: arr) {
        if (i.id == id) return &i;
    }
    return nullptr;
}

} /* end of namespace jup */
