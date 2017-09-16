#pragma once

#include "agent.hpp"
#include "server.hpp"
#include "simulation.hpp"

namespace jup {

void test_jdbg_diff();
    
struct Simulation_data {
	u8 test;
};

struct Mothership_test : Mothership {
	void init(Graph* graph) override;
	void on_sim_start(u8 agent, Simulation const& simulation, int sim_size) override;
	void pre_request_action() override;
	void pre_request_action(u8 agent, Percept const& perc, int perc_size) override;
	void on_request_action() override;
	void post_request_action(u8 agent, Buffer* into) override;

	Graph* graph = nullptr;
	Buffer general_buffer;
	Buffer step_buffer;
	int sim_offsets[agents_per_team];
	int perc_offsets[agents_per_team];
	int data_offset = 0;
	int agent_count = 0;
	int visit[16];
	int visit_old[16];
	int njobs = 0;

	auto& sim(int i = 0) { return general_buffer.get<Simulation>(sim_offsets[i]); }
	auto& perc(int i = 0) { return step_buffer.get<Percept>(perc_offsets[i]); }
	auto& data() {
		return general_buffer.get<Simulation_data>(data_offset);
	}

	u32 olddist = 0;
	Graph_position oldsp;
	Pos oldp;
	Pos target = { 0, 0 };
};

struct Mothership_dummy : Mothership {
	void init(Graph* graph) override;
	void on_sim_start(u8 agent, Simulation const& simulation, int sim_size) override;
	void pre_request_action() override;
	void pre_request_action(u8 agent, Percept const& perc, int perc_size) override;
	void on_request_action() override;
	void post_request_action(u8 agent, Buffer* into) override;
};

struct Mothership_test2 : Mothership {
	void init(Graph* graph) override;
	void on_sim_start(u8 agent, Simulation const& simulation, int sim_size) override;
	void pre_request_action() override;
	void pre_request_action(u8 agent, Percept const& perc, int perc_size) override;
	void on_request_action() override;
	void post_request_action(u8 agent, Buffer* into) override;

    auto& world() { return world_buffer.get<World>(0); }
    auto& sit() { return sit_buffer.get<Situation>(0); }
    auto& sit_old() { return sit_old_buffer.get<Situation>(0); }
    
    Crafting_plan crafting_plan;
    Buffer world_buffer;
    Buffer sit_buffer;
    Buffer sit_old_buffer;
    Buffer sim_buffer;
    Simulation_state sim_state;
    Diff_flat_arrays sit_diff;
    Graph* graph;
};

} /* end of namespace jup */
