

#include "objects.hpp"

namespace jup {

u16 operator-(Pos const p1, Pos const p2) {
	s16 dx = p1.lat - p2.lat, dy = p1.lon - p2.lon;
	return sqrt(dx*dx + dy*dy);
}

constexpr char const* Action::action_names[];
constexpr char const* Action::action_result_names[];


} /*end of namespace jup */
