
#include "agent.hpp"

#include "buffer.hpp"

namespace jup {

Action const& dummy_agent (Simulation const& sim, Perception const& perc) {
    static Buffer buffer;
    return buffer.emplace<Action_Skip>();
}

} /* end of namespace jup */

