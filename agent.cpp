
#include "agent.hpp"

#include <cmath>
#include <vector>
#include <limits>
                 
#include "buffer.hpp"
#include "messages.hpp"

namespace jup {


bool Mothership_simple::get_execution_plan(Job const& job, Buffer* into) {
    assert(into);

    int exe_offset = into->size();
    into->emplace_back<Job_execution>();
    auto exe = [into, exe_offset]() -> Job_execution& {
        return into->get<Job_execution>(exe_offset);
    };

    std::function<bool(Item_stack, u8, bool)> add_req;
    add_req = [this, exe, &add_req, into](Item_stack item, u8 depend, bool is_tool) {
        for (Storage const& storage: perc().storages) {
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

        int index = find_cheap(item.item);
        if (index != -1) {
            exe().cost += cheaps[index].price * item.amount; 
            exe().needed.push_back(Requirement {
                    Requirement::BUY_ITEM, depend, item, cheaps[index].shop, is_tool
            }, into);
            return true;
        }
        
        for (Shop const& shop: perc().shops) {
            for (Shop_item i: shop.items) {
                if (i.item == item.item) {
                    exe().cost += 10000 * item.amount;
                    exe().needed.push_back(Requirement {
                            Requirement::BUY_ITEM, depend, item, shop.name, is_tool
                    }, into);
                    return true;
                }
            }
        }

        // TODO: Respect restock
        // TODO: Try to use items in inventories of agents

        for (Product const& i: sim().products) {
            if (i.name == item.item and i.assembled) {
                u8 dep = exe().needed.size();
                exe().cost += perc().workshops[0].price * item.amount;
                exe().needed.push_back(Requirement {Requirement::CRAFT_ITEM, depend, item, i.name, is_tool}, into);
                for (Item_stack j: i.tools) {
                    exe().needed.push_back(Requirement {Requirement::CRAFT_ASSIST, dep, j, 0}, into);
                }
                for (Item_stack j: i.consumed) {
                    if (not add_req({j.item, (u8)(j.amount * item.amount)}, dep, false)) return false;
                }
                for (Item_stack j: i.tools) {
                    if (not add_req(j, 0xff, true)) return false;
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
    exe().cost += exe().needed.size() * 800;
    return true;
}

void Mothership_simple::on_sim_start(u8 agent, Simulation const& simulation, int sim_size) {
    sim_offsets[agent] = general_buffer.size();
    general_buffer.append(&simulation, sim_size);
    agent_task[agent] = { Requirement::NOTHING };
    agent_cs[agent] = 0;
    agent_last_go[agent] = 0;
    ++agent_count;
}

static u8 agent_indices[] = {4, 5, 6, 7, 8, 9, 10, 11, 0, 1, 2, 3, 12, 13, 14, 15};
struct Agent_iter {
    u8 const* begin() {
        return agent_indices;
    }
    u8 const* end() {
        return agent_indices + sizeof(agent_indices) / sizeof(agent_indices[0]);
    }
};

void Mothership_simple::pre_request_action() {
    assert(agent_count == 16);
    step_buffer.reset();
}

void Mothership_simple::pre_request_action(u8 agent, Perception const& perc, int perc_size) {
    perc_offsets[agent] = step_buffer.size();
    step_buffer.append(&perc, perc_size);
}

void Mothership_simple::on_request_action() {
    // TODO: Fix allocation
    general_buffer.reserve_space(256);

    for (int i: Agent_iter{}) {
        for (Shop const& j: perc(i).shops) {
            for (Shop_item item: j.items) {
                if (item.amount == 0xff) continue;
                int index = find_cheap(item.item);
                if (index == -1) {
                    index = cheaps.size();
                    cheaps.push_back({item.cost, item.item, j.name});
                } else if (cheaps[index].price >= item.cost) {
                    cheaps[index] = {item.cost, item.item, j.name};
                }
            }
        }
    }
    
    bool no_job_flag = true;
    if (jobexe_offset != 0) {
        for (auto const& j: perc().priced_jobs) {
            if (j.id == job().job) {
                no_job_flag = false;
                break;
            }
        }
    }
    if (no_job_flag) {
        for (int i = 0; i < 16; ++i) {
            if (agent_task[i].type != Requirement::VISIT) {
                agent_task[i].type = Requirement::NOTHING;
                agent_cs[i] = 0;
                agent_last_go[i] = 0;
            }
        }
        s32 max_val = 0; int index = -1;
        for (int i = 0; i < perc().priced_jobs.size(); ++i) {

            Job_priced const& jjob = perc().priced_jobs[i];
            
            if (not (jjob.end - perc().simulation_step) / jjob.items.size() > 30) continue;
            
            s32 val;
            jobexe_offset = general_buffer.size();
            if (get_execution_plan(jjob, &general_buffer)) {
                val = jjob.reward - job().cost;
                jout << "Job " << get_string_from_id(jjob.id).c_str() << " with cost of " << val << ' ' << " and " <<  (jjob.end - perc().simulation_step) / job().needed.size() << '\n';
                if (val > max_val and (jjob.end - perc().simulation_step) / job().needed.size() > 40) {
                    max_val = val;
                    index = i;
                }
            }    
            general_buffer.resize(jobexe_offset);
            jobexe_offset = 0;
        }
        if (index != -1) {
            jout << "Taking job "
                 << get_string_from_id(perc().priced_jobs[index].id).c_str() << '\n';
            jobexe_offset = general_buffer.size();
            if (not get_execution_plan(perc().priced_jobs[index], &general_buffer)) {
                general_buffer.resize(jobexe_offset);
                jobexe_offset = 0;
                return;
            }
            for (u8 i = 0; i < job().needed.size(); ++i) {
                job().needed[i].id = i;
            }
        } else {
            while (shop_visited_index < perc().shops.size()) {
                float min_dist = std::numeric_limits<float>::infinity();
                int index = -1;
                for (int j: Agent_iter{}) {
                    if (agent_task[j].type != Requirement::NOTHING or j > 12) continue;
                    float dist = perc(j).self.pos.dist2(perc().shops[shop_visited_index].pos) / sim(j).role.speed;
                    if (dist < min_dist) {
                        min_dist = dist;
                        index = j;
                    }
                }
                if (index != -1) {
                    agent_task[index] = {Requirement::VISIT, 0xff, {0, 0}, perc().shops[shop_visited_index].name};
                    ++shop_visited_index;
                } else {
                    break;
                }
            }
            
            return;
        }
    }

    auto is_dep = [this](u8 child, u8 parent) -> bool {
        while (true) {
            if (child == parent) {
                return true;
            } else if (child < parent) {
                return false;
            }
            child = job().needed[child].dependency;
            if (child == 0xff) {
                return false;
            }
        }
    };

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
                for (int j: Agent_iter{}) {
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
                for (int j: Agent_iter{}) {
                    if (agent_task[j].type == Requirement::NOTHING
                        or (agent_task[j].type == Requirement::CRAFT_ASSIST
                            and is_dep(i, agent_task[j].dependency))
                    ) {
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
                            jout << (int)req.type << ' ' << (int)req.dependency << ' ' << (int)i << ' ' << (int)j << ' ' << get_string_from_id(req.item.item).c_str() << ' ' << (int)req.item.amount << '\n';
                            req.type = Requirement::NOTHING;
                            break;
                        } else {
                            agent_task[j] = req;
                            jout << (int)req.type << ' ' << (int)req.dependency << ' ' << (int)i << ' ' << (int)j << ' ' << get_string_from_id(req.item.item).c_str() << ' ' << (int)req.item.amount << '\n';
                            agent_task[j].item.amount = max_count;
                            req.item.amount -= max_count;
                        }
                    }
                }
            }
        } else if (req.type == Requirement::CRAFT_ITEM) {
            bool assigned = false;

            if (req.is_tool) {
                for (int j: Agent_iter{}) {
                    for (auto item: perc(j).self.items) {
                        if (item.item == req.item.item) {
                            if (item.amount < req.item.amount) {
                                req.item.amount -= item.amount;
                            } else {
                                assigned = true;
                                break;
                            }     
                        }
                    }
                    if (assigned) break;
                }
            }
 
             
            if (not assigned) {
                for (int j: Agent_iter{}) {
                    if (agent_task[j].dependency == i and agent_task[j].state >= 2) {
                        if (req.is_tool and (not sim(j).role.tools.count(req.item.item) or sim(j).role.speed == 1)) continue;
                        agent_task[j] = req;
                        jout << (int)req.type << ' ' << (int)req.dependency << ' ' << (int)i << ' ' << (int)j << ' ' << get_string_from_id(req.item.item).c_str()  << ' ' << (int)req.item.amount << '\n';
                        req.type = Requirement::NOTHING;
                        assigned = true;
                        break;
                    }
                }
            }
            if (not assigned) {
                for (int j: Agent_iter{}) {
                    if (agent_task[j].type == Requirement::NOTHING) {
                        if (req.is_tool and (not sim(j).role.tools.count(req.item.item) or sim(j).role.speed == 1)) continue;
                        agent_task[j] = req;
                        jout << (int)req.type << ' ' << (int)req.dependency << ' ' << (int)i << ' ' << (int)j << ' ' << get_string_from_id(req.item.item).c_str()  << ' ' << (int)req.item.amount << '\n';
                        req.type = Requirement::NOTHING;
                        assigned = true;
                        break;
                    }
                }
            }
        } else if (req.type == Requirement::CRAFT_ASSIST) {
            bool assigned = false;
            for (int j: Agent_iter{}) {
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
                for (int j: Agent_iter{}) {
                    if (agent_task[j].type != Requirement::NOTHING) continue;
                    for (auto item: perc(j).self.items) {
                        if (item.item == req.item.item) {
                            agent_task[j] = req;
                            jout << (int)req.type << ' ' << (int)req.dependency << ' ' << (int)i << ' ' << (int)j << ' ' << get_string_from_id(req.item.item).c_str()  << ' ' << (int)req.item.amount << '\n';

                            if (item.amount < req.item.amount) {
                                req.item.amount -= item.amount;
                            } else {
                                req.type = Requirement::NOTHING;
                                assigned = true;
                                break;
                            }
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

template <typename T>
std::ostream& operator<< (std::ostream& os, std::pair<T, T> p) {
    return os << '(' << p.first << ", " << p.second << ')';
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

    // Emergency measures
    float min_dist = std::numeric_limits<float>::infinity();
    Charging_station const* station = nullptr;
    for (Charging_station const& i : perc().charging_stations) {
        float dist = i.pos.dist(perc(agent).self.pos);
		if (dist < min_dist) {
            min_dist = dist;
            station = &i;
        }
	}
    assert(station);

    bool charge_flag = false;

    float reach = (s.charge - 70) / 10 * sim(agent).role.speed * speed_conversion * 10.f / 13.f;
//    if (agent == 0) jout << (int)agent << ' ' << reach << ' ' << min_dist << " Reching\n";
    if (reach < min_dist) {
        charge_flag = true;
    } else if (agent_cs[agent] != 0) {
        charge_flag = true;
    } else if (perc(agent).self.route.size() > 1) {
        auto const& route = perc(agent).self.route;
        float dist_to_target = perc(agent).self.pos.dist(route[0]);
        for (int i = 1; i < route.size(); ++i) {
            dist_to_target += route[i-1].dist(route[i]);
        }
//        if (agent == 0) jout << dist_to_target << '\n';
        if (reach < dist_to_target) {
            charge_flag = true;
            float min_dist = std::numeric_limits<float>::infinity();
            for (Charging_station const& i : perc().charging_stations) {
                float d = i.pos.dist(perc(agent).self.pos);
                float dist = d + i.pos.dist(route.end()[-1]);
                if (dist < min_dist and d < reach and d > reach * 0.4) {
                    min_dist = dist;
                    station = &i;
                }
            }
        }
    }
    
	if (charge_flag) {
        if (agent_cs[agent] == 0) {
            agent_cs[agent] = station->name;
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
            if (s.last_action == Action::DELIVER_JOB
                and (s.last_action_result == Action::SUCCESSFUL
                    or s.last_action_result == Action::SUCCESSFUL_PARTIAL))
            {
                return true;
            } else {
                into->emplace_back<Action_Deliver_job>(job().job);
                return false;
            }
        } else {
            int other_agent = -1;
            for (int i: Agent_iter{}) {
                if (i == agent) continue;
                if (agent_task[i].type == Requirement::CRAFT_ITEM
                    /* and agent_task[i].where == job().needed[req.dependency].where */) {
                    other_agent = i;
                    break;
                }
            }

            if (job().needed[req.dependency].item.amount == 0) {
                return true;
            } else {
                if (other_agent != -1) {
                    req.type = Requirement::CRAFT_ASSIST;
                    req.state = 1;
                    into->emplace_back<Action_Assist_assemble>(sim(other_agent).id);
                    return false;
                }
                into->emplace_back<Action_Abort>();
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
                --req.item.amount;
                --job().needed[req.id].item.amount;
            }
            if (req.item.amount == 0) {
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
    } else if (req.type == Requirement::VISIT) {
        if (agent_goto(req.where, agent, into)) {
            req.type = Requirement::NOTHING;
        } else {
            return;
        }
    }
    
    into->emplace_back<Action_Abort>();
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

