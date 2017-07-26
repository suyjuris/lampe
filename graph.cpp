
#include "graph.hpp"
#include "debug.hpp"
#include <set>

namespace jup {

struct GH_Node {
	s32 edge_ref, lat, lon;
};

struct GH_Edge {
	s32 nodea, nodeb, linka, linkb, dist, flags, geo, name;
};

Node_range const Node::iter(Graph const& graph) const {
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
    assert(graph and edge != edge_invalid);
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

Pos get_pos_gh(Graph const& g, s32 gh_lat, s32 gh_lon) {
	double lat = gh_lat / int_deg_fac;
	double lon = gh_lon / int_deg_fac;
	double lat_diff = (g.map_max_lat - g.map_min_lat);
	double lon_diff = (g.map_max_lon - g.map_min_lon);
	double fac = 1 + 2 * lat_lon_padding;
	lat = (lat - g.map_min_lat + lat_diff * lat_lon_padding) / fac / lat_diff;
	lon = (lon - g.map_min_lon + lon_diff * lat_lon_padding) / fac / lon_diff;
	assert(0.0 <= lat and lat < 1.0 and 0.0 <= lon and lon < 1.0);
	return Pos{ (u16)(lat * 65536.0 + 0.5), (u16)(lon * 65536.0 + 0.5) };
}

Pos get_pos_gh(Graph const& g, GH_Node node) {
	return get_pos_gh(g, node.lat, node.lon);
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

Pos Graph_position::pos(Graph const& graph) const {

	if (edge_pos) {
		auto const& edge = graph.edges()[id];
		u8 n = edge.geo ? graph.geometry(edge.geo).size() + 1 : 1;
		auto* pos_pillar = new Pos[n + 1];
		auto* dist_pillar = new float[n + 1];
		u8 i = 0;
		Pos a = pos_pillar[0] = graph.nodes()[edge.nodea].pos;
		for (Pos p : graph.geometry(edge.geo)) {
			pos_pillar[++i] = p;
		}
		pos_pillar[++i] = graph.nodes()[edge.nodeb].pos;
		assert(i == n);
		float d = 0;
		dist_pillar[0] = 0.f;
		for (u8 i = 1; i <= n; ++i) {
			dist_pillar[i] = d += sqrt(a.dist2(pos_pillar[i]));
		}
		d *= get_edge_pos();
		i = 0;
		while (dist_pillar[++i] < d);
		auto pa = pos_pillar[i - 1], pb = pos_pillar[i];
		auto r = (d - dist_pillar[i - 1]) / (dist_pillar[i] - dist_pillar[i - 1]),
			s = 1.f - r;
		return { (u16)(pa.lat * s + pb.lat * r), (u16)(pa.lon * s + pb.lon * r) };
	} else {
		return graph.nodes()[id].pos;
	}
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

Graph_position Graph::pos(Pos pos) const {

	constexpr float const dist_invalid = std::numeric_limits<float>::max();
	// snap towards tower nodes
	constexpr float const edge_penalty = 2.f;

	auto dist_point = [pos](Pos a) -> float {
		return sqrt(pos.dist2(a));
	};

	auto dist_line = [dist_invalid, pos](Pos a, Pos b) -> float {
		float dlat = b.lat - a.lat,
			dlon = b.lon - a.lon,
			dplat = pos.lat - a.lat,
			dplon = pos.lon - a.lon;
		bool dir = abs(dlat) > abs(dlon);
		float r = dir ? (dplat + dplon*dlon / dlat) / (dlat + dlon*dlon / dlat)
					  : (dplon + dplat*dlat / dlon) / (dlon + dlat*dlat / dlon);
		if (r > 0 && r < 1) 
			return edge_penalty + std::abs(dir ? (dplon - dlon*r) / dlat : (dplat - dlat*r) / dlon) * sqrt(a.dist2(b));
		else return dist_invalid;
	};

	float min = dist_invalid;
	u32 id = node_invalid;
	bool is_node = true;

	// tower nodes
	for (u32 i = 0; i < nodes().size(); ++i) {
		auto const& node = nodes()[i];
		auto dist = dist_point(node.pos);
		if (dist < min) {
			min = dist;
			id = i;
		}
	}

	// edges
	for (u32 i = 0; i < edges().size(); ++i) {
		auto const& edge = edges()[i];
		if (edge.nodea == edge_invalid || edge.nodeb == edge_invalid) continue;
		Pos prev = nodes()[edge.nodea].pos;
		for (auto const& p : geometry(edge.geo)) {
			// line between two nodes
			auto dist = dist_line(prev, p);
			if (dist < min) {
				min = dist;
				is_node = false;
				id = i;
			}
			// pillar node
			dist = dist_point(p);
			if (dist < min) {
				min = dist;
				is_node = false;
				id = i;
			}
			prev = p;
		}
		// line to nodeb
		auto dist = dist_line(prev, nodes()[edge.nodeb].pos);
		if (dist < min) {
			min = dist;
			is_node = false;
			id = i;
		}
	}

	if (is_node) {
		return { id, (u8)0 };
	} else {
		auto const& edge = edges()[id];
		u8 n = edge.geo ? geometry(edge.geo).size() + 2 : 2;
		auto* pos_node = new Pos[n];
		auto* dist_node = new float[n];
		u8 i = 0;
		Pos a = pos_node[i++] = nodes()[edge.nodea].pos;
		for (Pos p : geometry(edge.geo)) {
			pos_node[i++] = p;
		}
		pos_node[i++] = nodes()[edge.nodeb].pos;
		assert(i == n);
		float d = 0;
		dist_node[0] = 0.f;
		for (u8 i = 1; i < n; ++i) {
			dist_node[i] = d += sqrt(pos_node[i - 1].dist2(pos_node[i]));
		}
		float min = dist_invalid;
		float edge_pos = 0.f;
		// pillar nodes
		for (u8 i = 1; i < n - 1; ++i) {
			float dist = dist_point(pos_node[i]);
			if (dist < min) {
				min = dist;
				edge_pos = dist_node[i] / d;
			}
		}

		a = pos_node[0];
		for (u8 i = 1; i < n; ++i) {
			// line between two nodes
			Pos b = pos_node[i];
			float dlat = b.lat - a.lat,
				dlon = b.lon - a.lon,
				dplat = pos.lat - a.lat,
				dplon = pos.lon - a.lon;
			bool dir = abs(dlat) > abs(dlon);
			float r = dir ? (dplat + dplon*dlon / dlat) / (dlat + dlon*dlon / dlat)
				: (dplon + dplat*dlat / dlon) / (dlon + dlat*dlat / dlon);
			if (r > 0 && r < 1) {
				float dist = std::abs(dir ? (dplon - dlon*r) / dlat
					: (dplat - dlat*r) / dlon) * sqrt(a.dist2(b));
				if (dist < min) {
					min = dist;
					edge_pos = (dist_node[i - 1] + r * (dist_node[i] - dist_node[i - 1])) / d;
				}
			}
			a = b;
		}

		return { id, edge_pos };
	}
}

u32 Graph::dijkstra(Graph_position s, Graph_position t, Buffer* into) const {
	if (s.edge_pos && t.edge_pos && s.id == t.id) {
		auto& edge = edges()[s.id];
		if(((s.edge_pos <= t.edge_pos) && (edge.flags & 1)) || ((s.edge_pos >= t.edge_pos) && (edge.flags & 2)))
			return abs(s.get_edge_pos() - t.get_edge_pos()) * edges()[s.id].dist;
	}

	auto dist = [this](Edge const& edge) -> double {
		return edge.dist;
		auto nodea = edge.nodea, nodeb = edge.nodeb;
		auto posa = get_pos_back(nodes()[nodea].pos), posb = get_pos_back(nodes()[nodeb].pos);
		auto deg2rad = [](double deg) -> double {
			return (deg * M_PI / 180);
		};
		double
		lat1r = deg2rad(posa.first),
		lon1r = deg2rad(posa.second),
		lat2r = deg2rad(posb.first),
		lon2r = deg2rad(posb.second),
		u = sin((lat2r - lat1r) / 2),
		v = sin((lon2r - lon1r) / 2);
		return 2.0 * 6371000000.0 * asin(sqrt(u * u + cos(lat1r) * cos(lat2r) * v * v));
	};
	auto nnodes = nodes().size();
	// upper bounds for node distances
	auto* ub = new double[nnodes];
	auto* next = new u32[nnodes];
	for (auto i = std::numeric_limits<u32>::min(); i < nnodes; ++i) {
		ub[i] = std::numeric_limits<double>::max();
		next[i] = edge_invalid;
	}

	auto ring = std::set<std::pair<double, u32>>();
	if (t.edge_pos) {
		auto const& et = edges()[t.id];
		if(et.flags & 1) ring.insert({ ub[edges()[t.id].nodea] = t.get_edge_pos() * dist(et), et.nodea });
		if(et.flags & 2) ring.insert({ ub[edges()[t.id].nodeb] = (1.f - t.get_edge_pos()) * dist(et), et.nodeb });
	} else {
		ub[t.id] = 0;
		ring.insert({ 0, t.id });
	}

	for (auto el = ring.begin(); el != ring.end()/*el->first < ub[s]*/; el = ring.begin()) {
		auto node = el->second;
		auto const& range = nodes()[node].iter(*this);
		for (auto it = range.begin(); it != range.end(); ++it) {
			auto other = it.is_nodea ? it->nodeb : it->nodea;
			auto newdist = el->first + dist(*it);
			u32 flags = edges()[it.edge].flags;

			if (it.is_nodea && (flags & 2) == 0) continue;
			if (!it.is_nodea && (flags & 1) == 0) continue;

			if (ub[other] > newdist) {
				// update distance
				if (ub[other] < std::numeric_limits<u32>::max()) ring.erase({ ub[other], other });
				ring.insert({ newdist, other });
				ub[other] = newdist;
				next[other] = node;
			}
		}
		ring.erase(el);
	}
	u32 firstnode, r;
	if (s.edge_pos) {
		auto const& es = edges()[s.id];
		auto dista = (es.flags & 2) == 0 ? std::numeric_limits<u32>::max() : ub[es.nodea] + s.get_edge_pos() * dist(es),
			distb = (es.flags & 1) == 0 ? std::numeric_limits<u32>::max() : ub[es.nodeb] + (1.f - s.get_edge_pos()) * dist(es);
		firstnode = dista < distb ? es.nodea : es.nodeb;
		r = (u32)std::min(dista, distb);
	} else {
		r = (u32)ub[firstnode = s.id];
	}
	if (into) {
		auto ofs = into->size();
		into->emplace_back<Route_t>().init(into);
		for (auto cur = firstnode; cur != node_invalid ; cur = next[cur]) {
			into->get<Route_t>(ofs).push_back(cur, into);
		}
	}
	delete[] ub;
	delete[] next;
	return r;
}

void Graph::init(Buffer_view name, Buffer_view node_filename, Buffer_view edge_filename, Buffer_view geometry_filename) {
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
	map_scale_lat = (pmax.first - pmin.first ) / 180.f * (radius_earth * M_PI) / 255.f;
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

	file.open(geometry_filename.c_str(), std::ios::in | std::ios::binary);
	// basic header
	read16();
	// "GH"
	assert(read16() == 0x4748);
	// file length
	read64();
	read32();

	// geometry header
	// data length
	s64 geometry_length = read32() + ((u64)read32() << 32);
	file.close();

    auto g = m_data.reserve_guard(
        Nodes_t::total_space(node_count) + Edges_t::total_space(edge_count) + 2 * geometry_length
    );

	// node data
	file.open(node_filename.c_str(), std::ios::in | std::ios::binary);
	file.seekg(100, file.beg);
	node_offset = m_data.size();
	auto& nodes = m_data.emplace_back<Nodes_t>();
	nodes.init(&m_data);
	for (s32 i = 0; i < node_count; ++i) {
		GH_Node node;
		file.read((char*)&node, sizeof(node));
		nodes.push_back({ (u32)node.edge_ref, get_pos_gh(*this, node) }, &m_data);
	}
	file.close();

	// edge data
	file.open(edge_filename.c_str(), std::ios::in | std::ios::binary);
	file.seekg(100, file.beg);
	edge_offset = m_data.size();
	auto& edges = m_data.emplace_back<Edges_t>();
	edges.init(&m_data);
	for (s32 i = 0; i < edge_count; ++i) {
		GH_Edge edge;
		file.read((char*)&edge, sizeof(edge));
		edges.push_back({ (u32)edge.nodea, (u32)edge.nodeb, (u32)edge.linka,
			(u32)edge.linkb, (u32)edge.dist, (u32)edge.flags, (u32)edge.geo, (u32)edge.name }, &m_data);
	}
	file.close();

	// geometry data
	file.open(geometry_filename.c_str(), std::ios::in | std::ios::binary);
	file.seekg(100, file.beg);
	geometry_offset = m_data.size();
	s64 ofs = 0;
	while (ofs < geometry_length) {
		s32 size;
		struct {
			s32 lat, lon;
		} pos;

		file.read((char*)&size, sizeof(size));
		assert(size <= std::numeric_limits<Geometry_t::Size_t>::max());
		++ofs;

		auto& geo = m_data.emplace_back<Geometry_t>();
		geo.init(&m_data);
		for (s32 i = 0; i < size; ++i) {
			file.read((char*)&pos, sizeof(pos));
			ofs += 2;
			geo.push_back(get_pos_gh(*this, pos.lat, pos.lon), &m_data);
		}
	}
}

} /* end of namespace jup */
