
#include "agent.hpp"

#include <cmath>

#include "buffer.hpp"

namespace jup {

Action const& dummy_agent (Agent const& agent) {
    static Buffer buffer;
    return buffer.emplace<Action_Skip>();
}

u8 find_item_source(u8 item, Simulation const& sim, Perception const& perc) {
    for (Storage const& i: perc.storages) {
        for (Storage_item j: i.items) {
            if (j.item == item and j.amount > j.delivered) {
                return i.name;
            }
        }
    }
    for (Shop const& i: perc.shops) {
        for (Shop_item j: i.items) {
            if (j.item == item) {
                return i.name;
            }
        }
    }
    for (Product const& i: sim.products) {
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
                    return find_item_source(j.item, sim, perc);
                }
            }
        }
    }
    
    return 0;
}
Job_priced const* find_job(u8 name, Perception const& perc) {
    for (Job_priced const& job: perc.priced_jobs) {
        if (job.id == name) return &job;        
    }
    return nullptr;
}

u8 get_random_loc() {
    int count = 0;
    count += world.charging_stations->size();
    count += world.dump_locations->size();
    count += world.shops->size();
    count += world.storages->size();
    count += world.workshops->size();

    int i = rand() % count;

    if (i < world.charging_stations->size()) {
        return (*world.charging_stations)[i].name;
    }
    i -= world.charging_stations->size();
    
    if (i < world.dump_locations->size()) {
        return (*world.dump_locations)[i].name;
    }
    i -= world.dump_locations->size();
    
    if (i < world.shops->size()) {
        return (*world.shops)[i].name;
    }
    i -= world.shops->size();
    
    if (i < world.storages->size()) {
        return (*world.storages)[i].name;
    }
    i -= world.storages->size();
    
    if (i < world.workshops->size()) {
        return (*world.workshops)[i].name;
    }

    return 0;
}


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
		Move{ assemble }, workshop{ w }, item{ i } {}
};

struct Move_Assist_Assemble : Move {
	u16 assemble;		// offset to Move_Assemble object
	Move_Assist_Assemble(u8 a) :
		Move{ assist_assemble }, assemble{ a } {}
};

struct Requirement {
    enum Type : u8 {
        GET_ITEM, BUY_ITEM, CRAFT_ITEM, DONE
    };
    
    u8 type;
    // The index of the Requirement this depends on, or 0xff
    u8 dependency;
    Item_stack item;
    u8 where;
};

struct Job_execution {
    u8 job;
    Flat_array<Requirement> needed;
};

void get_execution_plan(World const& world, Job const& job, Buffer* into) {
    assert(into);

    int exe_offset = into->size();
    into->emplace_back<Job_execution>();
    auto exe = [into, exe_offset]() -> Job_execution& {
        return into->get<Job_execution>(exe_offset);
    };

    std::function<void(Item_stack, u8)> add_req;
    add_req = [&world, exe, add_req, into](Item_stack item, u8 depend) {
        for (Storage const& storage: *world.storages) {
            for (Storage_item i: storage.items) {
                if (i.item == item.item and i.amount > i.delivered) {
                    if (i.amount - i.delivered >= item.amount) {
                        exe().needed.push_back(Requirement {
                                Requirement::GET_ITEM, depend, item, storage.name
                        }, into);
                        return;
                    } else {
                        exe().needed.push_back(Requirement {
                                Requirement::GET_ITEM, depend, {i.item, i.amount}, storage.name
                        }, into);
                        item.amount -= i.amount;
                    }
                }
            }
        }
        
        for (Shop const& shop: *world.shops) {
            for (Shop_item i: shop.items) {
                if (i.item == item.item) {
                    if (i.amount >= item.amount) {
                        exe().needed.push_back(Requirement {
                                Requirement::BUY_ITEM, depend, item, i.id
                        });
                        return;
                    } else {
                        exe().needed.push_back(Requirement {
                                Requirement::BUY_ITEM, depend, {i.item, i.amount}, shop.name
                        }, into);
                        item.amount -= i.amount;
                    }
                }
            }
        }

        // TODO: Respect restock
        // TODO: Try to use items in inventories of agents

        for (Product const& i: *world.products) {
            if (i.name == item.item and i.assembled) {
                item.amount = (item.amount - 1) / i.volume + 1;
                exe().needed.push_back(Requirement {Requirement::CRAFT_ITEM, depend, item, i.name}, into);
                for (Item_stack j: i.consumed) {
                    add_req(j, exe().needed.size() - 1);
                }
                return;
            }
        }
    };
    
    exe().needed.init(into);
    for (Job_item i: job.items) {
        add_req({i.item, i.amount - i.delivered}, 0xff);
    }
}

Action const& tree_agent(Agent const& agent) {



	return dummy_agent(agent);
}

Action const& random_agent (Agent const& agent) {
    static Buffer buffer;
    static u8 target[8];

	for (Charging_station const& i : *world.charging_stations) {
		if (i.name == agent.in_facility and agent.charge < agent.role.max_battery) {
			return buffer.emplace<Action_Charge>();
		}
	}

	if (agent.charge * agent.role.speed < 700) {
		int min_diff = 0x7fffffff;
		for (Charging_station const& i : *world.charging_stations) {
			auto const& p = agent.pos;
			auto const& q = i.pos;
			int diff = (p.lat - q.lat)*(p.lat - q.lat) + (p.lon - q.lon)*(p.lon - q.lon);
			diff = diff * 16 / (rand() % 16 + 8);
			if (diff < min_diff) {
				min_diff = diff;
				target[agent.action_id] = i.name;
				return buffer.emplace<Action_Goto1>(0, target[agent.action_id]);
			}
		}
	}

	if (agent.last_action_result == Action::SUCCESSFUL and agent.in_facility != target[agent.action_id]) {
		return buffer.emplace<Action_Continue>();
	}

	target[agent.action_id] = get_random_loc();
	buffer.emplace<Action_Goto1>(0, target[agent.action_id]);
    /*
    Pos cur_goal;
    cur_goal.lat = rand() % 128 + 64;
    cur_goal.lon = rand() % 128 + 64;
    return buffer.emplace_back<Action_Goto2>(cur_goal);
    
    */

/*
    static u8 cur_job;
    static u8 cur_goal;
    static u8 cur_target;
    static u16 random = rand();

    buffer.reset();

    if (!cur_job and perc.priced_jobs.size()) {
        cur_job = perc.priced_jobs[random % perc.priced_jobs.size()].id;
    }
    if (cur_job) {
        Job_priced const* job = find_job(cur_job, perc);
        if (job) {
            int start = random % job->items.size();
            for (int ind = 0; ind < job->items.size(); ++ind) {
                int i = (ind + start) % job->items.size();
                Job_item item = job->items[i];
                if (item.delivered < item.amount) {
                    u8 source = find_item_source(item.item, sim, perc);
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
*/
}

} /* end of namespace jup */

