#pragma once

#include "buffer.hpp"
#include "flat_data.hpp"
#include "objects.hpp"
#include "statistics.hpp"

namespace jup {

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