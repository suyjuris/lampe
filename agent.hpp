#pragma once

#include "global.hpp"

#include <functional>

#include "buffer.hpp"
#include "objects.hpp"
#include "world.hpp"

namespace jup {	


using Agent_callback = std::function< Action const&(Agent const& agent) >;

Action const& dummy_agent  (Agent const& agent);

Action const& random_agent (Agent const& agent);

} /* end of namespace jup */
