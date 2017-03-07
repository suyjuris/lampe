
#include "agent.hpp"

#include <cmath>
#include <vector>
#include <limits>
#include <deque>
                 
#include "buffer.hpp"
#include "messages.hpp"

#include "debug.hpp"

namespace jup {

static u8 agent_indices[] = {4, 5, 6, 7, 8, 9, 10, 11, 0, 1, 2, 3, 12, 13, 14, 15};
struct Agent_iter {
    u8 const* begin() {
        return agent_indices;
    }
    u8 const* end() {
        return agent_indices + sizeof(agent_indices) / sizeof(agent_indices[0]);
    }
};

bool Mothership_simple::get_execution_plan(Job const& job, Buffer* into) {
    assert(into);

    int exe_offset = into->size();
    into->emplace_back<Job_execution>();
    auto exe = [into, exe_offset]() -> Job_execution& {
        return into->get<Job_execution>(exe_offset);
    };

    std::function<bool(Item_stack, u8, bool, int)> add_req;
    add_req = [this, exe, &add_req, into](Item_stack item, u8 depend, bool is_tool, int depth) {
        for (u8 agent: Agent_iter{}) {
            if (is_tool and not sim(agent).role.tools.count(item.item))
                continue;
            for (Item_stack i: perc(agent).self.items) {
                if (i.item == item.item) {
                    int avail = i.amount;
                    for (auto r: exe().needed) {
                        if (r.type == Requirement::CRAFT_ASSIST and r.item.item == item.item
                            and r.where == (u8)(agent + 1)) {
                            avail -= r.item.amount;
                        }
                    }
                    assert(avail >= 0);
                    if (avail >= item.amount) {
                        exe().needed.push_back(Requirement {
                            Requirement::CRAFT_ASSIST, depend, item, (u8)(agent + 1), is_tool
                        }, into);                        
                        return true;
                    } else if (avail > 0) {
                        exe().needed.push_back(Requirement {
                            Requirement::CRAFT_ASSIST, depend, {item.item, (u8)avail}, (u8)(agent + 1), is_tool
                        }, into);
                        item.amount -= avail;
                    }
                }
            }
        }

        for (Deliver_item i: delivs) {
            bool job_exists = false;
            for (Job_priced const& job: perc().priced_jobs) {
                if (job.id == i.job) job_exists = true;
            }
            if (i.item.item == item.item and not job_exists) {
                int avail = i.item.amount;
                for (auto r: exe().needed) {
                    if (r.type == Requirement::GET_ITEM and r.item.item == item.item
                            and r.where == i.storage) {
                        avail -= r.item.amount;
                    }
                }
                assert(avail >= 0);
                if (avail >= item.amount) {
                    exe().needed.push_back(Requirement {
                        Requirement::GET_ITEM, depend, item, i.storage, is_tool
                    }, into);
                    return true;
                } else if (avail > 0) {
                    exe().needed.push_back(Requirement {
                        Requirement::GET_ITEM, depend, {item.item, (u8)avail}, i.storage, is_tool
                    }, into);
                    item.amount -= avail;
                }
            }
        }
        
        if (depth > 1) {
            int index = find_cheap(item.item);
            if (index != -1) {
                exe().cost += cheaps[index].price * item.amount; 
                exe().needed.push_back(Requirement {
                        Requirement::BUY_ITEM, depend, item, cheaps[index].shop, is_tool
                            }, into);
                return true;
            }
        }

        for (Product const& i: sim().products) {
            if (i.name == item.item and i.assembled) {
                u8 dep = exe().needed.size();
                exe().cost += perc().workshops[workshop].price * item.amount;
                exe().needed.push_back(Requirement {Requirement::CRAFT_ITEM, depend, item, i.name, is_tool}, into);
                for (Item_stack j: i.tools) {
                    exe().needed.push_back(Requirement {Requirement::CRAFT_ASSIST, dep, j, 0, true}, into);
                }
                for (Item_stack j: i.consumed) {
                    if (not add_req({j.item, (u8)(j.amount * item.amount)}, dep, false, depth + 1)) return false;
                }
                for (Item_stack j: i.tools) {
                    if (not add_req(j, 0xff, true, depth + 1)) return false;
                }
                return true;
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

        return false;
    };

    exe().job = job.id;
    exe().needed.init(into);
    for (Job_item i: job.items) {
        if (not add_req({i.item, (u8)(i.amount - i.delivered)}, 0xff, false, 0))
            return false;
    }
    exe().cost += exe().needed.size() * 150 + 2000;
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


void Mothership_simple::pre_request_action() {
    assert(agent_count == 16);
    
    std::swap(step_buffer, old_step_buffer);
    std::memcpy(old_perc_offsets, perc_offsets, sizeof(perc_offsets));
    
    step_buffer.reset();
}

void Mothership_simple::pre_request_action(u8 agent, Perception const& perc, int perc_size) {
    perc_offsets[agent] = step_buffer.size();
    step_buffer.append(&perc, perc_size);

    auto const& s = perc.self;
    
    if (s.last_action == Action::DELIVER_JOB) {
        if (s.last_action_result == Action::SUCCESSFUL_PARTIAL) {
            auto &o = old_perc(agent).self;
            for (auto i: o.items) {
                int count = i.amount;
                for (auto j: s.items) {
                    if (i.item == j.item) {
                        count -= j.amount;
                    }
                }
                assert(0 <= count and count < 256);
                if (count > 0) {
                    u8 storage = 0;
                    for (Job& k: old_perc().priced_jobs) {
                        if (k.id == job().job) {
                            storage = k.storage;
                            break;
                        }
                    }
                    delivs.push_back( {{i.item, (u8)count}, storage, job().job} );
                }
            }
        } else if (s.last_action_result == Action::SUCCESSFUL) {
            for (int i = 0; (unsigned)i < delivs.size(); ++i) {
                if (delivs[i].job == old_job) {
                    delivs.erase(delivs.begin() + i);
                    --i;
                }
            }
        }
    }
}

void Mothership_simple::on_request_action() {
    // TODO: Fix allocation
    general_buffer.reserve_space(256);

    if (workshop == 0xff) {
        float min_dist = 9e9;
        for (size_t i = 0; i < perc().workshops.size(); ++i) {
            if (perc().workshops[i].pos.dist(Pos {127, 127}) < min_dist) {
                min_dist = perc().workshops[i].pos.dist(Pos {127, 127});
                workshop = i;
            }
        }
    }
    
    for (int i: Agent_iter{}) {
        for (Shop const& j: perc(i).shops) {
            for (Shop_item item: j.items) {
                if (item.amount == 0xff) continue;
                bool found = false;
                for (auto& k: cheaps) {
                    if (k.shop == j.name and item.item == k.item) {
                        k.price = item.cost;
                        found = true;
                        break;
                    }
                }
                if (not found) {
                    cheaps.push_back({item.cost, item.item, j.name});
                }
            }
        }
    }
    std::sort(cheaps.begin(), cheaps.end());

    //if (perc().simulation_step == 109 or perc().simulation_step == 9999)
    //    jdbg < perc(),0;
    
    bool no_job_flag = true;
    if (jobexe_offset != 0) {
        for (auto const& j: perc().priced_jobs) {
            if (j.id == job().job) {
                no_job_flag = false;
                break;
            }
        }

        // Watchdog
        bool active = false;
        for (u8 agent: Agent_iter{}) {
            if (perc(agent).self.last_action != Action::ABORT
                    and (perc(agent).self.last_action_result == Action::SUCCESSFUL
                    or perc(agent).self.last_action_result == Action::SUCCESSFUL_PARTIAL
                    or perc(agent).self.last_action_result == Action::FAILED_RANDOM)) {
                active = true;
            }
        }
        if (not active and not no_job_flag) {
            ++watchdog_timer;
            if (watchdog_timer > 2) {
                no_job_flag = true;
                jdbg<"Watchdog activated.",0;
            }
        } else {
            watchdog_timer = 0;
        }
    }
    if (no_job_flag) {
        watchdog_timer = 0;
        reserved_items.clear();
        
        for (int i = 0; i < 16; ++i) {
            if (agent_task[i].type != Requirement::VISIT) {
                agent_task[i].type = Requirement::NOTHING;
                agent_cs[i] = 0;
                agent_last_go[i] = 0;
            }
        }
        float max_val = 0; int index = -1;
        for (int i = 0; i < perc().priced_jobs.size(); ++i) {
            Job_priced const& jjob = perc().priced_jobs[i];
            
            float val;
            jobexe_offset = general_buffer.size();
            if (get_execution_plan(jjob, &general_buffer)) {
                val = jjob.reward - job().cost;
                auto time_val = (std::min(jjob.end, sim().steps) - perc().simulation_step) / (job().needed.size() / 2 + 7);
                if (perc().simulation_step % 5 == 0) {
                    jout << "Job " << get_string_from_id(jjob.id).c_str() << " with cost of " << val << " and " << time_val << " (" << job().needed.size() << ")\n";
                }
                if (val > max_val and time_val > 12) {
                    max_val = val;
                    index = i;
                }
            }    
            general_buffer.resize(jobexe_offset);
            jobexe_offset = 0;
        }
        if (index != -1) {
            jobexe_offset = general_buffer.size();
            if (not get_execution_plan(perc().priced_jobs[index], &general_buffer)) {
                general_buffer.resize(jobexe_offset);
                jobexe_offset = 0;
                return;
            }
            for (u8 i = 0; i < job().needed.size(); ++i) {
                job().needed[i].id = i;
            }
            jout << "Taking job "
                 << get_string_from_id(perc().priced_jobs[index].id).c_str() << '\n';
            jdbg < job(),0;
            old_job = job().job;
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

    auto optimize_loc = [this](u8 agent, Requirement& req) {
        assert(req.type == Requirement::BUY_ITEM);

        float best_rating = 9e9;
        for (auto i: cheaps) {
            if (i.item != req.item.item) continue;
            Pos p1;
            for (Shop const& j: perc().shops) {
                if (j.name == i.shop) {
                    p1 = j.pos;
                    break;
                }
            }
            float rating = req.item.amount * i.price + dist_cost(p1, agent);
            if (rating < best_rating) {
                best_rating = rating;
                req.where = i.shop;
            }
        }
    };
    
    //auto& waiting = step_buffer.emplace_back<Flat_array<u8>>(&step_buffer);

    //int count_idle = 0;
    //for (auto i: agent_task) {
    //    if (i.type == Requirement::NOTHING) ++count_idle;
    //}

    /*if (perc().simulation_step == 196) {
        jdbg<=reserved_items,0;
    }
    
    if (perc().simulation_step == 196) {
        for (u8 j = 0; j < 16; ++j) {
            jdbg<agent_task[j]<"agent="<j,0;
        }
        jdbg<=job().needed,0;
        jdbg,0;
        }*/
    
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
            /*if (req.is_tool) {
                for (int j: Agent_iter{}) {
                    int avail = 0;
                    for (Item_stack i: perc(j).self.items) {
                        if (i.item == req.item.item) {
                            avail = i.amount;
                            break;
                        }
                    }
                    for (Reserved_item i: reserved_items) {
                        if (i.agent == j and i.item.item == req.item.item) {
                            avail -= i.item.amount;
                        }
                    }
                    
                    if (avail > 0) {
                        if (avail < req.item.amount) {
                            jdbg<"DiscaPart7"<req<"agent="<(int)j,0;
                            reserved_items.push_back({j, req.dependency, {req.item.item, (u8)avail}});
                            req.item.amount -= avail;
                        } else {
                            jdbg<"Discarded7"<req<"agent="<(int)j,0;
                            reserved_items.push_back({j, req.dependency, req.item});
                            assigned = true;
                            break;
                        }
                    }
                    if (assigned) break;
                }
                }*/

            if (not assigned) {
                for (u8 j: Agent_iter{}) {
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
                        int max_count = p->volume > 0 ? (sim(j).role.max_load -
                                        perc(j).self.load) / p->volume : 83726;
                        if (max_count == 0) {
                            continue;
                        } else if (max_count >= req.item.amount) {
                            agent_task[j] = req;
                            if (req.type == Requirement::BUY_ITEM)
                                optimize_loc(j, agent_task[j]);
                            jdbg<"Assigned1"<req<"agent="<(int)j,0;
                            reserved_items.push_back({j, req.dependency, req.item});
                            req.type = Requirement::NOTHING;
                            break;
                        } else {
                            agent_task[j] = req;
                            if (req.type == Requirement::BUY_ITEM)
                                optimize_loc(j, agent_task[j]);
                            jdbg<"Assigned2"<req<"agent="<(int)j,0;
                            reserved_items.push_back({j, req.dependency, req.item});
                            agent_task[j].item.amount = max_count;
                            req.item.amount -= max_count;
                        }
                    }
                }
            }
        } else if (req.type == Requirement::CRAFT_ITEM) {
            bool assigned = false;
            for (u8 j: Agent_iter{}) {
                if (agent_task[j].dependency == i and agent_task[j].state >= 2) {
                    Product const* p = nullptr;
                    for (auto const& j: sim().products) {
                        if (j.name == req.item.item) {
                            p = &j; break;
                        }
                    }
                    assert(p);
                    if (req.is_tool and
                        (not sim(j).role.tools.count(req.item.item)
                         or sim(j).role.speed == 1)) {
                        continue;
                    }
                    
                    int max_count = p->volume > 0 ? (sim(j).role.max_load
                            - perc(j).self.load) / p->volume : 83726;
                    if (max_count >= req.item.amount) {
                        agent_task[j] = req;
                        reserved_items.push_back({j, req.dependency, req.item});
                        jdbg<"Assigned3"<req<"agent="<(int)j,0;
                        req.type = Requirement::NOTHING;
                        assigned = true;
                        break;
                    } else if (max_count > 0) {
                        agent_task[j] = req;
                        agent_task[j].item.amount = max_count;
                        req.item.amount -= max_count;
                        jdbg<agent_task[j],0;
                        reserved_items.push_back({j, req.dependency, agent_task[j].item});
                        jdbg<"AssiPart3"<req<"agent="<(int)j,0;
                    }                        
                }
            }
            if (not assigned) {
                for (u8 j: Agent_iter{}) {
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
                        int max_count = p->volume > 0 ? (sim(j).role.max_load
                                        - perc(j).self.load) / p->volume : 83726;
                        
                        if (max_count >= req.item.amount) {
                            agent_task[j] = req;
                            reserved_items.push_back({j, req.dependency, req.item});
                            jdbg<"Assigned4"<req<"agent="<(int)j,0;
                            req.type = Requirement::NOTHING;
                            assigned = true;
                            break;
                        } else if (max_count > 0) {
                            agent_task[j] = req;
                            agent_task[j].item.amount = max_count;
                            req.item.amount -= max_count;
                            jdbg<agent_task[j],0;
                            reserved_items.push_back({j, req.dependency, agent_task[j].item});
                            jdbg<"AssiPart4"<req<"agent="<(int)j,0;
                        }
                    }
                }
            }
        } else if (req.type == Requirement::CRAFT_ASSIST) {
            bool assigned = false;
            // TODO: This if may actually be mostly redundant
            if (req.where != 0) {
                for (u8 j: Agent_iter{}) {
                    if (req.is_tool and not sim(j).role.tools.count(req.item.item))
                        continue;
                    int avail = 0;
                    for (Item_stack i: perc(j).self.items) {
                        if (i.item == req.item.item) {
                            avail = i.amount;
                            break;
                        }
                    }
                    for (Reserved_item i: reserved_items) {
                        if (i.agent == j and i.item.item == req.item.item) {
                            avail -= i.item.amount;
                        }
                    }
                    
                    if (avail > 0) {
                        if (req.is_tool or (agent_task[j].type != Requirement::NOTHING
                                and agent_task[j].dependency == req.dependency
                                and req.dependency != 0xff))
                        {
                            if (avail < req.item.amount) {
                                jdbg<"DiscaPart1"<req<"agent="<(int)j,0;
                                req.item.amount -= (u8)avail;
                                reserved_items.push_back({j, req.dependency, {req.item.item, (u8)avail}});
                            } else {
                                jdbg<"Discarded1"<req<"agent="<(int)j,0;
                                reserved_items.push_back({j, req.dependency, req.item});
                                req.type = Requirement::NOTHING;
                                assigned = true;
                            }
                        } else if (agent_task[j].type == Requirement::NOTHING) {
                            if (avail < req.item.amount) {
                                jdbg<"AssiPart5"<req<"agent="<(int)j,0;
                                reserved_items.push_back({j, req.dependency, {req.item.item, (u8)avail}});
                                req.item.amount -= avail;
                                agent_task[j] = req;
                                agent_task[j].item = {req.item.item, (u8)avail};
                                agent_task[j].where = 0;
                            } else {
                                jdbg<"Assigned5"<req<"agent="<(int)j,0;
                                reserved_items.push_back({j, req.dependency, req.item});
                                agent_task[j] = req;
                                agent_task[j].where = 0;
                                req.type = Requirement::NOTHING;
                                assigned = true;
                            }
                        }
                    }
                    if (assigned) break;
                }
            } else {
                for (u8 j: Agent_iter{}) {
                    if (req.is_tool and not sim(j).role.tools.count(req.item.item))
                        continue;
                    
                    int avail = 0;
                    for (Item_stack i: perc(j).self.items) {
                        if (i.item == req.item.item) {
                            avail = i.amount;
                            break;
                        }
                    }
                    for (Reserved_item i: reserved_items) {
                        if (i.agent == j and i.item.item == req.item.item and i.until != 0xff) {
                            avail -= i.item.amount;
                        }
                    }
                    if (agent_task[j].type == Requirement::NOTHING) {
                        if (avail >= req.item.amount) {
                            agent_task[j] = req;
                            reserved_items.push_back({j, req.dependency, req.item});
                            jdbg<"Assigned6"<req<"agent="<(int)j,0;
                            req.type = Requirement::NOTHING;
                            assigned = true;
                            break;
                        } else if (avail > 0) {
                            agent_task[j] = req;
                            reserved_items.push_back({j, req.dependency, {req.item.item, (u8)avail}});
                            jdbg<"AssiPart6"<req<"agent="<(int)j,0;
                            req.item.amount -= avail;
                        }
                    } else if (agent_task[j].dependency == req.dependency) {
                        if (avail >= req.item.amount) {
                            reserved_items.push_back({j, req.dependency, req.item});
                            jdbg<"Discarded2"<req<"agent="<(int)j,0;
                            req.type = Requirement::NOTHING;
                            assigned = true;
                            break;
                        } else if (avail > 0) {
                            reserved_items.push_back({j, req.dependency, {req.item.item, (u8)avail}});
                            jdbg<"DiscaPart2"<req<"agent="<(int)j,0;
                            req.item.amount -= avail;
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

    if (s.charge == 0) {
        agent_last_go[agent] = 0;
        jdbg<"Calling breakdown service!",0;
        into->emplace_back<Action_Call_breakdown_service>();
        return false;
    }
    
	for (Charging_station const& i : perc().charging_stations) {
		if (i.name == s.in_facility and s.charge < sim(agent).role.max_battery) {
            agent_last_cs[agent] = i.name;
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
    if (sim(agent).role.name == get_id_from_string("Drone")) {
        reach *= 1.5;
    }
    if (reach < min_dist) {
        charge_flag = true;
    } else if (agent_cs[agent] != 0) {
        charge_flag = true;
    } else if (perc(agent).self.route.size() > 1) {
        // TODO: The agent must be able to reach a charging station after reaching target
        auto const& route = perc(agent).self.route;
        float dist_to_target = perc(agent).self.pos.dist(route[0]);
        for (int i = 1; i < route.size(); ++i) {
            dist_to_target += route[i-1].dist(route[i]);
        }
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

        if (charge_flag and agent_last_cs[agent] == agent_cs[agent]) {
            // We're stuck in a loop
            charge_flag = false;
        } else {
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
    }

    if (agent_last_go[agent] != agent_last_cs[agent] and agent_last_go[agent] != where) {
        agent_last_cs[agent] = 0;
    }
    
    if (s.in_facility == where) {
        agent_last_cs[agent] = 0;
        return true;
    } else if (agent_last_go[agent] == where
               and s.last_action_result == Action::SUCCESSFUL) {
        if (agent == 0 and perc().simulation_step == 1) {
            charge_flag = false;
        }
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
            return perc().workshops[workshop].name;
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
            bool do_craft = false;
            for (int i: Agent_iter{}) {
                if (i == agent) continue;
                if (agent_task[i].type == Requirement::CRAFT_ITEM
                        and agent_task[i].id == req.dependency
                        and agent_task[i].item.amount > 0) {
                    other_agent = i;
                    do_craft = agent_task[i].state == 1;
                    break;
                }
            }
            
            if (do_craft) {
                req.type = Requirement::CRAFT_ASSIST;
                req.state = 3;
                into->emplace_back<Action_Assist_assemble>(sim(other_agent).id);
                return false;
            } else if (other_agent == -1
                    and job().needed[req.dependency].type == Requirement::NOTHING) {
                jdbg<"Delivered1"<req<"agent="<agent,0;
                return true;
            } else {
                into->emplace_back<Action_Abort>();
                return false;
            }
        }
    };

    auto const& s = perc(agent).self;

    if (req.type == Requirement::GET_ITEM) {
        if (req.state == 0) {
            if (agent_goto(req.where, agent, into)) {
                req.state = 1;
            } else {
                return;
            }
        }
        if (req.state == 1) {
            if (s.last_action == Action::RETRIEVE_DELIVERED and not s.last_action_result) {
                u8 amount = req.item.amount;
                for (int i = 0; (unsigned)i < delivs.size(); ++i) {
                    if (delivs[i].item.item == req.item.item and delivs[i].storage == req.where) {
                        if (delivs[i].item.amount > amount) {
                            delivs[i].item.amount -= amount;
                            break;
                        } else {
                            amount -= delivs[i].item.amount;
                            delivs.erase(delivs.begin() + i);
                            --i;
                            if (amount == 0) break;
                        }
                    }
                }
                if (req.is_tool) {
                    jdbg<"Delivered5"<req<"agent="<agent,0;
                    req.type = Requirement::NOTHING;
                } else {
                    req.state = 2;
                }
            } else {
                into->emplace_back<Action_Retrieve_delivered>(req.item);
                return;
            }
        }
        if (req.state == 2) {
            if (req.is_tool) {
                jdbg<"Delivered4"<req<"agent="<agent,0;
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
                    jdbg<"Delivered2"<req<"agent="<agent,0;
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
                jdbg<"Delivered3"<req<"agent="<agent,0;
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
            if (agent_goto(perc().workshops[workshop].name, agent, into)) {
                req.state = 1;
            } else {
                return;
            }
        }
        if (req.state == 1) {
            if (s.last_action == Action::ASSEMBLE and !s.last_action_result) {
                --req.item.amount;

                bool noone_left = req.item.amount == 0
                    and job().needed[req.id].type == Requirement::NOTHING;
                if (noone_left) {
                    for (u8 j: Agent_iter{}) {
                        if (agent_task[j].id == req.id and agent_task[j].item.amount) {
                            noone_left = false;
                            break;
                        }
                    }
                }
                if (noone_left) {
                    for (size_t i = 0; i < reserved_items.size(); ++i) {
                        if (reserved_items[i].until == req.id) {
                            reserved_items.erase(reserved_items.begin() + i);
                            --i;
                        }
                    }
                }
            }
            if (req.item.amount == 0) {
                req.state = 2;
            } else {
                into->emplace_back<Action_Assemble>(req.item.item);
            }
        }
        if (req.state == 2) {
            if (req.is_tool) {
                jdbg<"Delivered6"<req<"agent="<agent,0;
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
            if (agent_goto(get_target(), agent, into)) {
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
		world().team_id = simulation.team;
		world().seed_capital = simulation.seed_capital;
		world().max_steps = simulation.steps;
		world().products.init(simulation.products, &general_buffer);
		u8 i = 0;
		for (Product const& p : simulation.products) {
			world().products[i].consumed.init(p.consumed, &general_buffer);
			world().products[i++].tools.init(p.tools, &general_buffer);
		}
		world().roles.init(&general_buffer);
	}
	u8 rname = simulation.role.name;
	world().agents[agent].role = rname;
	if (get_by_id<Role>(rname) == nullptr) {
		world().roles.push_back(simulation.role, &general_buffer);
	}
}

void Mothership_complex::pre_request_action() {
    step_buffer.reset();
    situation_buffer.reset();
    
    
	Tree& tree {step_buffer.emplace_back<Tree>()};
    tree.situation = situation_buffer.size();
    tree.parent = 0;
    situation_buffer.emplace<Situation>();
}

void Mothership_complex::pre_request_action(u8 agent, Perception const& perc, int perc_size) {

    situation().agents[agent].pos = perc.self.pos;
    situation().agents[agent].charge = perc.self.charge;
    situation().agents[agent].load = perc.self.load;
    situation().agents[agent].last_action = perc.self.last_action;
    situation().agents[agent].last_action_result = perc.self.last_action_result;
    situation().agents[agent].in_facility = perc.self.in_facility;
    situation().agents[agent].f_position = perc.self.f_position;
    situation().agents[agent].route_length = perc.self.route_length;
    
    if (perc.simulation_step != 0) {
        situation().agents[agent].task = last_situation().agents[agent].task;
        situation().agents[agent].last_go = last_situation().agents[agent].last_go;
    }
    
    situation().agents[agent].items.init(perc.self.items, &situation_buffer);
    situation().agents[agent].route.init(perc.self.route, &situation_buffer);
    
	if (agent == agents_per_team - 1) {
		// prevent reference invalidation in this next segment (TODO: exact size calculation)
		step_buffer.reserve(0x8000);
		step_buffer.trap_alloc(true);
		if (perc.simulation_step == 0) {
			general_buffer.reserve(0x2000);
			general_buffer.trap_alloc(true);
			u8 i = 0;
			for (Entity const& e : perc.entities) {
				if (e.team != world().team_id) {
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
				s.total_capacity = f.total_capacity;
			}
			world().workshops.init(perc.workshops, &general_buffer);
			general_buffer.trap_alloc(true);
		}
		situation().deadline = perc.deadline;
		situation().simulation_step = perc.simulation_step;
		situation().team.money = perc.team.money;
		situation().team.jobs_taken.init(perc.team.jobs_taken, &situation_buffer);
		situation().team.jobs_posted.init(perc.team.jobs_posted, &situation_buffer);
		u8 i = 0;
		for (Entity const& e : perc.entities) {
			situation().opponents[i++].pos = e.pos;
		}
		situation().charging_stations.init(&situation_buffer);
		for (u8 i = 0; i < perc.charging_stations.size(); i++) {
			situation().charging_stations.emplace_back(&situation_buffer);
		}
		situation().shops.init(&situation_buffer);
		for (u8 i = 0; i < perc.shops.size(); i++) {
			situation().shops.emplace_back(&situation_buffer);
		}
		situation().storages.init(&situation_buffer);
		for (Storage const& f : perc.storages) {
			Storage_dynamic & s = situation().storages.emplace_back(&situation_buffer);
			s.used_capacity = f.used_capacity;
		}
		situation().auction_jobs.init(perc.auction_jobs, &situation_buffer);
		situation().priced_jobs.init(perc.priced_jobs, &situation_buffer);  
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

            // Metainformation from last step
            situation().goals.init(last_situation().goals, &situation_buffer);
            situation().current_job = last_situation().current_job;
            situation().crafting_fence = last_situation().crafting_fence;
		} else {
            situation().goals.init(&situation_buffer);            
        }
        
		i = 0;
		for (Shop const& f : perc.shops) {
			Shop_dynamic & s = situation().shops[i++];
			s.items.init(&situation_buffer);
			for (u8 i = 0; i < f.items.size(); i++) {
				s.items.emplace_back(&situation_buffer);
			}
		}
		i = 0;
		for (Storage const& f : perc.storages) {
			Storage_dynamic & s = situation().storages[i++];
			s.items.init(&situation_buffer);
			for (u8 i = 0; i < f.items.size(); i++) {
				s.items.emplace_back(&situation_buffer);
			}
		}
		i = 0;
		for (Job_auction const& f : perc.auction_jobs) {
			Job_auction & j = situation().auction_jobs[i++];
			j.items.init(f.items, &situation_buffer);
		}
		i = 0;
		for (Job_priced const& f : perc.priced_jobs) {
			Job_priced & j = situation().priced_jobs[i++];
			j.items.init(f.items, &situation_buffer);
		}
	}
    
	if (perc.simulation_step == 0) {
		//world().agents[agent].name = perc.self.name;
	}
    
	// locally visible data (TODO)
    /*
	u8 i = 0;
	for (Charging_station const& f : perc.charging_stations) {
		if (f.q_size != 0xff) {
			situation().charging_stations[i].q_size = f.q_size;
		}
		i++;
	}
    */
	u8 i = 0;
	for (Shop const& f : perc.shops) {
		u8 j = 0;
		for (Shop_item const& si : f.items) {
			if (si.cost != 0 && si.cost != world().shops[i].items[j].cost) {
				world().shops[i].items[j].cost = si.cost;
				item_cache[si.item] = 0;
			}
//			if (si.amount != 0xff) situation().shops[i].items[j].amount = si.amount;
//			if (si.restock != 0xff) situation().shops[i].items[j].restock = si.restock;
			j++;
		}
		i++;
	}

    last_situation_buffer = situation_buffer;
	if (perc.simulation_step == 0 && agent == agents_per_team - 1) {
		jdbg < world(), 0;
	}
}


bool Mothership_complex::agent_goto(Situation& sit, u8 where, u8 agent, Buffer* into) {
    auto& d = sit.agents[agent];
    
    if (d.in_facility == where) {
        return true;
    } else if (d.last_go == where
               and d.last_action_result == Action::SUCCESSFUL) {
        into->emplace_back<Action_Continue>();
        return false;
	} else {
        d.last_go = where;
        into->emplace_back<Action_Goto1>(where);
        return false;
    }
}

void Mothership_complex::get_agent_action(Situation& sit, u8 agent, Buffer* into) {
    auto& d = sit.agents[agent];
    
    if (d.task.type == Task::NONE or d.task.item.amount == 0) {
        into->emplace_back<Action_Abort>();
        return;
    } else if (d.task.type == Task::BUY_ITEM) {
        if (d.task.state == 0) {
            if (agent_goto(sit, d.task.where, agent, into)) {
                d.task.state = 1;
            } else {
                return;
            }
        }
        if (d.task.state == 1) {
            if (d.last_action == Action::BUY and not d.last_action_result) {
                d.task.item.amount = 0;
            } else {
                into->emplace_back<Action_Buy>(d.task.item);
                return;
            }
        }
    } else if (d.task.type == Task::CRAFT_ITEM) {
        if (d.task.state == 0) {
            if (agent_goto(sit, d.task.where, agent, into)) {
                d.task.state = 1;
            } else {
                return;
            }
        }
        if (d.task.state == 1) {
            if (d.last_action == Action::ASSEMBLE and not d.last_action_result) {
                sit.crafting_fence = d.task.item.amount;
                d.task.state = 2;
            } else {
                into->emplace_back<Action_Assemble>(d.task.item.item);
                return;
            }
        }
        if (d.task.state == 2) {
            if (d.last_action == Action::ASSEMBLE and not d.last_action_result) {
                --sit.crafting_fence;
                --d.task.item.amount;
            } else {
                into->emplace_back<Action_Assemble>(d.task.item.item);
                return;
            }
        }
    } else if (d.task.type == Task::CRAFT_ASSIST) {
        if (d.task.state == 0) {
            if (agent_goto(sit, d.task.where, agent, into)) {
                d.task.state = 1;
            } else {
                return;
            }
        }
        if (d.task.state == 1) {
            if (d.last_action == Action::ASSIST_ASSEMBLE and not d.last_action_result) {
                d.task.state = 2;
            } else {
                into->emplace_back<Action_Assemble>(d.task.item.item);
                return;
            }
        }
        if (d.task.state == 2) {
            if (d.last_action == Action::ASSIST_ASSEMBLE and not sit.crafting_fence) {
                d.task.item.amount = 0;
            } else {
                into->emplace_back<Action_Assemble>(d.task.item.item);
                return;
            }
        }
    } else if (d.task.type == Task::DELIVER_ITEM) {
        if (d.task.state == 0) {
            if (agent_goto(sit, d.task.where, agent, into)) {
                d.task.state = 1;
            } else {
                return;
            }
        }
        if (d.task.state == 1) {
            if (d.last_action == Action::DELIVER_JOB and not d.last_action_result) {
                --d.task.item.amount;
            } else {
                into->emplace_back<Action_Deliver_job>(sit.current_job);
                return;
            }
        }
    } else if (d.task.type == Task::CHARGE) {
        if (d.task.state == 0) {
            if (agent_goto(sit, d.task.where, agent, into)) {
                d.task.state = 1;
            } else {
                return;
            }
        }
        if (d.task.state == 1) {
            auto max_bat = world().roles[world().agents[agent].role].max_battery;
            if (d.last_action == Action::CHARGE and d.charge < max_bat) {
                d.task.item.amount = 0;
            } else {
                into->emplace_back<Action_Charge>();
                return;
            }
        }
    } else if (d.task.type == Task::VISIT) {
        if (agent_goto(sit, d.task.where, agent, into)) {
            d.task.item.amount = 0;
        } else {
            return;
        }
    } else {
        // TODO
        assert(false);
    }
    
    into->emplace_back<Action_Abort>();
    return;
}

void Mothership_complex::internal_simulation_step(u32 treeid) {
    auto sit = [this, treeid]() -> Situation& { return situation(tree(treeid)); };
    
    // Restock shops
    for (int i = 0; i < sit().shops.size(); ++i) {
        for (int j = 0; j < sit().shops[i].items.size(); ++j) {
            auto item = sit().shops[i].items[j];
            --item.restock;
            if (item.restock == 0) {
                ++item.amount;
                item.restock = world().shops[i].items[j].period;
            }
        }
    }

    // TODO Implement storage pricing

    // Execute agent actions
    for (int agent = 0; agent < agents_per_team; ++agent) {
        auto& d = sit().agents[agent];

        int action_offset = step_buffer.size();
        get_agent_action(sit(), agent, &step_buffer);
        u8 type = step_buffer.get<Action>(action_offset).type;
        d.last_action = type;
        if (type == Action::GOTO1) { 
            if (d.charge <= 0) {     
                d.last_action_result = Action::SUCCESSFUL;
                continue;
            }
            d.charge = std::max(d.charge - 100, 0);
        }
    }
}


u8 Mothership_complex::can_agent_do(Situation const& sit, u8 agent, Task task) {
    auto& s = world().agents[agent];
    auto& d = sit.agents[agent];
                                    
    if (task.type == Task::NONE) {  
        return task.item.amount;                
    } else if (task.type == Task::BUY_ITEM or task.type == Task::CRAFT_ITEM
               or task.type == Task::CRAFT_ASSIST) {
        auto vol = get_by_id<Product>(task.item.item)->volume;
        int max_count = vol > 0 ? (world().roles[s.role].max_load - d.load) / vol : 83726;
        return max_count;
    } else if (task.type == Task::DELIVER_ITEM) {
        for (auto const& i: d.items) {
            if (i.item == task.item.item) {
                return std::min(i.amount, task.item.amount);
            }
        }
        return 0;
    } else if (task.type == Task::CHARGE) {
        return task.item.amount;
    } else {
        assert(false);
        return 0;
    }
}

float Mothership_complex::task_heuristic(Situation const& sit, u8 agent, Task& task) {
    if (task.type == Task::BUY_ITEM) {
        u8 amount = can_agent_do(sit, agent, task);
        if (not amount) return -1.f;
        
        float best_shop_rating = 9e9;
        u8 best_shop = 0;
        for (int i = 0; i < sit.shops.size(); ++i) {
            u16 cost = 0;
            for (size_t j = 0; j < world().shops[i].items.size(); ++j) {
                if (world().shops[i].items[j].item == task.item.item) {
                    cost = world().shops[j].items[j].cost;
                    break;
                }
            }
            if (not cost) continue;
            float shop_rating = cost * task.item.amount
                + dist_cost(world().shops[i].pos, agent, sit);
            if (shop_rating < best_shop_rating) {
                best_shop_rating = shop_rating;
                best_shop = world().shops[i].name;
                task.where = best_shop;
            }
        }
        if (not best_shop) return -1.f;
        
        float rating = best_shop_rating;
        if (amount < task.item.amount) {
            rating += 500.f;
            task.item.amount = amount;
        }
        task.where = best_shop;
        return rating;
    } else if (task.type == Task::CRAFT_ITEM) {
        u8 amount = can_agent_do(sit, agent, task);
        if (not amount) return -1.f;

        float best_ws_rating = 9e9;
        u8 best_ws = 0;
        for (int i = 0; i < world().workshops.size(); ++i) {
            float ws_rating = task.item.amount * world().workshops[i].price
                + dist_cost(world().workshops[i].pos, agent, sit);
            if (ws_rating < best_ws_rating) {
                best_ws_rating = ws_rating;
                best_ws = i;
            }
        }
        assert(best_ws);

        float rating = best_ws_rating;
        if (amount < task.item.amount) {
            rating += 500.f;
            task.item.amount = amount;
        }
        task.where = best_ws;
        return rating;
    } else if (task.type == Task::CRAFT_ASSIST) {
        u8 amount = can_agent_do(sit, agent, task);
        if (not amount) return -1.f;

        float rating = dist_cost(get_by_id<Workshop>(task.where)->pos, agent, sit);
        if (amount < task.item.amount) {
            rating += 500.f;
            task.item.amount = amount;
        }
        return rating;
    } else if (task.type == Task::DELIVER_ITEM) {
        u8 amount = can_agent_do(sit, agent, task);
        if (not amount) return -1.f;

        float rating = dist_cost(get_by_id<Storage>(task.where)->pos, agent, sit);
        if (amount < task.item.amount) {
            rating += 500.f;
            task.item.amount = amount;
        }
        return rating;
    } else if (task.type == Task::VISIT) {
        return dist_cost(get_by_id<Facility>(task.where)->pos, agent, sit);
    } else {
        assert(false);
        return -1.f;
    }
}

bool Mothership_complex::refuel_if_needed(Situation& sit, u8 agent, u8 where) {
    Role const& role = world().roles[world().agents[agent].role];
    Pos p = get_by_id<Facility>(where)->pos;
    float reach = (sit.agents[agent].charge - 70) / 10 * role.speed * speed_conversion * 10.f/13.f;
    float dista = dist(p, sit.agents[agent].pos);
    if (reach < dista) {
        // Need refuel
        float best_rating = 9e9;
        u8 best_station = 0;
        for (auto const& chasta: world().charging_stations) {
            float rating = dist(chasta.pos, sit.agents[agent].pos) + dist(chasta.pos, p)
                + chasta.price * role.max_battery / chasta.rate;
            if (rating < best_rating) {
                best_rating = rating;
                best_station = chasta.name;
            }
        }
        sit.agents[agent].task = Task {Task::CHARGE, best_station, {0, 1}, 0};
        return true;
    }
    return false;
}

void Mothership_complex::heuristic_task_assign(Situation& sit) {
    if (sit.goals.size() and sit.goals[0].type == Task::CRAFT_ITEM) {
        // Resolve CRAFT_ITEM Tasks
        
        // Check whether all the tools are present
        Task task = sit.goals[0];        
        Product const& p = *get_by_id<Product>(task.item.item);
        bool everything_okay = true;
        for (Item_stack i: p.tools) {
            for (u8 agent = 0; agent < agents_per_team; ++agent) {
                for (Item_stack j: sit.agents[agent].items) {
                    if (i.item != j.item) continue;
                    if (i.amount > j.amount) {
                        i.amount -= j.amount;
                        break;
                    } else {  
                        i.amount = 0;
                        break;
                    }
                }
                if (i.amount == 0) break;
            }
            if (i.amount != 0) {
                // Not enough of this tool is there, create some
                add_req_tasks(sit, i);
                everything_okay = false;
            }
        }
        if (not everything_okay) return;
        
        for (Item_stack i: p.consumed) {
            for (u8 agent = 0; agent < agents_per_team; ++agent) {
                for (Item_stack j: sit.agents[agent].items) {
                    if (i.item != j.item) continue;
                    if (i.amount > j.amount) {
                        i.amount -= j.amount;
                        break;
                    } else {  
                        i.amount = 0;
                        break;
                    }
                }
                if (i.amount == 0) break;
            }
            if (i.amount != 0) {
                // Not enough of this item is there, but there should be something in production
                everything_okay = false;
                break;
            }
        }
        if (not everything_okay) return;

        // Find the agents and send them there. First, get the crafter
        for (u8 agent = 0; agent < agents_per_team; ++agent) {
            if (sit.agents[agent].task.item.amount) continue;
            u8 amount = can_agent_do(sit, agent, task);
            if (amount) {
                if (amount == task.item.amount) {
                    diff.remove(sit.goals, sit.goals.size() - 1);
                }
                task.item.amount = amount;
                task.state = 0xff;
                task_heuristic(sit, agent, task);
                sit.agents[agent].task = task;
                break;
            }
        }

        // Then, find the item providers
        for (Item_stack i: p.tools) {
            for (u8 agent = 0; agent < agents_per_team; ++agent) {
                if (sit.agents[agent].task.item.amount
                    and sit.agents[agent].task.state != 0xff) {
                    continue;
                }

                bool recruitment_flag = false;
                for (Item_stack j: sit.agents[agent].items) {
                    if (i.item != j.item) continue;
                    recruitment_flag = true;
                    if (i.amount > j.amount) {
                        i.amount -= j.amount;
                        break;
                    } else {  
                        i.amount = 0;
                        break;
                    }
                }
                if (recruitment_flag and sit.agents[agent].task.state != 0xff) {
                    sit.agents[agent].task = Task {Task::CRAFT_ASSIST, task.where, {0, 1}, 0xff};
                }

                if (i.amount == 0) break;
            }
            assert(i.amount == 0);
        }
        for (Item_stack i: p.consumed) {
            for (u8 agent = 0; agent < agents_per_team; ++agent) {
                if (sit.agents[agent].task.item.amount
                    and sit.agents[agent].task.state != 0xff) {
                    continue;
                }

                bool recruitment_flag = false;
                for (Item_stack j: sit.agents[agent].items) {
                    if (i.item != j.item) continue;
                    recruitment_flag = true;
                    if (i.amount > j.amount) {
                        i.amount -= j.amount;
                        break;
                    } else {
                        i.amount = 0;
                        break;
                    }
                }
                if (recruitment_flag and sit.agents[agent].task.state != 0xff) {
                    sit.agents[agent].task = Task {Task::CRAFT_ASSIST, task.where, {0, 1}, 0xff};
                }

                if (i.amount == 0) break;
            }
            assert(i.amount == 0);
        }

        for (u8 agent = 0; agent < agents_per_team; ++agent) {
            if (sit.agents[agent].task.state == 0xff) {
                sit.agents[agent].task.state = 0;
            }
        }
    } else {
        // Select for each task the agent that can do it the cheapest (or almost cheapest)
        for (int i = sit.goals.size() - 1; i >= 0; --i) {
            if (sit.goals[i].item.amount == 0) continue;
        
            Task task = sit.goals[i];
            float cheapest = 9e9;
            for (u8 agent = 0; agent < agents_per_team; ++agent) {
                Task temp = task;            
                float at_rating = task_heuristic(sit, agent, temp);
                if (at_rating > 0 and at_rating < cheapest) {
                    cheapest = at_rating;
                }
            }
            for (u8 agent = 0; agent < agents_per_team; ++agent) {
                if (refuel_if_needed(sit, agent, task.where)) continue;
                Task temp = task;            
                float at_rating = task_heuristic(sit, agent, temp);
                if (at_rating > 0 and at_rating < cheapest * 1.2
                        and sit.agents[agent].task.item.amount == 0) {
                    sit.agents[agent].task = temp;
                    sit.goals[i].item.amount -= temp.item.amount;
                    if (not sit.goals[i].item.amount) {
                        diff.remove(sit.goals, i);
                        break;                    
                    }
                }
            }
        }

    }
}
    
void Mothership_complex::add_req_tasks(Situation& sit, Item_stack i) {
    Product const* p2 = get_by_id<Product>(i.item);
    if (p2->assembled) {
        Task t2 {Task::CRAFT_ITEM, 0, i, 0};
        diff.add(sit.goals, t2);
        add_req_tasks(sit, t2);
    } else {
        Task t2 {Task::BUY_ITEM, 0, i, 0};
        diff.add(sit.goals, t2);
    }
}
    
void Mothership_complex::add_req_tasks(Situation& sit, Task task) {
    if (task.type == Task::CRAFT_ITEM) {
        Product const* prod = get_by_id<Product>(task.item.item);
        for (Item_stack i: prod->consumed) {
            i.amount *= task.item.amount;
            add_req_tasks(sit, i);
        }
    } else if (task.type == Task::DELIVER_ITEM) {
        add_req_tasks(sit, task.item);
    } else {
        return;
    }
}

void Mothership_complex::branch_tasks(u32 treeid, u8 agent, u8 depth) {
    int num_tasks = 0;
    auto sit = [this, treeid]() -> Situation& { return situation(tree(treeid)); };
    for (int i = sit().goals.size() - 1; i >= 0; --i) {
        if (can_agent_do(sit(), agent, sit().goals[i])) ++num_tasks;
    }

    if (num_tasks == 1) {
        int i;
        for (i = sit().goals.size() - 1; i >= 0; --i) {
            if (can_agent_do(sit(), agent, sit().goals[i])) break;
        }
        assert(i >= 0);
        sit().agents[agent].task = sit().goals[i];
        fast_forward(treeid, depth);
    }
    // TODO: unfinished
}

void Mothership_complex::branch_goals(u32 treeid, u8 depth) {
#if 0
    auto sit = [this, treeid]() -> Situation& { return situation(tree(treeid)); };
    assert(sit().goals.size() == 0);

    int sit_offset = (char*)&sit() - situation_buffer.data();
    int sit_length = situation_buffer.end() - (char*)&sit();
    tree(treeid).children.init(&step_buffer);
    s32 money_before = sit().team.money;
    s32 steps_before = sit().simulation_step;

    u32 new_pos = situation_buffer.size();
    for (auto const& job: sit().priced_jobs) {
        u32 child_tree = step_buffer.size();
        tree(treeid).children.push_back({new_pos, treeid, 0}, &step_buffer);
        
        std::memcpy(situation_buffer.data() + new_pos, &sit(), sit_length);
        fast_forward(child_tree, depth - 1);

        s32 money_after = situation(tree(treeid)).team.money;
        s32 steps_after = situation(tree(treeid)).simulation_step;
        tree(child_tree).rating = (float)(money_after - money_before) / (steps_after - steps_before);
    }
#endif
}

void Mothership_complex::fast_forward(u32 treeid, u8 depth) {
#if 0
    auto sit = [this, treeid]() -> Situation& { return situation(tree(treeid)); };

    // TODO: Check for off-by-ones
    while (sit().simulation_step < world().max_steps) {
        if (sit().goals.size() == 0) {
            if (depth != 0)
                branch_goals(treeid, depth - 1);
            return;
        }
        for (u8 i = 0; i < agents_per_team; ++i) {
            if (sit().agents[i].task.item.amount == 0) {
                if (depth != 0)
                    branch_tasks(treeid, i, depth - 1);
                return;
            }
        }

        internal_simulation_step(treeid);
    }
#endif
}


void Mothership_complex::select_diff_situation(Situation const& s) {
    diff.reset();

    // Fix the monotinic thingy
    for (auto const& i: s.agents) {
        diff.register_arr(i.items);
        diff.register_arr(i.route);
        jdbg < i,0;
    }

    diff.register_arr(s.charging_stations);
    diff.register_arr(s.shops);
    diff.register_arr(s.storages);
    diff.register_arr(s.auction_jobs);
    diff.register_arr(s.priced_jobs); 
    diff.register_arr(s.goals);

    for (auto const& i: s.shops) {
        diff.register_arr(i.items);
    }
    for (auto const& i: s.storages) {
        diff.register_arr(i.items);
    }
    for (auto const& i: s.auction_jobs) {
        diff.register_arr(i.items);
    }
    for (auto const& i: s.priced_jobs) {
        diff.register_arr(i.items);
    }
}

void Mothership_complex::on_request_action() {
    Situation& sit = situation();
    select_diff_situation(sit);
    
    if (sit.simulation_step == 0) {
        for (auto const& shop: world().shops) {
            diff.add(sit.goals, Task {Task::VISIT, shop.name, {0, 1}, 0});
        }
    }
    
    if (sit.goals.size() == 0) {
        // Select the first job
        if (sit.priced_jobs.size() > 0) {
            Job_priced const& job = sit.priced_jobs[0];
            sit.current_job = job.id;
            for (Job_item item: job.items) {
                Task task {Task::DELIVER_ITEM, job.storage, item, 0}; 
                diff.add(sit.goals, task);
                add_req_tasks(sit, task);
            }
        }
    }
    diff.apply();

    // Assign the jobs to the agents
    heuristic_task_assign(sit);
}

void Mothership_complex::post_request_action(u8 agent, Buffer* into) {
    get_agent_action(situation(), agent, into);
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
		ti_stack.push_back(team_items);
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
			team_items = ti_stack.back();
		}
		ti_stack.pop_back();
		if (bj == 0xffff) break;
		job_positions[n] = bj;
		// side effect: remove items from team_items
		r += rate_job(s.priced_jobs[bj]);
	}

	return r;
}
} /* end of namespace jup */

