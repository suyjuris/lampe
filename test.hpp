#pragma once

#include "server.hpp"
#include "agent.hpp"

namespace jup {

struct Mothership_test : Mothership {
	void init(Graph const* graph) override;
	void on_sim_start(u8 agent, Simulation const& simulation, int sim_size) override;
	void pre_request_action() override;
	void pre_request_action(u8 agent, Percept const& perc, int perc_size) override;
	void on_request_action() override;
	void post_request_action(u8 agent, Buffer* into) override;

	Graph const* graph = nullptr;
	Buffer general_buffer;
	Buffer step_buffer;
	int sim_offsets[agents_per_team];
	int perc_offsets[agents_per_team];
	int agent_count = 0;
	int visit[16];
	int visit_old[16];
	int njobs = 0;

	auto& sim(int i = 0) { return general_buffer.get<Simulation>(sim_offsets[i]); }
	auto& perc(int i = 0) { return step_buffer.get<Percept>(perc_offsets[i]); }

	u32 olddist = 0;
	Graph_position oldsp;
	Pos oldp;
	Pos target = { 0, 0 };
};

} /* end of namespace jup */
