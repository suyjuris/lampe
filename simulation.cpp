#include "simulation.hpp"

#include "debug.hpp"

namespace jup {

#define SIZE_ARRAY(obj, buf, name) \
    size += obj.name.extra_space(obj.name.size());
#define SIZE_SUBARR(obj, buf, name, sub) \
    for (int i = 0; i < obj.name.size(); ++i) \
        size += obj.name[i].sub.extra_space(obj.name[i].sub.size());
#define COPY_ARRAY(obj, buf, name) \
    this_->name.init(obj.name, (buf));
#define COPY_SUBARR(obj, buf, name, sub) \
    for (int i = 0; i < obj.name.size(); ++i) \
        this_->name[i].sub.init(obj.name[i].sub, buf);


World::World(Simulation const& s0, Graph* graph, Buffer* containing):
    team{s0.team},
    seed_capital{s0.seed_capital},
    steps{s0.steps},
    graph{graph}
{
    assert(graph);
    assert(containing and containing->inside(this));

    int this_offset = (char*)this - containing->begin();
    int size = 0;
    SIZE_ARRAY (s0, containing, items);
    SIZE_SUBARR(s0, containing, items, consumed);
    SIZE_SUBARR(s0, containing, items, tools);
    
    size += roles.extra_space(number_of_agents);
    auto guard = containing->reserve_guard(size);
    auto this_ = &containing->get<World>(this_offset);

    COPY_ARRAY (s0, containing, items);
    COPY_SUBARR(s0, containing, items, consumed);
    COPY_SUBARR(s0, containing, items, tools);

    this_->roles.init(number_of_agents, containing);
}

void World::update(Simulation const& s, u8 id, Buffer* containing) {
    assert(containing and containing->inside(this));
    
    assert(0 <= id and id < number_of_agents);
    roles[id].name = s.role.name;
    roles[id].speed = s.role.speed;
    roles[id].battery = s.role.battery;
    roles[id].load = s.role.load;
    // This may invalidate us, but that is okay
    roles[id].tools.init(s.role.tools, containing);
}


void World::step_init(Percept const& p0, Buffer* containing) {
    int this_offset = (char*)this - containing->begin();
    int size = 0;
    
    if (shop_limits.size() == 0) {
        int shop_items = 0;
        for (auto const& shop: p0.shops) {
            shop_items += shop.items.size();
        }
        size += decltype(shop_limits)::extra_space(shop_items);
    }
    if (item_costs.size() == 0) {
        size += decltype(item_costs)::extra_space(items.size());
    }

    auto guard = containing->reserve_guard(size);
    auto this_ = &containing->get<World>(this_offset);

    if (this_->shop_limits.size() == 0) {
        this_->shop_limits.init(containing);
        for (auto const& shop: p0.shops) {
            for (auto const& i: shop.items) {
                this_->shop_limits.push_back({shop.id, i}, containing);
            }
        }
    }
    // TODO: add else branch for recovery

    auto infer_assembled_item_cost = [this_]() {
        for (int i = 0; i < this_->item_costs.size(); ++i) {
            if (this_->items[i].consumed.size() == 0) continue;
            auto& item = this_->item_costs[i];
            item.sum = 0;
            item.count = 0;
        }
                
        int it;
        for (it = 0; it < this_->item_costs.size(); ++it) {
            bool dirty = false;
            for (int i = 0; i < this_->item_costs.size(); ++i) {
                auto& item = this_->item_costs[i];
                if (item.count) continue;

                dirty = true;
                
                u16 cost = 175;
                bool flag = false;
                for (auto const& j: this_->items[i].consumed) {
                    auto& j_item = get_by_id(this_->item_costs, j.id);
                    if (j_item.count == 0) {
                        flag = true;
                        break;
                    }
                    cost += j_item.value() * j.amount;
                }
                if (flag) continue;
        
                item.sum = cost;
                ++item.count;
            }
            if (not dirty) break;
        }
        assert(it < this_->item_costs.size());
    };
    
    if (this_->item_costs.size() == 0) {
        this_->item_costs.init(containing);
        for (Item const& item: this_->items) {
            this_->item_costs.push_back({item.id, 0, 0}, containing);
        }
        
        for (auto const& shop: p0.shops) {
            for (auto const& i: shop.items) {
                auto& j = get_by_id(this_->item_costs, i.id);
                ++j.count;
                j.sum += (u16)(i.cost * shop_price_factor);
            }
        }

        infer_assembled_item_cost();
        this_->item_costs_job = 0;
    }

#if 0
    // As it turns out, this does not really increase precision, but makes us vulnerable to the
    // opponent posting jobs. Maybe an approach using a Least-Squares approximation might work
    // better.
    
    std::function<u16(Flat_array<Item_stack> const&)> collect_costs;
    collect_costs = [this_, &collect_costs](Flat_array<Item_stack> const& from) {
        u16 cost = 0;
        for (auto i: from) {
            auto const& item = get_by_id(this_->items, i.id);
            if (item.consumed.size()) {
                cost += 175 * i.amount;
                cost += collect_costs(item.consumed) * i.amount;
            } else {
                cost += this_->item_costs[&item - this_->items.begin()].value()  * i.amount;
            }
        }
        return cost;
    };
    
    std::function<void(Flat_array<Item_stack> const&, float)> apply_factor;
    apply_factor = [this_, &apply_factor](Flat_array<Item_stack> const& to, float factor) {
        // If an item is needed for two separate assemblies, its value will be updated twice. Sad.
        for (auto i: to) {
            auto const& item = get_by_id(this_->items, i.id);
            if (item.consumed.size()) {
                apply_factor(item.consumed, factor);
            } else {
                auto& item_c = this_->item_costs[&item - this_->items.begin()];
                item_c.sum += (u16)(item_c.value() * factor);
                ++item_c.count;
            }
        }
    };

    bool dirty = false;
    for (int i = p0.jobs.size() - 1; i >= 0; --i) {
        auto const& job = p0.jobs[i];
        if (job.id == this_->item_costs_job) break;
        
        u16 estimated = collect_costs(job.required);
        float factor = (float)job.reward / (float)estimated;
        apply_factor(job.required, factor);
        dirty = true;
    }
    if (dirty) {
        this_->item_costs_job = p0.jobs.back().id;
        infer_assembled_item_cost();
    }
#endif
}

void World::step_update(Percept const& p, u8 id, Buffer* containing) {
    // nothing
}
    

Situation::Situation(Percept const& p0, Situation const* sit_old, Buffer* containing):
    initialized{true},
    simulation_step{p0.simulation_step},
    team_money{p0.team_money}
{
    assert(containing and containing->inside(this));
    
    int this_offset = (char*)this - containing->begin();
    
    {int size = 0;
    SIZE_ARRAY (p0, containing, entities);
    SIZE_ARRAY (p0, containing, charging_stations);
    SIZE_ARRAY (p0, containing, dumps);
    SIZE_ARRAY (p0, containing, shops);
    SIZE_SUBARR(p0, containing, shops, items);
    SIZE_ARRAY (p0, containing, storages);
    SIZE_SUBARR(p0, containing, storages, items);
    SIZE_ARRAY (p0, containing, workshops);
    SIZE_ARRAY (p0, containing, resource_nodes);
    SIZE_ARRAY (p0, containing, auctions);
    SIZE_SUBARR(p0, containing, auctions, required);
    SIZE_ARRAY (p0, containing, jobs);
    SIZE_SUBARR(p0, containing, jobs, required)
    SIZE_ARRAY (p0, containing, missions);
    SIZE_SUBARR(p0, containing, missions, required);
    SIZE_ARRAY (p0, containing, posteds);
    SIZE_SUBARR(p0, containing, posteds, required);
    
    auto guard = containing->reserve_guard(size);
    auto this_ = &containing->get<Situation>(this_offset);
    
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
    COPY_SUBARR(p0, containing, posteds, required);}

    int size = decltype(sit_old->book.delivered)::extra_space(sit_old ? sit_old->book.delivered.size() : 0);
    containing->reserve_space(size);
    auto this_ = &containing->get<Situation>(this_offset);
    
    this_->book.delivered.init(containing);
    if (sit_old) {
        for (auto i: sit_old->book.delivered) {
            // Ignore the ones there are no jobs for
            if (find_by_id(jobs, i.job_id)) {
                this_->book.delivered.push_back(i, containing);
            }
        }
    }
    
    if (sit_old) {
        for (u8 agent = 0; agent < number_of_agents; ++agent) {
            this_->self(agent).task_index = sit_old->self(agent).task_index;
            // get_action is already stateless, and task_update only uses states for communication
            this_->self(agent).task_state = 0;
            this_->self(agent).task_sleep = 0;
        }
        
        std::memcpy(&this_->strategy, &sit_old->strategy, sizeof(strategy));

        assert(&this_->strategy.task(0, 1) - &this_->strategy.task(0, 0) == 1);
        
        for (u8 agent = 0; agent < number_of_agents; ++agent) {
            if (this_->self(agent).task_index == 0) continue;
            int active_tasks = planning_max_tasks - this_->self(agent).task_index;
            std::memmove(
                &this_->strategy.task(agent, 0),
                &this_->strategy.task(agent, this_->self(agent).task_index),
                sizeof(Task_slot) * active_tasks
            );
            std::memset(
                &this_->strategy.task(agent, active_tasks),
                0,
                this_->self(agent).task_index
            );
            this_->self(agent).task_index = 0;
            this_->self(agent).task_state = 0;
            this_->self(agent).task_sleep = 0;
        }
    }
}
    

void Situation::update(Percept const& p, u8 id, Buffer* containing) {
    assert(containing and containing->inside(this));
    
    assert(0 <= id and id < number_of_agents);
    selves[id].id = p.self.id;
    selves[id].team = p.self.team;
    selves[id].pos = p.self.pos;
    selves[id].role = p.self.role;
    selves[id].charge = p.self.charge;
    selves[id].load = p.self.load;
    selves[id].action_type = p.self.action_type;
    selves[id].action_result = p.self.action_result;
    selves[id].facility = p.self.facility;

    int this_offset = (char*)this - containing->begin();
    int size = 0;
    SIZE_ARRAY(p.self, containing, items);
    SIZE_ARRAY(p.self, containing, route);
    
    auto guard = containing->reserve_guard(size);
    auto this_ = &containing->get<Situation>(this_offset);

    this_->selves[id].items.init(p.self.items, containing);
    this_->selves[id].route.init(p.self.route, containing);
}

#undef SIZE_ARRAY
#undef SIZE_SUBARR
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

void Situation::register_arr(Diff_flat_arrays* diff) {
    assert(diff);
    
    for (u8 i = 0; i < number_of_agents; ++i) {
        diff->register_arr(selves[i].items);
        diff->register_arr(selves[i].route);
    }
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
    
    while (d.task_index < planning_max_tasks) {
        auto& t = task(agent);

        bool move_on = false;

        if (t.task.type == Task::NONE) {
            into->emplace_back<Action_Abort>();
            return;
        } else if (t.task.type == Task::BUY_ITEM) {
            if (not agent_goto(t.task.where, agent, into)) {
                return;
            }
            
            if (d.action_type == Action::BUY and not d.action_result) {
                d.action_type = Action::ABORT;
                t.task.item.amount = 0;
            } 
            if (t.task.item.amount == 0) {
                move_on = true;
            } else {
                into->emplace_back<Action_Buy>(t.task.item);
                return;
            }
        } else if (t.task.type == Task::CRAFT_ITEM) {
            // TODO Consider the task_index when crafting, maybe add unique assembly id
            if (not agent_goto(t.task.where, agent, into)) {
                return;
            }
            
            if (d.action_type == Action::ASSEMBLE and not d.action_result) {
                d.action_type = Action::ABORT;
                --t.task.item.amount;
            } 
            if (t.task.item.amount == 0) {
                move_on = true;
            } else {
                into->emplace_back<Action_Assemble>(t.task.item.id);
                return;
            }
        } else if (t.task.type == Task::CRAFT_ASSIST) {
            if (not agent_goto(t.task.where, agent, into)) {
                return;
            }
            
            if (d.action_type == Action::ASSIST_ASSEMBLE and not d.action_result) {
                d.action_type = Action::ABORT;
                --t.task.cnt;
            } 
            if (t.task.cnt == 0) {
                move_on = true;
            } else {
                auto const& o_t = task(t.task.crafter.id);
                if (o_t.task.type == Task::CRAFT_ITEM and o_t.task.crafter == t.task.crafter) {
                    into->emplace_back<Action_Assist_assemble>(self(t.task.crafter.id).name);
                } else {
                    into->emplace_back<Action_Abort>();
                }
                return;
            }
        } else if (t.task.type == Task::DELIVER_ITEM) {
            if (not agent_goto(t.task.where, agent, into)) {
                return;
            }

            if (d.action_type == Action::DELIVER_JOB and not d.action_result) {
                d.action_type = Action::ABORT;
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
        } else if (t.task.type == Task::CHARGE) {
            if (not agent_goto(t.task.where, agent, into)) {
                return;
            }

            if (d.charge == world.roles[agent].battery) {
                move_on = true;
            } else {
                into->emplace_back<Action_Charge>();
                return;
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
        d.task_state = 0;
        d.task_sleep = 0;
    }
    
    into->emplace_back<Action_Abort>();
    return;
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
    auto& d = self(agent);
    if (d.facility == target_id) {
        return;
    }

    Pos target = find_pos(target_id);
    auto p1 = world.graph->pos(d.pos);
    auto p2 = world.graph->pos(target);
           
    Buffer tmp;
    u32 dist;
    if (world.roles[agent].speed == 5) {
        dist = world.graph->dist_air(d.pos, target);
    } else {
        dist = world.graph->dist_road(p1, p2, &tmp) / 1000;
    }

    u32 speed = world.roles[agent].speed * 500;
    
    if (dist > (d.charge / 10) * speed) {
        task(agent).result.err = Task_result::OUT_OF_BATTERY;
        d.task_state = 0xfe;
    } else {
        auto dur = (dist + speed-1) / speed;
        //jdbg < "DUR" < dur < dist < d.pos < target ,0;
        /*if (dist == 5615) {
        for (u32 node: tmp.get<Graph::Route_t>(0)) {
            jdbg < world.graph->nodes()[node].pos.lat < world.graph->nodes()[node].pos.lon ,0;
        }
        jdbg ,0;
        }*/
        d.charge -= dur * 10;
        d.task_sleep = dur;
        d.facility = target_id;
        d.pos = target;
    }
}

void Situation::task_update(World const& world, u8 agent, Diff_flat_arrays* diff) {
    assert(diff);

    auto& d = self(agent);
    auto& t = task(agent);

    if (t.task.type == Task::NONE) {
        t.result.err = Task_result::SUCCESS;
        d.task_state = 0xfe;
        return;
    } else if (t.task.type == Task::BUY_ITEM) {
        if (d.task_state == 0) {
            d.task_state = 1;
            agent_goto_nl(world, agent, t.task.where);
        }
        if (d.task_state == 1 and d.task_sleep == 0) {
            auto& shop = get_by_id(shops, t.task.where);
            auto& item = get_by_id(shop.items, t.task.item.id);

            auto const& w_item = get_by_id(world.items, item.id);
            if (d.load + w_item.volume * t.task.item.amount > world.roles[agent].load) {
                t.result.err = Task_result::MAX_LOAD;
                d.task_state = 0xfe;
                return;                
            }

            d.task_state = 0xff;
            if (item.amount < t.task.item.amount) {
                d.task_sleep = shop.restock * t.task.item.amount;
            } else {
                d.task_sleep = 1;
            }
            team_money -= t.task.item.amount * item.cost;
            item.amount -= t.task.item.amount;
            d.load += w_item.volume * t.task.item.amount;
            if (auto d_item = find_by_id(d.items, t.task.item.item)) {
                d_item->amount += t.task.item.amount;
            } else {
                diff->add(d.items, t.task.item);
            }
            return;
        }
    } else if (t.task.type == Task::CRAFT_ITEM) {
        if (d.task_state == 0) {
            d.task_state = 1;
            agent_goto_nl(world, agent, t.task.where);
        }

        auto agent_first = [](u8 agent) {
            static u8 result[number_of_agents];
            result[0] = agent;
            for (u8 i = 0; i < number_of_agents - 1; ++i)
                result[i+1] = i + (i >= agent);
            return Array_view<u8> {result, number_of_agents};
        };

        auto diff_min = [](Task const& task, u8 item_id) {
            // Returns an upper bound of the difference in item_id
        
            switch (task.type) {
            case Task::NONE:
            case Task::CHARGE:
            case Task::VISIT:
                return 0;
        
            case Task::BUY_ITEM:
            case Task::CRAFT_ITEM:
                return task.item.id != item_id ? 0 : task.item.amount;
        
            case Task::CRAFT_ASSIST:
            case Task::DELIVER_ITEM:
                return task.item.id != item_id ? 0 : -task.item.amount;
        
            default:
                assert(false);
            }
        };

        auto is_possible_item = [&](Item_stack i, bool is_tool, bool at_all) {
            int count = is_tool ? 1 : i.amount * t.task.item.amount;

            for (u8 o_agent: agent_first(agent)) {
                auto const& o_d = self(o_agent);
                if (o_d.task_index >= planning_max_tasks) continue;
                if (is_tool and not world.roles[o_agent].tools.count(i.id)) continue;
                
                auto const& o_t = strategy.task(o_agent, o_d.task_index);
                bool involved = (
                    o_agent == agent or (
                        o_t.task.type == Task::CRAFT_ASSIST
                        and o_t.task.crafter == t.task.crafter
                        and o_d.task_state == 2
                    )
                );
                int have = 0;
                if (auto j = find_by_id(o_d.items, i.item)) {
                    have = j->amount;
                }
                        
                if (involved) {
                    count -= have;
                } else if (at_all) {
                    // Check whether a future task may bring the items

                    for (u8 o_i = o_d.task_index; o_i < planning_max_tasks; ++o_i) {
                        auto const& o_tt = strategy.task(o_agent, o_i);
                        if (
                            o_tt.task.type == Task::CRAFT_ASSIST
                            and o_tt.task.crafter == t.task.crafter
                        ) {
                            count -= have;
                            break;
                        } else {
                            have += diff_min(o_tt.task, i.item);
                        }
                    }
                }
                if (count <= 0) break;
            }
                
            if (count > 0) {
                if (is_tool) {
                    t.result.err = Task_result::CRAFT_NO_TOOL;
                    t.result.err_arg = {i.id, 1};
                } else {
                    t.result.err = Task_result::CRAFT_NO_ITEM;
                    t.result.err_arg = {i.id, narrow<u8>(count)};
                }
                return false;
            }
            return true;
        };
        
        // Try to disprove that the agent can craft the item
        auto is_possible_at_all = [&]() -> bool {
            Item const& item = get_by_id(world.items, t.task.item.id);
  
            for (Item_stack i: item.consumed) {
                if (not is_possible_item(i, false, true)) return false;
            }
            for (u8 i: item.tools) {
                if (not is_possible_item({i, 1}, true, true)) return false;
            }
            
            return true;
        };

        // Check whether the agent can craft the item _right now_
        auto is_possible_right_now = [&]() -> bool {
            Item const& item = get_by_id(world.items, t.task.item.id);
  
            for (Item_stack i: item.consumed) {
                if (not is_possible_item(i, false, false)) return false;
            }
            for (u8 i: item.tools) {
                if (not is_possible_item({i, 1}, true, false)) return false;
            }
            
            return true;
        };

        if (d.task_state == 1 and d.task_sleep == 0) {            
            Item const& item = get_by_id(world.items, t.task.item.id);

            int overweight = d.load + item.volume - world.roles[agent].load;
            for (Item_stack i: item.consumed) {
                if (auto j = find_by_id(d.items, item.id)) {
                    overweight -= std::min(i.amount, j->amount) * get_by_id(world.items, j->id).volume;
                }
            }
            if (overweight > 0) {
                t.result.err = Task_result::MAX_LOAD;
                d.task_state = 0xfe;
                return;
            }
            
            if (is_possible_at_all()) {
                d.task_state = 2;
            } else {
                d.task_state = 0xfe;
                return;
            }
        }
        if (d.task_state == 2 and d.task_sleep == 0) {
            if (not is_possible_right_now()) {
                // One of the assists will hopefully wake us up
                d.task_sleep = 0xff;
                return;
            }

            // Execute the assembly
            Item const& item = get_by_id(world.items, t.task.item.id);
            for (Item_stack i: item.consumed) {
                int count = i.amount * t.task.item.amount;
                auto const& w_i = get_by_id(world.items, i.id);
                
                for (u8 o_agent: agent_first(agent)) {
                    auto& o_d = self(o_agent);
                    if (o_d.task_index == planning_max_tasks) continue;
                    auto& o_t = task(o_agent);
                    if (
                        o_agent != agent and (
                            o_t.task.type != Task::CRAFT_ASSIST
                            or o_t.task.crafter != t.task.crafter
                            or o_d.task_state != 2
                        )
                    ) continue;

                    if (auto j = find_by_id(o_d.items, i.id)) {
                        u8 rem = narrow<u8>(std::min((int)j->amount, count));
                        count -= rem;
                        j->amount -= rem;
                        o_d.load -= rem * w_i.volume;
                        if (count <= 0) break;
                    }
                }
            }
            
            d.task_state = 0xff;
            d.task_sleep = t.task.item.amount;
            if (auto d_item = find_by_id(d.items, t.task.item.id)) {
                d_item->amount += t.task.item.amount;
            } else {
                diff->add(d.items, t.task.item);
            }
            d.load += item.volume;
            
            for (u8 o_agent = 0; o_agent < number_of_agents; ++o_agent) {
                auto const& o_d = self(o_agent);
                if (o_d.task_index == planning_max_tasks) continue;
                auto const& o_t = task(o_agent);
                if (
                    o_t.task.type != Task::CRAFT_ASSIST
                    or o_t.task.crafter != t.task.crafter
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

            auto& c_s = self(t.task.crafter.id);

            // TODO: Check that the agent is able to contribute promised items
            
            bool found = false;
            u8 i;
            for (i = c_s.task_index; i < planning_max_tasks; ++i) {
                auto const& o_t = strategy.task(t.task.crafter.id, i);
                if (o_t.task.type == Task::CRAFT_ITEM and o_t.task.crafter == t.task.crafter) {
                    found = true;
                    break;
                }
            }
            if (not found) {
                t.result.err = Task_result::NO_CRAFTER_FOUND;
                d.task_state = 0xfe;
                return;
            }
            
            // Wake up the crafter
            if (i == c_s.task_index and c_s.task_state == 2) {
                c_s.task_state = 2;
                c_s.task_sleep = 0;
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
                d.task_state = 0xfe;
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
                d.task_state = 0xfe;
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
                            d.load -= deliv.amount * get_by_id(world.items, a_item->id).volume;
                            already += deliv.amount;
                        }
                    }
                }
                if (already < j_item.amount) {
                    flag = false;
                }
            }
            // TODO Handle useless deliveries.

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
            d.task_sleep = (world.roles[agent].battery - d.charge + station.rate-1) / station.rate;
            d.task_state = 0xff;
            d.charge = world.roles[agent].battery;
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

void Simulation_state::fast_forward() {
    fast_forward(sit().simulation_step + fast_forward_steps);
}
void Simulation_state::fast_forward(int max_step) {
    int initial_step = sit().simulation_step;
    
    u8 sleep_old = 0;
    while (sit().simulation_step < max_step) {
        for (u8 agent = 0; agent < number_of_agents; ++agent) {
            auto& d = sit().self(agent);
            if (d.task_sleep != 0xff) {
                d.task_sleep -= sleep_old;
            }

            if (d.task_sleep != 0 or d.task_index >= planning_max_tasks) continue;
            
            sit().task_update(*world, agent, &diff);
            
            if (d.task_state == 0xff and d.task_index < planning_max_tasks) {
                auto& r = sit().task(agent).result;
                r.time = (u8)(sit().simulation_step - initial_step + d.task_sleep);
                r.err = Task_result::SUCCESS;
                
                ++d.task_index;
                d.task_state = 0;
                // Do not reset sleep always, some tasks sleep upon completion
                if (d.task_index == planning_max_tasks) d.task_sleep = 0xff;
            } else if (d.task_state == 0xfe) {
                // Error encountered
                d.task_sleep = 0xff;
            }
        }

        u8 sleep_min = (u8)std::min(0xff, max_step - sit().simulation_step);
        for (u8 agent = 0; agent < number_of_agents; ++agent) {
            sleep_min = std::min(sleep_min, sit().self(agent).task_sleep);
        }
    
        diff.apply();

        // Update shops
        int j = 0;
        auto const& sl = world->shop_limits;
        auto f = [](int from, int len, int restock) {
            // This very carefully counts the number of restocks in (from, from+len]
            return (from + len + restock) / restock - (from + restock) / restock;
        };
        for (auto& shop: sit().shops) {
            u8 restocks = f(sit().simulation_step, sleep_min, shop.restock);
            for (auto& i: shop.items) {
                // shops and items should be sorted in some order, making this work.
                while (sl[j].item.id != i.id or sl[j].shop != shop.id) ++j;

                i.amount = std::min((u8)(i.amount + restocks), sl[j].item.amount);
                ++j;
            }

            // @Incomplete If the shop does not have an item, there will be no entry to increment
        }
        
        // sleep_min may be 0
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
        
        // @Incomplete Missions and Auctions
        
        sleep_old = sleep_min;
    }
    assert(sit().simulation_step == max_step);

    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        auto& d = sit().self(agent);
        if (d.task_index >= planning_max_tasks or d.task_state == 0xfe) continue;
        sit().task(agent).result.err = Task_result::SUCCESS;
        sit().task(agent).result.time = max_step - initial_step;
        ++sit().self(agent).task_index; // Making sure the last task is always at task_index - 1
    }
}

void Simulation_state::add_charging(u8 agent, u8 before) {
    auto& s = orig().strategy;
    u8 index;
    if (s.task(agent, before).task.type == Task::CHARGE) {
        std::swap(s.task(agent, before - 1), s.task(agent, before));
        index = before - 1;
    } else {
        index = before;
        s.insert_task(agent, index, Task {});
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
        u32 d = world->graph->dist_road(from_g, pos_g)
              + world->graph->dist_road(pos_g, to_g);
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
        
        // TODO: This does not look right, look into that
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

void Simulation_state::add_item_for(u8 for_agent, u8 for_index, Item_stack for_item, bool for_tool) {
    struct Viable_t {
        enum Type: u8 {
            INVALID = 0, ONLY_MOVE, BUY, CRAFT
        };
        u8 agent;
        u8 index;
        u8 type;
    };

    auto& s = orig().strategy;

    u8 viable_count = 0;
    Viable_t viables[number_of_agents * 3];

    bool is_deliver = sit().strategy.task(for_agent, for_index).task.type == Task::DELIVER_ITEM;

    u8 for_time = for_index > 0 ? sit().strategy.task(for_agent, for_index - 1).result.time : 0;

    bool in_shops = false;
    for (auto const& i: orig().shops) {
        if (auto j = find_by_id(i.items, for_item.id)) {
            if (j->amount >= for_item.amount) {
                in_shops = true;
                break;
            }
        }
    }

    bool may_craft = get_by_id(world->items, for_item.id).consumed.size() != 0;
    
    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        if (for_tool and not world->roles[agent].tools.count(for_item.id)) {
            // agent cannot handle the tool
            continue;
        }

        // When would we need to do this?
        u8 index;
        for (index = 0; index + 1 < planning_max_tasks; ++index) {
            u8 t = sit().strategy.task(agent, index).result.time
                + shop_assume_duration;
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

        bool in_inventory;
        if (for_tool) {
            in_inventory = count > 0;
        } else {
            // Check whether the items have not been used during the simulation
            // TODO: Remove the last iteration from this count maybe
            if (auto item = find_by_id(sit().self(agent).items, for_item.id)) {
                in_inventory = count > 0 and item->amount > 0;
            } else {
                in_inventory = false;
            }
        }

        // TODO: Respect agent's carrying capacity
        
        if (in_inventory) {
            viables[viable_count++] = {agent, index, Viable_t::ONLY_MOVE};
        }
        if (in_shops) {
            viables[viable_count++] = {agent, index, Viable_t::BUY};
        }
        if (may_craft) {
            viables[viable_count++] = {agent, index, Viable_t::CRAFT};
        }
    }

    if (viable_count == 0) {
        s.pop_task(for_agent, for_index);
        return;
    }

    auto way = viables[rng.gen_uni(viable_count)];
    u8 agent = way.agent;
    u8 index = way.index;

    if (way.type == Viable_t::ONLY_MOVE) {
        if (is_deliver) {
            // If its a delivery, move the task over
            s.insert_task(agent, index, s.pop_task(for_agent, for_index));
        } else if (for_agent != agent) { // No need to assist yourself
            s.insert_task(agent, index, {
                Task::CRAFT_ASSIST,
                s.task(for_agent, for_index).task.where,
                for_item,
                {},
                s.task(for_agent, for_index).task.item.amount
            });
            s.task(agent, index).task.crafter = s.task(for_agent, for_index).task.crafter;
        }
    } else if (way.type == Viable_t::BUY) {
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
            u32 d = world->graph->dist_road(from_g, pos_g)
                + world->graph->dist_road(pos_g, to_g);
            // TODO: Respect costs
        
            if (d < min_dist) {
                min_dist = d;
                min_arg = i.id;
            }
        }
        assert(min_arg != 0);

        s.insert_task(agent, index, {Task::BUY_ITEM, min_arg, for_item});
        if (is_deliver) {
            // If its a delivery, move the task over
            s.insert_task(agent, index + 1, s.pop_task(for_agent, for_index));
        } else if (for_agent != agent) { // No need to assist yourself
            s.insert_task(agent, index + 1, {
                Task::CRAFT_ASSIST,
                s.task(for_agent, for_index).task.where,
                for_item,
                {},
                s.task(for_agent, for_index).task.item.amount
            });
            s.task(agent, index + 1).task.crafter = s.task(for_agent, for_index).task.crafter;
        }
    } else if (way.type == Viable_t::CRAFT) {
        Pos from;
        if (index > 0) {
            from = orig().find_pos(s.task(agent, index - 1).task.where);
        } else {
            from = orig().self(agent).pos;
        }
        Pos to = orig().find_pos(s.task(for_agent, for_index).task.where);
        
        // Find nearest workshop (duplicate code)
        u32 min_dist = std::numeric_limits<u32>::max();
        u8 min_arg = 0;
        for (auto const& i: orig().workshops) {
            u32 dist = world->graph->dist_air(from, i.pos) + world->graph->dist_air(i.pos, to);
            if (dist < min_dist) {
                min_dist = dist;
                min_arg = i.id;
            }
        }
        assert(min_arg != 0);

        s.insert_task(agent, index, {Task::CRAFT_ITEM, min_arg, for_item, agent});
        s.task(agent, index).task.crafter.id = agent;
        s.task(agent, index).task.crafter.check = (u8)(rng.rand() & 0xff);
        
        if (is_deliver) {
            // If it's a delivery, move the task over
            s.insert_task(agent, index + 1, s.pop_task(for_agent, for_index));
        } else if (for_agent != agent) { // No need to assist yourself
            s.insert_task(agent, index + 1, {
                Task::CRAFT_ASSIST,
                s.task(for_agent, for_index).task.where,
                for_item,
                {},
                s.task(for_agent, for_index).task.item.amount
            });
            s.task(agent, index + 1).task.crafter = s.task(for_agent, for_index).task.crafter;
        }
    } else {
        assert(false);
    }
}

void Simulation_state::remove_task(u8 agent, u8 index) {
    orig().strategy.pop_task(agent, index);
}

void Simulation_state::reduce_load(u8 agent, u8 index) {
    int space = world->roles[agent].load - sit().self(agent).load;
    auto& t = orig().task(agent);
    int possible = space / get_by_id(world->items, t.task.item.id).volume;
    if (possible == 0) {
        orig().strategy.pop_task(agent, index);
    } else {
        t.task.item.amount = possible;
    }
}

void Simulation_state::fix_errors() {
    for (int it = 0; it < fixer_iterations; ++it) {
        reset();
        fast_forward();
        jdbg < sit().strategy.p_tasks() ,0;

        bool flag = true;

        for (u8 agent = 0; agent < number_of_agents; ++agent) {
            auto& d = sit().self(agent);
            if (d.task_state != 0xfe) continue;
            assert(d.task_index < planning_max_tasks);

            auto& r = sit().strategy.task(agent, d.task_index).result;
            if (r.err == Task_result::SUCCESS) {
                continue;
            } else if (r.err == Task_result::OUT_OF_BATTERY) {
                add_charging(agent, d.task_index);
            } else if (r.err == Task_result::CRAFT_NO_ITEM) {
                add_item_for(agent, d.task_index, r.err_arg, false);
            } else if (r.err == Task_result::CRAFT_NO_ITEM_SELF) {
                add_item_for(agent, d.task_index, r.err_arg, false);
            } else if (r.err == Task_result::CRAFT_NO_TOOL) {
                add_item_for(agent, d.task_index, r.err_arg, true);
            } else if (r.err == Task_result::NO_CRAFTER_FOUND) {
                remove_task(agent, d.task_index);
            } else if (r.err == Task_result::NOT_IN_INVENTORY) {
                add_item_for(agent, d.task_index, r.err_arg, false);
            } else if (r.err == Task_result::NOT_VALID_FOR_JOB) {
                remove_task(agent, d.task_index);
            } else if (r.err == Task_result::NO_SUCH_JOB) {
                remove_task(agent, d.task_index);
            } else if (r.err == Task_result::MAX_LOAD) {
                reduce_load(agent, d.task_index);
            } else {
                assert(false);
            }
            
            jdbg < it < agent < r ,0;
            flag = false;
            break;
        }

        if (flag) {
            //jdbg < "NOERR" < it ,0;
            break;
        }
    }
    jdbg < sit().strategy.p_results() ,0;
}

void Simulation_state::create_work() {
    u8 viable_agents[number_of_agents];
    int viable_agents_count = 0;
    
    auto& s = orig().strategy;
    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        int count = 0;
        for (u8 i = 0; i < planning_max_tasks; ++i) {
            count += s.task(agent, i).task.type != Task::NONE;
        }
        auto const& d = sit().self(agent);
        if (d.task_index == 0) {
            viable_agents[viable_agents_count++] = agent;
        } else {
            int last_time = sit().strategy.task(agent, d.task_index - 1).result.time;
            if (last_time < fast_forward_steps - max_idle_time and d.task_index < planning_max_tasks) {
                viable_agents[viable_agents_count++] = agent;
            }
        }
    }

    if (viable_agents_count < 4) return;

    if (orig().jobs.size()) {
        // Add a job

        struct Viable_job_t {
            u16 job_id;
        };

        Viable_job_t* viable_jobs = (Viable_job_t*)alloca(orig().jobs.size() * sizeof(Viable_job_t));
        int viable_job_count = 0;
        
        for (Job const& i: sit().jobs) {
            //bool started = find_by_id(sit().book.delivered, i.id);
            viable_jobs[viable_job_count++] = {i.id};
        }

        assert(viable_job_count > 0);

        u16 job_id = viable_jobs[rng.gen_uni(viable_job_count)].job_id;
        Job const& job = get_by_id(sit().jobs, job_id);

        assert(job.required.size() <= viable_agents_count);
        for (auto const& i: job.required) {
            int agent_index = rng.gen_uni(viable_agents_count);
            u8 agent = viable_agents[agent_index];
            viable_agents[agent_index] = viable_agents[--viable_agents_count];

            s.task(agent, sit().self(agent).task_index).task = Task {
                Task::DELIVER_ITEM, job.storage, i, job.id
            };
        }
    } else {
        // Add an item

        u8* viable_items = (u8*)alloca(world->items.size() * sizeof(u8));
        int viable_item_count = 0;

        for (Item const& i: world->items) {
            if (i.consumed.size() == 0) continue;
            viable_items[viable_item_count++] = i.id;
        }

        u8 item_id = viable_items[rng.gen_uni(viable_item_count)];
        u8 agent = viable_agents[rng.gen_uni(viable_agents_count)];

        // Find nearest workshop (duplicate code)
        u32 min_dist = std::numeric_limits<u32>::max();
        u8 min_arg = 0;
        Pos from = sit().self(agent).pos;
        for (auto const& i: orig().workshops) {
            u32 dist = world->graph->dist_air(from, i.pos);
            if (dist < min_dist) {
                min_dist = dist;
                min_arg = i.id;
            }
        }
        assert(min_arg != 0);

        int i = sit().self(agent).task_index;
        s.task(agent, i).task = Task { Task::CRAFT_ITEM, min_arg, {item_id, 1} };
        s.task(agent, i).task.crafter.id = agent;
        s.task(agent, i).task.crafter.check = (u8)(rng.rand() & 0xff);
    }
}

float Simulation_state::rate() {
    return 0.f;
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
