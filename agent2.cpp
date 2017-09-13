
#include "agent2.hpp"

#include "debug.hpp"

namespace jup {

void Mothership_complex::init(Graph* graph_) {
    graph = graph_;
    world_buffer.reset();
    sit_buffer.reset();
    sit_old_buffer.reset();
    strategies.reset();
    strategies.reserve(max_strategy_count);
    strategies_guard = strategies.m_data.alloc_guard();
}

void Mothership_complex::on_sim_start(u8 agent, Simulation const& simulation, int sim_size) {
    if (agent == 0) {
        world_buffer.emplace_back<World>(simulation, graph, &world_buffer);
    }
    world().update(simulation, agent, &world_buffer);
}

void Mothership_complex::pre_request_action() {
    std::swap(sit_buffer, sit_old_buffer);
    sit_buffer.reset();
}

void Mothership_complex::pre_request_action(u8 agent, Percept const& perc, int perc_size) {
    if (agent == 0) {
        Situation* old = sit_old_buffer.size() ? &sit_old_buffer.get<Situation>() : nullptr;
        sit_buffer.emplace_back<Situation>(perc, old, &sit_buffer);

        // This actually only invalidates the world in the first step, unless step_init changes
        world().step_init(perc, &world_buffer);
    }
    
    sit().update(perc, agent, &sit_buffer);
    world().step_update(perc, agent, &world_buffer);
}

void Mothership_complex::on_request_action() {
    world().step_post(&world_buffer);
    
    sit_diff.init(&sit_buffer);
    sit().register_arr(&sit_diff);
    
    // Flush all the old tasks out
    Situation* old = sit_old_buffer.size() ? &sit_old_buffer.get<Situation>() : nullptr;
    sit().flush_old(world(), *old, &sit_diff);
    sit_diff.apply();

    sim_buffer.reset();
    sim_buffer.append(sit_buffer);
    sim_state.init(&world(), &sim_buffer, 0, sim_buffer.size());

    strategies.reset();
    strategies.emplace_back();
    std::memcpy(&strategies[0].strategy, &sim_state.orig().strategy, sizeof(Strategy));
    strategies[0].rating = sim_state.rate();
    strategies[0].rating_sum = strategies[0].rating;
    strategies[0].visited = 1;

    while (strategies.size() < max_strategy_count) {
        // Choose the strategy to explore
        int best_arg = 0;
        float best_value = 0;
        for (int i_it = 0; i_it < strategies.size(); ++i_it) {
            auto const& i = strategies[i_it];
            float value = i.rating_sum / i.visited / search_rating_max;
            value += search_exploration * std::sqrt(2*std::log(strategies.size()) / i.visited);
            if (value > best_value) {
                best_arg = i_it;
                best_value = value;
            }
        }

        // Explore
        std::memcpy(&sim_state.orig().strategy, &strategies[best_arg].strategy, sizeof(Strategy));
        sim_state.reset();
        sim_state.fast_forward();
        sim_state.create_work();
        sim_state.fix_errors();
        sim_state.optimize();

        int index = strategies.size();
        strategies.emplace_back();
        std::memcpy(&strategies[index].strategy, &sim_state.orig().strategy, sizeof(Strategy));
        strategies[index].rating = sim_state.rate();
        strategies[index].rating_sum = strategies[index].rating;
        strategies[index].visited = 1;
        
        strategies[best_arg].rating_sum += strategies[index].rating;
        strategies[best_arg].visited += 1;
    }

    // Choose the best strategy
    int best_arg = 0;
    float best_value = 0;
    for (int i_it = 0; i_it < strategies.size(); ++i_it) {
        auto const& i = strategies[i_it];
        if (i.rating > best_value) {
            best_arg = i_it;
            best_value = i.rating;
        }
    }
    
    std::memcpy(&sit().strategy, &strategies[best_arg].strategy, sizeof(Strategy));

    JDBG_L < sit().strategy.p_tasks() ,0;
    
    /*if (sit().simulation_step == 30) {
        die(false);
        }*/

    
    /*if (auto item = find_by_id(sim_state.orig().self(2).items, get_id_from_string("tool1"))) {
        if (item->amount > 0) {
            for (u8 i = 0; i < planning_max_tasks; ++i) {
                auto const& t = sim_state.orig().strategy.task(2, i).task;
                if (t.type == Task::BUY_ITEM and t.item.id == get_id_from_string("tool1")) {
                    die(false);
                }
            }
        }
        }*/
    
        /*} else {
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
        }*/
    
    /*if (sit().simulation_step == 21) {
        JDBG_L < sim_state.orig().strategy.p_results() ,0;
        JDBG_L < sim_state.orig().selves ,0;
        JDBG_L < sim_state.orig().jobs ,0;
        die(false);
        }*/
    /*
    for (u8 agent = 0; agent < number_of_agents; ++agent) {
        for (u8 i = 0; i < planning_max_tasks; ++i) {
            auto const& t = sim_state.orig().strategy.task(agent, i).task;
            auto const& r = sim_state.sit().strategy.task(agent, i).result;
            if (t.type != Task::CRAFT_ITEM and t.type != Task::CRAFT_ASSIST) continue;
            if (r.time != 80) continue;
            for (u8 j = i; j < planning_max_tasks; ++j) {
                JDBG_L < agent < sim_state.orig().strategy.task(agent, j).task ,0;
            }
            break;
        }
        }*/
    
    std::memcpy(&sit().strategy, &sim_state.sit().strategy, sizeof(sit().strategy));
}

void Mothership_complex::post_request_action(u8 agent, Buffer* into) {
    Situation* old = sit().simulation_step == 0 ? &sit() : &sit_old();
    sit().get_action(world(), *old, agent, into, &sit_diff);
    sit_diff.apply();
}


} /* end of namespace jup */
