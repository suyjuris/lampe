#include "objects.hpp"

#include <cmath>

#include "world.hpp"
#include "messages.hpp"

namespace jup {

constexpr char const* Action::action_names[];
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

Product const* Product::getByID(u8 id) {
	for (Product const& p : *world.products) {
		if (p.name == id) {
			return &p;
		}
	}
	return nullptr;
}

Charging_station const* Charging_station::getByID(u8 id) {
	for (Charging_station const& c : *world.charging_stations) {
		if (c.name == id) {
			return &c;
		}
	}
	return nullptr;
}

Dump_location const* Dump_location::getByID(u8 id) {
	for (Dump_location const& d : *world.dump_locations) {
		if (d.name == id) {
			return &d;
		}
	}
	return nullptr;
}

Shop const* Shop::getByID(u8 id) {
	for (Shop const& s : *world.shops) {
		if (s.name == id) {
			return &s;
		}
	}
	return nullptr;
}

Storage const* Storage::getByID(u8 id) {
	for (Storage const& s : *world.storages) {
		if (s.name == id) {
			return &s;
		}
	}
	return nullptr;
}

Workshop const* Workshop::getByID(u8 id) {
	for (Workshop const& w : *world.workshops) {
		if (w.name == id) {
			return &w;
		}
	}
	return nullptr;
}

} /*end of namespace jup */
