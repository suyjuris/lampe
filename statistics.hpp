#pragma once

#include "agent.hpp"
#include "objects.hpp"

namespace jup {

struct Game_statistic {
	u16 seed_capital;
	u16 steps;
	Flat_array<Product> products;
	Flat_list<Role> roles;
	Flat_array<Entity> agents;
	Flat_array<Charging_station> charging_stations;
	Flat_array<Dump_location> dump_locations;
	Flat_array<Shop> shops;
	Flat_array<Storage> storages; // items will stay uninitialized
	Flat_array<Workshop> workshops;
	Flat_list<Job_auction> auction_jobs;
	Flat_list<Job_priced> priced_jobs;
};

struct Mothership_statistics : Mothership {

	void on_sim_start(u8 agent, Simulation const& simulation, int sim_size) override;
	void pre_request_action() override;
	void pre_request_action(u8 agent, Perception const& perc, int perc_size) override;
	void on_request_action() override;
	void post_request_action(u8 agent, Buffer* into) override;


	Buffer general_buffer;
	Buffer step_buffer;
	Buffer stat_buffer;
	int sim_offsets[agents_per_team];
	int perc_offsets[agents_per_team];
	int agent_count = 0;
	int visit[16];
	int visit_old[16];
	int njobs = 0;

	auto& sim(int i = 0) { return general_buffer.get<Simulation>(sim_offsets[i]); }
	auto& perc(int i = 0) { return step_buffer.get<Perception>(perc_offsets[i]); }
	auto& stat() { return stat_buffer.get<Game_statistic>(0); }
	
	Buffer get_statistic() {
		return stat_buffer;
	}

};

void print_diagram(u16 const* values, u16 size, u16 width = 50, u16 height = 20,
	double blur = 0.05, FILE* out = stdout);
void print_diagram_log(u16 const* values, u16 size, u16 width = 50, u16 height = 20,
	double blur = 0.05, FILE* out = stdout);
void statistics_main();

} /* end of namespace jup */

