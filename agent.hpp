<<<<<<< HEAD
#pragma once

#include "global.hpp"

#include <functional>

#include "buffer.hpp"
#include "objects.hpp"

namespace jup {	

using Agent = std::function< Action const&(Simulation const&, Perception const&) >;

Action const& dummy_agent (Simulation const& sim, Perception const& perc);

Action const& greedy_agent (Simulation const& sim, Perception const& perc);

} /* end of namespace jup */
