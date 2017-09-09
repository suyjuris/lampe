
#include "agent2.hpp"

#include "debug.hpp"

namespace jup {

void Mothership_complex::init(Graph* graph_) {
    graph = graph_;
    world_buffer.reset();
    sit_buffer.reset();
    sit_old_buffer.reset();
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
    sit_diff.init(&sit_buffer);
    sit().register_arr(&sit_diff);
    
    sim_buffer.reset();
    sim_buffer.append(sit_buffer);
    sim_state.init(&world(), &sim_buffer, 0, sim_buffer.size());
    
    sim_state.reset();
    sim_state.fast_forward();
    sim_state.create_work();
    sim_state.fix_errors();

    //jdbg < sim_state.sit().strategy.p_tasks() ,0;
    
    std::memcpy(&sit().strategy, &sim_state.sit().strategy, sizeof(sit().strategy));
}

void Mothership_complex::post_request_action(u8 agent, Buffer* into) {
    Situation* old = sit().simulation_step == 0 ? &sit() : &sit_old();
    sit().get_action(world(), *old, agent, into, &sit_diff);
    sit_diff.apply();
}


} /* end of namespace jup */
