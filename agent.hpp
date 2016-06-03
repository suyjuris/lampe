#pragma once

#include "global.hpp"

#include <functional>

#include "buffer.hpp"
#include "objects.hpp"
#include "server.hpp"

namespace jup {	

struct Requirement {
    enum Type : u8 {
        GET_ITEM, BUY_ITEM, CRAFT_ITEM, CRAFT_ASSIST, NOTHING
    };
    
    u8 type;
    // The index of the Requirement this depends on, or 0xff
    u8 dependency;
    Item_stack item;
    u8 where;
    bool is_tool;
    u8 state = 0;
};

struct Job_execution {
    u8 job;
    Flat_array<Requirement> needed;
};

struct Mothership_simple: Mothership {

	constexpr char const static unsigned agents_per_team = 4;

	struct Agent : Entity {
		u16 action_id;
		u16 charge;
		u16 load;
		u8 last_action;
		u8 last_action_result;
		u8 in_facility; /* 0 if not in a facility */
		u8 f_position;
		u8 route_length;
		Flat_array<Item_stack>* items;
		Flat_array<Pos>* route;
		Job_execution *currentJob;
	};

	struct World {
		u64 deadline;
		u16 seed_capital;
		u16 max_steps;
		u16 simulation_step;
		u8 simulation_id;
		u8 team;
		Agent agents[agents_per_team];
		Entity opponents[agents_per_team];
		Flat_array<Charging_station>* charging_stations;
		Flat_array<Dump_location>* dump_locations;
		Flat_array<Shop>* shops;
		Flat_array<Storage>* storages;
		Flat_array<Workshop>* workshops;
		Flat_array<Job_auction>* auction_jobs;
		Flat_array<Job_priced>* priced_jobs;
		Flat_array<Product>* products;

		template <typename T>
		T * getByID(u8 id) {
			return nullptr;
		}

	} world;

	struct Situation {

		u16 money;
		u16 simulation_step;

		struct Compact_agent {
			Pos pos;
			u16 charge;
			u16 load;
			u8 in_facility; /* 0 if not in a facility */
			Flat_array<Item_stack>* items;
			Job_execution* currentJob;
		} agents[agents_per_team];

		struct Compact_shop {
			struct Compact_shop_item {
				u8 amount;
				u8 restock;
			};

			Flat_array<Compact_shop_item> items;
		};

		Flat_array<Compact_shop> shops;

		u16 rate() {
			return money;
		}

	};

	void on_sim_start(u8 agent, Simulation const& simulation, int sim_size) override;
    void pre_request_action() override;
    void pre_request_action(u8 agent, Perception const& perc, int perc_size) override;
    void on_request_action() override;
    void post_request_action(u8 agent, Buffer* into) override;

    bool agent_goto(u8 where, u8 agent, Buffer* into);


    Buffer general_buffer;
    Buffer step_buffer;
    int sim_offsets[agents_per_team];
    int perc_offsets[agents_per_team];
    int jobexe_offset = 0;
    Requirement agent_task[agents_per_team];
    u8 agent_cs[agents_per_team];
    u8 agent_last_go[agents_per_team];
    int agent_count = 0;

    auto& job() { return general_buffer.get<Job_execution>(jobexe_offset); }
    auto& sim(int i = 0) { return general_buffer.get<Simulation>(sim_offsets[i]); }
    auto& perc(int i = 0) { return step_buffer.get<Perception>(perc_offsets[i]); }
};

template <>
Charging_station * Mothership_simple::World::getByID<Charging_station>(u8 id);

template <>
Shop * Mothership_simple::World::getByID<Shop>(u8 id);

} /* end of namespace jup */
