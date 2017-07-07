#include "flat_data.hpp"
#include "debug.hpp"
#include "test.hpp"
#include "messages.hpp"

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

void Mothership_test::init(Graph const* g) {
	graph = g;
}

void Mothership_test::on_sim_start(u8 agent, Simulation const& simulation, int sim_size) {
	sim_offsets[agent] = general_buffer.size();
	general_buffer.append(&simulation, sim_size);
	srand(time(nullptr));
}

void Mothership_test::pre_request_action() {
	//assert(agent_count == 16);
	step_buffer.reset();
}

void Mothership_test::pre_request_action(u8 agent, Percept const& perc, int perc_size) {
	perc_offsets[agent] = step_buffer.size();
	step_buffer.append(&perc, perc_size);
}

void Mothership_test::on_request_action() {
}

struct Dist_stat {
	double diff;
	double rlen;
	double bdiff;
	s32 dlat;
	s32 dlon;
	double eps;
};

Buffer stat_buffer;

void Mothership_test::post_request_action(u8 agent, Buffer* into) {
	auto dist2 = [this](Pos const& pos1, Pos const& pos2) -> double {
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
	Graph_position spos = graph->pos_node(pos);
	if (perc().simulation_step > 0) {
		u32 newdist = graph->dijkstra(spos, graph->pos_edge(perc().shops[target].pos));
		u32 dist = graph->dijkstra(oldsp, spos);
		assert(newdist > 10000);
		jout << (olddist - newdist) / 1000.0 << "|" << dist / 1000.0 
			<< "|" << dist2(oldp, pos) / 1000.0 << endl;
		stat_buffer.get<Flat_array<Dist_stat, u32, u32>>().push_back(Dist_stat { 
			(olddist - newdist) / 1000.0, dist / 1000.0, dist2(oldp, pos) / 1000.0,
			pos.lat - oldp.lat, pos.lon - oldp.lon, (dist2(pos, graph->nodes()[spos.node].pos)
			+ dist2(oldp, graph->nodes()[oldsp.node].pos)) / 1000.0 }, &stat_buffer);
	} else {
		stat_buffer.emplace_back<Flat_array<Dist_stat, u32, u32>>().init(&stat_buffer);
	} do {
		target = rand() % perc().shops.size();
		olddist = graph->dijkstra(spos, graph->pos_edge(perc().shops[target].pos));
	} while (olddist < 2000000);
	oldp = pos;
	oldsp = spos;
	if (spos.type == Graph_position::on_edge) {
		auto edge = graph->edges()[spos.edge];
		jdbg < "on edge: " < graph->nodes()[edge.nodea].pos < graph->nodes()[edge.nodeb].pos, 0;
	} else if (spos.type == Graph_position::on_node) {
		jdbg < "on node: " < graph->nodes()[spos.node].pos;
	} else assert(false);
	if (perc().simulation_step == 100) {
		for (Dist_stat s : stat_buffer.get<Flat_array<Dist_stat, u32, u32>>()) {
			//auto e = graph->pos_edge(s.pos);
			/*jdbg < pos;
			if (e.type == Graph_position::on_node) jdbg < "NODE " < graph->nodes()[e.node].pos, 0;
			else if (e.type == Graph_position::on_edge) {
				auto ed = graph->edges()[e.edge];
				jdbg < "EDGE " < graph->nodes()[ed.nodea].pos < "- " < graph->nodes()[ed.nodeb].pos, 0;
			}
			else assert(false);*/
			printf("%6.2f   %6.2f   %6.2f   %5d   %5d   %6.2f\n",
				s.diff, s.rlen, s.bdiff, s.dlat, s.dlon, s.eps);
		}
	}
	/*for (auto node : route_buffer.get<Graph::Route_t>()) {
		jdbg < graph->nodes()[node].pos, 0;
	}*/
	into->emplace_back<Action_Goto2>(perc().shops[target].pos);
	//into->emplace_back<Action_Skip>();
}


} /* end of namespace jup */










