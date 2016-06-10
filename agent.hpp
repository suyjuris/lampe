#pragma once

#include "global.hpp"

#include <functional>

#include "buffer.hpp"
#include "objects.hpp"
#include "server.hpp"

namespace jup {	

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
    int sim_offsets[16];
    int perc_offsets[16];
    int jobexe_offset = 0;
    Requirement agent_task[16];
    u8 agent_cs[16];
    u8 agent_last_go[16];
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



} /* end of namespace jup */
