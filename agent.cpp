
#include "agent.hpp"

#include <cmath>
#include <vector>
#include <limits>
#include <deque>
                 
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

template <typename T>
struct Table {
	T data[256];

	T& operator[](u8 i) {
		return data[i];
	}

	void init() {
		for (u16 i = 0; i < 256; i++) {
			data[i] = 0;
		}
	}
};

Table<u32> item_cache;

void Mothership_complex::on_sim_start(u8 agent, Simulation const& simulation, int sim_size) {
	if (agent == 0) {
		item_cache.init();
		general_buffer.reset();
		general_buffer.emplace_back<World>();
		world().simulation_id = simulation.id;
		world().team = simulation.team;
		world().seed_capital = simulation.seed_capital;
		world().max_steps = simulation.steps;
		world().products.init(simulation.products, &general_buffer);
		world().roles.init(&general_buffer);
	}
	u8 rname = simulation.role.name;
	world().agents[agent].role = rname;
	if (get_by_id<Role>(rname) == nullptr) {
		world().roles.push_back(simulation.role, &general_buffer);
	}
}

void Mothership_complex::pre_request_action() {
	std::swap(last_step_buffer, step_buffer);
	step_buffer.reset();
	step_buffer.emplace_back<Situation>();
}

void Mothership_complex::pre_request_action(u8 agent, Perception const& perc, int perc_size) {
	if (agent == 0) {
		if (perc.simulation_step == 0) {
			u8 i = 0;
			for (Entity const& e : perc.entities) {
				if (e.team != world().team) {
					world().opponent_team = e.team;
					world().opponents[i].name = e.name;
					world().opponents[i++].role = e.role;
				}
			}
			world().charging_stations.init(&general_buffer);
			for (Charging_station const& f : perc.charging_stations) {
				Charging_station_static & s = world().charging_stations.emplace_back(&general_buffer);
				s.name = f.name;
				s.pos = f.pos;
				s.price = f.price;
				s.rate = f.rate;
				s.slots = f.slots;
			}
			world().dump_locations.init(perc.dump_locations, &general_buffer);
			world().shops.init(&general_buffer);
			for (Shop const& f : perc.shops) {
				Shop_static & s = world().shops.emplace_back(&general_buffer);
				s.name = f.name;
				s.pos = f.pos;
			}
			i = 0;
			for (Shop const& f : perc.shops) {
				Shop_static & s = world().shops[i++];
				s.items.init(&general_buffer);
				for (Shop_item i : f.items) {
					Shop_item_static & is = s.items.emplace_back(&general_buffer);
					is.item = i.item;
				}
			}
			world().storages.init(&general_buffer);
			for (Storage const& f : perc.storages) {
				Storage_static & s = world().storages.emplace_back(&general_buffer);
				s.name = f.name;
				s.pos = f.pos;
				s.price = f.price;
				s.totalCapacity = f.totalCapacity;
			}
			world().workshops.init(perc.workshops, &general_buffer);
		}
		situation().deadline = perc.deadline;
		situation().simulation_step = perc.simulation_step;
		situation().team.money = perc.team.money;
		situation().team.jobs_taken.init(perc.team.jobs_taken, &step_buffer);
		situation().team.jobs_posted.init(perc.team.jobs_posted, &step_buffer);
		u8 i = 0;
		for (Entity const& e : perc.entities) {
			situation().opponents[i++].pos = e.pos;
		}
		situation().charging_stations.init(&step_buffer);
		for (u8 i = 0; i < perc.charging_stations.size(); i++) {
			situation().charging_stations.emplace_back(&step_buffer);
		}
		situation().shops.init(&step_buffer);
		for (u8 i = 0; i < perc.shops.size(); i++) {
			situation().shops.emplace_back(&step_buffer);
		}
		i = 0;
		for (Shop const& f : perc.shops) {
			Shop_dynamic & s = situation().shops[i++];
			s.items.init(&step_buffer);
			for (u8 i = 0; i < f.items.size(); i++) {
				s.items.emplace_back(&step_buffer);
			}
		}
		situation().storages.init(&step_buffer);
		for (Storage const& f : perc.storages) {
			Storage_dynamic & s = situation().storages.emplace_back(&step_buffer);
			s.usedCapacity = f.usedCapacity;
		}
		situation().auction_jobs.init(perc.auction_jobs, &step_buffer);
		i = 0;
		for (Job_auction const& f : perc.auction_jobs) {
			Job_auction & j = situation().auction_jobs[i++];
			j.items.init(f.items, &step_buffer);
		}
		situation().priced_jobs.init(perc.priced_jobs, &step_buffer);
		i = 0;
		for (Job_priced const& f : perc.priced_jobs) {
			Job_priced & j = situation().priced_jobs[i++];
			j.items.init(f.items, &step_buffer);
		}
		if (perc.simulation_step != 0) {
			// locally visible data from last step
			u8 i = 0;
			for (Charging_station_dynamic const& f : last_situation().charging_stations) {
				if (f.q_size != 0xff) {
					situation().charging_stations[i].q_size = f.q_size;
				}
				i++;
			}
			i = 0;
			for (Shop_dynamic const& f : last_situation().shops) {
				u8 j = 0;
				for (Shop_item_dynamic const& si : f.items) {
					if (si.amount != 0xff) situation().shops[i].items[j].amount = si.amount;
					if (si.restock != 0xff) situation().shops[i].items[j].restock = si.restock;
					j++;
				}
				i++;
			}
		}
	}

	if (perc.simulation_step == 0) {
		//world().agents[agent].name = perc.self.name;
	}

	// locally visible data
	u8 i = 0;
	for (Charging_station const& f : perc.charging_stations) {
		if (f.q_size != 0xff) {
			situation().charging_stations[i].q_size = f.q_size;
		}
		i++;
	}
	i = 0;
	for (Shop const& f : perc.shops) {
		u8 j = 0;
		for (Shop_item const& si : f.items) {
			if (si.cost != 0 && si.cost != world().shops[i].items[j].cost) {
				world().shops[i].items[j].cost = si.cost;
				item_cache[si.item] = 0;
			}
			if (si.amount != 0xff) situation().shops[i].items[j].amount = si.amount;
			if (si.restock != 0xff) situation().shops[i].items[j].restock = si.restock;
			j++;
		}
		i++;
	}
}


bool Mothership_complex::agent_goto(Situation& sit, u8 where, u8 agent, Buffer* into) {
    auto const& s = world().agents[agent];
    auto const& d = sit.agents[agent];
    
    if (s.in_facility == where) {
        return true;
    } else if (d.last_go == where
               and s.last_action_result == Action::SUCCESSFUL) {
        into->emplace_back<Action_Continue>();
        return false;
	} else {
        d.last_go = where;
        into->emplace_back<Action_Goto1>(where);
        return false;
    }
}

void Mothership_complex::get_agent_action(Situation const& sit, u8 agent, Buffer* into) {
    auto const& s = world().agents[agent];
    auto const& d = sit.agents[agent];
    
    if (d.task.type == Task::NONE) {
        into->emplace_back<Action_Abort>();
        return;
    } else if (d.task.type == Task::BUY_ITEM) {
        if (d.task.state == 0) {
            if (agent_goto(world(), sit, d.task.where, agent, into)) {
                d.task.state = 1;
            } else {
                return;
            }
        }
        if (d.task.state == 1) {
            if (d.last_action == Action::BUY and not d.last_action_result) {
                d.task.type = Task::NONE;
            } else {
                into->emplace_back<Action_Buy>(req.item);
                return;
            }
        }
    } else {
        // TODO
        assert(false);
    }
    
    into->emplace_back<Action_Abort>();
    return;
}

void Mothership_complex::internal_simulation_step(Situation& sit) {
    // Restock shops
    for (int i = 0; i < sit.shops.size(); ++i) {
        for (int j = 0; j < sit.shops[i].size(); ++j) {
            auto item = sit.shops[i].items[j];
            --item.restock;
            if (item.restock == 0) {
                ++item.amount;
                item.restock = world().shops[i].items[j].period;
            }
        }
    }

    // TODO Implement storage pricing

    // Execute agent actions
    for (int i = 0; i < agents_per_team; ++i) {
        int action_offset = step
    }
}



void Mothership_complex::on_request_action() {

}

void Mothership_complex::post_request_action(u8 agent, Buffer* into) {
	into->emplace_back<Action_Abort>();
	return;

}


u32 Mothership_complex::rate_situation(Situation const& s) {

	constexpr u8 job_depth = 4;

	Table<u8> team_items;
	team_items.init();

	std::deque<Table<u8>> ti_stack;

	for (Agent_dynamic const& a : s.agents) {
		for (Item_stack const& i : a.items) {
			team_items[i.item] += i.amount;
		}
	}

	std::function<u16 (u8)> rate_item = [&](u8 item) -> u16 {

		if(item_cache[item] != 0) {
			return item_cache[item];
		}

		u16 mc = 0xffff;

		for (Shop_static const& shop : world().shops) {
			for (Shop_item_static const& si : shop.items) {
				if (si.item == item) {
					if (si.cost != 0 && si.cost < mc) {
						mc = si.cost;
					}
				}
			}
		}

		u16 ac = 0xffff;
		Product const* p = get_by_id<Product>(item);
		if (p->assembled) {
			ac = 0;
			for (Item_stack const& i : p->consumed) {
				ac += rate_item(i.item) * i.amount;
			}
		}

		return ac < mc ? ac : (mc == 0xffff ? 0 : mc);
	};

	auto rate_job = [&](Job_priced const& j) -> u16 {
		// underestimate of job total cost
		u32 rt = 0;
		// underestimate of job completion cost
		u32 rc = 0;
		for (Job_item ji : j.items) {
			u16 ir = rate_item(ji.item);
			rt += ir * ji.amount;
			rc += ir * ji.delivered;

			std::function<void(Item_stack)> process_item = [&](Item_stack is) {
				u8 it = is.item, am = is.amount;
				if (team_items[it] < am) {
					rc += ir * team_items[it];
					am -= team_items[it];
					team_items[it] = 0;
					Product const* p = get_by_id<Product>(it);
					if (p->assembled) {
						for (Item_stack i : p->consumed) {
							i.amount *= is.amount;
							process_item(i);
						}
					}
				} else {
					rc += ir * am;
					team_items[it] -= am;
				}
			};

			Item_stack is;
			is.item = ji.item;
			is.amount = ji.amount - ji.delivered;
			process_item(is);
		}

		if (j.reward < rt) {
			return 0;
		}
		return rc + (j.reward - rc) * rc / rt;
	};

	u16 job_positions[job_depth];
	for (u8 i = 0; i < job_depth; i++) {
		job_positions[i] = 0xffff;
	}

	u16 r = s.team.money;

	for (u8 n = 0; n < job_depth; n++) {
		ti_stack.push(team_items);
		u16 br = 0;
		u16 bj = 0xffff;
		for (u16 i = 0; i < s.priced_jobs.size(); i++) {
			Job_priced const& j = s.priced_jobs[i];
			bool jc = false;
			for (u16 j = 0; j < job_depth; j++) {
				if (job_positions[j] == i) {
					jc = true;
					break;
				}
			}
			if (jc) continue;
			u16 jr = rate_job(j);
			if (jr > br) {
				br = jr;
				bj = i;
			}
			team_items = ti_stack.top();
		}
		ti_stack.pop();
		if (bj == 0xffff) break;
		job_positions[n] = bj;
		// side effect: remove items from team_items
		r += rate_job(s.priced_jobs[bj]);
	}

	return r;
}
} /* end of namespace jup */

