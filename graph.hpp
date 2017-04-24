#pragma once

#include "flat_data.hpp"
#include "objects.hpp"

namespace jup {

struct Edge_iterator;

struct Node {
	u32 edge;
	Pos pos;
	Edge_iterator const begin() const;
	Edge_iterator const end() const;
};

struct Edge {
	u32 nodea, nodeb, linka, linkb, dist;
};

Flat_array<Node, u32, u32> const& nodes();
Flat_array<Edge, u32, u32> const& edges();

struct Edge_iterator : std::iterator<Edge, std::forward_iterator_tag> {
	u32 edge;
	bool is_nodea;
	Edge_iterator() : edge{ 0xffff }, is_nodea{ false } {}
	Edge_iterator(u32 edge, bool is_nodea) : edge{ edge }, is_nodea{ is_nodea } {}
	Edge_iterator(Edge_iterator const& orig) : edge{ orig.edge }, is_nodea{ orig.is_nodea } {}
	bool operator==(Edge_iterator const& other) const { return edge == other.edge && is_nodea == other.is_nodea; }
	bool operator!=(Edge_iterator const& other) const { return edge != other.edge || is_nodea != other.is_nodea; }
	Edge const& operator*() const { assert(edge != 0xffff); return edges()[edge]; }
	Edge_iterator& operator++() {
		Edge const& e = **this;
		u32 node = is_nodea ? e.nodea : e.nodeb;
		edge = is_nodea ? e.linka : e.linkb;
		if (edge == 0xffff) is_nodea = false;
		else is_nodea = (**this).nodea == node;
		return *this;
	}
};

// Some constants for distance calculations
constexpr double lat_lon_padding = 0.2;
extern double map_min_lat;
extern double map_max_lat;
extern double map_min_lon;
extern double map_max_lon;
extern float map_scale_lat;
extern float map_scale_lon;
// The approximate radius of the earth in m
static constexpr float radius_earth = 6371e3;
// The speed that 1 (e.g. the truck) represents in m/step
static constexpr float speed_conversion = 500;

/**
* Returns the actual coordinates from a position
*/
std::pair<double, double> get_pos_back(Pos pos);

void graph_init(char const* node_filename, char const* edge_filename);

} /* end of namespace jup */
