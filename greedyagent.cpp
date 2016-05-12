#include "agent.hpp"

namespace jup {

Action const& greedy_agent (Simulation const& sim, Perception const& perc) {
    static Buffer buffer;
    return buffer.emplace<Action_Skip>();    
}

}

