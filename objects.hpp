#pragma once

#include "flat_data.hpp"

namespace jup {	

constexpr u8 agents_per_team = 11;

struct Item_stack {
    union {u8 item; u8 id;};
	u8 amount;

    bool operator== (Item_stack o) const {
        return id == o.id and amount == o.amount;
    }
};

struct Pos {
	u16 lat;
	u16 lon;

    float dist2(Pos p) const;
};

u16 operator-(Pos const p1, Pos const p2);

struct Item {
	union {u8 name; u8 id;};
	u16 volume;
	Flat_array<Item_stack> consumed;
	Flat_array<u8> tools;
};

struct Role {
	union {u8 name; u8 id;};
	u8 speed;
	u16 battery;
	u16 load;
	Flat_array<u8> tools;
};

struct Action {
	// THESE MUST BE IN THE SAME ORDER!!!
	enum Action_type: u8 {
		GOTO, GIVE, RECEIVE, STORE, RETRIEVE, RETRIEVE_DELIVERED,
		ASSEMBLE, ASSIST_ASSEMBLE, BUY, DELIVER_JOB, BID_FOR_JOB,
		POST_JOB, DUMP, CHARGE, RECHARGE, CONTINUE, SKIP, ABORT,
		UNKNOWN_ACTION, RANDOM_FAIL, NO_ACTION, GATHER, GOTO0, GOTO1, GOTO2
	};
	static constexpr char const* action_names[] = {
		"goto", "give", "receive", "store", "retrieve", "retrieve_delivered",
		"assemble", "assist_assemble", "buy", "deliver_job", "bid_for_job",
		"post_job", "dump", "charge", "recharge", "continue", "skip", "abort",
		"unknownAction", "randomFail", "noAction", "gather"
    };
	enum Action_result: u8 {
		SUCCESSFUL = 0, /* guarantee this, so that users may assert */
		FAILED_LOCATION, FAILED_UNKNOWN_ITEM, FAILED_UNKNOWN_AGENT,
	    FAILED_UNKNOWN_JOB, FAILED_UNKNOWN_FACILITY, FAILED_NO_ROUTE,
	    FAILED_ITEM_AMOUNT, FAILED_CAPACITY, FAILED_WRONG_FACILITY,
	    FAILED_TOOLS, FAILED_ITEM_TYPE, FAILED_JOB_STATUS, FAILED_JOB_TYPE,
		FAILED_COUNTERPART, FAILED_WRONG_PARAM, FAILED_FACILITY_STATE, FAILED,
        SUCCESSFUL_PARTIAL, USELESS
	};
	static constexpr char const* action_result_names[] = {
		"successful", "failed_location", "failed_unknown_item",
	    "failed_unknown_agent", "failed_unknown_job", "failed_unknown_facility",
	    "failed_no_route", "failed_item_amount", "failed_capacity",
	    "failed_wrong_facility", "failed_tools", "failed_item_type",
	    "failed_job_status", "failed_job_type", "failed_counterpart",
	    "failed_wrong_param", "failed_facility_state", "failed",
        "successful_partial", "useless"
	};
	
	static u8 get_id(char const* str) {
		constexpr int count = sizeof(action_names) / sizeof(action_names[0]);
		static_assert(count < 256, "Too many elements in action_names");
		for (u8 i = 0; i < count; ++i) {
			if (std::strcmp(str, action_names[i]) == 0) return i;
		}
		assert(false);
		return -1;
	}
	static char const* get_name(int id) {
		constexpr int count = sizeof(action_names) / sizeof(action_names[0]);
        switch (id) {
		case GOTO0:
		case GOTO1:
        case GOTO2: id = GOTO; break;
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
        if (*str == '\0') {
            return -1;
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


struct Action_Goto0: Action {
	Action_Goto0():
		Action{GOTO0} {}
};

struct Action_Goto1: Action {
	Action_Goto1(u8 fac):
		Action{GOTO1}, fac{fac} {}
	u8 fac;
};

struct Action_Goto2: Action {
	Action_Goto2(Pos pos) :
		Action{GOTO2}, pos{pos} {}
	Action_Goto2(u16 lat, u16 lon):
		Action{GOTO2}, pos{lat, lon} {}
	Pos pos;
};

struct Action_Give: Action {
	Action_Give(u8 agent, Item_stack item):
		Action{GIVE}, agent{agent}, item{item} {}
	u8 agent;
	Item_stack item;
};

struct Action_Receive: Action {
	Action_Receive(): Action{RECEIVE} {}
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

struct Action_Buy: Action {
	Action_Buy(Item_stack item):
		Action{BUY}, item{item} {}
	Item_stack item;
};

struct Action_Deliver_job: Action {
	Action_Deliver_job(u16 job):
		Action{DELIVER_JOB}, job{job} {}
	u16 job;
};

struct Action_Bid_for_job: Action {
	Action_Bid_for_job(u16 job, u32 bid):
		Action{BID_FOR_JOB}, job{job}, bid{bid} {}
	u16 job;
	u32 bid;
};

struct Action_Post_job: Action {
	Action_Post_job(u32 reward, u16 duration, u8 storage):
		Action{POST_JOB}, reward{reward}, duration{duration},
		storage{storage} {}
	u32 reward;
	u16 duration;
	u8 storage;
	Flat_array<Item_stack> items;
};

struct Action_Dump: Action {
	Action_Dump(Item_stack item):
		Action{DUMP}, item{item} {}
	Item_stack item;
};

struct Action_Charge: Action {
	Action_Charge(): Action{CHARGE} {}
};

struct Action_Recharge: Action {
	Action_Recharge(): Action{RECHARGE} {}
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

struct Action_Gather: Action {
	Action_Gather(): Action{GATHER} {}
};

struct Simulation {
	u8 id;
	u8 map;
	u8 team;
	u16 seed_capital;
	u16 steps;
	Role role;
	Flat_array<Item> items;
};

struct Entity {
	union {u8 name; u8 id;};
	u8 team;
	Pos pos;
	u8 role;
};

struct Self: Entity {
	u16 charge;
	u16 load;
	u8 action_type;
	u8 action_result;
    u8 facility;
	Flat_array<Item_stack> items;
    Flat_array<Pos> route;
};

struct Facility {
	union {u8 name; u8 id;};
	Pos pos;
};

struct Charging_station: Facility {
	u8 rate;
};

struct Dump: Facility {
};

struct Shop_item: Item_stack {
	u16 cost;
};

struct Shop: Facility {
	u8 restock;
	Flat_array<Shop_item> items;
};

struct Storage_item: Item_stack {
	u8 delivered;
};

struct Storage: Facility {
	u16 total_capacity;
	u16 used_capacity;
	Flat_array<Storage_item> items;
};

struct Workshop: Facility {
};

struct Job {
    enum Type: u8 {
        NONE, JOB, AUCTION, MISSION, POSTED
    };
    
	u16 id;
	u8 storage;
	u16 start;
	u16 end;
	u32 reward;
	Flat_array<Item_stack> required;
};

struct Auction: Job {
	u16 auction_time;
	u32 fine;
	u32 max_bid;
};

using Mission = Auction;

struct Posted: Job { };

struct Resource_node: Facility {
	u8 resource;
};

struct Percept {
	u64 deadline;
	u16 id;
	u16 simulation_step;
	s32 team_money;
	Self self;
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
};

} /* end of namespace jup */
