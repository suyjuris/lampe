#include "statistics.hpp"

namespace jup {

void Mothership_statistics::init(Graph* graph) {
}

void Mothership_statistics::on_sim_start(u8 agent, Simulation const& simulation, int sim_size) {
	sim_offsets[agent] = general_buffer.size();
	general_buffer.append(&simulation, sim_size);
	visit[agent] = false;
#if 0
	visit[agent_count++] = false;
	if (agent == 0) {
		// create Game_statistic, fill with Products
		stat_buffer.emplace_back<Game_statistic>();
		stat().seed_capital = simulation.seed_capital;
		stat().steps = simulation.steps;
		stat().items.init(simulation.items, &stat_buffer);
		u8 i = 0;
		for (Item const& p : simulation.items) {
			stat().items[i].consumed.init(p.consumed, &stat_buffer);
			stat().items[i].tools.init(p.tools, &stat_buffer);
			++i;
		}
		stat().auctions.init(&stat_buffer);
		stat().jobs.init(&stat_buffer);
	}
	// initialize roles
	for (Role const& r : stat().roles) {
		if (r.name == simulation.role.name) goto role_end;
	}
	stat().roles.push_back(simulation.role, &stat_buffer);
	stat().roles.back().tools.init(simulation.role.tools, &stat_buffer);
role_end:
#endif
	return;
}

void Mothership_statistics::pre_request_action() {
	//assert(agent_count == 16);
	step_buffer.reset();
}

void Mothership_statistics::pre_request_action(u8 agent, Percept const& perc, int perc_size) {
	perc_offsets[agent] = step_buffer.size();
	step_buffer.append(&perc, perc_size);
	return;
#if 0
	if (agent == 0) {
		if (perc.simulation_step == 0) {
			// initialize entities
			stat().agents.init(perc.entities, &stat_buffer);
			// initialize facilities
			stat().charging_stations.init(perc.charging_stations, &stat_buffer);
			stat().dumps.init(perc.dumps, &stat_buffer);
			stat().shops.init(perc.shops, &stat_buffer);
			stat().storages.init(perc.storages, &stat_buffer);
			stat().workshops.init(perc.workshops, &stat_buffer);
			// assign shops to agents
			u8 i = 0;
			for (; i < perc.shops.size(); ++i) {
				visit[i] = i + 1;
			}
			for (; i < agent_count; ++i) {
				visit[i] = 0;
			}
		} else if (perc.simulation_step % 100 == 0) {
			// assign new agents, if old ones failed to get to a shop
			u8 i = 0, j = 0;
			memcpy(visit_old, visit, agent_count * sizeof(int));
			while (true) {
				// look for failed visit
				while (visit_old[i] <= 0) if (++i == agent_count) goto assign_end;
				// look for free agent
				while (visit[j] != 0) assert(++j < agent_count);
				jout << "Shop " << visit[i] << " has been transferred from " << (u16)i << " to " << (u16)j << endl;
				visit[j] = visit[i];
				visit[i++] = -1;
			}
		}
	assign_end:
		// write new jobs into buffer
		for (Auction const& j : perc.auctions) {
			if (j.start == perc.simulation_step - 1) {
				Auction& k = stat().auctions.push_back(j, &stat_buffer);
				k.required.init(j.required, &stat_buffer);
			}
		}
		for (Job const& j : perc.jobs) {
			if (j.start == perc.simulation_step - 1) {
				Job& k = stat().jobs.push_back(j, &stat_buffer);
				k.required.init(j.required, &stat_buffer);
			}
		}
		if (perc.simulation_step >= sim().steps - 1) {
			// ensure detailed shop information is available
			for (u8 i = 0; i < agent_count; i++) {
				assert(visit[i] <= 0);
			}
		}
	}
	if (visit[agent] > 0) {
		Shop const& s = perc.shops[visit[agent] - 1];
		// check whether detailed information is available
		if (s.items[0].amount < 255) {
			jout << "Agent " << (u16)agent << " has visited shop " << visit[agent] - 1 << endl;
			// write detailed information into buffer
			stat().shops[visit[agent] - 1].items.init(s.items, &stat_buffer);
			visit[agent] = 0;
		}
	}
#endif
}

void Mothership_statistics::on_request_action() {
}

void Mothership_statistics::post_request_action(u8 agent, Buffer* into) {
#if 1
	if (perc(agent).resource_nodes.size() > 0) {
		auto spos = perc(agent).self.pos;
		auto rpos = perc(agent).resource_nodes[0].pos;
		//jout << (u16)spos.lat << "|" << (u16)spos.lon << "-" << (u16)rpos.lat << "|" << (u16)rpos.lon << endl;
		if ((abs(spos.lat - rpos.lat) <= 5 && abs(spos.lon - rpos.lon) <= 5)) {
			into->emplace_back<Action_Gather>();
		} else {
			into->emplace_back<Action_Goto2>(rpos);
		}
	} else {
		into->emplace_back<Action_Skip>();
	}
	return;
#else
	// move agents to assigned shops
	if (visit[agent] > 0) {
		into->emplace_back<Action_Goto1>(perc().shops[visit[agent] - 1].name);
	} else {
		into->emplace_back<Action_Skip>();
	}
#endif
}

/**
* print a frequency diagram
*/
void print_diagram(u16 const* values, u16 size, u16 width, u16 height,
	double blur, FILE* out) {
	u16 min = 0xffff, max = 0;
	for (u16 i = 0; i < size; ++i) {
		u16 v = values[i];
		if (v < min) min = v;
		if (v > max) max = v;
	}
	u16 left = floor(min * (1 - blur));
	u16 right = floor(max * (1 + blur));
	double xscale = (right - left) / (double)width;
	auto to_scale = [left, xscale](double val) -> u16 {
		return (u16)floor((val - left) / xscale);
	};
	int* count = new int[width]();
	for (u16 i = 0; i < size; ++i) {
		u16 v = values[i];
		for (u16 j = to_scale(v * (1 - blur)); j <= to_scale(v*(1 + blur)); ++j) {
			++count[j];
		}
	}
	u16 ymax = 0;
	for (u16 i = 0; i < width; ++i) {
		u16 c = count[i];
		if (c > ymax) ymax = c;
	}
	u16 yscale = ceil((double)ymax / height);
	ymax += yscale - ymax % yscale;
	for (u16 i = ymax; i > 0; i -= yscale) {
		fputc('\n', out);
		fprintf(out, "%5hu  ", i);
		for (u16 x = 0; x < width; x++) {
			if (count[x] >= i) fputc('X', out);
			else if (count[x] >= i - yscale / 2) fputc('x', out);
			else fputc(' ', out);
		}
	}
	fputc('\n', out);
	fprintf(out, "       %-5hu", left);
	for (u16 i = 0; i < width - 10; i++) fputc(' ', out);
	fprintf(out, "%5hu\n", right);
}

/**
* as print_diagram, but with logarithmic x-scale
*/
void print_diagram_log(u16 const* values, u16 size, u16 width, u16 height,
	double blur, FILE* out) {
	u16 min = 0xffff, max = 0;
	for (u16 i = 0; i < size; ++i) {
		u16 v = values[i];
		if (v < min) min = v;
		if (v > max) max = v;
	}
	u16 left = floor(min * (1 - blur));
	u16 right = floor(max * (1 + blur));
	double xscale = pow(((double)right / left), 1.0/width);
	double log_xscale = log(xscale);
	double log_left = log((double)left) / log_xscale;
	auto to_scale = [log_xscale, log_left](double val) -> u16 {
		return (u16)floor(log(val)/log_xscale - log_left);
	};
	int* count = new int[width]();
	for (u16 i = 0; i < size; ++i) {
		u16 v = values[i];
		for (u16 j = to_scale(v * (1 - blur)); j <= to_scale(v*(1 + blur)); ++j) {
			++count[j];
		}
	}
	u16 ymax = 0;
	for (u16 i = 0; i < width; ++i) {
		u16 c = count[i];
		if (c > ymax) ymax = c;
	}
	u16 yscale = ceil((double)ymax / height);
	ymax += yscale - ymax % yscale;
	for (u16 i = ymax; i > 0; i -= yscale) {
		fputc('\n', out);
		fprintf(out, "%5hu  ", i);
		for (u16 x = 0; x < width; x++) {
			if (count[x] >= i) fputc('X', out);
			else if (count[x] >= i - yscale / 2) fputc('x', out);
			else fputc(' ', out);
		}
	}
	fputc('\n', out);
	fprintf(out, "       %-5hu", left);
	for (u16 i = 0; i < width - 10; i++) fputc(' ', out);
	fprintf(out, "%5hu\n", right);
}

void statistics_main() {
	// example: average item prices
	Buffer b;
	b.read_from_file("statistics.dat");
	auto const& list = b.get<Flat_list<Game_statistic, u16, u32>>();
	u16 llen = list.size();
	jout << "sample size: " << llen << endl;
	u16* avg_pri = new u16[llen];
	u16 i = 0;
	for (Game_statistic const& stat : list) {
		u32 apsum = 0;
		u8 bp = 0;
		for (Item const& p : stat.items) {
			u8 name = p.name;
			u8 as = 0;
			u32 psum = 0;
			for (Shop const& s : stat.shops) {
				for (Shop_item const& si : s.items) {
					if (si.item == name) {
						psum += si.cost;
						++as;
						break;
					}
				}
			}
			if (as > 0) {
				apsum += psum / as;
				++bp;
			}
		}
		avg_pri[i++] = apsum / bp;
	}
	print_diagram_log(avg_pri, llen, 60, 20, 0);
	print_diagram_log(avg_pri, llen, 60, 20, 0.05);
	print_diagram_log(avg_pri, llen, 60, 20, 0.1);
	print_diagram_log(avg_pri, llen, 60, 20, 0.15);
	print_diagram_log(avg_pri, llen, 60, 20, 0.2);
	getchar();
	return;
}

} /* end of namespace jup */
