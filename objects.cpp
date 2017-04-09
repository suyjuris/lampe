#include "objects.hpp"

#include <cmath>

#include "objects.hpp"
#include "messages.hpp"

namespace jup {

constexpr char const* Action::action_names[];
constexpr char const* Action::action_args[];
constexpr char const* Action::action_result_names[];

float Pos::dist(Pos p) const {
    return std::sqrt(dist2(p));
}
float Pos::dist2(Pos p) const {
    float diff_x = (lat - p.lat) * mess_scale_lat;
    float diff_y = (lon - p.lon) * mess_scale_lon;
    return diff_x*diff_x + diff_y*diff_y;
}
float Pos::distr(Pos p) const {
    float diff_x = (lat - p.lat) * mess_scale_lat;
    float diff_y = (lon - p.lon) * mess_scale_lon;
    return std::abs(diff_x) + std::abs(diff_y);
}

} /*end of namespace jup */
