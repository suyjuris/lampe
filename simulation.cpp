#include "simulation.hpp"

namespace jup {

Internal_simulation::Internal_simulation(Game_statistic const& stat) {
	sim_buffer.emplace_back<Simulation_information>();
	d().products.init(stat.products, &sim_buffer);
	d().roles.init(stat.roles, &sim_buffer);
	u8 i = 0;
	for (Role const& r : stat.roles) {
		d().roles[i++].tools.init(r.tools, &sim_buffer);
	}
	d().charging_stations.init(stat.charging_stations, &sim_buffer);
	d().dump_locations.init(stat.dump_locations, &sim_buffer);
	d().shops.init(stat.shops, &sim_buffer);
	i = 0;
	for (Shop const& s : stat.shops) {
		d().shops[i++].items.init(s.items, &sim_buffer);
	}
	d().storages.init(stat.storages, &sim_buffer);
	d().workshops.init(stat.workshops, &sim_buffer);
	d().auction_jobs.init(stat.auction_jobs, &sim_buffer);
	i = 0;
	for (Job_auction const& j : stat.auction_jobs) {
		d().auction_jobs[i++].items.init(j.items, &sim_buffer);
	}
	d().priced_jobs.init(stat.priced_jobs, &sim_buffer);
	i = 0;
	for (Job_auction const& j : stat.auction_jobs) {
		d().auction_jobs[i++].items.init(j.items, &sim_buffer);
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
		a.charge = r.max_battery;
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
		sim.products.init(d().products, into);
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
		Perception& perc = into->emplace_back<Perception>();
		perc.id = i;
		perc.simulation_step = step;
		//perc.auction_jobs = ...
		//perc.priced_jobs = ...
		perc.charging_stations.init(d().charging_stations, into);
		perc.dump_locations.init(d().dump_locations, into);
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
		perc.self.route.init(a.route, into);
		// perc.team = ...
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
