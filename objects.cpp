

#include "objects.hpp"
#include "world.hpp"

namespace jup {

u16 operator-(Pos const p1, Pos const p2) {
	s16 dx = p1.lat - p2.lat, dy = p1.lon - p2.lon;
	return sqrt(dx*dx + dy*dy);
}

constexpr char const* Action::action_names[];
constexpr char const* Action::action_result_names[];

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
