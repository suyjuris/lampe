
#include "agent.hpp"

#include <cmath>

#include "buffer.hpp"

namespace jup {

Action const& dummy_agent (u8 id, Simulation const& sim, Perception const& perc) {
    static Buffer buffer;
    return buffer.emplace<Action_Skip>();
}

u8 find_item_source(u8 item, Perception const& perc) {
    for (Storage const& i: world->storages) {
        for (Storage_item j: i.items) {
            if (j.item == item and j.amount > j.delivered) {
                return i.name;
            }
        }
    }
    for (Shop const& i: world->shops) {
        for (Shop_item j: i.items) {
            if (j.item == item) {
                return i.name;
            }
        }
    }
    for (Product const& i: *products) {
        if (i.name == item and i.assembled) {
            for (u8 j: i.tools) {
                bool flag = false;
                for (Item_stack k: perc.self.items) {
                    if (k.item == j) {
                        flag = true;
                        break;
                    }
                }
                if (!flag) return 0;
            }
            for (Item_stack j: i.consumed) {
                bool flag = false;
                for (Item_stack k: perc.self.items) {
                    if (k.item == j.item and k.amount >= j.amount) {
                        flag = true;
                        break;
                    }
                }
                if (!flag) {
                    return find_item_source(j.item, perc);
                }
            }
        }
    }
    
    return 0;
}

Facility const* findFacility(u8 id) {
	for (Shop const& s : world->shops) {
		if (s.name == id) {
			return &s;
		}
	}
	for (Storage const& s : world->storages) {
		if (s.name == id) {
			return &s;
		}
	}
	return nullptr;
}

Job_priced const* find_job(u8 name) {
    for (Job_priced const& job: world->priced_jobs) {
        if (job.id == name) return &job;        
    }
    return nullptr;
}

u8 get_random_loc() {
    int count = 0;
    count += world->charging_stations.size();
    count += world->dump_locations.size();
    count += world->shops.size();
    count += world->storages.size();
    count += world->workshops.size();

    int i = rand() % count;

    if (i < world->charging_stations.size()) {
        return world->charging_stations[i].name;
    }
    i -= world->charging_stations.size();
    
    if (i < world->dump_locations.size()) {
        return world->dump_locations[i].name;
    }
    i -= world->dump_locations.size();
    
    if (i < world->shops.size()) {
        return world->shops[i].name;
    }
    i -= world->shops.size();
    
    if (i < world->storages.size()) {
        return world->storages[i].name;
    }
    i -= world->storages.size();
    
    if (i < world->workshops.size()) {
        return world->workshops[i].name;
    }

    return 0;
}

struct Job_processing : Job {

	Flat_array<Self const*> agents;
	Flat_array<Item_stack> base_items_needed;
	Flat_array<Item_stack> final_items_needed;

};




struct Move {
	enum Move_Type : u8 {
		undefined,
		charge,
		buy,
		assemble,
		assist_assemble,
		deliver
	};

	Move_Type type;

};

struct Move_Charge : Move {
	u8 station;
	Move_Charge(u8 s) :
		Move{ charge }, station{ s } {}
};

struct Move_Buy : Move {
	u8 shop;
	Item_stack item;
	Move_Buy(u8 s, u8 i) :
		Move{ buy }, shop{ s }, item{ i } {}
};

struct Move_Assemble : Move {
	u8 workshop;
	u8 item;
	Move_Assemble(u8 w, u8 i) :
		Move{ assemble }, workshop { w }, item{ i } {}
};

struct Move_Assist_Assemble : Move {
	u16 assemble;		// offset to Move_Assemble object
	Move_Assist_Assemble(u8 a) :
		Move{ assist_assemble }, assemble{ a } {}
};

struct Job_requirement {
	Flat_array<Item_stack> base_items;
	Flat_array<u8> tools;
};

Action const& tree_agent(u8 id, Simulation const& sim, Perception const& perc) {
	if (sim.team == 'B') {
		return dummy_agent(0, sim, perc);
	}

	return dummy_agent(0, sim, perc);
}

Action const& random_agent (u8 id, Simulation const& sim, Perception const& perc) {
    static Buffer buffer;

    /*static u8 target[8];

    for (Charging_station const& i: perc.charging_stations) {
        if (i.name == perc.self.in_facility and perc.self.charge < sim.role.max_battery) {
            return buffer.emplace<Action_Charge>();
        }
    }
    
    if (perc.self.charge * sim.role.speed < 700) {
        int min_diff = 0x7fffffff;
        for (Charging_station const& i: perc.charging_stations) {
            auto const& p = perc.self.pos;
            auto const& q = i.pos;
            int diff = (p.lat-q.lat)*(p.lat-q.lat) + (p.lon-q.lon)*(p.lon-q.lon);
            diff = diff * 16 / (rand() % 16 + 8);
            if (diff < min_diff) {
                min_diff = diff;
                target[id] = i.name;
                return buffer.emplace<Action_Goto1>(0, target[id]);
            }
        }
    }
    
    if (perc.self.last_action_result == Action::SUCCESSFUL and perc.self.in_facility != target[id]) {
        return buffer.emplace<Action_Continue>();
    }

    target[id] = get_random_loc(perc);
    return buffer.emplace<Action_Goto1>(0, target[id]);*/
    

    /*
    Pos cur_goal;
    cur_goal.lat = rand() % 128 + 64;
    cur_goal.lon = rand() % 128 + 64;
    return buffer.emplace_back<Action_Goto2>(cur_goal);
    
    */


    static u8 cur_job;
    static u8 cur_goal;
    static u8 cur_target;
    static u16 random = rand();

    buffer.reset();

    if (!cur_job and world->priced_jobs.size()) {
        cur_job = world->priced_jobs[random % world->priced_jobs.size()].id;
		jout << "New job!" << endl;
    }
    if (cur_job) {
        Job_priced const* job = find_job(cur_job);
        if (job) {
            int start = random % job->items.size();
            for (int ind = 0; ind < job->items.size(); ++ind) {
                int i = (ind + start) % job->items.size();
                Job_item item = job->items[i];
                if (item.delivered < item.amount) {
                    u8 source = find_item_source(item.item, perc);
                    if (source) {
                        cur_target = source;
                        cur_goal = item.item;
                        jout << "We're going!\n";
                        return buffer.emplace_back<Action_Goto1>(source);
                    }
                }
            }
        }
    }

    buffer.reserve(cur_goal & cur_target);
    
    return buffer.emplace_back<Action_Skip>();
}

} /* end of namespace jup */

