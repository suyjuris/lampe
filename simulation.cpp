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

    if (sit_old) {
        this_->book.other_team_auction = sit_old->book.other_team_auction;
        for (auto const& job: this_->auctions) {
            if (job.lowest_bid > 0 and job.lowest_bid < job.reward) {
                this_->book.other_team_auction = true;
            }
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
    
void Situation::flush_old(World const& world, Situation const& old, Diff_flat_arrays* diff) {
    assert(diff);
    moving_on(world, old, diff);
    
    assert(&strategy.task(0, 1) - &strategy.task(0, 0) == 1);

    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        if (self(agent).task_index == 0) continue;
        while (self(agent).task_index) {
            strategy.pop_task(agent, 0);
            --self(agent).task_index;
        }
        self(agent).task_index = 0;
        self(agent).task_state = 0;
        self(agent).task_sleep = 0;
    }
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
    diff->register_commit();
}

bool Situation::agent_goto(u8 where, u8 agent, Buffer* into) {
    auto& d = self(agent);

    // Could send continue if same location as last time
    if (d.charge < 10) {
        into->emplace_back<Action_Recharge>();
        return false;
    } else if (d.facility == where) {
        return true;
    } else {
        into->emplace_back<Action_Goto1>(where);
        return false;
    }
}

void Situation::moving_on(World const& world, Situation const& old, Diff_flat_arrays* diff) {
    moving_on_one(world, old, diff);

    while (true) {
        // Check whether some assemblers are not required
        Crafting_plan plan = combined_plan(world);

        for (u8 agent = 0; agent < number_of_agents; ++agent) {
            bool flag = false;
            if (plan.slot(agent).type == Crafting_slot::USELESS) {
                flag = true;
            } else if (plan.slot(agent).type == Crafting_slot::UNINVOLVED
                and self(agent).task_index < planning_max_tasks
                and task(agent).task.type == Task::CRAFT_ASSIST)
            {
                // Only need this so long as we cannot trust the ASSIST_ASSEMBLE results
                flag = true;
                for (u8 o_agent = 0; o_agent < number_of_agents; ++o_agent) {
                    for (u8 i = 0; i < planning_max_tasks; ++i) {
                        if (strategy.task(o_agent, i).task.type == Task::CRAFT_ITEM
                            and strategy.task(o_agent, i).task.craft_id == task(agent).task.craft_id)
                        {
                            flag = false;
                        }
                    }
                }
            }
            
            if (flag) {
                auto& d = self(agent);
                ++d.task_index;
                d.task_state = 0;
                d.task_sleep = 0;
            }
        }

        if (not moving_on_one(world, old, diff)) break;
    }
}
    
bool Situation::moving_on_one(World const& world, Situation const& old, Diff_flat_arrays* diff) {
    bool dirty = false;
    
    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        auto& d = self(agent);
        while (d.task_index < planning_max_tasks) {
            auto& t = task(agent);
        
            bool move_on = false;
        
            if (t.task.type == Task::NONE) {
                break;
            } else if (t.task.type == Task::BUY_ITEM) {
                if (d.facility != t.task.where) break;
                
                if (d.action_type == Action::BUY and not d.action_result) {
                    d.action_type = Action::ABORT;
                    t.task.item.amount = 0;
                } 
                if (t.task.item.amount == 0) {
                    move_on = true;
                } else {
                    break;
                }
            } else if (t.task.type == Task::RETRIEVE) {
                if (d.facility != t.task.where) break;
                
                if (d.action_type == Action::RETRIEVE_DELIVERED and not d.action_result) {
                    d.action_type = Action::ABORT;
                    t.task.item.amount = 0;
                } 
                if (t.task.item.amount == 0) {
                    move_on = true;
                } else {
                    break;
                }
            } else if (t.task.type == Task::CRAFT_ITEM) {
                if (d.facility != t.task.where) break;
                
                if (d.action_type == Action::ASSEMBLE and not d.action_result) {
                    d.action_type = Action::ABORT;
                    --t.task.item.amount;
                } 
                if (t.task.item.amount == 0) {
                    move_on = true;
                } else {
                    break;
                }
            } else if (t.task.type == Task::CRAFT_ASSIST) {
                if (d.facility != t.task.where) break;
                
                if (d.action_type == Action::ASSIST_ASSEMBLE and not d.action_result) {
                    d.action_type = Action::ABORT;
                    //--t.task.cnt;
                } 
                if (t.task.cnt == 0) {
                    move_on = true;
                } else {
                    break;
                }
            } else if (t.task.type == Task::DELIVER_ITEM) {
                if (d.facility != t.task.where) break;
        
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
                    break;
                }            
            } else if (t.task.type == Task::CHARGE) {
                if (d.facility != t.task.where) break;
        
                if (d.charge == world.roles[agent].battery) {
                    move_on = true;
                } else {
                    break;
                }
            } else if (t.task.type == Task::VISIT) {
                if (d.facility != t.task.where) break;
                move_on = true;
            } else {
                assert(false);
            }
        
            assert(move_on);
        
            ++d.task_index;
            d.task_state = 0;
            d.task_sleep = 0;
            dirty = true;
        }
    }

    return dirty;
}

void Situation::idle_task(World const& world, Situation const& old, u8 agent,
    Array<Auction_bet>* bets, Buffer* into)
{
    if (bets->size()) {
        auto bet = bets->back();
        bets->addsize(-1);
        into->emplace_back<Action_Bid_for_job>(bet.job_id, bet.bet);
    } else {
        into->emplace_back<Action_Recharge>();
    }
}

void Situation::get_action(World const& world, Situation const& old, u8 agent,
    Crafting_slot const& cs, Array<Auction_bet>* bets, Buffer* into)
{
    assert(bets);
    auto& t = task(agent);

    if (t.task.type == Task::NONE) {
        idle_task(world, old, agent, bets, into);
        return;
    } else if (t.task.type == Task::BUY_ITEM) {
        if (agent_goto(t.task.where, agent, into)) {
            into->emplace_back<Action_Buy>(t.task.item);
        }
        return;

    } else if (t.task.type == Task::RETRIEVE) {
        if (agent_goto(t.task.where, agent, into)) {
            into->emplace_back<Action_Retrieve_delivered>(t.task.item);
        }
        return;

    } else if (t.task.type == Task::CRAFT_ITEM or t.task.type == Task::CRAFT_ASSIST) {
        if (agent_goto(t.task.where, agent, into)) {
            if (cs.type == Crafting_slot::UNINVOLVED or cs.type == Crafting_slot::IDLE) {
                idle_task(world, old, agent, bets, into);
            } else if (cs.type == Crafting_slot::EXECUTE) {
                assert(task(cs.agent).task.type == Task::CRAFT_ITEM
                    and task(cs.agent).task.craft_id == t.task.craft_id);
                if (t.task.type == Task::CRAFT_ITEM) {
                    into->emplace_back<Action_Assemble>(t.task.item.id);
                } else {
                    into->emplace_back<Action_Assist_assemble>(self(cs.agent).name);
                }
            } else if (cs.type == Crafting_slot::GIVE) {
                into->emplace_back<Action_Give>(self(cs.agent).name, cs.item);
            } else if (cs.type == Crafting_slot::RECEIVE) {
                into->emplace_back<Action_Receive>();
            } else if (cs.type == Crafting_slot::USELESS) {
                t.task.item.amount = 0;
                idle_task(world, old, agent, bets, into);
            } else {
                assert(false);
            }
        }
        return;

    } else if (t.task.type == Task::DELIVER_ITEM) {
        if (agent_goto(t.task.where, agent, into)) {
            into->emplace_back<Action_Deliver_job>(t.task.job_id);
        }
        return;

    } else if (t.task.type == Task::CHARGE) {
        if (agent_goto(t.task.where, agent, into)) {
            into->emplace_back<Action_Charge>();
        }
        return;

    } else if (t.task.type == Task::VISIT) {
        agent_goto(t.task.where, agent, into);
        return;
    } else {
        assert(false);
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

void Situation::add_item_to_agent(u8 agent, Item_stack item, Diff_flat_arrays* diff) {
    for (auto& i: self(agent).items) {
        if (i.id == item.id or i.amount == 0) {
            i.id = item.id;
            i.amount += item.amount;
            return;
        }
    }
    diff->add(self(agent).items, item);
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

    u32 dist_add = std::numeric_limits<u32>::max();
    for (auto const& i: charging_stations) {
        dist_add = std::min(dist_add, (u32)dist_cache->lookup(target_id, i.id));
    }

    if (dist + dist_add + speed > d.charge / 10 * speed) {
        task(agent).result.err = Task_result::OUT_OF_BATTERY;
        d.task_state = 0xfe;
        d.task_sleep = 0xff;
    } else {
        auto dur = (dist + speed-1) / speed;
        d.charge -= dur * 10;
        d.task_sleep = dur;
        d.facility = target_id;
        d.pos = find_pos(target_id);
        dist_cache->move_to(d.name, target_id);
    }
}

static Array_view<u8> agent_first(u8 agent) {
    static u8 result[number_of_agents];
    result[0] = agent;
    for (u8 i = 0; i < number_of_agents - 1; ++i)
        result[i+1] = i + (i >= agent);
    return {result, number_of_agents};
}
static u8 diff_min(Task const& task, u8 item_id) {
    // Returns an upper bound of the difference in item_id

    switch (task.type) {
    case Task::NONE:
    case Task::CHARGE:
    case Task::VISIT:
        return 0;

    case Task::BUY_ITEM:
    case Task::RETRIEVE:
    case Task::CRAFT_ITEM:
        return task.item.id != item_id ? 0 : task.item.amount;

    case Task::CRAFT_ASSIST:
    case Task::DELIVER_ITEM:
        return task.item.id != item_id ? 0 : -task.item.amount;

    default:
        assert(false);
    }
}

bool Situation::is_possible_item(World const& world, u8 agent, Task_slot& t, Item_stack i,
    bool is_tool, bool at_all, Crafting_plan* plan)
{
    int count = is_tool ? 1 : i.amount * t.task.item.amount;

    for (u8 o_agent: agent_first(agent)) {
        auto const& o_d = self(o_agent);
        if (o_d.task_index >= planning_max_tasks) continue;
        if (is_tool and not world.roles[o_agent].tools.count(i.id)) continue;
        
        auto const& o_t = strategy.task(o_agent, o_d.task_index);
        bool involved = (
            o_agent == agent or (
                o_t.task.type == Task::CRAFT_ASSIST
                and o_t.task.craft_id == t.task.craft_id
                and o_d.facility == o_t.task.where
                and (o_d.task_sleep == 0 or o_d.task_state == 2)
            )
        );
        int have = 0;
        if (auto j = find_by_id(o_d.items, i.id)) {
            have = j->amount;
        }

        if (plan) {
            if (involved and plan->slot(o_agent).type == Crafting_slot::UNINVOLVED) {
                plan->slot(o_agent).type = Crafting_slot::USELESS;                
            }
            if (involved and count and have) {
                if (is_tool) {
                    plan->slot(o_agent).type = Crafting_slot::IDLE;
                } else if (plan->slot(o_agent).type == Crafting_slot::USELESS) {
                    plan->slot(o_agent).type = Crafting_slot::GIVE;
                    plan->slot(o_agent).item = {i.id, (u8)std::min(have, count)};
                }
            }
        }

        if (involved) {
            count -= std::min(have, count);
        } else if (at_all) {
            // Check whether a future task may bring the items

            for (u8 o_i = o_d.task_index; o_i < planning_max_tasks; ++o_i) {
                auto const& o_tt = strategy.task(o_agent, o_i);
                if (
                    o_tt.task.type == Task::CRAFT_ASSIST
                    and o_tt.task.craft_id == t.task.craft_id
                ) {
                    count -= std::min(have, count);
                    break;
                } else {
                    have += diff_min(o_tt.task, i.item);
                }
            }
        }
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
}

Crafting_plan Situation::crafting_orchestrator(World const& world, u8 agent) {
    auto& t = task(agent);
    assert(t.task.type == Task::CRAFT_ITEM);

    Crafting_plan plan = {};

    if (t.task.where != self(agent).facility) return plan;
    plan.slot(agent).type = Crafting_slot::IDLE;
    
    // Check whether the agent can craft the item _right now_
    auto is_possible_right_now = [&]() -> bool {
        Item const& item = get_by_id(world.items, t.task.item.id);

        // Evaluate all of the, because the states in plan need to be updated.
        bool possible = true;
        for (Item_stack i: item.consumed) {
            possible &= is_possible_item(world, agent, t, i, false, false, &plan);
        }
        for (u8 i: item.tools) {
            possible &= is_possible_item(world, agent, t, {i, 1}, true, false, &plan);
        }
        
        return possible;
    };

    if (is_possible_right_now()) {
        for (u8 o_agent = 0; o_agent < number_of_agents; ++o_agent) {
            if (plan.slot(o_agent).type == Crafting_slot::UNINVOLVED) continue;
            plan.slot(o_agent).type = Crafting_slot::EXECUTE;
            plan.slot(o_agent).agent = agent;
        }
    } else {
        // Try to give the items to someone else
        for (u8 o_agent = number_of_agents - 1; o_agent < number_of_agents; --o_agent) {
            if (plan.slot(o_agent).type != Crafting_slot::GIVE) continue;

            auto const& w_item = get_by_id(world.items, plan.slot(o_agent).item.id);
            int load = w_item.volume * plan.slot(o_agent).item.amount;

            u8 found_agent = 0xff;
            for (u8 q_agent = number_of_agents - 1; q_agent < number_of_agents; --q_agent) {
                if (plan.slot(q_agent).type != Crafting_slot::IDLE
                    and plan.slot(q_agent).type != Crafting_slot::RECEIVE) continue;
                //and not (plan.slot(o_agent).type == Crafting_slot::GIVE and q_agent > o_agent)*/
                if (world.roles[q_agent].load - self(q_agent).load
                    - plan.slot(q_agent).extra_load < load) continue;

                found_agent = q_agent;
                break;
            }

            if (found_agent != 0xff) {
                plan.slot(found_agent).type = Crafting_slot::RECEIVE;
                plan.slot(found_agent).extra_load += load;
                plan.slot(o_agent).agent = found_agent;
            } else {
                plan.slot(o_agent).type = Crafting_slot::IDLE;
            }
        }
    }

    return plan;
}


Crafting_plan Situation::combined_plan(World const& world) {
    Crafting_plan result = Crafting_plan {};
    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        if (task(agent).task.type == Task::CRAFT_ITEM) {
            auto plan = crafting_orchestrator(world, agent);
            for (u8 o_agent = 0; o_agent < number_of_agents; ++o_agent) {
                assert(not (result.slot(o_agent).type and plan.slot(o_agent).type));
                if (plan.slot(o_agent).type) {
                    result.slot(o_agent) = plan.slot(o_agent);
                }
            }
        }
    }
    return result;
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
            add_item_to_agent(agent, t.task.item, diff);
            return;
        }
    } else if (t.task.type == Task::RETRIEVE) {
        if (d.task_state == 0) {
            d.task_state = 1;
            agent_goto_nl(world, dist_cache, agent, t.task.where);
        }
        if (d.task_state == 1 and d.task_sleep == 0) {
            auto& storage = get_by_id(storages, t.task.where);
            auto const& w_item = get_by_id(world.items, t.task.item.id);
            auto item = find_by_id(storage.items, t.task.item.id);

            if (not item or item->delivered < t.task.item.amount) {
                t.result.err = Task_result::NOT_IN_SHOP;
                t.result.err_arg = {item->id, (u8)(t.task.item.amount - item->delivered)};
                d.task_state = 0xfe;
                return;
            } else {
                d.task_sleep = 1;
            }

            if (d.load + w_item.volume * t.task.item.amount > world.roles[agent].load) {
                t.result.err = Task_result::MAX_LOAD;
                d.task_state = 0xfe;
                return;                
            }

            d.task_state = 0xff;
            item->delivered -= t.task.item.amount;
            t.result.left = item->delivered;
            d.load += w_item.volume * t.task.item.amount;
            add_item_to_agent(agent, t.task.item, diff);
            return;
        }
    } else if (t.task.type == Task::CRAFT_ITEM) {
        if (d.task_state == 0) {
            d.task_state = 1;
            agent_goto_nl(world, dist_cache, agent, t.task.where);
        }

        // Try to disprove that the agent can craft the item
        auto is_possible_at_all = [&]() -> bool {
            Item const& item = get_by_id(world.items, t.task.item.id);

            for (u8 i: item.tools) {
                if (not is_possible_item(world, agent, t, {i, 1}, true, true)) return false;
            }
            for (Item_stack i: item.consumed) {
                if (not is_possible_item(world, agent, t, i, false, true)) return false;
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
            // Wait one fast_forward iteration for concurrent actions to be commited
            return;
        }
        if (d.task_state == 2 and d.task_sleep == 0) {
            Crafting_plan plan = crafting_orchestrator(world, agent);
            if (plan.slot(agent).type != Crafting_slot::EXECUTE) {
                if (is_possible_at_all()) {
                    // One of the assists will hopefully wake us up
                    d.task_sleep = 0xff;

                    // Execute the crafting plan
                    for (u8 o_agent = 0; o_agent < number_of_agents; ++o_agent) {
                        auto const& cs = plan.slot(o_agent);
                        switch (cs.type) {
                        case Crafting_slot::UNINVOLVED:
                        case Crafting_slot::IDLE:
                        case Crafting_slot::RECEIVE:
                            break;
                        case Crafting_slot::USELESS:
                            self(o_agent).task_state = 0xff;
                            self(o_agent).task_sleep = 1;
                            break;
                        case Crafting_slot::GIVE: {
                            auto const& w_item = get_by_id(world.items, cs.item.id);
                            self(o_agent).load  -= w_item.volume * cs.item.amount;
                            self(cs.agent).load += w_item.volume * cs.item.amount;
                            get_by_id(self(o_agent).items, cs.item.id).amount -= cs.item.amount;
                            add_item_to_agent(cs.agent, cs.item, diff);
                            d.task_sleep = 1;
                            self(o_agent).task_state = 0xff;
                            self(o_agent).task_sleep = 1;
                        } break;
                        case Crafting_slot::EXECUTE:
                        default:
                            assert(false); break;
                        }
                    }
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
                            or o_t.task.craft_id != t.task.craft_id
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
            add_item_to_agent(agent, t.task.item, diff);
            d.load += item.volume * t.task.item.amount;
            
            for (u8 o_agent = 0; o_agent < number_of_agents; ++o_agent) {
                auto const& o_d = self(o_agent);
                if (o_d.task_index == planning_max_tasks) continue;
                auto const& o_t = task(o_agent);
                if (
                    o_t.task.type != Task::CRAFT_ASSIST
                    or o_t.task.craft_id != t.task.craft_id
                    or o_d.task_state != 2
                ) continue;
                self(o_agent).task_state = 0xff;
                self(o_agent).task_sleep = d.task_sleep;
            }
            return;
        }
    } else if (t.task.type == Task::CRAFT_ASSIST) {
        if (d.task_state == 0) {
            d.task_state = 1;
            agent_goto_nl(world, dist_cache, agent, t.task.where);
        }
        if (d.task_state == 1 and d.task_sleep == 0) {
            d.task_sleep = 0xff;
            d.task_state = 2;


            bool found = false;
            u8 o_agent;
            u8 i;
            for (o_agent = 0; o_agent < number_of_agents; ++o_agent) {
                for (i = self(o_agent).task_index; i < planning_max_tasks; ++i) {
                    auto const& o_t = strategy.task(o_agent, i);
                    if (o_t.task.type == Task::CRAFT_ITEM and o_t.task.craft_id == t.task.craft_id) {
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
            if (not found) {
                t.result.err = Task_result::NO_CRAFTER_FOUND;
                d.task_state = 0xfe;
                return;
            }
            auto& c_s = self(o_agent);
            
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
            d.task_state = 1;
            agent_goto_nl(world, dist_cache, agent, t.task.where);
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
            d.task_state = 1;
            agent_goto_nl(world, dist_cache, agent, t.task.where);

            if (d.task_state == 0xfe and t.result.err == Task_result::OUT_OF_BATTERY and d.task_index == 0) {
                d.task_sleep = 1;
                d.task_state = 1;
                t.result.err = 0;
                d.charge += recharge_rate;
            }
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
    orig().register_arr(&diff);

    // Changes to orig() may be made here, probably using diff

    // Add some space to the inventories for performance
    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        auto& items = orig().self(agent).items;
        for (int i = items.size(); i < inventory_size_min; ++i) {
            diff.add(items, {0, 0});
        }
    }
    diff.apply();
    diff.reset();
    
    orig_size = sit_buffer_->size() - sit_offset_;
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
        //dist_cache.calc_facilities();
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
    fast_forward(std::min(sit().simulation_step + fast_forward_steps, (int)world->steps));
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
        // If two items of the same type get added, things break. Only consider agents' inventories,
        // as this is currently the only place this happens
        for (u8 agent = 0; agent < number_of_agents; ++agent) {
            auto& d = sit().self(agent);
            for (u8 i = 0; i+1 < d.items.size(); ++i) {
                for (u8 j = i+1; j < d.items.size(); ++j) {
                    if (d.items[i].id == d.items[j].id) {
                        d.items[i].amount += d.items[j].amount;
                        d.items[j].id = 0;
                        diff.remove(d.items, j);
                    }
                }
            }
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

            // TODO: If the shop does not have an item, there will be no entry to increment
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
        assert(before != 0);
        std::swap(s.task(agent, before - 1), s.task(agent, before));
        index = before - 1;
    } else {
        index = before;
        s.insert_task(agent, index, Task {0, 0, ++s.task_next_id});
    }

    u8 from_id = index > 0
        ? s.task(agent, index - 1).task.where
        : orig().self(agent).name;
    u8 to_id = index + 1 < planning_max_tasks
        ? s.task(agent, index + 1).task.where
        : from_id;
    
    u16 min_dist = std::numeric_limits<u16>::max();
    u8 min_arg = 0;
    for (auto& i: orig().charging_stations) {
        u16 d = dist_cache.lookup_old(from_id, i.id) + dist_cache.lookup_old(i.id, to_id);

        // TODO: Respect charging time
        
        if (d < min_dist) {
            min_dist = d;
            min_arg = i.id;
        }
    }
    
    s.task(agent, index).task = Task {Task::CHARGE, min_arg, ++s.task_next_id, {}};
}

void Simulation_state::add_item_for(u8 for_agent, u8 for_index, Item_stack for_item, bool for_tool) {
    struct Viable_t {
        enum Type: u8 {
            INVALID = 0, ONLY_MOVE, BUY, RETRIEVE, CRAFT, FATTEN
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
    Viable_t viables[number_of_agents * 5];

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

    u8 storage_amount = 0;
    for (auto const& i: orig().storages) {
        if (auto j = find_by_id(i.items, for_item.id)) {
            storage_amount = std::max(j->delivered, storage_amount);
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
                    and s.task(agent, i).task.craft_id == t.task.craft_id
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
                    and not sit().strategy.task(agent, i).result.err
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
            have = involved ? 0 : std::min(item->amount, for_item.amount);
        }

        // Fattening
        u8 fatten_amount = 0;
        u8 fatten_index = 0;
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
                if (fatten_amount) {
                    fatten_index = i;
                    break;
                }
            }
        }

        // Consider whether there are enough slots
        u8 idling = sit().simulation_step - last_time(agent);
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
        }
        u8 final_amount = std::min((u8)(for_item.amount - have), carryable);

        if (slots_1 and have > 0) { // Note that this may not be enough items
            viables[viable_count++] = {
                (u8)(rating + rate_additem_inventory), agent, index, Viable_t::ONLY_MOVE, involved, have
            };
        }
        if (slots_1 and in_shops and have < for_item.amount) {
            viables[viable_count++] = {
                (u8)(rating + rate_additem_shop), agent, index, Viable_t::BUY,
                involved, final_amount
            };
        }
        if (slots_1 and storage_amount > 0 and have < for_item.amount) {
            viables[viable_count++] = {
                (u8)(rating + rate_additem_retrieve), agent, index, Viable_t::RETRIEVE,
                involved, std::min(storage_amount, final_amount)
            };
        }
        if (slots_1 and may_craft and have < for_item.amount) {
            viables[viable_count++] = {
                (u8)(rating + rate_additem_crafting), agent, index, Viable_t::CRAFT,
                involved, final_amount
            };
        }
        if (slots_2 and fatten_amount > 0 and have < for_item.amount) { // Note that this may not be enough items
            viables[viable_count++] = {
                (u8)(rating + rate_additem_fatten), agent, fatten_index, Viable_t::FATTEN, involved,
                std::min(fatten_amount, (u8)(for_item.amount - have))
            };
        }
    }

    auto way = rng.choose_weighted(viables, viable_count);
    if (not way) {
        s.pop_task(for_agent, for_index);
        return;
    }

    u8 agent = way->agent;
    u8 index = way->index;
    for_item.amount = way->amount;

    // Add the tail
    if (not way->no_tail) {
        u8 tail_index = 0;
        for (u8 i = 0; i < planning_max_tasks; ++i) {
            tail_index += s.task(agent, i).task.type != Task::NONE;
        }
        // If there is an error, insert the task in front, to prevent deadlocks
        auto& d = sit().self(agent);
        if (d.task_index and (sit().strategy.task(agent, d.task_index - 1).result.err
            or last_time(agent) == sit().simulation_step))
        {
            tail_index = d.task_index - 1;
        } else {
            assert(tail_index == d.task_index);
        }
        
        if (is_deliver) {
            // If it's a delivery, move the task over
            Task new_task = s.pop_task(for_agent, for_index);
            new_task.id = ++s.task_next_id;
            s.insert_task(agent, tail_index, new_task);
        } else {
            s.insert_task(agent, tail_index, Task {
                Task::CRAFT_ASSIST,
                s.task(for_agent, for_index).task.where,
                ++s.task_next_id,
                for_item,
                s.task(for_agent, for_index).task.craft_id,
                s.task(for_agent, for_index).task.item.amount
            });
        }
    } else {
        if (is_deliver and for_agent != agent) {
            // If it's a delivery, delete the old task
            s.pop_task(for_agent, for_index);
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
        u16 min_dist = std::numeric_limits<u16>::max();
        u8 min_arg = 0;
        for (auto const& i: orig().shops) {
            auto j = find_by_id(i.items, for_item.id);
            if (not j or j->amount < for_item.amount) continue;
            
            u16 d = dist_cache.lookup_old(from_id, i.id) + dist_cache.lookup_old(i.id, to_id);
            d = d / dist_price_fac + j->cost * for_item.amount;
        
            if (d < min_dist) {
                min_dist = d;
                min_arg = i.id;
            }
        }
        assert(min_arg != 0);

        s.insert_task(agent, index, Task {Task::BUY_ITEM, min_arg, ++s.task_next_id, for_item});
    } else if (way->type == Viable_t::RETRIEVE) {
        u8 min_arg = 0;
        for (auto const& i: orig().storages) {
            auto j = find_by_id(i.items, for_item.id);
            if (j and j->delivered >= for_item.amount) {
                min_arg = i.id;
                break;
            }
        }
        assert(min_arg != 0);

        s.insert_task(agent, index, Task {Task::RETRIEVE, min_arg, ++s.task_next_id, for_item});
    } else if (way->type == Viable_t::CRAFT) {
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

        u16 id = ++s.task_next_id;
        s.insert_task(agent, index, Task {Task::CRAFT_ITEM, min_arg, id, for_item, id});
    } else if (way->type == Viable_t::FATTEN) {
        s.task(agent, index).task.item.amount += way->amount;
        s.task(agent, index).task.id = ++s.task_next_id;
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

bool Simulation_state::fix_errors() {
    //debug_flag = orig().strategy.s_id == 3170 or orig().strategy.s_id == 3169;
    bool dirty = false;
    for (int it = 0; it < fixer_iterations; ++it) {
        reset();
        fast_forward();

        JDBG_D < sit().strategy.p_results() ,1;
        JDBG_D < orig().strategy.p_tasks() ,0;

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
            u16 index_diff = orig().strategy.task_next_id + (u16)(-ot.task.id);
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
            dirty = true;
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
            if (fix_deadlock()) {
                dirty = true;
            } else {
                break;
            }
        }
    }
    return dirty;
}
    
bool Simulation_state::fix_deadlock() {
    // Detect deadlock
    struct Edge_t {
        u8 a, b;
        u16 task_id;
    };
    struct Node_t {
        u16 id;
        u8 in, out;
        bool invalid;
    };

    u8 max_nodes = narrow<u8>(number_of_agents * planning_max_tasks);
    Array_view_mut<Node_t> nodes {(Node_t*)alloca(max_nodes * sizeof(Node_t)), max_nodes};
    std::memset(nodes.begin(), 0, nodes.as_bytes().size());
    u8 node_count = 0;

    u8 max_edges = narrow<u8>(number_of_agents * (planning_max_tasks - 1));
    Array_view_mut<Edge_t> edges {(Edge_t*)alloca(max_nodes * sizeof(Edge_t)), max_edges};
    u8 edge_count = 0;

    // Build the dependency graph
    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        u8 last_node = 0xff;
        u16 last_edge_id;
        for (u8 i = 0; i < planning_max_tasks; ++i) {
            auto& o_t = orig().strategy.task(agent, i);
            auto& s_t = sit().strategy.task(agent, i);
            if (o_t.task.type != Task::CRAFT_ITEM and o_t.task.type != Task::CRAFT_ASSIST) {
                continue;
            }
            if (s_t.result.time < sim_time() and i < sit().self(agent).task_index) continue;

            u8 index;
            if (auto node = find_by_id(nodes, o_t.task.craft_id)) {
                index = node - nodes.begin();
            } else {
                index = node_count++;
                nodes[index] = {o_t.task.craft_id, 0, 0, false};
            }
            if (last_node != 0xff) {
                edges[edge_count++] = {last_node, index, last_edge_id};
                ++nodes[last_node].out;
                ++nodes[index].in;
            }
            last_node = index;
            last_edge_id = o_t.task.id;
        }
    }

    nodes = {nodes.begin(), node_count};
    edges = {edges.begin(), edge_count};

    //for (auto i: nodes) { JDBG_L < i.id < i.in < i.out < i.invalid ,0; }
    //for (auto i: edges) { JDBG_L < i.a < i.b < i.task_id ,0; }

    // Remove all nodes with out-degree or in-degree 0
    while (true) {
        u8 index = 0xff;
        for (u8 i = 0; i < node_count; ++i) {
            if (not nodes[i].invalid and (nodes[i].in == 0 or nodes[i].out == 0)) {
                index = i;
                break;
            }
        }
        if (index == 0xff) break;

        for (auto e: edges) {
            nodes[e.a].out -= e.b == index;
            nodes[e.b].in  -= e.a == index;
        }
        nodes[index].invalid = true;
    }
    
    // Choose the edge with the newest task_id
    u16 task_id;
    u16 diff_value = std::numeric_limits<u16>::max();
    bool found = false;
    for (auto e: edges) {
        if (nodes[e.a].invalid or nodes[e.b].invalid) continue;

        u16 index_diff = orig().strategy.task_next_id + (u16)(-e.task_id);
        if (index_diff <= diff_value) {
            task_id = e.task_id;
            diff_value = index_diff;
            found = true;
        }
    }

    // There is no deadlock
    if (not found) return false;

    // Find the slot
    u8 agent, index;
    for (agent = 0; agent < number_of_agents; ++agent) {
        for (index = 0; index < planning_max_tasks; ++index) {
            if (orig().strategy.task(agent, index).task.id == task_id) break;
        }
        if (index < planning_max_tasks) break;
    }
    assert(agent != number_of_agents);

    // Move the associated task behind all other tasks that are in the circle and update their ids
    u8 to_index = 0xff;
    for (u8 i = index + 1; i < planning_max_tasks; ++i) {
        auto& o_t = orig().strategy.task(agent, i);
        if (o_t.task.type != Task::CRAFT_ITEM and o_t.task.type != Task::CRAFT_ASSIST) {
            continue;
        }
        if (auto node = find_by_id(nodes, o_t.task.craft_id)) {
            if (not node->invalid) {
                to_index = i;
                o_t.task.id = ++orig().strategy.task_next_id;
            }
        }
    }

    Task t = orig().strategy.pop_task(agent, index);
    orig().strategy.insert_task(agent, to_index, t);

    return true;
}

bool Simulation_state::create_work() {
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
            if (last_time(agent) < sit().simulation_step - max_idle_time
                and d.task_index + 1 < planning_max_tasks)
            {
                viable_agents[viable_agents_count++] = agent;
            }
        }
    }
    if (viable_agents_count == 0) return false;

    bool has_err = false;
    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        auto const& d = sit().self(agent);
        if (d.task_index > 0 and sit().strategy.task(agent, d.task_index - 1).result.err) {
            has_err = true;
        }
    }
    if (has_err) return false;

    struct Viable_job_t {
        u16 rating;
        u16 job_id;
    };

    Viable_job_t* viable_jobs = (Viable_job_t*)alloca(orig().jobs.size() * sizeof(Viable_job_t));
    int viable_job_count = 0;

    auto add_job = [&](Job const& job, int reward, int fine) {
        bool started = false;
        for (u8 agent = 0; agent < number_of_agents; ++agent) {
            for (u8 i = 0; i < planning_max_tasks; ++i) {
                auto const& t = sit().strategy.task(agent, i);
                started |= t.task.type == Task::DELIVER_ITEM and t.task.job_id == job.id;
            }
        }
        for (auto i: sit().book.delivered) {
            started |= i.job_id == job.id;
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
        
        float profit = ((float)(reward + fine) - (float)cost * rate_job_cost) / (float)complexity;
        profit = std::max(profit, 0.f);

        u8 rating = 1;
        rating += started ? rate_job_started : 0;
        rating += (u8)(profit * rate_job_profit);
        if (rating > 0) {
            viable_jobs[viable_job_count++] = {rating, job.id};
        }
    };
    
    for (Job const& job: sit().jobs) {
        // Exclude jobs posted by the other team
        if (job.required.size() < 2 or job.required.size() > 6) continue;
        
        add_job(job, job.reward, 0);
    }
    for (Auction const& job: sit().auctions) {
        // Exclude auctions still in progress
        if (orig().simulation_step < job.start + job.auction_time) continue;
        
        add_job(job, job.lowest_bid, job.fine);
    }
    for (Mission const& job: sit().missions) {
        add_job(job, job.reward, job.fine);
    }

    bool dirty = false;
    if (viable_job_count > 0) {
        u16 job_id = rng.choose_weighted(viable_jobs, viable_job_count)->job_id;
        u8 job_type;
        Job const& job = sit().get_by_id_job(job_id, &job_type);

        bool break_imm = rng.gen_bool(200);
        for (auto i: job.required) {
            // Make i a copy, to be able to modify it
            
            //if (viable_agents_count <= 1) break;

            u8 already = 0;
            for (auto j: sit().book.delivered) {
                if (j.job_id == job.id and j.item.id == i.id) {
                    already = j.item.amount;
                    break;
                }
            }
            if (already >= i.amount) continue;
            i.amount -= already;
            
            // TODO: more intelligent agent dispatch, to save add_item_for some work
            int agent_index = rng.gen_uni(viable_agents_count);
            u8 agent = viable_agents[agent_index];
            viable_agents[agent_index] = viable_agents[--viable_agents_count];

            s.task(agent, sit().self(agent).task_index).task = Task {
                Task::DELIVER_ITEM, job.storage, ++s.task_next_id, i, job.id
            };
            dirty = true;

            if (break_imm) break; // Only one item at a time
        }
    } else {
        // Let the agents buy tools

        u8 prior_tools[number_of_agents] = {};
        
        constexpr int viable_tool_max = 16;
        u8* viable_tools = (u8*)alloca(viable_tool_max * sizeof(u8));
        
        while (viable_agents_count) {
            int agent_index = rng.gen_uni(viable_agents_count);
            u8 agent = viable_agents[agent_index];
            auto& d = sit().self(agent);
            viable_agents[agent_index] = viable_agents[--viable_agents_count];

            int viable_tool_count = 0;
            for (u8 tool: world->roles[agent].tools) {
                bool already = false;
                for (u8 o_agent = 0; o_agent < number_of_agents; ++o_agent) {
                    if (auto item = find_by_id(sit().self(o_agent).items, tool)) {
                        already |= item->amount > 0;
                    }
                }
                for (u8 i: prior_tools) {
                    already |= tool == i;
                }
                if (already) continue;

                viable_tools[viable_tool_count++] = tool;
                if (viable_tool_count == viable_tool_max) break;
            }

            if (viable_tool_count == 0) continue;

            u8 tool = viable_tools[rng.gen_uni(viable_tool_count)];
            prior_tools[agent] = tool;

            u8 from_id = d.task_index
                ? s.task(agent, d.task_index - 1).task.where
                : orig().self(agent).name;
    
            int min_dist = std::numeric_limits<int>::max();
            u8 min_arg = 0;
            for (auto const& i: orig().shops) {
                auto j = find_by_id(i.items, tool);
                if (not j or j->amount == 0) continue;
                
                u16 dd = dist_cache.lookup_old(from_id, i.id);
                dd = dd / dist_price_fac + j->cost;
            
                if (dd < min_dist) {
                    min_dist = dd;
                    min_arg = i.id;
                }
            }
            if (min_arg == 0) continue;

            s.insert_task(agent, d.task_index, Task {
                Task::BUY_ITEM, min_arg, ++s.task_next_id, {tool, 1}
            });
            dirty = true;
        }
    }
    return dirty;
}

bool Simulation_state::optimize() {    
    for (int it = 0; it < optimizer_iterations; ++it) {
        reset();
        fast_forward();

        bool dirty = false;
        auto& s = orig().strategy;
        
        // Remove unnecessary items
        for (u8 agent = 0; agent < number_of_agents; ++agent) {
            auto& d = sit().self(agent);
            if (last_time(agent) == sit().simulation_step) continue;
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

        // Remove tasks not finished in time
        for (u8 agent = 0; agent < number_of_agents; ++agent) {
            auto& d = sit().self(agent);
            if (d.task_index == 0 or last_time(agent) < world->steps) continue;
            
            for (u8 i = d.task_index - 1; i < planning_max_tasks; ++i) {
                orig().strategy.pop_task(agent, i);
                dirty = true;
            }
        }

        if (not dirty) return it > 0;
    }
    return true;
}

float Simulation_state::rate() {
    reset();
    fast_forward();
        
    float rating = sit().team_money;

    // Count all items inside the agents inventory
    float item_rating = 0;
    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        for (auto const& i: sit().self(agent).items) {
            if (i.id == 0) continue;
            item_rating += get_by_id(world->item_costs, i.id).value() * i.amount;
        }
    }
    float fadeoff = std::min(1.f, (world->steps - sit().simulation_step) / rate_fadeoff);
    rating += item_rating * rate_val_item * fadeoff;
    //rating += item_rating * rate_val_item;
    

    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        if (sit().strategy.task(agent, 0).result.err) {
            rating -= rate_error;
        }
        rating += (sit().simulation_step - last_time(agent)) * rate_idletime;
    }

    return rating;
}

void Simulation_state::auction_bets(Array<Auction_bet>* bets) {
    assert(bets);
    bets->reset();
    bets->reserve(max_bets_per_step);

    for (Auction const& job: sit().auctions) {
        if (orig().simulation_step + 1 != job.start + job.auction_time) continue;

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
        
        float profit = ((float)job.reward - (float)job.fine * auction_fine_fac
            - (float)cost * rate_job_cost);
        float profit_min = auction_profit_min * (float)complexity / auction_profit_fac;

        if (profit > profit_min) {
            int max_bid = 1;
            if (orig().book.other_team_auction) {
                max_bid = (int)(profit - profit_min + 1);
            }
            bets->push_back({job.id, (u32)(job.reward - rng.gen_uni(max_bid))});
        }
        if (bets->size() == max_bets_per_step) return;
    }
}


} /* end of namespace jup */
