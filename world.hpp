#pragma once

#include "objects.hpp"

namespace jup {

struct Agent : Self {
	u16 action_id;
	Role role;
};

struct World {
	u64 deadline;
	u16 seed_capital;
	u16 max_steps;
	u16 simulation_step;
	u8 simulation_id;
	u8 team_id;
	Team* team;
	Flat_array<Agent>* agents;
	Flat_array<Entity>* opponents;
	Flat_array<Charging_station>* charging_stations;
	Flat_array<Dump_location>* dump_locations;
	Flat_array<Shop>* shops;
	Flat_array<Storage>* storages;
	Flat_array<Workshop>* workshops;
	Flat_array<Job_auction>* auction_jobs;
	Flat_array<Job_priced>* priced_jobs;
	Flat_array<Product>* products;
};

extern World world;

}
