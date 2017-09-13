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
                
                u16 cost = price_craft_val;
                u8 craftval = 1;
                bool flag = false;
                for (auto const& j: this_->items[i].consumed) {
                    auto& j_item = get_by_id(this_->item_costs, j.id);
                    if (j_item.count == 0) {
                        flag = true;
                        break;
                    }
                    cost += j_item.value() * j.amount;
                    craftval += j_item.craftval * j.amount;
                }
                if (flag) continue;
        
                item.sum = cost;
                item.craftval = craftval;
                ++item.count;
            }
            if (not dirty) break;
        }
        assert(it < this_->item_costs.size());
    };
    
    if (this_->item_costs.size() == 0) {
        this_->item_costs.init(containing);
        for (Item const& item: this_->items) {
            this_->item_costs.push_back({item.id, 0, 0, 0}, containing);
        }
        
        for (auto const& shop: p0.shops) {
            for (auto const& i: shop.items) {
                auto& j = get_by_id(this_->item_costs, i.id);
                ++j.count;
                j.sum += (u16)(i.cost * price_shop_factor);
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

void World::step_update(Percept const& p, u8 id, Buffer* containing) {}

void World::step_post(Buffer* containing) {}
    
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
            if (find_by_id_job(i.job_id)) {
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
    }
}
    
void Situation::flush_old(World const& world, Situation const& old, Diff_flat_arrays* diff) {
    assert(diff);
    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        get_action(world, old, agent, nullptr, diff);
    }
    
    assert(&strategy.task(0, 1) - &strategy.task(0, 0) == 1);

    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        if (self(agent).task_index == 0) continue;
        int active_tasks = planning_max_tasks - self(agent).task_index;
        std::memmove(
            &strategy.task(agent, 0),
            &strategy.task(agent, self(agent).task_index),
            sizeof(Task_slot) * active_tasks
        );
        std::memset(
            &strategy.task(agent, active_tasks),
            0,
            self(agent).task_index
        );
        self(agent).task_index = 0;
        self(agent).task_state = 0;
        self(agent).task_sleep = 0;
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
        diff->register_arr(selves[i].items, "self.items");
        diff->register_arr(selves[i].route, "self.route");
    }
    diff->register_arr(entities, "entities");
    diff->register_arr(charging_stations, "charging_stations");
    diff->register_arr(dumps, "dumps");
    diff->register_arr(shops, "shops");
    for (auto& i: shops) diff->register_arr(i.items, "shop.items");
    diff->register_arr(storages, "storages");
    for (auto& i: storages) diff->register_arr(i.items, "storage.items");
    diff->register_arr(workshops, "workshops");
    diff->register_arr(resource_nodes, "resource_nodes");
    diff->register_arr(auctions, "auctions");
    for (auto& i: auctions) diff->register_arr(i.required, "auction.required");
    diff->register_arr(jobs, "jobs");
    for (auto& i: jobs) diff->register_arr(i.required, "job.required");
    diff->register_arr(missions, "missions");
    for (auto& i: missions) diff->register_arr(i.required, "mission.required");
    diff->register_arr(posteds, "posteds");
    for (auto& i: posteds) diff->register_arr(i.required, "posted.required");
    diff->register_arr(book.delivered, "book.delivered");
}

bool Situation::agent_goto(u8 where, u8 agent, Buffer* into) {
    auto& d = self(agent);

    // Could send continue if same location as last time
    if (d.facility == where) {
        return true;
    } else {
        if (into) into->emplace_back<Action_Goto1>(where);
        return false;
    }
}

void Situation::get_action(World const& world, Situation const& old, u8 agent, Buffer* into, Diff_flat_arrays* diff) {
    auto& d = self(agent);
    
    while (d.task_index < planning_max_tasks) {
        auto& t = task(agent);

        bool move_on = false;

        if (t.task.type == Task::NONE) {
            if (into) into->emplace_back<Action_Abort>();
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
                if (into) into->emplace_back<Action_Buy>(t.task.item);
                return;
            }
        } else if (t.task.type == Task::CRAFT_ITEM) {
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
                if (into) into->emplace_back<Action_Assemble>(t.task.item.id);
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
                    if (into) into->emplace_back<Action_Assist_assemble>(self(t.task.crafter.id).name);
                } else {
                    if (into) into->emplace_back<Action_Abort>();
                }
                return;
            }
        } else if (t.task.type == Task::DELIVER_ITEM) {
            if (not agent_goto(t.task.where, agent, into)) {
                return;
            }

            if (d.action_type == Action::DELIVER_JOB and (not d.action_result
                    or d.action_result == Action::SUCCESSFUL_PARTIAL)) {
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
                if (into) into->emplace_back<Action_Deliver_job>(t.task.job_id);
                return;
            }            
        } else if (t.task.type == Task::CHARGE) {
            if (not agent_goto(t.task.where, agent, into)) {
                return;
            }

            if (d.charge == world.roles[agent].battery) {
                move_on = true;
            } else {
                if (into) into->emplace_back<Action_Charge>();
                return;
            }
        } else if (t.task.type == Task::VISIT) {
            if (agent_goto(t.task.where, agent, into)) {
                move_on = true;
            } else {
                return;
            }
        } else {
            assert(false);
        }

        assert(move_on);

        ++d.task_index;
        d.task_state = 0;
        d.task_sleep = 0;
    }
    
    if (into) into->emplace_back<Action_Abort>();
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

Job* Situation::find_by_id_job(u16 id, u8* type) {
    for (auto& i: jobs) {
        if (i.id == id) {
            if (type) *type = Job::JOB;
            return &i;
        }
    }
    for (auto& i: auctions) {
        if (i.id == id) {
            if (type) *type = Job::AUCTION;
            return &i;
        }
    }
    for (auto& i: missions) {
        if (i.id == id) {
            if (type) *type = Job::MISSION;
            return &i;
        }
    }
    for (auto& i: posteds) {
        if (i.id == id) {
            if (type) *type = Job::POSTED;
            return &i;
        }
    }
    if (type) *type = Job::NONE;
    return nullptr;
}
Job& Situation::get_by_id_job(u16 id, u8* type) {
    Job* result = find_by_id_job(id, type);
    assert(result);
    return *result;
}

u16 Situation::agent_dist(World const& world, Dist_cache* dist_cache, u8 agent, u8 target_id) {
    auto& d = self(agent);
    if (world.roles[agent].speed == 5) {
        Pos target = find_pos(target_id);
        return (u16)world.graph->dist_air(d.pos, target);
    } else {
        return dist_cache->lookup(d.name, target_id);
        /*auto p1 = world.graph->pos(d.pos);
        auto p2 = world.graph->pos(target);
        
        if (dist != world.graph->dist_road(p1, p2) / 1000) {
            jdbg < get_string_from_id(d.name).c_str() < get_string_from_id(target_id).c_str() < d.name < target_id < d.pos < target,0;
            jdbg < dist_cache->id_to_index2[d.name] < dist_cache->id_to_index2[target_id] < dist_cache->positions[dist_cache->id_to_index2[d.name]] < dist_cache->positions[dist_cache->id_to_index2[target_id]]< p1 < p2 ,0;
            jdbg < dist < (world.graph->dist_road(p1, p2) / 1000) < d.name < target_id,0;
        }
        assert(dist == world.graph->dist_road(p1, p2) / 1000);*/
    }
}

void Situation::agent_goto_nl(World const& world, Dist_cache* dist_cache, u8 agent, u8 target_id) {
    auto& d = self(agent);
    if (d.facility == target_id) return;
    
    u16 dist = agent_dist(world, dist_cache, agent, target_id);
    u32 speed = world.roles[agent].speed * 500;

    u16 dist_add = std::numeric_limits<u16>::max();
    for (auto const& i: charging_stations) {
        dist_add = std::min(dist_add, dist_cache->lookup(target_id, i.id));
    }
    
    if (dist + dist_add > (d.charge / 10 - 1) * speed) {
        task(agent).result.err = Task_result::OUT_OF_BATTERY;
        d.task_state = 0xfe;
    } else {
        auto dur = (dist + speed-1) / speed;
        d.charge -= dur * 10;
        d.task_sleep = dur;
        d.facility = target_id;
        d.pos = find_pos(target_id);
        dist_cache->move_to(d.name, target_id);
    }
}

void Situation::task_update(World const& world, Dist_cache* dist_cache, u8 agent, Diff_flat_arrays* diff) {
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
            agent_goto_nl(world, dist_cache, agent, t.task.where);
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
                t.result.err = Task_result::NOT_IN_SHOP;
                t.result.err_arg = {item.id, (u8)(t.task.item.amount - item.amount)};
                d.task_state = 0xfe;
                return;
            } else {
                d.task_sleep = 1;
            }
            team_money -= t.task.item.amount * item.cost;
            item.amount -= t.task.item.amount;
            t.result.left = item.amount;
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
            agent_goto_nl(world, dist_cache, agent, t.task.where);
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
                if (at_all) {
                    if (is_tool) {
                        t.result.err = Task_result::CRAFT_NO_TOOL;
                        t.result.err_arg = {i.id, 1};
                    } else {
                        t.result.err = Task_result::CRAFT_NO_ITEM;
                        t.result.err_arg = {i.id, narrow<u8>(count)};
                    }
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

            int overweight = d.load + item.volume * t.task.item.amount - world.roles[agent].load;
            for (Item_stack i: item.consumed) {
                if (auto j = find_by_id(d.items, i.id)) {
                    overweight -= std::min(i.amount * t.task.item.amount, (int)j->amount)
                        * get_by_id(world.items, j->id).volume;
                }
            }
            if (overweight > 0) {
                t.result.err = Task_result::MAX_LOAD;
                d.task_state = 0xfe;
                return;
            }
            
            d.task_state = 2;
        }
        if (d.task_state == 2 and d.task_sleep == 0) {
            if (not is_possible_right_now()) {
                if (is_possible_at_all()) {
                    // One of the assists will hopefully wake us up
                    d.task_sleep = 0xff;
                    return;
                } else {
                    d.task_state = 0xfe;
                    return;
                }
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
            agent_goto_nl(world, dist_cache, agent, t.task.where);
            d.task_state = 1;
        }
        if (d.task_state == 1 and d.task_sleep == 0) {
            d.task_sleep = 0xff;
            d.task_state = 2;

            auto& c_s = self(t.task.crafter.id);

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
            
            int have = 0;
            if (auto item = find_by_id(d.items, t.task.item.id)) {
                have = item->amount;
            }
            if (have < t.task.item.amount) {
                t.result.err = Task_result::ASSIST_USELESS;
                t.result.err_arg = {t.task.item.id, (u8)(t.task.item.amount - have)};
                d.task_state = 0xfe;
                return;
            }
            
            // Wake up the crafter
            if (i == c_s.task_index and c_s.task_state == 2) {
                c_s.task_sleep = 0;
            }
        }
        // d.task_state == 2 is handled externally by the crafter
    } else if (t.task.type == Task::DELIVER_ITEM) {
        if (d.task_state == 0) {
            agent_goto_nl(world, dist_cache, agent, t.task.where);
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

            u8 job_type;
            Job* job = find_by_id_job(t.task.job_id, &job_type);
            
            if (job_type == Job::NONE or job_type == Job::POSTED or job->end < simulation_step) {
                t.result.err = Task_result::NO_SUCH_JOB;
                d.task_state = 0xfe;
                return;
            }

            bool useless = true;
            bool completed = true;
            
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
                            useless = false;
                        }
                    }
                }
                if (already < j_item.amount) {
                    completed = false;
                }
            }

            // Handle useless deliveries
            if (useless) {
                t.result.err = Task_result::DELIVERY_USELESS;
                d.task_state = 0xfe;
                return;
            }

            // Check whether the job is completed.
            if (completed) {
                // Do not remove the items from book.delivered, because that information is nice to have.
                
                if (job_type == Job::JOB) {
                    diff->remove_ptr(jobs, job);
                } else if (job_type == Job::AUCTION) {
                    diff->remove_ptr(auctions, (Auction*)job);
                } else if (job_type == Job::MISSION) {
                    diff->remove_ptr(missions, (Mission*)job);
                } else {
                    assert(false);
                }
                team_money += job->reward;
            }
            
            d.task_sleep = (job->start > simulation_step ? job->start - simulation_step : 0) + 1;
            d.task_state = 0xff;
            return;
        }
    } else if (t.task.type == Task::CHARGE) {
        if (d.task_state == 0) {
            agent_goto_nl(world, dist_cache, agent, t.task.where);
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
    assert(sit_size_ == sit_buffer_->size() - sit_offset_);

    // Changes to orig() may be made here, probably using diff
    
    orig_size = sit_size_;
    sit_offset = sit_buffer_->size();
        
    if (dist_cache.facility_count == 0) {
        // TODO: Make this work with ressource nodes
        int count = orig().charging_stations.size() + orig().dumps.size() + orig().shops.size()
            + orig().storages.size() + orig().workshops.size();
        dist_cache.init(count, world->graph);
        for (auto const& i: orig().charging_stations) dist_cache.register_pos(i.id, i.pos);
        for (auto const& i: orig().dumps)             dist_cache.register_pos(i.id, i.pos);
        for (auto const& i: orig().shops)             dist_cache.register_pos(i.id, i.pos);
        for (auto const& i: orig().workshops)         dist_cache.register_pos(i.id, i.pos);
        for (auto const& i: orig().storages)          dist_cache.register_pos(i.id, i.pos);
        dist_cache.calc_facilities();
    }

    dist_cache.reset();
    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        dist_cache.register_pos(orig().self(agent).name, orig().self(agent).pos);
    }
    dist_cache.calc_agents();
}

void Simulation_state::reset() {
    // Copy the original into the working space
    buf().resize(sit_offset + orig_size);
    std::memcpy(&sit(), &orig(), orig_size);
    diff.reset();
    sit().register_arr(&diff);
    dist_cache.load_positions();
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
                d.task_sleep -= std::min(sleep_old, d.task_sleep);
            }

            if (d.task_sleep != 0 or d.task_index >= planning_max_tasks) continue;
            
            sit().task_update(*world, &dist_cache, agent, &diff);
            
            if (d.task_state == 0xff and d.task_index < planning_max_tasks) {
                auto& r = sit().task(agent).result;
                r.time = (u8)(sit().simulation_step - initial_step + d.task_sleep);
                r.err = Task_result::SUCCESS;
                r.load = d.load;

                orig().strategy.task(agent, d.task_index).task.fixer_it = 0;
                
                ++d.task_index;
                d.task_state = 0;
                // Do not reset sleep always, some tasks sleep upon completion
                if (d.task_index == planning_max_tasks) d.task_sleep = 0xff;
            } else if (d.task_state == 0xfe) {
                // Error encountered
                auto& r = sit().task(agent).result;
                r.time = (u8)(sit().simulation_step - initial_step);
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
        // Do not remove the items from book.delivered, because that information is nice to have.
        for (Job const& job: sit().jobs) {
            if (job.end < sit().simulation_step) {
                diff.remove_ptr(sit().jobs, &job);
            }
        }
        for (Auction const& job: sit().auctions) {
            if (job.end < sit().simulation_step) {
                diff.remove_ptr(sit().auctions, &job);
                sit().team_money -= job.fine;
            }
        }
        for (Mission const& job: sit().missions) {
            if (job.end < sit().simulation_step) {
                diff.remove_ptr(sit().missions, &job);
                sit().team_money -= job.fine;
            }
        }

        // TODO: Betting on auctions
        
        sleep_old = sleep_min;
    }
    assert(sit().simulation_step == max_step);
    
    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        // Make sure that task_index holds the number of tasks executed
        auto& d = sit().self(agent);
        if (d.task_index >= planning_max_tasks
            or orig().strategy.task(agent, d.task_index).task.type == Task::NONE
        ) {
            d.task_state = 0;
        } else if (d.task_state == 0xfe) {
            ++d.task_index;
        } else {
            auto& t = sit().task(agent);
            t.result.err = Task_result::SUCCESS;
            t.result.time = max_step - initial_step;
            t.result.load = d.load;
            ++d.task_index;
        }

        // Reset the unneeded results
        for (u8 i = d.task_index; i < planning_max_tasks; ++i) {
            sit().strategy.task(agent, i).result = Task_result {};
        }
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
        s.insert_task(agent, index, Task {0, 0, ++task_next_id});
    }

    u8 from_id = index > 0
        ? s.task(agent, index - 1).task.where
        : orig().self(agent).name;
    u8 to_id = index + 1 < planning_max_tasks
        ? s.task(agent, index + 1).task.where
        : from_id;
    Pos from = index > 0
        ? orig().find_pos(s.task(agent, index - 1).task.where)
        : orig().self(agent).pos;
    Pos to = index + 1 < planning_max_tasks
        ? orig().find_pos(s.task(agent, index + 1).task.where)
        : from;
    
    auto from_g = world->graph->pos(from);
    auto to_g   = world->graph->pos(to  );

    u16 min_dist = std::numeric_limits<u16>::max();
    u8 min_arg;
    for (auto& i: orig().charging_stations) {
        auto pos_g = world->graph->pos(i.pos);
        u16 d = dist_cache.lookup_old(from_id, i.id) + dist_cache.lookup_old(i.id, to_id);
        assert(d == (world->graph->dist_road(from_g, pos_g) / 1000 + world->graph->dist_road(pos_g, to_g) / 1000));
        
        // TODO: Respect charging time
        
        if (d < min_dist) {
            min_dist = d;
            min_arg = i.id;
        }
    }
    
    s.task(agent, index).task = Task {Task::CHARGE, min_arg, ++task_next_id, {}};
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
            INVALID = 0, ONLY_MOVE, BUY, CRAFT, FATTEN
        };
        u8 rating;
        u8 agent;
        u8 index;
        u8 type;
        bool no_tail;
        u8 amount;
    };

    auto& s = orig().strategy;
    auto& t = s.task(for_agent, for_index);

    u8 viable_count = 0;
    Viable_t viables[number_of_agents * 4];

    bool is_deliver = t.task.type == Task::DELIVER_ITEM;

    bool in_shops = false;
    for (auto const& i: orig().shops) {
        if (auto j = find_by_id(i.items, for_item.id)) {
            if (j->amount >= for_item.amount) {
                in_shops = true;
                break;
            }
        }
    }

    auto const& w_item = get_by_id(world->items, for_item.id);
    bool may_craft = w_item.consumed.size() != 0;
    
    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        // Can the agent handle the tool?
        if (for_tool and not world->roles[agent].tools.count(for_item.id)) {
            continue;
        }

        // Are there other tasks?
        u8 task_count = 0;
        for (u8 i = 0; i < planning_max_tasks; ++i) {
            task_count += s.task(agent, i).task.type != Task::NONE;
        }
        u8 index = task_count; // Assume the NONEs are in the back

        // If there is an error, insert the task in front, to prevent deadlocks
        if (index and sit().strategy.task(agent, index - 1).result.err) {
            --index;
        }

        // Is the agent already partaking in the assembly?
        bool involved = false;
        if (not is_deliver) {
            for (u8 i = 0; i < planning_max_tasks; ++i) {
                if (
                    (s.task(agent, i).task.type == Task::CRAFT_ASSIST
                        or s.task(agent, i).task.type == Task::CRAFT_ITEM)
                    and s.task(agent, i).task.crafter == t.task.crafter
                ) {
                    involved = true;
                    index = i;
                    break;
                }
            }
        } else {
            for (u8 i = 0; i < planning_max_tasks; ++i) {
                if (
                    s.task(agent, i).task.type == Task::DELIVER_ITEM
                    and s.task(agent, i).task.job_id == t.task.job_id
                ) {
                    involved = true;
                    index = i;
                    break;
                }
            }
        }
        
        // Does the agent have the item already?
        u8 have = 0;
        if (auto item = find_by_id(sit().self(agent).items, for_item.id)) {
            have = involved ? 0 : item->amount;
        }

        // Fattening
        u8 fatten_amount = 0;
        u8 fatten_index;
        for (u8 i = 0; i < planning_max_tasks; ++i) {
            auto const& t = s.task(agent, i);
            auto const& t_ = sit().strategy.task(agent, i);
            if (t.task.type == Task::BUY_ITEM and t.task.item.id == for_item.id and t_.result.left > 0) {
                u8 max_load = 0;
                for (u8 j = i; j < planning_max_tasks; ++j) {
                    max_load = std::max(sit().strategy.task(agent, i).result.load, max_load);
                }
                fatten_amount = std::min(
                    (u8)((world->roles[agent].load - max_load) / w_item.volume),
                    t_.result.left
                );
                fatten_amount = std::min(for_item.amount, fatten_amount);
                if (fatten_amount) {
                    fatten_index = i;
                    break;
                }
            }
        }

        // Consider whether there are enough slots
        u8 idling = fast_forward_steps - sit().last_time(agent);
        bool idleflag = idling > max_idle_time;
        bool slots_1 = task_count < planning_max_tasks - (not involved)     and idleflag;
        bool slots_2 = task_count < planning_max_tasks - (not involved) + 1 and idleflag;

        // The agent should be able to carry at least one item
        u8 carryable = (world->roles[agent].load - sit().self(agent).load) / w_item.volume;
        if (carryable == 0) continue;

        u8 rating = 1;
        if (involved) {
            rating += rate_additem_involved;
        }
        if (idling > max_idle_time) {
            rating += (u8)((idling - max_idle_time) * rate_additem_idlescale);
        }
        if (carryable >= for_item.amount) {
            rating += rate_additem_carryall;
        } else {
            for_item.amount = carryable;
        }

        if (slots_1 and have > 0) { // Note that this may not be enough items
            viables[viable_count++] = {
                (u8)(rating + rate_additem_inventory), agent, index, Viable_t::ONLY_MOVE, involved
            };
        }
        if (slots_1 and in_shops and have < for_item.amount) {
            viables[viable_count++] = {
                (u8)(rating + rate_additem_shop), agent, index, Viable_t::BUY, involved,
                (u8)(for_item.amount - have)
            };
        }
        if (slots_1 and may_craft and have < for_item.amount) {
            viables[viable_count++] = {
                (u8)(rating + rate_additem_crafting), agent, index, Viable_t::CRAFT, involved,
                (u8)(for_item.amount - have)
            };
        }
        if (slots_2 and fatten_amount > 0 and have < for_item.amount) { // Note that this may not be enough items
            viables[viable_count++] = {
                (u8)(rating + rate_additem_fatten), agent, fatten_index, Viable_t::FATTEN, involved,
                (u8)(fatten_amount - have)
            };
        }
    }

    auto way = rng.choose_weighted(viables, viable_count);
    if (not way) {
        s.pop_task(for_agent, for_index);
        return;
    }

    //JDBG_D < way->rating < way->agent < way->index < way->type < way->no_tail < way->amount,0;
    
    u8 agent = way->agent;
    u8 index = way->index;

    // Add the tail
    if (not way->no_tail) {
        u8 tail_index = 0;
        for (u8 i = 0; i < planning_max_tasks; ++i) {
            tail_index += s.task(agent, i).task.type != Task::NONE;
        }
        // If there is an error, insert the task in front, to prevent deadlocks
        if (tail_index and sit().strategy.task(agent, tail_index - 1).result.err) {
            --tail_index;
        }
        
        if (is_deliver) {
            // If it's a delivery, move the task over
            Task new_task = s.pop_task(for_agent, for_index);
            new_task.id = ++task_next_id;
            s.insert_task(agent, tail_index, new_task);
        } else {
            s.insert_task(agent, tail_index, Task {
                Task::CRAFT_ASSIST,
                s.task(for_agent, for_index).task.where,
                ++task_next_id,
                for_item,
                {},
                s.task(for_agent, for_index).task.item.amount
            });
            s.task(agent, tail_index).task.crafter = s.task(for_agent, for_index).task.crafter;
        }
    }
    
    // Pathfinding considerations
    u8 from_id = index > 0
        ? s.task(agent, index - 1).task.where
        : orig().self(agent).name;
    u8 to_id = s.task(agent, index).task.where;
    
    if (way->type == Viable_t::ONLY_MOVE) {
        // nothing
    } else if (way->type == Viable_t::BUY) {
        for_item.amount = way->amount;
        u16 min_dist = std::numeric_limits<u32>::max();
        u8 min_arg = 0;
        for (auto const& i: orig().shops) {
            if (auto j = find_by_id(i.items, for_item.id)) {
                if (j->amount < for_item.amount) continue;
            } else {
                continue;
            }
            
            u16 d = dist_cache.lookup_old(from_id, i.id) + dist_cache.lookup_old(i.id, to_id);
            
            // TODO: Respect costs
        
            if (d < min_dist) {
                min_dist = d;
                min_arg = i.id;
            }
        }
        assert(min_arg != 0);

        s.insert_task(agent, index, Task {Task::BUY_ITEM, min_arg, ++task_next_id, for_item});
    } else if (way->type == Viable_t::CRAFT) {
        for_item.amount = way->amount;
        Pos from = index > 0
            ? orig().find_pos(s.task(agent, index - 1).task.where)
            : orig().self(agent).pos;
        Pos to = orig().find_pos(s.task(agent, index).task.where);
        
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

        s.insert_task(agent, index, Task {Task::CRAFT_ITEM, min_arg, ++task_next_id, for_item, agent});
        s.task(agent, index).task.crafter.id = agent;
        s.task(agent, index).task.crafter.check = (u8)(rng.rand() & 0xff);
    } else if (way->type == Viable_t::FATTEN) {
        s.task(agent, index).task.item.amount += way->amount;
        s.task(agent, index).task.id = ++task_next_id;
    } else {
        assert(false);
    }
}

void Simulation_state::remove_task(u8 agent, u8 index) {
    orig().strategy.pop_task(agent, index);
}

void Simulation_state::reduce_load(u8 agent, u8 index) {
    int space = world->roles[agent].load - sit().self(agent).load;
    auto& t = orig().strategy.task(agent, index);
    int possible = space / get_by_id(world->items, t.task.item.id).volume;
    if (possible == 0) {
        orig().strategy.pop_task(agent, index);
    } else {
        t.task.item.amount = possible;
    }
}

void Simulation_state::reduce_buy(u8 agent, u8 index, Item_stack arg) {
    auto& t = orig().strategy.task(agent, index);
    if (arg.amount < t.task.item.amount) {
        t.task.item.amount -= arg.amount;
    } else {
        orig().strategy.pop_task(agent, index);
    }
}


void Simulation_state::reduce_assist(u8 agent, u8 index, Item_stack arg) {
    auto& t = orig().strategy.task(agent, index);
    if (arg.amount < t.task.item.amount) {
        t.task.item.amount -= arg.amount;
    } else {
        orig().strategy.pop_task(agent, index);
    }
}


void Strategy::insert_task(u8 agent, u8 index, Task task_) {
    for (u8 i = planning_max_tasks - 1; i > index; --i) {
        task(agent, i) = task(agent, i-1);
    }
    task(agent, index).task = task_;
}

void Simulation_state::fix_errors() {
    for (int it = 0; it < fixer_iterations; ++it) {
        reset();
        fast_forward();

        //JDBG_D < sit().strategy.p_results() ,1;
        JDBG_D < sit().strategy.p_tasks() ,0;

        u8 best_agent = number_of_agents;
        int value = std::numeric_limits<int>::max();
        for (u8 agent = 0; agent < number_of_agents; ++agent) {
            auto& d = sit().self(agent);
            if (d.task_state != 0xfe) continue;
            assert(d.task_index > 0);
            
            u8 index = d.task_index - 1;
            auto& r = sit().strategy.task(agent, index).result;
            if (r.err == Task_result::SUCCESS) {
                continue;
            }

            auto& ot = orig().strategy.task(agent, index);
            u16 index_diff = task_next_id - ot.task.id;
            if (index_diff < value) {
                best_agent = agent;
                value = index_diff;
            }
        }

        if (best_agent != number_of_agents) {
            u8 agent = best_agent;
            
            auto& d = sit().self(agent);
            u8 index = d.task_index - 1;
            auto& r = sit().strategy.task(agent, index).result;

            auto& ot = orig().strategy.task(agent, index);
            ++ot.task.fixer_it;
            if (ot.task.fixer_it > fixer_it_limit) {
                remove_task(agent, index);
            } else if (r.err == Task_result::OUT_OF_BATTERY) {
                add_charging(agent, index);
            } else if (r.err == Task_result::CRAFT_NO_ITEM) {
                add_item_for(agent, index, r.err_arg, false);
            } else if (r.err == Task_result::CRAFT_NO_ITEM_SELF) {
                add_item_for(agent, index, r.err_arg, false);
            } else if (r.err == Task_result::CRAFT_NO_TOOL) {
                add_item_for(agent, index, r.err_arg, true);
            } else if (r.err == Task_result::NO_CRAFTER_FOUND) {
                remove_task(agent, index);
            } else if (r.err == Task_result::NOT_IN_INVENTORY) {
                add_item_for(agent, index, r.err_arg, false);
            } else if (r.err == Task_result::NOT_VALID_FOR_JOB) {
                remove_task(agent, index);
            } else if (r.err == Task_result::NO_SUCH_JOB) {
                remove_task(agent, index);
            } else if (r.err == Task_result::MAX_LOAD) {
                reduce_load(agent, index);
            } else if (r.err == Task_result::ASSIST_USELESS) {
                reduce_assist(agent, index, r.err_arg);
            } else if (r.err == Task_result::DELIVERY_USELESS) {
                remove_task(agent, index);
            } else if (r.err == Task_result::NOT_IN_SHOP) {
                reduce_buy(agent, index, r.err_arg);
            } else {
                assert(false);
            }
            
            JDBG_D < it < agent < index < r ,0;
        } else {
            jdbg < "NOERR" < it ,0;
            break;
        }
    }
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
            if (sit().last_time(agent) < fast_forward_steps - max_idle_time
                and d.task_index + 1 < planning_max_tasks)
            {
                viable_agents[viable_agents_count++] = agent;
            }
        }
    }

    bool has_err = false;
    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        auto const& d = sit().self(agent);
        if (d.task_index > 0 and sit().strategy.task(agent, d.task_index - 1).result.err) {
            has_err = true;
        }
    }
    if (has_err) return;

    struct Viable_job_t {
        u16 rating;
        u16 job_id;
    };

    Viable_job_t* viable_jobs = (Viable_job_t*)alloca(orig().jobs.size() * sizeof(Viable_job_t));
    int viable_job_count = 0;

    auto add_job = [&](Job const& job, int fine) {
        bool started = false;
        for (u8 agent = 0; agent < number_of_agents; ++agent) {
            for (u8 i = 0; i < planning_max_tasks; ++i) {
                auto const& t = sit().strategy.task(agent, i);
                started |= t.task.type == Task::DELIVER_ITEM and t.task.job_id == job.id;
            }
        }
        if (not started and job.required.size()+2 > viable_agents_count) return;

        int cost = 0;
        int complexity = 0;
        for (auto i: job.required) {
            int need = i.amount;
            for (u8 agent = 0; agent < number_of_agents; ++agent) {
                if (auto j = find_by_id(sit().self(agent).items, i.id)) {
                    need -= std::min((int)j->amount, need);
                }
            }

            auto const& i_cost = get_by_id(world->item_costs, i.id);
            cost += i_cost.value() * need;
            cost += (u8)((float)(i_cost.value() * (i.amount - need)) * rate_job_havefac) ;
            complexity += i_cost.craftval * i.amount;
        }
        
        float profit = ((float)(job.reward + fine) - (float)cost * rate_job_cost) / (float)complexity;
        profit = std::max(profit, 0.f);

        u8 rating = 1;
        rating += started ? rate_job_started : 0;
        rating += (u8)(profit * rate_job_profit);
        viable_jobs[viable_job_count++] = {rating, job.id};
    };
    
    for (Job const& job: sit().jobs) {
        add_job(job, 0);
    }
    for (Mission const& job: sit().missions) {
        add_job(job, job.fine);
    }

    if (viable_job_count > 0) {
        u16 job_id = rng.choose_weighted(viable_jobs, viable_job_count)->job_id;
        u8 job_type;
        Job const& job = sit().get_by_id_job(job_id, &job_type);

        for (auto const& i: job.required) {
            if (viable_agents_count <= 1) break;
                
            // TODO: more intelligent agent dispatch, to save add_item_for some work
            int agent_index = rng.gen_uni(viable_agents_count);
            u8 agent = viable_agents[agent_index];
            viable_agents[agent_index] = viable_agents[--viable_agents_count];

            s.task(agent, sit().self(agent).task_index).task = Task {
                Task::DELIVER_ITEM, job.storage, ++task_next_id, i, job.id
            };
        }
    } else {
        // Add an item

        /*u8* viable_items = (u8*)alloca(world->items.size() * sizeof(u8));
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
        s.task(agent, i).task = Task { Task::CRAFT_ITEM, min_arg, ++task_next_id, {item_id, 1} };
        s.task(agent, i).task.crafter.id = agent;
        s.task(agent, i).task.crafter.check = (u8)(rng.rand() & 0xff);
        */
    }
}

void Simulation_state::optimize() {
    // Remove unnecessary items
    
    for (int it = 0; it < optimizer_iterations; ++it) {
        reset();
        fast_forward();

        //JDBG_L < orig().strategy.p_results() ,1;
        //JDBG_L < orig().strategy.p_tasks() ,0;
        
        bool dirty = false;
        auto& s = orig().strategy;
        for (u8 agent = 0; agent < number_of_agents; ++agent) {
            auto& d = sit().self(agent);
            if (sit().last_time(agent) == fast_forward_steps) continue;
            if (d.task_index and sit().strategy.task(agent, d.task_index - 1).result.err) continue;
            
            for (auto const& item: d.items) {
                int extra = item.amount - world->roles[agent].tools.count(item.id);
                if (extra <= 0) continue;
        
                for (u8 i = planning_max_tasks - 1; i < planning_max_tasks; --i) {
                    auto& t = s.task(agent, i);
                    if (t.task.type == Task::BUY_ITEM or t.task.type == Task::CRAFT_ITEM) {
                        int remove = std::min(extra, (int)t.task.item.amount);
                        t.task.item.amount -= remove;
                        extra -= remove;
                        if (t.task.item.amount == 0) {
                            s.pop_task(agent, i);
                        }
                        dirty = true;
                    }
                }
            }
        }

        if (not dirty) break;
        //JDBG_L ,0;
    }
}

float Simulation_state::rate() {
    float rating = sit().team_money;

    // Count all items inside the agents inventory
    float item_rating = 0;
    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        for (auto const& i: sit().self(agent).items) {
            item_rating += get_by_id(world->item_costs, i.id).value() * i.amount;
        }
    }
    rating += item_rating * rate_val_item;

    return rating;
}


} /* end of namespace jup */
