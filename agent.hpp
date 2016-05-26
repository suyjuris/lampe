#pragma once

#include "global.hpp"

#include <functional>

#include "buffer.hpp"
#include "objects.hpp"

namespace jup {	

using Agent_callback = std::function< Action const&(u8 id, Simulation const&, Perception const&) >;

Action const& dummy_agent  (u8 id, Simulation const& sim, Perception const& perc);

Action const& random_agent (u8 id, Simulation const& sim, Perception const& perc);

Action const& greedy_agent (u8 id, Simulation const& sim, Perception const& perc);

} /* end of namespace jup */
