
#include "graph.hpp"

namespace jup {

struct GH_Node {
	s32 edge_ref, lat, lon;
};

struct GH_Edge {
	s32 nodea, nodeb, linka, linkb, dist, flags, geo, name;
};

double map_min_lat;
double map_max_lat;
double map_min_lon;
double map_max_lon;
float map_scale_lat;
float map_scale_lon;

Buffer graph_buffer;
int node_offset, edge_offset;

Flat_array<Node, u32, u32> const& nodes() {
	return graph_buffer.get<Flat_array<Node, u32, u32>>(node_offset);
}

Flat_array<Edge, u32, u32> const& edges() {
	return graph_buffer.get<Flat_array<Edge, u32, u32>>(edge_offset);
}

Edge_iterator const Node::begin() const {
	u32 node = &nodes()[0] - this;
	bool is_nodea;
	if (edge == 0xffff) is_nodea = false;
	else is_nodea = edges()[edge].nodea == node;
	return Edge_iterator{ edge, is_nodea };
}

Edge_iterator const Node::end() const {
	return Edge_iterator();
}

Pos get_pos(GH_Node node) {
	auto deg = [](s32 val) -> double {
		constexpr double const degree_factor = std::numeric_limits<s32>().max() / 400.0;
		return val / degree_factor;
	};
	constexpr static double pad = lat_lon_padding;
	double lat = deg(node.lat);
	double lon = deg(node.lon);
	double lat_diff = (map_max_lat - map_min_lat);
	double lon_diff = (map_max_lon - map_min_lon);
	lat = (lat - map_min_lat + lat_diff * pad) / (1 + 2 * pad) / lat_diff;
	lon = (lon - map_min_lon + lon_diff * pad) / (1 + 2 * pad) / lon_diff;
	assert(0.0 <= lat and lat < 1.0);
	assert(0.0 <= lon and lon < 1.0);
	return Pos{ (u16)(lat * 65536.0 + 0.5), (u16)(lon * 65536.0 + 0.5) };
}

std::pair<double, double> get_pos_back(Pos pos) {
	constexpr static double pad = lat_lon_padding;
	double lat_diff = (map_max_lat - map_min_lat);
	double lon_diff = (map_max_lon - map_min_lon);
	double lat = (double)pos.lat / 65536.0;
	double lon = (double)pos.lon / 65536.0;
	lat = lat * lat_diff * (1 + 2 * pad) - lat_diff * pad + map_min_lat;
	lon = lon * lon_diff * (1 + 2 * pad) - lon_diff * pad + map_min_lon;
	return{ lat, lon };
}

void graph_init(char const* node_filename, char const* edge_filename) {
	graph_buffer.reset();
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
	auto read64 = [&file]() -> s32 {
		s64 r;
		for (u8 i = 0; i < 8; ++i) {
			r = (r << 8) | file.get();
		}
		return r;
	};

	// nodes
	file.open(node_filename, std::ios::in | std::ios::binary);
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
	map_min_lon = read32();
	map_max_lon = read32();
	map_min_lat = read32();
	map_max_lat = read32();
	float lon_radius = std::cos((map_max_lat + map_min_lat) / 360. * M_PI) * radius_earth;
	auto pmin = get_pos_back({ 0, 0 });
	auto pmax = get_pos_back({ 255, 255 });
	map_scale_lat = (pmax.first - pmin.first) / 180.f * (radius_earth * M_PI) / 255.f;
	map_scale_lon = (pmax.second - pmin.second) / 180.f * (lon_radius   * M_PI) / 255.f;
	file.close();

	file.open(edge_filename, std::ios::in | std::ios::binary);
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

	graph_buffer.reserve(sizeof(Node) * node_count + sizeof(Edge) * edge_count + 16);
	node_offset = graph_buffer.size();
	auto& nodes = graph_buffer.emplace_back<Flat_array<Node, u32, u32>>();
	nodes.init(&graph_buffer);

	file.open(node_filename, std::ios::in | std::ios::binary);
	file.seekg(100, file.beg);
	for (s32 i = 0; i < node_count; ++i) {
		GH_Node node;
		file.read((char*)&node, sizeof(node));
		nodes.push_back({ (u32)node.edge_ref, get_pos(node) }, &graph_buffer);
	}
	file.close();

	file.open(edge_filename, std::ios::in | std::ios::binary);
	file.seekg(100, file.beg);
	edge_offset = graph_buffer.size();
	auto& edges = graph_buffer.emplace_back<Flat_array<Edge, u32, u32>>();
	edges.init(&graph_buffer);
	for (s32 i = 0; i < edge_count; ++i) {
		GH_Edge edge;
		file.read((char*)&edge, sizeof(edge));
		edges.push_back({ (u32)edge.nodea, (u32)edge.nodeb, (u32)edge.linka,
			(u32)edge.linkb, (u32)edge.dist }, &graph_buffer);
	}
	file.close();
}

} /* end of namespace jup */
