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

void test_jdbg_diff() {
    {int a = 4, b = 15;
    jdbg_diff(a, b);
    jdbg_diff(b, a);}

    {Buffer q; q.reserve(2048);
    auto& a = q.emplace_back<Flat_array<u8>>(&q);
    for (u8 i: {1, 3, 5, 7, 9, 11, 13, 15, 17}) a.push_back(i, &q);
    auto& b = q.emplace_back<Flat_array<u8>>(&q);
    for (u8 i: {2, 3, 5, 7, 11, 13, 17, 19, 21}) b.push_back(i, &q);
    jdbg_diff(a, b);
    jdbg_diff(b, a);}

    init_messages();
    u8 id[6];
    for (int i = 0; i < (int)std::size(id); ++i) {
        id[i] = register_id(jup_printf("item%d", i));
    }
    u8 role[6];
    for (int i = 0; i < (int)std::size(id); ++i) {
        role[i] = register_id(jup_printf("role%d", i));
    }
    
    {Buffer q; q.reserve(2048);
    auto& a = q.emplace_back<Flat_array<Item_stack>>(&q);
    for (Item_stack i: {Item_stack {id[0], 10}, {id[1], 10}, {id[2], 10}, {id[3], 10}, {id[4], 10}}) a.push_back(i, &q);
    auto& b = q.emplace_back<Flat_array<Item_stack>>(&q);
    for (Item_stack i: {Item_stack {id[0], 10}, {id[2], 12}, {id[3], 7}, {id[4], 10}, {id[5], 10}}) b.push_back(i, &q);
    jdbg_diff(a, b);
    jdbg_diff(b, a);}

    {Buffer q; q.reserve(2048);
    auto& a = q.emplace_back<Flat_array<Role>>(&q);
    for (u8 i: {10, 20, 30, 40, 50}) a.push_back(Role {role[a.size()], i, 17, 19}, &q);
    for (int i = 0; i < a.size(); ++i) {
        a[i].tools.init(&q);
        if (i == 0) { for (u8 j: {0, 1, 2}) a[i].tools.push_back(id[j], &q); }
        if (i == 2) { for (u8 j: {1, 3, 4}) a[i].tools.push_back(id[j], &q); }
        if (i == 3) { for (u8 j: {3, 4}   ) a[i].tools.push_back(id[j], &q); }
    }
    auto& b = q.emplace_back<Flat_array<Role>>(&q);
    for (u8 i: {10, 20, 30, 40}) b.push_back(Role {role[b.size()], i, 17, 19}, &q);
    for (int i = 0; i < b.size(); ++i) {
        b[i].tools.init(&q);
        if (i == 0) { for (u8 j: {0, 1, 3}) b[i].tools.push_back(id[j], &q); }
        if (i == 2) { for (u8 j: {1, 4}) b[i].tools.push_back(id[j], &q); }
        if (i == 3) { for (u8 j: {3, 4}) b[i].tools.push_back(id[j], &q); }
    }
    jdbg_diff(a, b);
    jdbg_diff(b, a);}
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

void Mothership_dummy::init(Graph* graph) {}
void Mothership_dummy::on_sim_start(u8 agent, Simulation const& simulation, int sim_size) {}
void Mothership_dummy::pre_request_action() {}
void Mothership_dummy::pre_request_action(u8 agent, Percept const& perc, int perc_size) {}
void Mothership_dummy::on_request_action() {}
void Mothership_dummy::post_request_action(u8 agent, Buffer* into) {
    into->emplace_back<Action_Skip>();
}


void Mothership_test2::init(Graph* graph_) {
    graph = graph_;
    world_buffer.reset();
    sit_buffer.reset();
    sit_old_buffer.reset();
}

void Mothership_test2::on_sim_start(u8 agent, Simulation const& simulation, int sim_size) {
    if (agent == 0) {
        world_buffer.emplace_back<World>(simulation, graph, &world_buffer);
    }
    world().update(simulation, agent, &world_buffer);
}

void Mothership_test2::pre_request_action() {
    std::swap(sit_buffer, sit_old_buffer);
    sit_buffer.reset();
}

void Mothership_test2::pre_request_action(u8 agent, Percept const& perc, int perc_size) {
    if (agent == 0) {
        Situation* old = sit_old_buffer.size() ? &sit_old_buffer.get<Situation>() : nullptr;
        sit_buffer.emplace_back<Situation>(perc, old, &sit_buffer);

        // This actually only invalidates the world in the first step, unless step_init changes
        world().step_init(perc, &world_buffer);
    }
    
    sit().update(perc, agent, &sit_buffer);
    world().step_update(perc, agent, &world_buffer);
}

void Mothership_test2::on_request_action() {
    sit_diff.init(&sit_buffer);
    sit().register_arr(&sit_diff);
    
    // Flush all the old tasks out
    Situation* old = sit_old_buffer.size() ? &sit_old_buffer.get<Situation>() : nullptr;
    sit().flush_old(world(), *old, &sit_diff);
    sit_diff.apply();
    
    if (sit().simulation_step > 0) {
        //jdbg_diff(sit_old(), sit());
    }
    
#if 0
    if (sit().simulation_step == 0) {
        {auto const& shop = sit().shops[0];
        sit().strategy.task(3, 0).task = Task {Task::BUY_ITEM, shop.id, 1, {shop.items[0].id, 2}};}
        {auto const& shop = sit().shops[1];
        sit().strategy.task(3, 1).task = Task {Task::BUY_ITEM, shop.id, 2, {shop.items[0].id, 1}};}
        {auto const& shop = sit().shops[2];
        sit().strategy.task(3, 2).task = Task {Task::BUY_ITEM, shop.id, 3, {shop.items[0].id, 1}};}

        sim_buffer.reset();
        sim_buffer.append(sit_buffer);
        sim_state.init(&world(), &sim_buffer, 0, sim_buffer.size());
    }
#else
    if (sit().simulation_step == 0) {
        /*sit().strategy.task(0, 0).task = Task {Task::DELIVER_ITEM, get_id_from_string("storage4"), 4,
          Item_stack {get_id_from_string("item10"), 1}, 3393};*/
        sit().strategy.task(1, 0).task = Task {Task::DELIVER_ITEM, get_id_from_string("storage4"), 3,
            Item_stack {get_id_from_string("item11"), 1}, 3393};
        sit().strategy.task(2, 0).task = Task {Task::DELIVER_ITEM, get_id_from_string("storage4"), 2,
            Item_stack {get_id_from_string("item14"), 1}, 3393};
        sit().strategy.task(3, 0).task = Task {Task::DELIVER_ITEM, get_id_from_string("storage4"), 1,
        Item_stack {get_id_from_string("item9" ), 1}, 3393};
        sit().strategy.task_next_id = 3;
        
        sim_buffer.reset();
        sim_buffer.append(sit_buffer);
        sim_state.init(&world(), &sim_buffer, 0, sim_buffer.size());
        sim_state.fix_errors();
        std::memcpy(&sit().strategy, &sim_state.sit().strategy, sizeof(sit().strategy));
    }
#endif
    
    if (sit().simulation_step > 0) {
        sim_state.reset();
        sim_state.fast_forward(sit().simulation_step);
        
        // These are just to make the output easier on the eyes
        for (u8 agent = 0; agent < number_of_agents; ++agent) {
            sim_state.sit().self(agent).action_type = sit().self(agent).action_type;
            sim_state.sit().self(agent).action_result = sit().self(agent).action_result;
            sim_state.sit().self(agent).task_sleep = sit().self(agent).task_sleep;
            sim_state.sit().self(agent).task_state = sit().self(agent).task_state;
            sim_state.sit().self(agent).task_index = sit().self(agent).task_index;
        }
        std::memcpy(&sim_state.sit().strategy, &sit().strategy, sizeof(sit().strategy));

        jdbg_diff(sim_state.sit(), sit());
    }

    if (sit().simulation_step == 20) {
        //jdbg_diff(sit(), sim_state.sit());
        die(false);
    }

    crafting_plan = sit().combined_plan(world());
    
    //JDBG_L < sit() ,0;
}

void Mothership_test2::post_request_action(u8 agent, Buffer* into) {
    Situation* old = sit().simulation_step == 0 ? &sit() : &sit_old();
    sit().get_action(world(), *old, agent, crafting_plan.slot(agent), into);
}



} /* end of namespace jup */










