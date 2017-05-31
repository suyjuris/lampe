
#include "graph.hpp"

namespace jup {

struct GH_Node {
	s32 edge_ref, lat, lon;
};

struct GH_Edge {
	s32 nodea, nodeb, linka, linkb, dist, flags, geo, name;
};

Node_range const Node::iter(Graph const& graph) {
    assert(graph.m_data.inside(this));
    return Node_range {&graph, (u32)(this - &graph.nodes()[0]), edge};
}

Edge_iterator Node_range::begin() const {
    assert(graph and node != node_invalid and edge != edge_invalid);
	bool is_nodea = edge != edge_invalid and graph->edges()[edge].nodea == node;
	return Edge_iterator {graph, edge, is_nodea};
}

Edge_iterator Node_range::end() const {
	return Edge_iterator {};
}

Edge const& Edge_iterator::operator*() const {
    assert(graph and edge != 0xffff);
    return graph->edges()[edge];
}
Edge_iterator& Edge_iterator::operator++() {
    Edge const& e = **this;
    u32 node = is_nodea ? e.nodea : e.nodeb;
    edge = is_nodea ? e.linka : e.linkb;
    is_nodea = edge != edge_invalid and (**this).nodea == node;
    return *this;
}

// The conversion between GH s32 degrees and actual doubles
static constexpr double int_deg_fac = std::numeric_limits<s32>::max() / 400.0;

// Additional padding added around the borders of the map to handle slightly out-of-border agents.
static constexpr double lat_lon_padding = 0.2;

// The approximate radius of the earth in m
static constexpr float radius_earth = 6371e3;

// The speed that 1 (e.g. the truck) represents in m/step
static constexpr float speed_conversion = 500;

Pos get_pos_gh(Graph const& g, GH_Node node) {
	double lat = node.lat / int_deg_fac;
	double lon = node.lon / int_deg_fac;
	double lat_diff = (g.map_max_lat - g.map_min_lat);
	double lon_diff = (g.map_max_lon - g.map_min_lon);
    double fac =  1 + 2 * lat_lon_padding;
	lat = (lat - g.map_min_lat + lat_diff * lat_lon_padding) / fac / lat_diff;
	lon = (lon - g.map_min_lon + lon_diff * lat_lon_padding) / fac / lon_diff;
	assert(0.0 <= lat and lat < 1.0 and 0.0 <= lon and lon < 1.0);
	return Pos{ (u16)(lat * 65536.0 + 0.5), (u16)(lon * 65536.0 + 0.5) };
}

std::pair<double, double> Graph::get_pos_back(Pos pos) const {
	double lat_diff = (map_max_lat - map_min_lat);
	double lon_diff = (map_max_lon - map_min_lon);
	double lat = (double)pos.lat / 65536.0;
	double lon = (double)pos.lon / 65536.0;
    double fac =  1 + 2 * lat_lon_padding;
	lat = lat * lat_diff * fac - lat_diff * lat_lon_padding + map_min_lat;
	lon = lon * lon_diff * fac - lon_diff * lat_lon_padding + map_min_lon;
	return {lat, lon};
}

Pos Graph::get_pos(double lat, double lon) const {
    double lat_diff = (map_max_lat - map_min_lat);
	double lon_diff = (map_max_lon - map_min_lon);
    double fac = (1 + 2*lat_lon_padding);
	lat = (lat - map_min_lat + lat_diff * lat_lon_padding) / fac / lat_diff;
	lon = (lon - map_min_lon + lon_diff * lat_lon_padding) / fac / lon_diff;
	assert(0.0 <= lat and lat < 1.0);
	assert(0.0 <= lon and lon < 1.0);
	return Pos {(u16)(lat * 65536.0 + 0.5), (u16)(lon * 65536.0 + 0.5)};
}

void Graph::init(Buffer_view name, Buffer_view node_filename, Buffer_view edge_filename) {
	m_data.reset();

    name_offset = m_data.size();
    name_size = name.size();
    m_data.append(name);
    m_data.append("", 1);
    
	std::ifstream file; 
	auto read16 = [&file]() -> s16 {
		return file.get() << 8 | file.get();
	};
	auto read32 = [&file]() -> s32 {
		s32 r;
		for (u8 i = 0; i < 4; ++i) {
			r = (r << 8) | file.get();
		}
		return r;
	};
	auto read64 = [&file]() -> s64 {
		s64 r;
		for (u8 i = 0; i < 8; ++i) {
			r = (r << 8) | file.get();
		}
		return r;
	};

	// nodes
	file.open(node_filename.c_str(), std::ios::in | std::ios::binary);
	// basic header
	read16();
	// "GH"
	assert(read16() == 0x4748);
	// file length
	read64();
	read32();

	// nodes header
	read32();
	// element length
	assert(read32() == 12);
	s32 node_count = read32();
	map_min_lon = read32() / int_deg_fac;
	map_max_lon = read32() / int_deg_fac;
	map_min_lat = read32() / int_deg_fac;
	map_max_lat = read32() / int_deg_fac;
	float lon_radius = std::cos((map_max_lat + map_min_lat) / 360. * M_PI) * radius_earth;
	auto pmin = get_pos_back({0, 0});
	auto pmax = get_pos_back({255, 255});
	map_scale_lat = (pmax.first  - pmin.first ) / 180.f * (radius_earth * M_PI) / 255.f;
	map_scale_lon = (pmax.second - pmin.second) / 180.f * (lon_radius   * M_PI) / 255.f;
	file.close();

	file.open(edge_filename.c_str(), std::ios::in | std::ios::binary);
	// basic header
	read16();
	// "GH"
	assert(read16() == 0x4748);
	// file length
	read64();
	read32();

	// edges header
	// element length
	assert(read32() == 32);
	s32 edge_count = read32();
	file.close();

    auto g = m_data.reserve_guard(
        Nodes_t::total_space(node_count) + Edges_t::total_space(edge_count)
    );
	node_offset = m_data.size();
	auto& nodes = m_data.emplace_back<Nodes_t>();
	nodes.init(&m_data);

	file.open(node_filename.c_str(), std::ios::in | std::ios::binary);
	file.seekg(100, file.beg);
	for (s32 i = 0; i < node_count; ++i) {
		GH_Node node;
		file.read((char*)&node, sizeof(node));
		nodes.push_back({ (u32)node.edge_ref, get_pos_gh(*this, node) }, &m_data);
	}
	file.close();

	file.open(edge_filename.c_str(), std::ios::in | std::ios::binary);
	file.seekg(100, file.beg);
	edge_offset = m_data.size();
	auto& edges = m_data.emplace_back<Edges_t>();
	edges.init(&m_data);
	for (s32 i = 0; i < edge_count; ++i) {
		GH_Edge edge;
		file.read((char*)&edge, sizeof(edge));
		edges.push_back({ (u32)edge.nodea, (u32)edge.nodeb, (u32)edge.linka,
			(u32)edge.linkb, (u32)edge.dist }, &m_data);
	}
	file.close();
}

} /* end of namespace jup */
