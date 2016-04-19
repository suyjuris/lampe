#pragma once

#include "flat_data.hpp"

namespace jup {

struct Item_stack {
	u8 item;
	u8 amount;
};

struct Product {
	u8 name;
	bool assembled;
	u16 volume;
	Flat_array<Item_stack> consumed;
	Flat_array<u8> tools;
};

struct Role {
	u8 name;
	u8 speed;
	u16 max_battery;
	u16 max_load;
	Flat_array<u8> tools;
};

struct Simulation {
	u8 id;
	u8 team;
	u16 seed_capital;
	u16 steps;
	Role role;
	Flat_array<Product> products;
};


} /* end of namespace jup */
