
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

Graph_position Graph::pos_node(Pos pos) const {
	u32 n, min = std::numeric_limits<u32>::max(), i = 0;
	for (auto const& node : nodes()) {
		u32 l = pos.dist2(node.pos);
		if (l < min) {
			min = l;
			n = i;
		}
		++i;
	}
	Graph_position p;
	p.type = Graph_position::on_node;
	p.node = n;
	p.eps = min;
	return Graph_position(Graph_position::on_node, n, min);
}

Graph_position Graph::pos_edge(Pos pos) const {
	Graph_position min;
	min.type = Graph_position::on_edge;
	min.eps = 0xffff;
	for (u32 i = 0; i < edges().size(); ++i) {
		auto const& edge = edges()[i];
		if (edge.nodea == edge_invalid || edge.nodeb == edge_invalid) continue;
		Graph_position cur;
		auto apos = nodes()[edge.nodea].pos, bpos = nodes()[edge.nodeb].pos;
		double dlat = bpos.lat - apos.lat,
			dlon = bpos.lon - apos.lon,
			dplat = pos.lat - apos.lat,
			dplon = pos.lon - apos.lon;
		if (std::abs(dlat) < 0.01 || std::abs(dlon) < 0.01 || std::abs(dlon) / std::abs(dlat) < 0.01) continue;
		// TODO
		/*bool b = abs(dlat) > abs(dlon);
		double r = b ? ((pos.lat - apos.lat) + (pos.lon - apos.lon) * (dlon / dlat)) / (1 + dlon * dlon / dlat)
			: ((pos.lon - apos.lon) + (pos.lat - apos.lat) * (dlat / dlon)) / (1 + dlat * dlat / dlon);*/
		double r = (dplon + dplat*dlat / dlon) / (dlon + dlat*dlat / dlon);
		if (std::isnan(r)) continue;
		if (r <= 0) {
			cur = { Graph_position::on_node, edge.nodea, (u16)std::min(sqrt(pos.dist2(apos)), (double)0xffff), 0 };
		} else if (r >= 1) {
			cur = { Graph_position::on_node, edge.nodeb, (u16)std::min(sqrt(pos.dist2(bpos)), (double)0xffff), 0 };
		} else {
			cur = Graph_position{ Graph_position::on_edge, i, (u16)(std::min(std::abs((dplat - dlat*r) / (dlon)
				* sqrt(apos.dist2(bpos))), (double)0xfffe)), (u8)floor(r * 256.0) };
		}
		if (cur.eps < min.eps)  min = cur;
	}
	assert(min.eps < 0xfffe);
	return min;
}

u32 Graph::dijkstra(Graph_position s, Graph_position t, Buffer* into) const {

	if (s.type == Graph_position::on_edge && t.type == Graph_position::on_edge && s.edge == t.edge) {
		return abs((s16)s.edge_pos - (s16)t.edge_pos) / 256.0 * edges()[s.edge].dist;
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
	auto* multi = new u8[nnodes];
	for (auto i = std::numeric_limits<u32>::min(); i < nnodes; ++i) {
		ub[i] = std::numeric_limits<double>::max();
		next[i] = edge_invalid;
		multi[i] = 0;
	}

	auto ring = std::set<std::pair<double, u32>>();
	if (t.type == Graph_position::on_edge) {
		auto const& et = edges()[t.edge];
		auto ad = ub[edges()[t.edge].nodea] = t.edge_pos / 256.0 * dist(et),
			bd = ub[edges()[t.edge].nodeb] = (1 - t.edge_pos / 256.0) * dist(et);
		ring.insert({ ad, et.nodea });
		ring.insert({ bd, et.nodeb });
	} else if (t.type == Graph_position::on_node) {
		ub[t.node] = 0;
		ring.insert({ 0, t.node });
	} else assert(false);

	for (auto el = ring.begin(); el != ring.end()/*el->first < ub[s]*/; el = ring.begin()) {
		auto node = el->second;
		auto const& range = nodes()[node].iter(*this);
		for (auto it = range.begin(); it != range.end(); ++it) {
			auto other = it.is_nodea ? it->nodeb : it->nodea;
			auto newdist = el->first + dist(*it);
			//auto test = dist2(node, other) / (double)it->dist;
			//if (test > 2 || test < 0.5) jout << dist2(node, other) << "|" << it->dist << endl;
			if (ub[other] > newdist) {
				// update distance
				if (ub[other] < std::numeric_limits<u32>::max()) ring.erase({ ub[other], other });
				ring.insert({ newdist, other });
				ub[other] = newdist;
				next[other] = node;
				multi[other] = 0;
			} else if (ub[other] == newdist) {
				++multi[other];
			}
		}
		ring.erase(el);
	}
	u32 firstnode, r;
	if (s.type == Graph_position::on_edge) {
		auto const& es = edges()[s.edge];
		auto dista = ub[es.nodea] + (s.edge_pos / 256.0) * dist(es),
			distb = ub[es.nodeb] + (1 - (s.edge_pos / 256.0)) * dist(es);
		firstnode = dista < distb ? es.nodea : es.nodeb;
		r = (u32)std::min(dista, distb);
	} else if (s.type == Graph_position::on_node) {
		r = (u32)ub[firstnode = s.node];
	}
	else assert(false);
	if (into) {
		auto ofs = into->size();
		into->emplace_back<Route_t>().init(into);
		for (auto cur = firstnode; cur != node_invalid ; cur = next[cur]) {
			if (multi[cur] > 0) jerr << "WARNING: Ambiguous shortest path" << endl;
			into->get<Route_t>(ofs).push_back(cur, into);
		}
	}
	delete[] ub;
	delete[] next;
	return r;
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
