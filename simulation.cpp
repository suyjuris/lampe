#include "simulation.hpp"

namespace jup {

static constexpr Params params;

#define COPY_ARRAY(obj, buf, name) \
    name.init(obj.name, (buf));
#define COPY_SUBARR(obj, buf, name, sub) \
    for (int i = 0; i < obj.name.size(); ++i) \
        name[i].sub.init(obj.name[i].sub, buf);


World::World(Simulation const& s0, Graph const* graph, Buffer* containing):
    team{s0.team},
    seed_capital{s0.seed_capital},
    steps{s0.steps},
    graph{graph}
{
    assert(graph);
    assert(containing and containing->inside(this));    

    COPY_ARRAY (s0, containing, items);
    COPY_SUBARR(s0, containing, items, consumed);
    COPY_SUBARR(s0, containing, items, tools);

    roles.init(number_of_agents, containing);
}

void World::update(Simulation const& s, u8 id, Buffer* containing) {
    assert(containing and containing->inside(this));
    
    assert(0 <= id and id < number_of_agents);
    roles[id].name = s.role.name;
    roles[id].speed = s.role.speed;
    roles[id].battery = s.role.battery;
    roles[id].load = s.role.load;
    roles[id].tools.init(s.role.tools, containing);
}

Situation::Situation(Percept const& p0, Bookkeeping const* book_old, Buffer* containing):
    initialized{true},
    simulation_step{p0.simulation_step},
    team_money{p0.team_money}
{
    assert(containing and containing->inside(this));

    COPY_ARRAY (p0, containing, entities);
    COPY_ARRAY (p0, containing, charging_stations);
    COPY_ARRAY (p0, containing, dumps);
    COPY_ARRAY (p0, containing, shops);
    COPY_SUBARR(p0, containing, shops, items);
    COPY_ARRAY (p0, containing, storages);
    COPY_SUBARR(p0, containing, storages, items);
    COPY_ARRAY (p0, containing, workshops);
    COPY_ARRAY (p0, containing, resource_nodes);
    COPY_ARRAY (p0, containing, auctions);
    COPY_SUBARR(p0, containing, auctions, required);
    COPY_ARRAY (p0, containing, jobs);
    COPY_SUBARR(p0, containing, jobs, required)
    COPY_ARRAY (p0, containing, missions);
    COPY_SUBARR(p0, containing, missions, required);
    COPY_ARRAY (p0, containing, posteds);
    COPY_SUBARR(p0, containing, posteds, required);

    book.delivered.init(containing);
    if (book_old) {
        for (auto i: book_old->delivered) {
            // Ignore the ones there are no jobs for, these jobs have been completeted or they have
            // expired.
            if (find_by_id(jobs, i.job_id)) {
                book.delivered.push_back(i, containing);
            }
        }
    }
}
    
#undef COPY_ARRAY
#undef COPY_SUBARR

void Bookkeeping::add_item_to_job(u16 job_id, Item_stack item, Diff_flat_arrays* diff) {
    for (Job_item& i: delivered) {
        if (i.job_id != job_id) continue;
        if (i.item.id != item.id) continue;
        i.item.amount += item.amount;
        return;
    }
    diff->add(delivered, {job_id, item});
}

void Situation::update(Percept const& p, u8 id, Buffer* containing) {
    assert(containing and containing->inside(this));
    
    assert(0 <= id and id < number_of_agents);
    selves[id].charge = p.self.charge;
    selves[id].load = p.self.load;
    selves[id].action_type = p.self.action_type;
    selves[id].action_result = p.self.action_result;
    selves[id].items.init(p.self.items, containing);
}

void Situation::register_arr(Diff_flat_arrays* diff) {
    assert(diff);
    
    diff->register_arr(entities);
    diff->register_arr(charging_stations);
    diff->register_arr(dumps);
    diff->register_arr(shops);
    for (auto& i: shops) diff->register_arr(i.items);
    diff->register_arr(storages);
    for (auto& i: storages) diff->register_arr(i.items);
    diff->register_arr(workshops);
    diff->register_arr(resource_nodes);
    diff->register_arr(auctions);
    for (auto& i: auctions) diff->register_arr(i.required);
    diff->register_arr(jobs);
    for (auto& i: jobs) diff->register_arr(i.required);
    diff->register_arr(missions);
    for (auto& i: missions) diff->register_arr(i.required);
    diff->register_arr(posteds);
    for (auto& i: posteds) diff->register_arr(i.required);
    diff->register_arr(book.delivered);
}

bool Situation::agent_goto(u8 where, u8 agent, Buffer* into) {
    auto& d = self(agent);

    // Could send continue if same location as last time
    if (d.facility == where) {
        return true;
    } else {
        into->emplace_back<Action_Goto1>(where);
        return false;
    }
}

void Situation::get_action(World const& world, Situation const& old, u8 agent, Buffer* into, Diff_flat_arrays* diff) {
    assert(into);

    auto& d = self(agent);
    while (true) {
        auto& t = task(agent);

        bool move_on = false;

        if (t.task.type == Task::NONE) {
            into->emplace_back<Action_Abort>();
            return;
        } else if (t.task.type == Task::BUY_ITEM) {
            if (d.task_state == 0) {
                if (agent_goto(t.task.where, agent, into)) {
                    d.task_state = 1;
                } else {
                    return;
                }
            }

            if (d.task_state == 1) {
                if (d.action_type == Action::BUY and not d.action_result) {
                    --t.task.item.amount;
                } 
                if (t.task.item.amount == 0) {
                    move_on = true;
                } else {
                    into->emplace_back<Action_Buy>(t.task.item);
                    return;
                }
            }
        } else if (t.task.type == Task::CRAFT_ITEM) {
            // TODO Consider the task_index when crafting, maybe add unique assembly id
            if (d.task_state == 0) {
                if (agent_goto(t.task.where, agent, into)) {
                    d.task_state = 1;
                } else {
                    return;
                }
            }

            if (d.task_state == 1) {
                if (d.action_type == Action::ASSEMBLE and not d.action_result) {
                    --t.task.item.amount;
                } 
                if (t.task.item.amount == 0) {
                    move_on = true;
                } else {
                    into->emplace_back<Action_Assemble>(t.task.item.item);
                    return;
                }
            }
        } else if (t.task.type == Task::CRAFT_ASSIST) {
            // TODO Consider the task_index when crafting, maybe add unique assembly id
            if (d.task_state == 0) {
                if (agent_goto(t.task.where, agent, into)) {
                    d.task_state = 1;
                } else {
                    return;
                }
            }

            if (d.task_state == 1) {
                if (d.action_type == Action::ASSIST_ASSEMBLE and not d.action_result) {
                    --t.task.item.amount;
                } 
                if (t.task.item.amount == 0) {
                    move_on = true;
                } else {
                    into->emplace_back<Action_Assist_assemble>(t.task.crafter_id);
                    return;
                }
            }
        } else if (t.task.type == Task::DELIVER_ITEM) {
            if (d.task_state == 0) {
                if (agent_goto(t.task.where, agent, into)) {
                    d.task_state = 1;
                } else {
                    return;
                }
            }

            if (d.task_state == 1) {
                if (d.action_type == Action::DELIVER_JOB and not d.action_result) {
                    for (auto old_item: old.self(agent).items) {
                        if (auto new_item = find_by_id(d.items, old_item.id)) {
                            assert(old_item.amount >= new_item->amount);
                            if (old_item.amount > new_item->amount) {
                                Item_stack deliv {old_item.id, (u8)(old_item.amount - new_item->amount)};
                                book.add_item_to_job(t.task.job_id, deliv, diff);
                            }
                        } else if (old_item.amount > 0) {
                            book.add_item_to_job(t.task.job_id, old_item, diff);
                        }
                    }
                    
                    move_on = true;
                } else {
                    into->emplace_back<Action_Deliver_job>(t.task.job_id);
                    return;
                }
            }
        } else if (t.task.type == Task::CHARGE) {
            if (d.task_state == 0) {
                if (agent_goto(t.task.where, agent, into)) {
                    d.task_state = 1;
                } else {
                    return;
                }
            }

            if (d.task_state == 1) {
                auto max_bat = world.roles[agent].battery;
                if (d.action_type == Action::CHARGE and d.charge < max_bat) {
                    move_on = true;
                } else {
                    into->emplace_back<Action_Charge>();
                    return;
                }
            }
        } else if (t.task.type == Task::VISIT) {
            if (agent_goto(t.task.where, agent, into)) {
                move_on = true;
            } else {
                return;
            }
        } else {
            // TODO
            assert(false);
        }

        assert(move_on);

        ++d.task_index;
    }
}

Pos Situation::find_pos(u8 id) const {
    for (auto const& i: charging_stations) {
        if (i.id == id) return i.pos;
    }
    for (auto const& i: dumps) {
        if (i.id == id) return i.pos;
    }
    for (auto const& i: shops) {
        if (i.id == id) return i.pos;
    }
    for (auto const& i: storages) {
        if (i.id == id) return i.pos;
    }
    for (auto const& i: workshops) {
        if (i.id == id) return i.pos;
    }
    assert(false);
}

void Situation::agent_goto_nl(World const& world, u8 agent, u8 target_id) {
    if (self(agent).facility == target_id) {
        return;
    }
    auto& d = self(agent);

    Pos target = find_pos(target_id);
    auto p1 = world.graph->pos(d.pos);
    auto p2 = world.graph->pos(target);
    u32 dist = world.graph->dijkstra(p1, p2);

    auto speed = world.roles[agent].speed;
    
    if (dist > (d.charge / 10) * speed) {
        task(agent).result.err = Task_result::OUT_OF_BATTERY;
        d.task_sleep = 0xff;
    } else {
        d.charge -= (dist / speed) * 10;
        d.task_sleep = dist / speed;
    }
}

void Situation::task_update(World const& world, u8 agent, Diff_flat_arrays* diff) {
    //@Incomplete Care about carrying capacity.
    assert(diff);

    auto& d = self(agent);
    auto& t = task(agent);

    if (t.task.type == Task::NONE) {
        d.task_sleep = 255;
        return;
    } else if (t.task.type == Task::BUY_ITEM) {
        if (d.task_state == 0) {
            agent_goto_nl(world, agent, t.task.where);
            d.task_state = 1;
        }
        if (d.task_state == 1 and d.task_sleep == 0) {
            auto& shop = get_by_id(shops, t.task.where);
            auto& item = get_by_id(shop.items, t.task.item.item);

            d.task_state = 0xff;
            if (item.amount < t.task.item.amount) {
                d.task_sleep = shop.restock * t.task.item.amount;
            } else {
                d.task_sleep = 1;
            }
            team_money -= t.task.item.amount * item.cost;
            item.amount -= t.task.item.amount;
            if (auto d_item = find_by_id(d.items, t.task.item.item)) {
                d_item->amount += t.task.item.amount;
            } else {
                diff->add(d.items, t.task.item);
            }
            return;
        }
    } else if (t.task.type == Task::CRAFT_ITEM) {
        if (d.task_state == 0) {
            agent_goto_nl(world, agent, t.task.where);
            d.task_state = 1;
        }

        auto is_possible = [&](bool apply_err) -> bool {
            Item const& item = get_by_id(world.items, t.task.item.id);
            for (Item_stack i: item.consumed) {
                int count = i.amount * t.task.item.amount;
                for (u8 o_agent = 0; o_agent < number_of_agents; ++o_agent) {
                    auto const& o_d = self(o_agent);
                    auto const& o_t = task(o_agent);
                    if (
                        o_agent != agent and (
                            o_t.task.type != Task::CRAFT_ASSIST
                            or o_t.task.crafter_id != agent
                            or o_d.task_state != 2
                        )
                    ) continue;

                    if (auto j = find_by_id(o_d.items, i.item)) {
                        count -= j->amount;
                        if (count <= 0) break;
                    }
                }
                if (count > 0) {
                    if (apply_err) {
                        t.result.err = Task_result::CRAFT_NO_ITEM;
                        t.result.err_arg = {i.item, narrow<u8>(count)};
                    }
                    return false;
                }
            }
            
            for (u8 i: item.tools) {
                bool found = false;
                for (u8 o_agent = 0; o_agent < number_of_agents; ++o_agent) {
                    auto const& o_d = self(o_agent);
                    auto const& o_t = task(o_agent);
                    if (
                        o_agent != agent and (
                            o_t.task.type != Task::CRAFT_ASSIST
                            or o_t.task.crafter_id != agent
                            or o_d.task_state != 2
                            or not world.roles[o_agent].tools.count(i)
                        )
                    ) continue;

                    if (auto j = find_by_id(o_d.items, i)) {
                        if (j->amount > 0) {
                            found = true;
                            break;
                        }
                    }
                }
                if (not found) {
                    if (apply_err) {
                        t.result.err = Task_result::CRAFT_NO_TOOL;
                        t.result.err_arg = {i, 1};
                    }
                    return false;
                }
            }
            
            return true;
        };

        if (d.task_state == 1 and d.task_sleep == 0) {
            if (not is_possible(false)) {
                d.task_state = 2;
                d.task_sleep = params.craft_max_wait;
                return;
            }
        } else if (d.task_state == 2 and d.task_sleep == 0) {
            // We have come here by waking up from the craft_max_wait, call is_possible again to set
            // the error and abort.
            if (not is_possible(true)) {
                d.task_state = 3; // ensure that we are not waked up
                d.task_sleep = 0xff;
                return;
            } else {
                // Should be impossible, but meh
            }
        }
        
        if ((d.task_state == 1 or d.task_state == 2) and d.task_sleep == 0) {
            // is_possible is true here, so just execute the assembly

            Item const& item = get_by_id(world.items, t.task.item.item);
            for (Item_stack i: item.consumed) {
                int count = i.amount * t.task.item.amount;
                for (u8 o_agent = 0; o_agent < number_of_agents; ++o_agent) {
                    auto& o_d = self(o_agent);
                    auto& o_t = task(o_agent);
                    if (
                        o_agent != agent and (
                            o_t.task.type != Task::CRAFT_ASSIST
                            or o_t.task.crafter_id != agent
                            or o_d.task_state != 2
                        )
                    ) continue;

                    if (auto j = find_by_id(o_d.items, i.item)) {
                        u8 rem = narrow<u8>(std::min((int)j->amount, count));
                        count -= rem;
                        j->amount -= rem;
                        if (count <= 0) break;
                    }
                }
            }
            
            d.task_state = 0xff;
            d.task_sleep = t.task.item.amount;
            if (auto d_item = find_by_id(d.items, t.task.item.item)) {
                d_item->amount += t.task.item.amount;
            } else {
                diff->add(d.items, t.task.item);
            }
            
            for (u8 o_agent = 0; o_agent < number_of_agents; ++o_agent) {
                auto const& o_d = self(o_agent);
                auto const& o_t = task(o_agent);
                if (
                    o_t.task.type != Task::CRAFT_ASSIST
                    or o_t.task.crafter_id != agent
                    or o_d.task_state != 2
                ) continue;
                self(o_agent).task_state = 0xff;
                self(o_agent).task_sleep = d.task_sleep;
            }
            return;
        }
    } else if (t.task.type == Task::CRAFT_ASSIST) {
        if (d.task_state == 0) {
            agent_goto_nl(world, agent, t.task.where);
            d.task_state = 1;
        }
        if (d.task_state == 1 and d.task_sleep == 0) {
            d.task_sleep = 0xff;
            d.task_state = 2;
            
            bool found = false;
            for (u8 i = 0; i < planning_max_tasks; ++i) {
                if (task(t.task.crafter_id).task.type == Task::CRAFT_ITEM) {
                    found = true;
                    break;
                }
            }
            if (not found) {
                t.result.err = Task_result::NO_CRAFTER_FOUND;
                d.task_sleep = 0xff;
                return;
            }
        }
        // d.task_state == 2 is handled externally by the crafter
    } else if (t.task.type == Task::DELIVER_ITEM) {
        if (d.task_state == 0) {
            agent_goto_nl(world, agent, t.task.where);
            d.task_state = 1;
        }
        if (d.task_state == 1 and d.task_sleep == 0) {
            auto task_item = t.task.item;
            auto item = find_by_id(d.items, task_item.id);
            if (not item or item->amount < task_item.amount) {
                task_item.amount -= item ? item->amount : 0;
                t.result.err = Task_result::NOT_IN_INVENTORY;
                t.result.err_arg = task_item;
                d.task_sleep = 0xff;
                return;
            }
            
            Job* job = find_by_id(jobs, t.task.job_id);
            u8 job_type = 0;
            if (not job) {
                job = find_by_id(auctions, t.task.job_id);
                job_type = 1;
            }
            if (not job) {
                job = find_by_id(missions, t.task.job_id);
                job_type = 2;
            }
            if (not job) {
                t.result.err = Task_result::NO_SUCH_JOB;
                d.task_sleep = 0xff;
                return;
            }

            bool flag = true;
            
            // While the task has a specific item to be delivered, other items may be delivered,
            // too. This should not pose a problem, and the single-item case models most realistic
            // cases better.
            for (auto j_item: job->required) {
                u8 already = 0;
                for (auto i: book.delivered) {
                    if (i.job_id == t.task.job_id and i.item.id == j_item.id) {
                        already = i.item.amount;
                        break;
                    }
                }
                if (already < j_item.amount) {
                    if (auto a_item = find_by_id(d.items, j_item.id)) {
                        if (a_item->amount > 0) {
                            Item_stack deliv {j_item.id, std::min((u8)(j_item.amount - already), a_item->amount)};
                            book.add_item_to_job(t.task.job_id, deliv, diff);
                            a_item->amount -= deliv.amount;
                            already += deliv.amount;
                        }
                    }
                }
                if (already < j_item.amount) {
                    flag = false;
                }
            }
            // @Incomplete Handle useless deliveries.

            // Check whether the job is completed.
            if (flag) {
                for (auto const& i: book.delivered) {
                    if (i.job_id == t.task.job_id) {
                        diff->remove_ptr(book.delivered, &i);
                    }
                }
                if (job_type == 0) {
                    diff->remove_ptr(jobs, job);
                } else if (job_type == 1) {
                    diff->remove_ptr(auctions, (Auction*)job);
                } else if (job_type == 2) {
                    diff->remove_ptr(missions, (Mission*)job);
                } else {
                    assert(false);
                }
                team_money += job->reward;
            }
            
            d.task_sleep = 1;
            d.task_state = 0xff;
            return;
        }
    } else if (t.task.type == Task::CHARGE) {
        if (d.task_state == 0) {
            agent_goto_nl(world, agent, t.task.where);
            d.task_state = 1;
        }
        if (d.task_state == 1 and d.task_sleep == 0) {
            auto& station = get_by_id(charging_stations, t.task.where);
            d.task_sleep = (world.roles[agent].battery - d.charge) / station.rate;
            d.task_state = 0xff;
        }
    }
        
}

void Simulation_state::init(World* world_, Buffer* sit_buffer_, int sit_offset_, int sit_size_) {
    assert(world_ and sit_buffer_);
    world = world_;
    diff.init(sit_buffer_);
    orig_offset = sit_offset_;
    orig_size = sit_size_;
    sit_offset = sit_buffer_->size();
}

void Simulation_state::reset() {
    // Copy the original into the working space
    buf().resize(sit_offset + orig_size);
    std::memcpy(&sit(), &orig(), orig_size);
    diff.reset();
    sit().register_arr(&diff);
}

template <typename Job_t>
void jobs_update(Flat_array<Job_t> const& jobs, Diff_flat_arrays* diff) {
    assert(diff);
}

void Simulation_state::fast_forward(int max_step) {
    u8 sleep_old = 0;
    while (sit().simulation_step < max_step) {
        u8 sleep_min = (u8)std::min(0xff, max_step - sit().simulation_step);
        for (u8 agent = 0; agent < number_of_agents; ++agent) {
            auto& d = sit().self(agent);
            if (d.task_sleep != 0xff) {
                d.task_sleep -= sleep_old;
            }
            if (d.task_sleep) continue;
            
            sit().task_update(*world, agent, &diff);
            sleep_min = std::min(sleep_min, d.task_sleep);
            if (d.task_state == 0xff) {
                sit().task(agent).result.time = sit().simulation_step + d.task_sleep;
                sit().task(agent).result.err = Task_result::SUCCESS;
                
                ++d.task_index;
                d.task_state = 0;
                // Do not reset sleep, some tasks sleep upon completion
            }

            // Wakeup crafter on entering CRAFT_ASSIST
            auto const& t = sit().strategy.task(agent, 0).task;
            if (
                t.type == Task::CRAFT_ASSIST
                and d.task_state == 2
                and t.crafter_id < agent
                and sit().self(t.crafter_id).task_state == 2
            ) {
                sit().self(t.crafter_id).task_sleep = 1;
                sleep_min = 1;
            }        
        }

        diff.apply();

        assert(sleep_min > 0);
        sit().simulation_step += sleep_min;

        // Update jobs
        for (Job const& job: sit().jobs) {
            if (job.end < sit().simulation_step) {
                for (auto const& i: sit().book.delivered) {
                    if (i.job_id == job.id) {
                        diff.remove_ptr(sit().book.delivered, &i);
                    }
                }
                diff.remove_ptr(sit().jobs, &job);
            }
        }
        //@Incomplete Missions and Auctions
        
        sleep_old = sleep_min;
    }
    assert(sit().simulation_step == max_step);
}

void Simulation_state::add_charging(u8 agent, u8 before) {
    auto& s = orig().strategy;
    u8 index;
    if (s.task(agent, before).task.type == Task::CHARGE) {
        std::swap(s.task(agent, before - 1), s.task(agent, before));
        index = before - 1;
    } else {
        for (int i = planning_max_tasks - 1; i > before; --i) {
            s.task(agent, i).task = s.task(agent, i-1).task;
        }
        index = before;
    }

    Pos from;
    if (index > 0) {
        from = orig().find_pos(s.task(agent, index - 1).task.where);
    } else {
        from = orig().self(agent).pos;
    }
    Pos to = orig().find_pos(s.task(agent, index + 1).task.where);
    auto from_g = world->graph->pos(from);
    auto to_g   = world->graph->pos(to  );

    u32 min_dist = std::numeric_limits<u32>::max();
    u8 min_arg;
    for (auto& i: orig().charging_stations) {
        auto pos_g = world->graph->pos(i.pos);
        u32 d = world->graph->dijkstra(from_g, pos_g)
              + world->graph->dijkstra(pos_g, to_g);
        // TODO: Respect charging time
        
        if (d < min_dist) {
            min_dist = d;
            min_arg = i.id;
        }
    }
    
    s.task(agent, index).task = Task {Task::CHARGE, min_arg};
}

s8 task_item_diff(World const& world, Task const& task, Task_result const& result, u8 item_id) {
    if (task.type == Task::NONE or task.type == Task::CHARGE or task.type == Task::VISIT) {
        return 0;
    } else if (task.type == Task::BUY_ITEM) {
        if (result.err or task.item.id != item_id) {
            return 0;
        } else {
            return task.item.amount;
        }
    } else if (task.type == Task::CRAFT_ITEM or task.type == Task::CRAFT_ASSIST) {
        if (result.err) { return 0; }
        
        if (task.item.id == item_id) {
            return task.item.amount;
        }
        
        Item const& item = get_by_id(world.items, task.item.item);
        for (Item_stack i: item.consumed) {
            if (i.id == item_id) return - i.amount;
        }
        
        return 0;
    } else if (task.type == Task::DELIVER_ITEM) {
        if (result.err) { return 0; }
        
        if (task.item.id == item_id) {
            return -task.item.amount;
        }
        
        return 0;
    } else {
        assert(false);
    }
}

struct Viable_t {
    u8 agent;
    u8 index;
    bool only_move;
};

void Simulation_state::add_item_for(u8 for_agent, u8 for_index, Item_stack for_item, bool for_tool) {
    auto& s = orig().strategy;

    u8 viable_count = 0;
    Viable_t viables[number_of_agents];

    u8 for_time = for_index > 0 ? sit().strategy.task(for_agent, for_index - 1).result.time : 0;

    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        if (for_tool and not world->roles[agent].tools.count(for_item.id)) {
            // agent cannot handle the tool
            continue;
        }

        // When would we need to do this?
        u8 index;
        for (index = 0; index + 1 < planning_max_tasks; ++index) {
            u8 t = sit().strategy.task(agent, index).result.time
                + params.shop_assume_duration;
            if (t > for_time) break;
        }
        
        // Does the agent have the item already?
        s8 count = 0;
        if (auto item = find_by_id(orig().self(agent).items, for_item.id)) {
            count = item->amount;
        }
        
        for (u8 i = 0; i < index; ++i) {
            count += task_item_diff(
                *world,
                s.task(agent, i).task,
                sit().strategy.task(agent, i).result,
                for_item.id
            );
        }

        bool flag;
        if (for_tool) {
            flag = count > 0;
        } else {
            // Check whether the items have not been used during the simulation
            // TODO Remove the last iteration from this count maybe
            if (auto item = find_by_id(sit().self(agent).items, for_item.id)) {
                flag = count > 0 and item->amount > 0;
            } else {
                flag = false;
            }
        }
        viables[viable_count++] = {agent, index, flag};
    }

    if (viable_count == 0) return;

    auto way = viables[jrand() % viable_count];
    u8 agent = way.agent;
    u8 index = way.index;

    if (way.only_move) {
        for (int i = planning_max_tasks - 1; i > index; --i) {
            s.task(agent, i) = s.task(agent, i-1);
        }
        s.task(agent, index).task = {
            Task::CRAFT_ASSIST,
            s.task(for_agent, for_index).task.where,
            s.task(for_agent, for_index).task.item
        };

        s.task(agent, index).task.crafter_id = for_agent;
    } else {
        Pos from;
        if (index > 0) {
            from = orig().find_pos(s.task(agent, index - 1).task.where);
        } else {
            from = orig().self(agent).pos;
        }
        Pos to = orig().find_pos(s.task(for_agent, for_index).task.where);
        auto from_g = world->graph->pos(from);
        auto to_g   = world->graph->pos(to  );

        u32 min_dist = std::numeric_limits<u32>::max();
        u8 min_arg = 0;
        for (auto const& i: orig().shops) {
            if (auto j = find_by_id(i.items, for_item.id)) {
                if (j->amount < for_item.amount) continue;
            } else {
                continue;
            }
            
            auto pos_g = world->graph->pos(i.pos);
            u32 d = world->graph->dijkstra(from_g, pos_g)
                + world->graph->dijkstra(pos_g, to_g);
            // TODO: Respect costs
        
            if (d < min_dist) {
                min_dist = d;
                min_arg = i.id;
            }
        }
        if (min_arg == 0) return;

        for (int i = planning_max_tasks - 1; i > index + 1; --i) {
            s.task(agent, i) = s.task(agent, i-2);
        }
        s.task(agent, index).task = {Task::BUY_ITEM, min_arg, for_item};
        s.task(agent, index + 1).task = {
            Task::CRAFT_ASSIST,
            s.task(for_agent, for_index).task.where,
            s.task(for_agent, for_index).task.item
        };
        s.task(agent, index + 1).task.crafter_id = for_agent;
    }
}

void Simulation_state::remove_task(u8 agent, u8 index) {
    auto& s = orig().strategy;
    for (int i = index; i < planning_max_tasks; ++i) {
        s.task(agent, i) = s.task(agent, i+1);
    }
}

void Simulation_state::fix_errors(int max_step, int max_iterations) {
    for (int it = 0; it < 16; ++it) {
        reset();
        fast_forward(max_step);

        for (u8 agent = 0; agent < number_of_agents; ++agent) {
            auto& d = orig().self(agent);
            if (d.task_index == planning_max_tasks) continue;
            auto& t = orig().strategy.task(agent, d.task_index);

            if (t.result.err == Task_result::SUCCESS) {
                continue;
            } else if (t.result.err == Task_result::OUT_OF_BATTERY) {
                add_charging(agent, d.task_index);
            } else if (t.result.err == Task_result::CRAFT_NO_ITEM) {
                add_item_for(agent, d.task_index, t.result.err_arg, false);
            } else if (t.result.err == Task_result::CRAFT_NO_TOOL) {
                add_item_for(agent, d.task_index, t.result.err_arg, true);
            } else if (t.result.err == Task_result::NO_CRAFTER_FOUND) {
                remove_task(agent, d.task_index);
            } else if (t.result.err == Task_result::NOT_IN_INVENTORY) {
                add_item_for(agent, d.task_index, t.result.err_arg, false );
            } else if (t.result.err == Task_result::NOT_VALID_FOR_JOB) {
                remove_task(agent, d.task_index);
            } else if (t.result.err == Task_result::NO_SUCH_JOB) {
                remove_task(agent, d.task_index);
            } else {
                assert(false);
            }
            break;
        }
    }
}

Internal_simulation::Internal_simulation(Game_statistic const& stat) {
	sim_buffer.emplace_back<Simulation_information>();
	d().items.init(stat.items, &sim_buffer);
	d().roles.init(stat.roles, &sim_buffer);
	u8 i = 0;
	for (Role const& r : stat.roles) {
		d().roles[i++].tools.init(r.tools, &sim_buffer);
	}
	d().charging_stations.init(stat.charging_stations, &sim_buffer);
	d().dump_locations.init(stat.dumps, &sim_buffer);
	d().shops.init(stat.shops, &sim_buffer);
	i = 0;
	for (Shop const& s : stat.shops) {
		d().shops[i++].items.init(s.items, &sim_buffer);
	}
	d().storages.init(stat.storages, &sim_buffer);
	d().workshops.init(stat.workshops, &sim_buffer);
	d().auction_jobs.init(stat.auctions, &sim_buffer);
	i = 0;
	for (Auction const& j : stat.auctions) {
		d().auction_jobs[i++].required.init(j.required, &sim_buffer);
	}
	d().priced_jobs.init(stat.jobs, &sim_buffer);
	i = 0;
	for (Job const& j : stat.jobs) {
		d().auction_jobs[i++].required.init(j.required, &sim_buffer);
	}
	d().agents.init(&sim_buffer);
	for (Entity const& e : stat.agents) {
		Agent& a = d().agents.emplace_back(&sim_buffer);
		a.name = e.name;
		a.pos = e.pos;
		u8 i = 0;
		for (Role const& r : d().roles) {
			if (r.name == e.role) a.role_index = i;
			++i;
		}
		Role const& r = d().roles[a.role_index];
		a.charge = r.battery;
		a.load = 0;
		a.team = e.team;
	}
	agent_count = d().agents.size();
}

void Internal_simulation::on_sim_start(Buffer* into) {
	for (u8 i = 0; i < agent_count; ++i) {
		Simulation& sim = into->emplace_back<Simulation>();
		Agent const& a = d().agents[i];
		sim.id = a.name;
		sim.items.init(d().items, into);
		sim.role = d().roles[a.role_index];
		sim.role.tools.init(d().roles[a.role_index].tools, into);
		sim.seed_capital = d().seed_capital;
		sim.steps = d().steps;
		sim.team = a.team;
	}
	step = 0;
}

void Internal_simulation::pre_request_action(Buffer* into) {
	++step;
	for (u8 i = 0; i < agent_count; ++i) {
		Percept& perc = into->emplace_back<Percept>();
		perc.id = i;
		perc.simulation_step = step;
		//perc.auction_jobs = ...
		//perc.priced_jobs = ...
		perc.charging_stations.init(d().charging_stations, into);
		perc.dumps.init(d().dump_locations, into);
		perc.shops.init(d().shops, into);
		u8 j = 0;
		for (Shop const& s : d().shops) {
			perc.shops[j++].items.init(s.items, into);
		}
		perc.storages.init(d().storages, into);
		j = 0;
		for (Storage const& s : d().storages) {
			perc.storages[j++].items.init(s.items, into);
		}
		perc.workshops.init(d().workshops, into);
		perc.deadline = time(nullptr) + 4;
		perc.entities.init(into);
		for (Agent const& a : d().agents) {
			Entity& e = perc.entities.emplace_back(into);
			e.name = a.name;
			e.pos = a.pos;
			e.role = a.role_index;
			e.team = a.team;
		}
		Agent const& a = d().agents[i];
		perc.self = a;
	}
}

void Internal_simulation::post_reqest_action(Flat_list<Action> actions) {
	u8 i = 0;
	for (Action const& ac : actions) {
		switch (ac.type) {

		}
		++i;
	}
}

} /* end of namespace jup */
