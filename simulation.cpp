#include "simulation.hpp"

namespace jup {

#define COPY_ARRAY(obj, buf, name) \
    name.init(obj.name, (buf));
#define COPY_SUBARR(obj, buf, name, sub) \
    for (int i = 0; i < obj.name.size(); ++i) \
        name[i].sub.init(obj.name[i].sub, buf);

Situation::Situation(Percept const& p0, Buffer* containing):
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
}
    
#undef COPY_ARRAY
#undef COPY_SUBARR

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

void Situation::get_action(World const& world, u8 agent, Buffer* into) {
    assert(into);

    auto& d = self(agent);
    while (true) {
        auto& task = strategy.task(agent, 0);

        bool move_on = false;

        if (task.type == Task::NONE) {
            into->emplace_back<Action_Abort>();
            return;
        } else if (task.type == Task::BUY_ITEM) {
            if (d.task_state == 0) {
                if (agent_goto(task.where, agent, into)) {
                    d.task_state = 1;
                } else {
                    return;
                }
            }
            if (d.task_state == 1) {
                if (d.action_type == Action::BUY and not d.action_result) {
                    --task.item.amount;
                } 
                if (task.item.amount == 0) {
                    move_on = true;
                } else {
                    into->emplace_back<Action_Buy>(task.item);
                    return;
                }
            }
        } else if (task.type == Task::CRAFT_ITEM) {
            if (d.task_state == 0) {
                if (agent_goto(task.where, agent, into)) {
                    d.task_state = 1;
                } else {
                    return;
                }
            }
            if (d.task_state == 1) {
                if (d.action_type == Action::ASSEMBLE and not d.action_result) {
                    --task.item.amount;
                } 
                if (task.item.amount == 0) {
                    move_on = true;
                } else {
                    into->emplace_back<Action_Assemble>(task.item.item);
                    return;
                }
            }
        } else if (task.type == Task::CRAFT_ASSIST) {
            if (d.task_state == 0) {
                if (agent_goto(task.where, agent, into)) {
                    d.task_state = 1;
                } else {
                    return;
                }
            }
            if (d.task_state == 1) {
                if (d.action_type == Action::ASSIST_ASSEMBLE and not d.action_result) {
                    --task.item.amount;
                } 
                if (task.item.amount == 0) {
                    move_on = true;
                } else {
                    into->emplace_back<Action_Assist_assemble>(task.crafter_id);
                    return;
                }
            }
        } else if (task.type == Task::DELIVER_ITEM) {
            if (d.task_state == 0) {
                if (agent_goto(task.where, agent, into)) {
                    d.task_state = 1;
                } else {
                    return;
                }
            }
            if (d.task_state == 1) {
                if (d.action_type == Action::DELIVER_JOB and not d.action_result) {
                    --task.item.amount;
                } 
                if (task.item.amount == 0) {
                    move_on = true;
                } else {
                    into->emplace_back<Action_Deliver_job>(task.job_id);
                    return;
                }
            }
        } else if (task.type == Task::CHARGE) {
            if (d.task_state == 0) {
                if (agent_goto(task.where, agent, into)) {
                    d.task_state = 1;
                } else {
                    return;
                }
            }
            if (d.task_state == 1) {
                auto max_bat = world.role.battery;
                if (d.action_type == Action::CHARGE and d.charge < max_bat) {
                    move_on = true;
                } else {
                    into->emplace_back<Action_Charge>();
                    return;
                }
            }
        } else if (task.type == Task::VISIT) {
            if (agent_goto(task.where, agent, into)) {
                move_on = true;
            } else {
                return;
            }
        } else {
            // TODO
            assert(false);
        }

        assert(move_on);

        for (int i = 1; i < planning_max_tasks; ++i) {
            strategy.task(agent, i-1) = strategy.task(agent, i);
        }
    }
}


u8 Situation::get_nonlinearity(World const& world, u8 agent) {
    return 0xff;
    // TODO
    
    /*
    auto& d = self(agent);
    auto& task = strategy.task(agent, 0);

    if (task.type == Task::NONE) {
        return 0xff;
    } else if (task.type == Task::BUY_ITEM) {
        if (d.task_state == 0) {
            if (agent_goto(task.where, agent, into)) {
                d.task_state = 1;
            } else {
                return;
            }
        }
        if (d.task_state == 1) {
            if (d.action_type == Action::BUY and not d.action_result) {
                --task.item.amount;
            } 
            if (task.item.amount == 0) {
                move_on = true;
            } else {
                into->emplace_back<Action_Buy>(task.item);
                return;
            }
        }
    } else if (task.type == Task::CRAFT_ITEM) {
        if (d.task_state == 0) {
            if (agent_goto(task.where, agent, into)) {
                d.task_state = 1;
            } else {
                return;
            }
        }
        if (d.task_state == 1) {
            if (d.action_type == Action::ASSEMBLE and not d.action_result) {
                --task.item.amount;
            } 
            if (task.item.amount == 0) {
                move_on = true;
            } else {
                into->emplace_back<Action_Assemble>(task.item.item);
                return;
            }
        }
    } else if (task.type == Task::CRAFT_ASSIST) {
        if (d.task_state == 0) {
            if (agent_goto(task.where, agent, into)) {
                d.task_state = 1;
            } else {
                return;
            }
        }
        if (d.task_state == 1) {
            if (d.action_type == Action::ASSIST_ASSEMBLE and not d.action_result) {
                --task.item.amount;
            } 
            if (task.item.amount == 0) {
                move_on = true;
            } else {
                into->emplace_back<Action_Assist_assemble>(task.crafter_id);
                return;
            }
        }
    } else if (task.type == Task::DELIVER_ITEM) {
        if (d.task_state == 0) {
            if (agent_goto(task.where, agent, into)) {
                d.task_state = 1;
            } else {
                return;
            }
        }
        if (d.task_state == 1) {
            if (d.action_type == Action::DELIVER_JOB and not d.action_result) {
                --task.item.amount;
            } 
            if (task.item.amount == 0) {
                move_on = true;
            } else {
                into->emplace_back<Action_Deliver_job>(task.job_id);
                return;
            }
        }
    } else if (task.type == Task::CHARGE) {
        if (d.task_state == 0) {
            if (agent_goto(task.where, agent, into)) {
                d.task_state = 1;
            } else {
                return;
            }
        }
        if (d.task_state == 1) {
            auto max_bat = world.role.battery;
            if (d.action_type == Action::CHARGE and d.charge < max_bat) {
                move_on = true;
            } else {
                into->emplace_back<Action_Charge>();
                return;
            }
        }
    } else if (task.type == Task::VISIT) {
        if (agent_goto(task.where, agent, into)) {
            move_on = true;
        } else {
            return;
        }
    } else {
        // TODO
        assert(false);
    }
    */
}

void Situation::fast_forward(World const& world, Diff_flat_arrays* diff) {
    assert(diff);

    u8 next_nl = 255;

    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        auto& d = self(agent);
        d.task_nl = get_nonlinearity(world, agent);
        if (d.task_nl < next_nl) {
            next_nl = d.task_nl;
        }
    }

    int dur = next_nl - simulation_step;
    simulation_step += dur;
    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        auto& d = self(agent);
        d.task_nl -= dur;

        if (d.task_nl == 0) {
            task_update(world, agent, diff);
        }
    }
}

void Situation::task_update(World const& world, u8 agent, Diff_flat_arrays* diff) {
    // TODO
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
