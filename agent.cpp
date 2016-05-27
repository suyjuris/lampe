
#include "agent.hpp"

#include <cmath>
#include <vector>
                 
#include "buffer.hpp"

namespace jup {


bool get_execution_plan(Simulation const& sim, Perception const& perc, Job const& job, Buffer* into) {
    assert(into);

    int exe_offset = into->size();
    into->emplace_back<Job_execution>();
    auto exe = [into, exe_offset]() -> Job_execution& {
        return into->get<Job_execution>(exe_offset);
    };

    std::function<bool(Item_stack, u8, bool)> add_req;
    add_req = [&perc, &sim, exe, &add_req, into](Item_stack item, u8 depend, bool is_tool) {
        for (Storage const& storage: perc.storages) {
            for (Storage_item i: storage.items) {
                if (i.item == item.item and i.amount > i.delivered) {
                    if (i.amount - i.delivered >= item.amount) {
                        exe().needed.push_back(Requirement {
                                Requirement::GET_ITEM, depend, item, storage.name, is_tool
                        }, into);
                        return true;
                    } else {
                        exe().needed.push_back(Requirement {
                                Requirement::GET_ITEM, depend, {i.item, i.amount}, storage.name, is_tool
                        }, into);
                        item.amount -= i.amount;
                    }
                }
            }
        }
        
        for (Shop const& shop: perc.shops) {
            for (Shop_item i: shop.items) {
                if (i.item == item.item) {
                    exe().needed.push_back(Requirement {
                            Requirement::BUY_ITEM, depend, item, shop.name, is_tool
                    }, into);
                    return true;
                }
            }
        }

        // TODO: Respect restock
        // TODO: Try to use items in inventories of agents

        for (Product const& i: sim.products) {
            if (i.name == item.item and i.assembled) {
                item.amount = (item.amount - 1) / i.volume + 1;
                u8 dep = exe().needed.size();
                exe().needed.push_back(Requirement {Requirement::CRAFT_ITEM, depend, item, i.name, is_tool}, into);
                for (u8 j: i.tools) {
                    exe().needed.push_back(Requirement {Requirement::CRAFT_ASSIST, dep, {j, 1}, 0}, into);
                }
                for (Item_stack j: i.consumed) {
                    if (not add_req(j, dep, false)) return false;
                }
                for (u8 j: i.tools) {
                    if (not add_req({j, 1}, 0xff, true)) return false;
                }
                return true;
            }
        }

        return false;
    };

    exe().job = job.id;
    exe().needed.init(into);
    for (Job_item i: job.items) {
        if (not add_req({i.item, (u8)(i.amount - i.delivered)}, 0xff, false))
            return false;
    }
    return true;
}

void Mothership_simple::on_sim_start(u8 agent, Simulation const& simulation, int sim_size) {
    if (agent >= 4) return;
    sim_offsets[agent] = general_buffer.size();
    general_buffer.append(&simulation, sim_size);
    agent_task[agent] = { Requirement::NOTHING };
    agent_cs[agent] = 0;
    agent_last_go[agent] = 0;
    ++agent_count;
}

void Mothership_simple::pre_request_action() {
    step_buffer.reset();
}

void Mothership_simple::pre_request_action(u8 agent, Perception const& perc, int perc_size) {
    if (agent >= 4) return;
    perc_offsets[agent] = step_buffer.size();
    step_buffer.append(&perc, perc_size);
}

void Mothership_simple::on_request_action() {
    // TODO: Fix allocation
    general_buffer.reserve_space(256);
    
    if (jobexe_offset == 0) {
        if (perc().priced_jobs.size() != 0) {
            jobexe_offset = general_buffer.size();
            if (not get_execution_plan(sim(), perc(), perc().priced_jobs[0], &general_buffer)) {
                general_buffer.resize(jobexe_offset);
                jobexe_offset = 0;
                return;
            }
        } else {
            return;
        }
    }

    //auto& waiting = step_buffer.emplace_back<Flat_array<u8>>(&step_buffer);

    //int count_idle = 0;
    //for (auto i: agent_task) {
    //    if (i.type == Requirement::NOTHING) ++count_idle;
    //}

    int dep = 0;
    for (int i = job().needed.size() - 1; i >= dep; --i) {
        //if (not count_idle) return;
        //if (waiting.count(i)) continue;
        Requirement& req = job().needed[i];
        //waiting.push_back(req.dependency, &step_buffer);

        if (req.type == Requirement::NOTHING) {
            continue;
        }
        if (req.dependency != 0xff and req.dependency > dep) {
            dep = req.dependency;
        }

        if (req.type == Requirement::GET_ITEM or req.type == Requirement::BUY_ITEM) {
            bool assigned = false;
            if (req.is_tool) {
                for (int j = 0; j < agent_count; ++j) {
                    for (auto item: perc(j).self.items) {
                        if (item.item == req.item.item) {
                            assigned = true;
                            break;
                        }
                    }
                    if (assigned) break;
                }
            }

            if (not assigned) {
                for (int j = 0; j < agent_count; ++j) {
                    if (agent_task[j].type == Requirement::NOTHING) {
                        if (req.is_tool and (not sim(j).role.tools.count(req.item.item)
                            or sim(j).role.speed == 1)) continue;
                    
                        Product const* p = nullptr;
                        for (auto const& j: sim().products) {
                            if (j.name == req.item.item) {
                                p = &j; break;
                            }
                        }
                        assert(p);
                        int max_count = (sim(j).role.max_load - perc(j).self.load) / p->volume;
                        if (max_count == 0) {
                            continue;
                        } else if (max_count >= req.item.amount) {
                            agent_task[j] = req;
                            jout << (int)req.type << ' ' << (int)req.dependency << ' ' << (int)i << ' ' << (int)j << ' ' << (int)req.item.item << ' ' << (int)req.item.amount << '\n';
                            req.type = Requirement::NOTHING;
                            break;
                        } else {
                            agent_task[j] = req;
                            jout << (int)req.type << ' ' << (int)req.dependency << ' ' << (int)i << ' ' << (int)j << ' ' << (int)req.item.item << ' ' << (int)req.item.amount << '\n';
                            agent_task[j].item.amount = max_count;
                            req.item.amount -= max_count;
                        }
                    }
                }
            }
        } else if (req.type == Requirement::CRAFT_ITEM) {
            bool assigned = false;

            if (req.is_tool) {
                for (int j = 0; j < agent_count; ++j) {
                    for (auto item: perc(j).self.items) {
                        if (item.item == req.item.item) {
                            assigned = true;
                            break;
                        }
                    }
                    if (assigned) break;
                }
            }
 
             
            if (not assigned) {
                for (int j = 0; j < agent_count; ++j) {
                    if (agent_task[j].dependency == i and agent_task[j].state >= 2) {
                        if (req.is_tool and (not sim(j).role.tools.count(req.item.item) or sim(j).role.speed == 1)) continue;
                        agent_task[j] = req;
                        jout << (int)req.type << ' ' << (int)req.dependency << ' ' << (int)i << ' ' << (int)j << ' ' << (int)req.item.item << ' ' << (int)req.item.amount << '\n';
                        req.type = Requirement::NOTHING;
                        assigned = true;
                        break;
                    }
                }
            }
            if (not assigned) {
                for (int j = 0; j < agent_count; ++j) {
                    if (agent_task[j].type == Requirement::NOTHING) {
                        if (req.is_tool and (not sim(j).role.tools.count(req.item.item) or sim(j).role.speed == 1)) continue;
                        agent_task[j] = req;
                        jout << (int)req.type << ' ' << (int)req.dependency << ' ' << (int)i << ' ' << (int)j << ' ' << (int)req.item.item << ' ' << (int)req.item.amount << '\n';
                        req.type = Requirement::NOTHING;
                        assigned = true;
                        break;
                    }
                }
            }
        } else if (req.type == Requirement::CRAFT_ASSIST) {
            bool assigned = false;
            for (int j = 0; j < agent_count; ++j) {
                if (agent_task[j].dependency != req.dependency) continue;
                for (auto item: perc(j).self.items) {
                    if (item.item == req.item.item) {
                        req.type = Requirement::NOTHING;
                        assigned = true;
                        break;
                    }
                }
            }
            if (not assigned) {
                for (int j = 0; j < agent_count; ++j) {
                    if (agent_task[j].type != Requirement::NOTHING) continue;
                    for (auto item: perc(j).self.items) {
                        if (item.item == req.item.item) {
                            agent_task[j] = req;
                        jout << (int)req.type << ' ' << (int)req.dependency << ' ' << (int)i << ' ' << (int)j << ' ' << (int)req.item.item << ' ' << (int)req.item.amount << '\n';
                            req.type = Requirement::NOTHING;
                            assigned = true;
                            break;
                        }
                    }
                }
            }
        } else {
            assert(false);
        }

        //--count_idle;
    }
}

bool Mothership_simple::agent_goto(u8 where, u8 agent, Buffer* into) {
    auto const& s = perc(agent).self;
    
	for (Charging_station const& i : perc().charging_stations) {
		if (i.name == s.in_facility and s.charge < sim(agent).role.max_battery) {
            agent_cs[agent] = 0;
            into->emplace_back<Action_Charge>();
            return false;
		}
	}

	if (s.charge * sim(agent).role.speed < 370) {
        if (agent_cs[agent] == 0) {
            int min_diff = 0x7fffffff;
            for (Charging_station const& i : perc().charging_stations) {
                auto const& p = s.pos;
                auto const& q = i.pos;
                int diff = (p.lat - q.lat)*(p.lat - q.lat) + (p.lon - q.lon)*(p.lon - q.lon);
                if (diff < min_diff) {
                    min_diff = diff;
                    agent_cs[agent] = i.name;
                }
            }
        }
        if (s.last_action_result == Action::SUCCESSFUL and s.in_facility != agent_cs[agent]
            and agent_last_go[agent] == agent_cs[agent]) {
            into->emplace_back<Action_Continue>();
            return false;
        } else {
            into->emplace_back<Action_Goto1>(agent_cs[agent]);
            agent_last_go[agent] = agent_cs[agent];
            return false;
        }
	}

    if (s.in_facility == where) {
        return true;
    } else if (agent_last_go[agent] == where
        and s.last_action_result == Action::SUCCESSFUL) {
        into->emplace_back<Action_Continue>();
        return false;
	} else {
        agent_last_go[agent] = where;
        into->emplace_back<Action_Goto1>(where);
        return false;
    }
}

void Mothership_simple::post_request_action(u8 agent, Buffer* into) {
    if (agent >= 4) {
        into->emplace_back<Action_Skip>();
        return;
    }
    
    auto& req = agent_task[agent];

    auto get_target = [&req, this]() {
        if (req.dependency == 0xff) {
            for (auto const& j: perc().priced_jobs) {
                if (j.id == job().job) {
                    return j.storage;
                }
            }
            assert(false);
            return (u8)0;
        } else {
            return perc().workshops[0].name;
        }
    };
    auto deliver = [&req, agent, this, into]() {
        auto const& s = perc(agent).self;
        if (req.dependency == 0xff) {
            if (s.last_action == Action::DELIVER_JOB and !s.last_action_result) {
                return true;
            } else {
                into->emplace_back<Action_Deliver_job>(job().job);
                return false;
            }
        } else {
            if (s.last_action == Action::ASSIST_ASSEMBLE and !s.last_action_result) {
                return true;
            } else {
                for (int i = 0; i < agent_count; ++i) {
                    if (i == agent) continue;
                    if (agent_task[i].type == Requirement::CRAFT_ITEM
                        and agent_task[i].where == job().needed[req.dependency].where) {
                        into->emplace_back<Action_Assist_assemble>(sim(i).id);
                        return false;
                    }
                }
                into->emplace_back<Action_Skip>();
                return false;
            }
        }
    };
    
    auto const& s = perc(agent).self;
    if (req.type == Requirement::GET_ITEM) {
        assert(false); // Not implemented
    } else if (req.type == Requirement::BUY_ITEM) {
        if (req.state == 0) {
            if (agent_goto(req.where, agent, into)) {
                req.state = 1;
            } else {
                return;
            }
        }
        if (req.state == 1) {
            if (s.last_action == Action::BUY and not s.last_action_result) {
                if (req.is_tool) {
                    req.type = Requirement::NOTHING;
                } else {
                    req.state = 2;
                }
            } else {
                into->emplace_back<Action_Buy>(req.item);
                return;
            }
        }
        if (req.state == 2) {
            if (req.is_tool) {
                req.type = Requirement::NOTHING;
            } else if (agent_goto(get_target(), agent, into)) {
                req.state = 3;
            } else {
                return;
            }
        }
        if (req.state == 3) {
            if (deliver()) {
                req.type = Requirement::NOTHING;
            } else {
                return;
            }
        }
    } else if (req.type == Requirement::CRAFT_ITEM) {
        if (req.state == 0) {
            if (agent_goto(perc().workshops[0].name, agent, into)) {
                req.state = 1;
            } else {
                return;
            }
        }
        if (req.state == 1) {
            if (s.last_action == Action::ASSEMBLE and !s.last_action_result) {
                req.state = 2;
            } else {
                into->emplace_back<Action_Assemble>(req.item.item);
            }
        }
        if (req.state == 2) {
            if (req.is_tool) {
                req.type = Requirement::NOTHING;
            } else if (agent_goto(get_target(), agent, into)) {
                req.state = 3;
            } else {
                return;
            }
        }
        if (req.state == 3) {
            if (deliver()) {
                req.type = Requirement::NOTHING;
            } else {
                return;
            }
        }
    } else if (req.type == Requirement::CRAFT_ASSIST) {
        if (req.state == 0) {
            if (agent_goto(perc().workshops[0].name, agent, into)) {
                req.state = 1;
            } else {
                return;
            }
        }
        if (req.state == 1) {
            if (deliver()) {
                req.type = Requirement::NOTHING;
            } else {
                return;
            }
        }
    }
    
    into->emplace_back<Action_Skip>();
    return;
}

/*
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
*//*
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
}
*/

} /* end of namespace jup */

