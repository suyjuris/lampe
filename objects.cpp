
#include "objects.hpp"
#include "messages.hpp"

namespace jup {

constexpr char const* Action::action_names[];
constexpr char const* Action::action_result_names[];

float Pos::dist2(Pos p) const {
	float dlat = lat - p.lat,
		dlon = lon - p.lon;
	return dlat*dlat + dlon*dlon;
}

} /*end of namespace jup */
