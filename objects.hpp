#pragma once

#include "global.hpp"

#include "flat_data.hpp"

namespace jup {	

struct Item_stack {
	u8 item;
	u8 amount;
};

struct Pos {
	u8 lat;
	u8 lon;
	u8 estimateDistance(Pos p) {
		s16 dx = lat - p.lat, dy = lat - p.lat;
		return sqrt(dx*dx + dy*dy);
	}
};

struct Product {
	u8 name;
	bool assembled;
	u16 volume;
	Flat_array<Item_stack> consumed;
	Flat_array<u8> tools;

	static Product const* getByID(u8 id);
};

struct Role {
	u8 name;
	u8 speed;
	u16 max_battery;
	u16 max_load;
	Flat_array<u8> tools;
};

struct Action {
	// THESE MUST BE IN THE SAME ORDER!!!
	enum Action_type: u8 {
		GOTO, BUY, GIVE, RECIEVE, STORE, RETRIEVE, RETRIEVE_DELIVERED, DUMP,
		ASSEMBLE, ASSIST_ASSEMBLE, DELIVER_JOB, CHARGE, BID_FOR_JOB, POST_JOB,
		CALL_BREAKDOWN_SERVICE, CONTINUE, SKIP, ABORT, GOTO1, GOTO2, POST_JOB1,
		POST_JOB2
	};
	static constexpr char const* action_names[] = {
		"goto", "buy", "give", "recieve", "store", "retrieve",
		"retrieve_delivered", "dump", "assemble", "assist_assemble",
		"deliver_job", "charge", "bid_for_job", "post_job",
		"call_breakdown_service", "continue", "skip", "abort"
    };
	enum Action_result: u8 {
		SUCCESSFUL = 0, /* guarantee this, so that users may assert */
		FAILED_LOCATION, FAILED_UNKNOWN_ITEM, FAILED_UNKNOWN_AGENT,
	    FAILED_UNKNOWN_JOB, FAILED_UNKNOWN_FACILITY, FAILED_NO_ROUTE,
	    FAILED_ITEM_AMOUNT, FAILED_CAPACITY, FAILED_WRONG_FACILITY,
	    FAILED_TOOLS, FAILED_ITEM_TYPE, FAILED_JOB_STATUS, FAILED_JOB_TYPE,
		FAILED_COUNTERPART, FAILED_WRONG_PARAM, FAILED_UNKNOWN_ERROR,
		SUCCESSFUL_PARTIAL, USELESS, FAILED_RANDOM
	};
	static constexpr char const* action_result_names[] = {
		"successful", "failed_location", "failed_unknown_item",
	    "failed_unknown_agent", "failed_unknown_job", "failed_unknown_facility",
	    "failed_no_route", "failed_item_amount", "failed_capacity",
	    "failed_wrong_facility", "failed_tools", "failed_item_type",
	    "failed_job_status", "failed_job_type", "failed_counterpart",
	    "failed_wrong_param", "failed_unknown_error", "successful_partial",
	    "useless", "failed_random"
	};
	
	static u8 get_id(char const* str) {
		constexpr int count = sizeof(action_names) / sizeof(action_names[0]);
		static_assert(count < 256, "Too many elements in action_names");
		for (u8 i = 0; i < count; ++i) {
			if (std::strcmp(str, action_names[i])) return i;
		}
		assert(false);
		return -1;
	}
	static char const* get_name(int id) {
		constexpr int count = sizeof(action_names) / sizeof(action_names[0]);
        switch (id) {
        case GOTO1:
        case GOTO2: id = GOTO; break;
        case POST_JOB1:
        case POST_JOB2: id = POST_JOB; break;
        default: break;
        };
		assert(0 <= id and id < count);
		return action_names[id];
	}
		
	static u8 get_result_id(char const* str) {
		constexpr int count = sizeof(action_result_names) / sizeof(action_result_names[0]);
		static_assert(count < 256, "Too many elements in action_result_names");
		for (u8 i = 0; i < count; ++i) {
			if (std::strcmp(str, action_result_names[i]) == 0) return i;
		}
		assert(false);
		return -1;
	}
	static char const* get_result_name(int id) {
		constexpr int count = sizeof(action_result_names) / sizeof(action_result_names[0]);
		assert(0 <= id and id < count);
		return action_result_names[id];
	}

	Action(u8 type): type{type} {}

	u8 type;
};


struct Action_Goto1: Action {
	Action_Goto1(u8 facility):
		Action{GOTO1}, facility{facility} {}
	u8 facility;
};

struct Action_Goto2: Action {
	Action_Goto2(Pos pos):
		Action{GOTO2}, pos{pos} {}
	Pos pos;
};

struct Action_Buy: Action {
	Action_Buy(Item_stack item):
		Action{BUY}, item{item} {}
	Item_stack item;
};

struct Action_Give: Action {
	Action_Give(u8 agent, Item_stack item):
		Action{GIVE}, agent{agent}, item{item} {}
	u8 agent;
	Item_stack item;
};

struct Action_Recieve: Action {
	Action_Recieve(): Action{RECIEVE} {}
};

struct Action_Store: Action {
	Action_Store(Item_stack item):
		Action{STORE}, item{item} {}
	Item_stack item;
};

struct Action_Retrieve: Action {
	Action_Retrieve(Item_stack item):
		Action{RETRIEVE}, item{item} {}
	Item_stack item;
};

struct Action_Retrieve_delivered: Action {
	Action_Retrieve_delivered(Item_stack item):
		Action{RETRIEVE_DELIVERED}, item{item} {}
	Item_stack item;
};

struct Action_Dump: Action {
	Action_Dump(Item_stack item):
		Action{DUMP}, item{item} {}
	Item_stack item;
};

struct Action_Assemble: Action {
	Action_Assemble(u8 item):
		Action{ASSEMBLE}, item{item} {}
	u8 item;
};

struct Action_Assist_assemble: Action {
	Action_Assist_assemble(u8 assembler):
		Action{ASSIST_ASSEMBLE}, assembler{assembler} {}
	u8 assembler;
};

struct Action_Deliver_job: Action {
	Action_Deliver_job(u8 job):
		Action{DELIVER_JOB}, job{job} {}
	u8 job;
};

struct Action_Charge: Action {
	Action_Charge(): Action{CHARGE} {}
};

struct Action_Bid_for_job: Action {
	Action_Bid_for_job(u8 job, u16 price):
		Action{BID_FOR_JOB}, job{job}, price{price} {}
	u8 job;
	u16 price;
};

struct Action_Post_job1: Action {
	Action_Post_job1(u16 max_price, u16 fine, u16 active_steps,
					 u16 auction_steps, u8 storage):
		Action{POST_JOB1}, max_price{max_price}, fine{fine},
		active_steps{active_steps}, auction_steps{auction_steps},
		storage{storage} {}
	u16 max_price, fine, active_steps, auction_steps;
	u8 storage;
	Flat_array<Item_stack> items;
};

struct Action_Post_job2: Action {
	Action_Post_job2(u16 price, u16 active_steps, u8 storage):
		Action{POST_JOB2}, price{price}, active_steps{active_steps},
		storage{storage} {}
	u16 price, active_steps;
	u8 storage;
	Flat_array<Item_stack> items;
};

struct Action_Call_breakdown_service: Action {
	Action_Call_breakdown_service(): Action{CALL_BREAKDOWN_SERVICE} {}
};

struct Action_Continue: Action {
	Action_Continue(): Action{CONTINUE} {}
};

struct Action_Skip: Action {
	Action_Skip(): Action{SKIP} {}
};

struct Action_Abort: Action {
	Action_Abort(): Action{ABORT} {}
};

struct Simulation {
	u8 id;
	u8 team;
	u16 seed_capital;
	u16 steps;
	Role role;
	Flat_array<Product> products;
};

struct Self {
	u16 charge;
	u16 load;
	u8 last_action;
	u8 last_action_result;
	Pos pos;
	u8 in_facility; /* 0 if not in a facility */
	u8 f_position;
	u8 route_length;
	Flat_array<Item_stack> items;
	Flat_array<Pos> route;
};

struct Team {
	u16 money;
	Flat_array<u8> jobs_taken;
	Flat_array<u8> jobs_posted;
};

struct Entity {
	u8 name;
	u8 team;
	Pos pos;
	u8 role;
};

struct Facility {
	u8 name;
	Pos pos;
};

struct Charging_station : Facility {
	u8 rate;
	u16 price;
	u8 slots;
	u8 q_size; // -1 if <info> not visible

	static Charging_station const* getByID(u8 id);
};

struct Dump_location : Facility {
	u16 price;

	static Dump_location const* getByID(u8 id);
};

struct Shop_item : Item_stack {
	u16 cost;
	u8 restock;
};

struct Shop : Facility {
	Flat_array<Shop_item> items;

	static Shop const* getByID(u8 id);
};

struct Storage_item : Item_stack {
	u8 delivered;
};

struct Storage : Facility {
	u8 price;
	u16 totalCapacity;
	u16 usedCapacity;
	Flat_array<Storage_item> items;

	static Storage const* getByID(u8 id);
};

struct Workshop : Facility {
	u16 price;

	static Workshop const* getByID(u8 id);
};

struct Job_item: Item_stack {
	u8 delivered;
};

struct Job {
	u8 id;
	u8 storage;
	u16 begin;
	u16 end;
	Flat_array<Job_item> items;
};

struct Job_auction : Job {
	u16 fine;
	u16 max_bid;
};

struct Job_priced : Job {
	u16 reward;
};

struct Perception {
	u64 deadline;
	u16 id;
	u16 simulation_step;
	Self self;
	Team team;
	Flat_array<Entity> entities;
	Flat_array<Charging_station> charging_stations;
	Flat_array<Dump_location> dump_locations;
	Flat_array<Shop> shops;
	Flat_array<Storage> storages;
	Flat_array<Workshop> workshops;
	Flat_array<Job_auction> auction_jobs;
	Flat_array<Job_priced> priced_jobs;
};

} /* end of namespace jup */
