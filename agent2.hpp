#pragma once

#include "buffer.hpp"
#include "objects.hpp"
#include "server.hpp"
#include "simulation.hpp"

namespace jup {

constexpr int max_strategy_count = 256;

constexpr float search_rating_max  = 5e5;
constexpr float search_exploration = 0.005f;

constexpr float deadline_offset = 2.f;

struct Strategy_slot {
    Strategy strategy;
    float rating = 0.f;
    int visited = 0;
    float rating_sum = 0.f; // Sum of own and children's ratings
    u8 flags = 0;
};

struct Mothership_complex : Mothership {    
	void init(Graph* graph) override;
	void on_sim_start(u8 agent, Simulation const& simulation, int sim_size) override;
	void pre_request_action() override;
	void pre_request_action(u8 agent, Percept const& perc, int perc_size) override;
	void on_request_action() override;
	void post_request_action(u8 agent, Buffer* into) override;

    auto& world() { return world_buffer.get<World>(0); }
    auto& sit() { return sit_buffer.get<Situation>(0); }
    auto& sit_old() { return sit_old_buffer.get<Situation>(0); }
    
    Buffer world_buffer;
    Buffer sit_buffer;
    Buffer sit_old_buffer;
    Buffer sim_buffer;
    Simulation_state sim_state;
    Diff_flat_arrays sit_diff;
    Graph* graph;
    Crafting_plan crafting_plan;
    Array<Auction_bet> auction_bets;
    double deadline = 0;

    u32 strategy_next_id = 0;
    Array<Strategy_slot> strategies;
    Buffer_guard strategies_guard;
};


} /* end of namespace jup */
