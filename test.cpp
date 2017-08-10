#include "flat_data.hpp"
#include "debug.hpp"
#include "test.hpp"
#include "messages.hpp"
#include <set>
#include <ctime>

namespace jup {

/*
void test_delete_empty_lines() {    
    bool last;
    {
        char buf[] = "1\n2\n\n3\n\n\n4\n\n";
        int len = sizeof(buf) - 1;
        delete_empty_lines(buf, &len, &last);
        jdbg < Repr{buf} < len < last ,0;
    } {
        char buf[] = "\n\n5\n2\n\n3\n\n\n4";
        int len = sizeof(buf) - 1;
        delete_empty_lines(buf, &len, &last);
        jdbg < Repr{buf} < len < last ,0;
    } {
        char buf[] = "\n";
        int len = sizeof(buf) - 1;
        delete_empty_lines(buf, &len, &last);
        jdbg < Repr{buf} < len < last ,0;
    } {
        char buf[] = "\n";
        int len = sizeof(buf) - 1;
        delete_empty_lines(buf, &len, &last);
        jdbg < Repr{buf} < len < last ,0;
    }
}
*/

void test_flat_diff() {
    Buffer b;
    b.reserve(1024);
    auto& lst = b.emplace<Flat_array<Flat_array<u8>>>();
    lst.init(&b);
    auto& lst1 = lst.emplace_back(&b);
    auto& lst2 = lst.emplace_back(&b);
    auto& lst3 = lst.emplace_back(&b);
    lst1.init(&b);
    lst1.push_back(10, &b);
    lst1.push_back(20, &b);
    lst2.init(&b);
    lst2.push_back(30, &b);
    lst2.push_back(40, &b);
    lst3.init(&b);
    lst3.push_back(50, &b);
    lst3.push_back(60, &b);

    Diff_flat_arrays diff {&b};
    diff.register_arr(lst);
    diff.register_arr(lst1);
    diff.register_arr(lst2);
    diff.register_arr(lst3);

    diff.add(lst2, 99);
    diff.apply();
    jdbg < lst ,0;

    diff.remove(lst2, 0);
    diff.apply();
    jdbg < lst ,0;
}

void Mothership_test::init(Graph* g) {
	graph = g;
}

void Mothership_test::on_sim_start(u8 agent, Simulation const& simulation, int sim_size) {
	sim_offsets[agent] = general_buffer.size();
	general_buffer.append(&simulation, sim_size);
	data_offset = general_buffer.size();
	general_buffer.emplace_back<Simulation_data>();
	srand(time(nullptr));
}

void Mothership_test::pre_request_action() {
	//assert(agent_count == 16);
	step_buffer.reset();
}

u32 dt, dls, dlm, dc, dhm, dhs, dm;
u32 it = 0, it_p = 0;
u64 total_time = 0, total_time_p = 0;

void Mothership_test::pre_request_action(u8 agent, Percept const& perc, int perc_size) {
	perc_offsets[agent] = step_buffer.size();
	step_buffer.append(&perc, perc_size);
	if (agent == 0 && perc.simulation_step == 0) {
		jout << "NODES: " << graph->nodes().size() << endl;
		jout << "EDGES: " << graph->edges().size() << endl;
		if (false) {
			for (auto const& f : perc.charging_stations) {
				graph->add_landmark(graph->pos(f.pos));
			}
			for (auto const& f : perc.dumps) {
				graph->add_landmark(graph->pos(f.pos));
			}
			for (auto const& f : perc.shops) {
				graph->add_landmark(graph->pos(f.pos));
			}
			for (auto const& f : perc.storages) {
				graph->add_landmark(graph->pos(f.pos));
			}
			for (auto const& f : perc.workshops) {
				graph->add_landmark(graph->pos(f.pos));
			}
		}
	}
}

void Mothership_test::on_request_action() {
}

void Mothership_test::post_request_action(u8 agent, Buffer* into) {
	auto haversine = [this](Pos const& pos1, Pos const& pos2) -> double {
		auto posa = graph->get_pos_back(pos1), posb = graph->get_pos_back(pos2);
		auto deg2rad = [](double deg) -> double {
			return (deg * M_PI / 180);
		};
		double
			lat1r = deg2rad(posa.first),
			lon1r = deg2rad(posa.second),
			lat2r = deg2rad(posb.first),
			lon2r = deg2rad(posb.second),
			u = sin((lat2r - lat1r) / 2),
			v = sin((lon2r - lon1r) / 2);
		return 2.0 * 6371000000.0 * asin(sqrt(u * u + cos(lat1r) * cos(lat2r) * v * v));
	};
	if (agent != 0) {
		into->emplace_back<Action_Skip>();
		return;
	}
	Pos pos = perc().self.pos;
	auto start = std::chrono::high_resolution_clock::now();
	Graph_position spos = graph->pos(pos);
	total_time_p += (std::chrono::high_resolution_clock::now() - start).count();
	++it_p;
	if (perc().simulation_step > 0) {
		start = std::chrono::high_resolution_clock::now();
		auto tmpgp = graph->pos(target);
		total_time_p += (std::chrono::high_resolution_clock::now() - start).count();
		++it_p;
		step_buffer.emplace_back<Graph_position>(tmpgp);
		start = std::chrono::high_resolution_clock::now();
		u32 newdist = graph->A*(spos, tmpgp);
		u32 dist = graph->A*(oldsp, spos);
		total_time += (std::chrono::high_resolution_clock::now() - start).count();
		it += 2;
		if (dist < 1000) {
			throw(0);
		}
		if (dist < 390000) goto ls;
		else if (dist < 410000) { ++dm; --dt; }
		else if (dist < 570000) {
			ls:
			++dls;
			printf("<<<<<\n<<<<<\n<<<<<\n<<<<<\n<<<<<\n");
			jdbg < oldp < oldsp.pos(*graph) < target < graph->pos(target).pos(*graph), 0;
			auto a = graph->get_pos_back(oldsp.pos(*graph)),
				b = graph->get_pos_back(spos.pos(*graph));
			jout << a.first << ", " << a.second << ", " << b.first << ", " << b.second << endl;
		}
		else if (dist < 594000) ++dlm;
		else if (dist < 606000) ++dc;
		else if (dist < 630000) ++dhm;
		else {
			++dhs;
			printf(">>>>>\n>>>>>\n>>>>>\n>>>>>\n>>>>>\n");
			jdbg < oldp < oldsp.pos(*graph) < target < graph->pos(target).pos(*graph), 0;
			auto a = graph->get_pos_back(oldsp.pos(*graph)),
				b = graph->get_pos_back(spos.pos(*graph));
			jout << a.first << ", " << a.second << ", " << b.first << ", " << b.second << endl;
		}
		double q = 100.0 / ++dt;
		printf("%6d | %6d | %6.3f | %6.3f | %6.3f | %6.3f | %6.3f\n", dt, dm, dls * q, dlm * q, dc * q, dhm * q, dhs * q);

		assert(newdist > 10000);
		jout << dist / 1000.0 << " | " << (olddist - newdist) / 1000.0
			<< " | " << haversine(oldp, pos) / 1000.0 << endl;
	}
	do {
		target = perc().shops[rand() % perc().shops.size()].pos;
		start = std::chrono::high_resolution_clock::now();
		auto tmpgp = graph->pos(target);
		total_time_p += (std::chrono::high_resolution_clock::now() - start).count();
		++it_p;
		step_buffer.emplace_back<Graph_position>(tmpgp);
		start = std::chrono::high_resolution_clock::now();
		olddist = graph->A*(spos, tmpgp);
		total_time += (std::chrono::high_resolution_clock::now() - start).count();
		++it;
	} while (olddist < 2500000);
	oldp = pos;
	oldsp = spos;
	into->emplace_back<Action_Goto2>(target);
	jout << "TIME: " << total_time / (it * 1000000.0) << endl;
	jout << "TIME_P: " << total_time_p / (it_p * 1000000.0) << endl;
}


} /* end of namespace jup */










