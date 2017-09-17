
#include "objects.hpp"
#include "graph.hpp"
#include <stack>

namespace jup {

struct GH_Node {
	s32 edge_ref, lat, lon;
};

struct GH_Edge {
	s32 nodea, nodeb, linka, linkb, dist, flags, geo, name;
};

Node_range const Node::iter(Graph const& graph) const {
	assert(graph.m_data.inside(this));
	return Node_range{ &graph, (u32)(this - &graph.nodes()[0]), edge };
}

Edge_iterator Node_range::begin() const {
	assert(graph and node != node_invalid and edge != edge_invalid);
	bool is_nodea = edge != edge_invalid and graph->edges()[edge].nodea == node;
	return Edge_iterator{ graph, edge, is_nodea };
}

Edge_iterator Node_range::end() const {
	return Edge_iterator{};
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
	double fac = 1 + 2 * lat_lon_padding;
	lat = lat * lat_diff * fac - lat_diff * lat_lon_padding + map_min_lat;
	lon = lon * lon_diff * fac - lon_diff * lat_lon_padding + map_min_lon;
	return{ lat, lon };
}

Pos Graph_position::pos(Graph const& graph) const {

	if (is_node()) {
		return graph.nodes()[id].pos;
	} else {
		auto const& edge = graph.edges()[id];
		u8 n = edge.geo ? graph.geometry(edge.geo).size() + 1 : 1;
		auto pos_pillar = std::make_unique<Pos[]>(n + 1);
		auto dist_pillar = std::make_unique<float[]>(n + 1);
		u8 i = 0;
		pos_pillar[0] = graph.nodes()[edge.nodea].pos;
		for (Pos p : graph.geometry(edge.geo)) {
			pos_pillar[++i] = p;
		}
		pos_pillar[++i] = graph.nodes()[edge.nodeb].pos;
		assert(i == n);
		float d = 0;
		dist_pillar[0] = 0.f;
		for (u8 i = 1; i <= n; ++i) {
			dist_pillar[i] = d += graph.dist_air(pos_pillar[i - 1], pos_pillar[i]);
		}
		d *= get_edge_pos();
		i = 0;
		while (dist_pillar[++i] < d);
		auto a = pos_pillar[i - 1], b = pos_pillar[i];
		auto r = (d - dist_pillar[i - 1]) / (dist_pillar[i] - dist_pillar[i - 1]),
			s = 1.f - r;
		assert(0 <= r and r <= 1);
		return{ (u16)(a.lat * s + b.lat * r), (u16)(a.lon * s + b.lon * r) };
	}
}

Pos Graph::get_pos(double lat, double lon) const {
	double lat_diff = (map_max_lat - map_min_lat);
	double lon_diff = (map_max_lon - map_min_lon);
	double fac = (1 + 2 * lat_lon_padding);
	lat = (lat - map_min_lat + lat_diff * lat_lon_padding) / fac / lat_diff;
	lon = (lon - map_min_lon + lon_diff * lat_lon_padding) / fac / lon_diff;
	assert(0.0 <= lat and lat < 1.0);
	assert(0.0 <= lon and lon < 1.0);
	return Pos{ (u16)(lat * 65536.0 + 0.5), (u16)(lon * 65536.0 + 0.5) };
}

float Graph::dist_air(Pos const a, Pos const b) const {
	float dlat = (a.lat - b.lat) * map_scale_lat;
	float dlon = (a.lon - b.lon) * map_scale_lon;
	return sqrt(dlat*dlat + dlon*dlon);
}

Graph_position Graph::pos(Pos const pos) const {

	constexpr float const dist_invalid = std::numeric_limits<float>::max();
	// snap towards tower nodes
	constexpr float const edge_penalty = 2.f;
	// size of array to save nearest nodes for edge filtering
	constexpr u8 const bsize = 8;

	float min = dist_invalid;
	u32 id = node_invalid;
	float edge_pos = 0.f;
	bool is_node = true;

	std::pair<float, u32> best[bsize];
	for (u8 i = 0; i < bsize; ++i) best[i] = { dist_invalid, node_invalid };
	// tower nodes
	for (u32 node_id = 0; node_id < nodes().size(); ++node_id) {
		auto const& node = nodes()[node_id];
		if (node.edge == edge_invalid) continue;
		auto d = dist_air(pos, node.pos);
		std::pair<float, u32> c = { d, node_id };
		if (c < best[bsize - 1]) {
			best[bsize - 1] = c;
			std::sort(best, best + bsize);
		}
		if (d < min) {
			min = d;
			id = node_id;
		}
	}

	// edges
	for (u32 edge_id = 0; edge_id < edges().size(); ++edge_id) {
		auto const& edge = edges()[edge_id];
		if (edge.nodea == edge_invalid or edge.nodeb == edge_invalid) continue;
		bool b = false;
		for (u8 i = 0; i < bsize; ++i) {
			auto n = best[i].second;
			if (edge.nodea == n or edge.nodeb == n) {
				b = true;
				break;
			}
		}
		if (!b) continue;
		u8 n = edge.geo ? geometry(edge.geo).size() + 2 : 2;
		auto pos_node = std::make_unique<Pos[]>(n);
		pos_node[0];
		auto dist_node = std::make_unique<float[]>(n);
		u8 i = 0;
		Pos a = pos_node[i++] = nodes()[edge.nodea].pos;
		for (Pos p : geometry(edge.geo)) {
			pos_node[i++] = p;
		}
		pos_node[i++] = nodes()[edge.nodeb].pos;
		assert(i == n);
		float ed = 0;
		dist_node[0] = 0.f;
		for (u8 i = 1; i < n; ++i) {
			dist_node[i] = ed += dist_air(pos_node[i - 1], pos_node[i]);
		}
		// pillar nodes
		for (u8 i = 1; i < n - 1; ++i) {
			float d = dist_air(pos, pos_node[i]) + edge_penalty;
			if (d < min) {
				min = d;
				id = edge_id;
				edge_pos = dist_node[i] / ed;
				is_node = false;
			}
		}

		a = pos_node[0];
		for (u8 i = 1; i < n; ++i) {
			// line between two nodes
			Pos b = pos_node[i];
			float dlat = (b.lat - a.lat) * map_scale_lat,
				dlon = (b.lon - a.lon) * map_scale_lon,
				dplat = (pos.lat - a.lat) * map_scale_lat,
				dplon = (pos.lon - a.lon) * map_scale_lon;
			bool dir = abs(dlat) > abs(dlon);
			float r = dir ? (dplat + dplon*dlon / dlat) / (dlat + dlon*dlon / dlat)
				: (dplon + dplat*dlat / dlon) / (dlon + dlat*dlat / dlon);
			if (r > 0 and r < 1) {
				float d = std::abs(dir ? (dplon - dlon*r) / dlat
					: (dplat - dlat*r) / dlon) * dist_air(a, b) + edge_penalty;
				if (d < min) {
					min = d;
					id = edge_id;
					edge_pos = (dist_node[i - 1] + r * (dist_node[i] - dist_node[i - 1])) / ed;
					is_node = false;
				}
			}
			a = b;
		}
	}

	if (is_node) {
		return{ id, (u8)0 };
	} else {
		return{ id, edge_pos };
	}
}

u32 Graph::dist_road(Graph_position const s, Graph_position const t, Buffer* into) const {
	// bidirectional
	constexpr auto const dist_invalid = std::numeric_limits<u32>::max();
	// underestimate rounding error correction
	constexpr auto const dist_margin = 1000.f;

	if (s.is_node() and t.is_node() and s.id == t.id) {
		if (into) {
			int route_ofs = into->size();
			into->emplace_back<Route_t>();
			into->get<Route_t>(route_ofs).init(into);
			into->get<Route_t>(route_ofs).push_back(s.id, into);
		}
		return 0;
	}
	if (s.is_edge() and t.is_edge() and s.id == t.id) {
		auto& edge = edges()[s.id];
		if (((s.edge_pos <= t.edge_pos) and (edge.flags & 1)) or ((s.edge_pos >= t.edge_pos) and (edge.flags & 2))) {
			if (into) {
				int route_ofs = into->size();
				into->emplace_back<Route_t>();
				into->get<Route_t>(route_ofs).init(into);
			}
			return abs(s.get_edge_pos() - t.get_edge_pos()) * edges()[s.id].dist;
		}
	}
	
	auto spos = s.pos(*this);
	auto tpos = t.pos(*this);
	auto estimatef = [this, tpos](u32 const node) -> u32 {
		assert(node != node_invalid);
		float d = dist_air(tpos, nodes()[node].pos) * 1000.f - dist_margin;
		if (d < 0) return 0;
		return (u32)d;
	};
	auto estimateb = [this, spos](u32 const node) -> u32 {
		assert(node != node_invalid);
		float d = dist_air(spos, nodes()[node].pos) * 1000.f - dist_margin;
		if (d < 0) return 0;
		return (u32)d;
	};
	auto nnodes = nodes().size();
	Buffer buf;
	// upper bounds for node distances
	auto distf = std::make_unique<u32[]>(nnodes);
	auto distb = std::make_unique<u32[]>(nnodes);
	auto prev = std::make_unique<u32[]>(nnodes);
	auto next = std::make_unique<u32[]>(nnodes);
	auto visitedf = std::make_unique<u32[]>(nnodes / 8 + 1);
	auto visitedb = std::make_unique<u32[]>(nnodes / 8 + 1);
	for (u32 i = 0; i < nnodes; ++i) {
		distf[i] = dist_invalid;
		distb[i] = dist_invalid;
		prev[i] = edge_invalid;
		next[i] = edge_invalid;
	}
	for (u32 i = 0; i * 8 < nnodes; ++i) {
		visitedf[i] = 0;
		visitedb[i] = 0;
	}
	auto setvf = [&visitedf](u32 id) {
		visitedf[id >> 3] |= 1 << (id & 7);
	};
	auto getvf = [&visitedf](u32 id) -> bool {
		return visitedf[id >> 3] & 1 << (id & 7);
	};
	auto setvb = [&visitedb](u32 id) {
		visitedb[id >> 3] |= 1 << (id & 7);
	};
	auto getvb = [&visitedb](u32 id) -> bool {
		return visitedb[id >> 3] & 1 << (id & 7);
	};

	// total distance underestimate with associated node for forward search
	auto ringf = std::set<std::pair<u32, u32>>();
	if (s.is_edge()) {
		auto const& es = edges()[s.id];
		assert(es.nodea != node_invalid and es.nodeb != node_invalid);
		if (es.flags & 2)
			ringf.insert({ (distf[es.nodea] = (u32)(s.get_edge_pos() * es.dist)) + estimatef(es.nodea), es.nodea });
		if (es.flags & 1)
			ringf.insert({ (distf[es.nodeb] = (u32)((1.f - s.get_edge_pos()) * es.dist)) + estimatef(es.nodeb), es.nodeb });
	} else {
		distf[s.id] = 0;
		ringf.insert({ estimatef(s.id), s.id });
	}

	// total distance underestimate with associated node for backward search
	auto ringb = std::set<std::pair<u32, u32>>();
	if (t.is_edge()) {
		auto const& et = edges()[t.id];
		assert(et.nodea != node_invalid and et.nodeb != node_invalid);
		if (et.flags & 1)
			ringb.insert({ (distb[et.nodea] = (u32)(t.get_edge_pos() * et.dist)) + estimateb(et.nodea), et.nodea });
		if (et.flags & 2)
			ringb.insert({ (distb[et.nodeb] = (u32)((1.f - t.get_edge_pos()) * et.dist)) + estimateb(et.nodeb), et.nodeb });
	} else {
		distb[t.id] = 0;
		ringb.insert({ estimateb(t.id), t.id });
	}

	// middle node of incumbent best path
	auto midnode = node_invalid;
	// (upper bound for) length of best path
	auto inc = dist_invalid;
	while (true) {
		if (ringf.empty()) break;
		auto elf = ringf.begin();
		if (elf->first >= inc) break;
		auto nodef = elf->second;
		// bidirectional paths meet
		if (getvb(nodef)) {
			assert(distb[nodef] != dist_invalid);
			auto d = distf[nodef] + distb[nodef];
			if (d < inc) {
				// update incumbent
				midnode = nodef;
				inc = d;
				if (t.is_node() and (u32)t.id == nodef) break;
			}
		} else {
			setvf(nodef);
			auto const& rangef = nodes()[nodef].iter(*this);
			for (auto it = rangef.begin(); it != rangef.end(); ++it) {
				// skip misaligned one-way streets
				u32 flags = edges()[it.edge].flags;
				if (it.is_nodea and (flags & 1) == 0) continue;
				if (!it.is_nodea and (flags & 2) == 0) continue;

				auto other = it.is_nodea ? it->nodeb : it->nodea;
				assert(other != node_invalid);
				auto newdist = distf[nodef] + it->dist;
				assert(newdist > estimateb(other));

				if (newdist < distf[other]) {
					auto e = estimatef(other);
					// check against upper bound
					if (newdist + e < inc) {
						if(distf[other] == dist_invalid) ringf.insert({ newdist + e, other });
						distf[other] = newdist;
						prev[other] = nodef;
					}
				}
			}
		}
		ringf.erase(elf);

		if (ringb.empty()) break;
		auto elb = ringb.begin();
		if (elb->first >= inc) break;
		auto nodeb = elb->second;
		// bidirectional paths meet
		if (getvf(nodeb)) {
			assert(distf[nodeb] != dist_invalid);
			auto d = distf[nodeb] + distb[nodeb];
			if (d < inc) {
				// update incumbent
				midnode = nodeb;
				inc = d;
				if (s.is_node() and (u32)s.id == nodeb) break;
			}
		} else {
			setvb(nodeb);
			auto const& rangeb = nodes()[nodeb].iter(*this);
			for (auto it = rangeb.begin(); it != rangeb.end(); ++it) {
				// skip misaligned one-way streets
				u32 flags = edges()[it.edge].flags;
				if (it.is_nodea and (flags & 2) == 0) continue;
				if (!it.is_nodea and (flags & 1) == 0) continue;

				auto other = it.is_nodea ? it->nodeb : it->nodea;
				assert(other != node_invalid);
				auto newdist = distb[nodeb] + it->dist;
				assert(newdist > estimatef(other));

				if (newdist < distb[other]) {
					auto e = estimateb(other);
					// check against upper bound
					if (newdist + e < inc) {
						if(distb[other] == dist_invalid) ringb.insert({ newdist + e, other });
						distb[other] = newdist;
						next[other] = nodeb;
					}
				}
			}
		}
		ringb.erase(elb);
	}

	if (inc == dist_invalid) {
		jerr << "No path found in A*" << endl;
		jout << (u16)s.edge_pos << ", " << s.id << ", " << (u16)t.edge_pos << ", " << t.id << endl;
		auto a = get_pos_back(s.is_edge() ? nodes()[edges()[s.id].nodea].pos : nodes()[s.id].pos),
			b = get_pos_back(t.is_edge() ? nodes()[edges()[t.id].nodea].pos : nodes()[t.id].pos);
		jout << a.first << ", " << a.second << ", " << b.first << ", " << b.second << endl;
		if (s.is_edge() and (edges()[s.id].flags & 3) < 3) {
			auto const& edge = edges()[s.id];
			if (edge.flags & 1) jout << edge.linkb << ", " << nodes()[edge.nodeb].edge << endl;
			if (edge.flags & 2) jout << edge.linka << ", " << nodes()[edge.nodea].edge << endl;
		}
		if (t.is_edge() and (edges()[t.id].flags & 3) < 3) {
			auto const& edge = edges()[t.id];
			if (edge.flags & 1) jout << edge.linkb << ", " << nodes()[edge.nodeb].edge << endl;
			if (edge.flags & 2) jout << edge.linka << ", " << nodes()[edge.nodea].edge << endl;
		}
		assert(false);
		return inc;
	}

	if (into) {
		auto ofs = into->size();
		into->emplace_back<Route_t>().init(into);
		// write route from (excluded) midnode to begin
		for (auto cur = prev[midnode]; cur != node_invalid; cur = prev[cur]) {
			into->get<Route_t>(ofs).push_back(cur, into);
		}
		auto& route = into->get<Route_t>(ofs);
		auto size = route.size();
		// reverse route
		for (Route_t::Size_t i = 0; 2 * i < size - 1; ++i) {
			Route_t::Size_t o = size - i - 1;
			auto tmp = route[i];
			route[i] = route[o];
			route[o] = tmp;
		}
		// write route from midnode to end
		for (auto cur = midnode; cur != node_invalid; cur = next[cur]) {
			into->get<Route_t>(ofs).push_back(cur, into);
		}
	}
	return inc;
}

void Dist_cache::add_lookup(Graph_position pos) {
	assert(pos.id != node_invalid);
	lookup_buffer.reserve_space(sizeof(Lookups_t::Type));
	auto& ls = lookup_buffer.get<Lookups_t>();
	u32 l = ls.size();
	ls.push_back({ pos, l }, &lookup_buffer);
	std::sort(ls.begin(), ls.end());

	auto nnodes = graph->nodes().size();
	lookup_data.addsize(4 * nnodes * sizeof(u32));

	// forward dijkstra
	u32* dist = (u32*)lookup_data.end() - 4 * nnodes;
	u32* prev = (u32*)lookup_data.end() - 3 * nnodes;
	for (auto i = std::numeric_limits<u32>::min(); i < nnodes; ++i) {
		dist[i] = std::numeric_limits<u32>::max();
		prev[i] = edge_invalid;
	}
	auto ring = std::set<std::pair<u32, u32>>();

	if (pos.is_edge()) {
		auto const& es = graph->edges()[pos.id];
		assert(es.nodea != node_invalid and es.nodeb != node_invalid);
		if (es.flags & 2)
			ring.insert({ (dist[es.nodea] = (u32)(pos.get_edge_pos() * es.dist)), es.nodea });
		if (es.flags & 1)
			ring.insert({ (dist[es.nodeb] = (u32)((1.f - pos.get_edge_pos()) * es.dist)), es.nodeb });
	} else {
		dist[pos.id] = 0;
		ring.insert({ 0, pos.id });
	}

	for (auto el = ring.begin(); el != ring.end(); el = ring.begin()) {
		auto node = el->second;
		auto const& range = graph->nodes()[node].iter(*graph);
		for (auto it = range.begin(); it != range.end(); ++it) {
			u32 flags = it->flags;

			if (it.is_nodea and (flags & 1) == 0) continue;
			if (!it.is_nodea and (flags & 2) == 0) continue;

			auto other = it.is_nodea ? it->nodeb : it->nodea;
			assert(other != node_invalid);
			auto newdist = el->first + it->dist;

			if (dist[other] > newdist) {
				// update distance
				if (dist[other] < std::numeric_limits<u32>::max()) ring.erase({ dist[other], other });
				ring.insert({ newdist, other });
				dist[other] = newdist;
				prev[other] = node;
			}
		}
		ring.erase(el);

	}

	// backward dijkstra
	dist = (u32*)lookup_data.end() - 2 * nnodes;
	u32* next = (u32*)lookup_data.end() - 1 * nnodes;
	for (auto i = std::numeric_limits<u32>::min(); i < nnodes; ++i) {
		dist[i] = std::numeric_limits<u32>::max();
		next[i] = edge_invalid;
	}

	if (pos.is_edge()) {
		auto const& edge = graph->edges()[pos.id];
		assert(edge.nodea != node_invalid and edge.nodeb != node_invalid);
		if (edge.flags & 1)
			ring.insert({ (dist[edge.nodea] = (u32)(pos.get_edge_pos() * edge.dist)), edge.nodea });
		if (edge.flags & 2)
			ring.insert({ (dist[edge.nodeb] = (u32)((1.f - pos.get_edge_pos()) * edge.dist)), edge.nodeb });
	} else {
		ring.insert({ dist[pos.id] = 0, pos.id });
	}

	for (auto el = ring.begin(); el != ring.end(); el = ring.begin()) {
		auto node = el->second;
		auto const& range = graph->nodes()[node].iter(*graph);
		for (auto it = range.begin(); it != range.end(); ++it) {
			auto other = it.is_nodea ? it->nodeb : it->nodea;
			auto newdist = el->first + it->dist;
			u32 flags = it->flags;

			if (it.is_nodea and (flags & 2) == 0) continue;
			if (!it.is_nodea and (flags & 1) == 0) continue;

			if (dist[other] > newdist) {
				// update distance
				if (dist[other] < std::numeric_limits<u32>::max()) ring.erase({ dist[other], other });
				ring.insert({ newdist, other });
				dist[other] = newdist;
				next[other] = node;
			}
		}
		ring.erase(el);

	}
}

u32 Dist_cache::get_lookup(Graph_position pos) const {
	auto const& ls = lookup_buffer.get<Lookups_t>();
	// binary search
	s32 l = 0, r = ls.size() - 1;
	while (l <= r) {
		s32 m = (r + l) / 2;
		auto el = ls[m];
		if (el.first == pos) return el.second;
		if (el.first < pos) {
			l = m + 1;
		} else {
			r = m - 1;
		}
	}
	return lookup_invalid;
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
	auto pmin = get_pos_back({ 0, 0 });
	auto pmax = get_pos_back({ 65535, 65535 });
	map_scale_lat = (pmax.first - pmin.first) / 180.f * (radius_earth * M_PI) / 65535.f;
	map_scale_lon = (pmax.second - pmin.second) / 180.f * (lon_radius   * M_PI) / 65535.f;
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
	for (u32 i = 0; i < (u32)edge_count; ++i) {
		GH_Edge edge;
		file.read((char*)&edge, sizeof(edge));
		edges.push_back({ (u32)edge.nodea, (u32)edge.nodeb, (u32)edge.linka,
			(u32)edge.linkb, (u32)edge.dist, (u32)edge.flags, (u32)edge.geo, (u32)edge.name }, &m_data);
	}
	{
		// remove subnetworks by running Tarjan's strongly connected components algorithm
		auto in_main = std::make_unique<bool[]>(nodes.size());
		auto index = std::make_unique<u32[]>(nodes.size());
		auto lowlink = std::make_unique<u32[]>(nodes.size());
		auto stack_pos = std::make_unique<u32[]>(nodes.size());
		auto stack = std::make_unique<u32[]>(nodes.size());
		for (u32 i = 0; i < nodes.size(); ++i) {
			in_main[i] = false;
			index[i] = lowlink[i] = stack[i] = stack_pos[i] = node_invalid;
		}
		struct Call_stack_t {
			u32 node;
			u32 other;
			Edge_iterator it;
		};
		auto call_stack = std::make_unique<Call_stack_t[]>(nodes.size());
		u32 current_index = 0;

		const Edge_iterator it_invalid{};
		for (u32 i = 0; i < nodes.size(); ++i) {
			if (index[i] == node_invalid) {
				u32 node = i;
				u32 stack_size = 0;
				u32 call_depth = 0;
			new_node:
				index[node] = lowlink[node] = current_index++;
				stack[stack_size] = node;
				stack_pos[node] = stack_size++;
				call_stack[++call_depth] = { node, node_invalid, nodes[node].iter(*this).begin() };
			next_edge:
				auto& call = call_stack[call_depth];
				node = call.node;
				auto it = call.it;
				if (it == it_invalid) {
					if (lowlink[node] == index[node]) {
						if (stack_size - stack_pos[node] > nodes.size() / 2) {
							while (stack_size-- > stack_pos[node]) {
								in_main[stack[stack_size]] = true;
							}
							goto prune;
						} else {
							u32 c = node_invalid;
							while (c != node) {
								assert(stack_size > 0);
								c = stack[--stack_size];
								stack_pos[c] = node_invalid;
							}
						}
					}
					--call_depth;
					auto& call = call_stack[call_depth];
					node = call.node;
					u32 other = call.other;
					assert(other != node_invalid);
					u32 l = lowlink[other];
					if (l < lowlink[node]) lowlink[node] = l;
					goto next_edge;
				}
				++call.it;
				u32 flags = edges[it.edge].flags;
				if (it.is_nodea and (flags & 1) == 0) goto next_edge;
				if (!it.is_nodea and (flags & 2) == 0) goto next_edge;
				u32 other = it.is_nodea ? it->nodeb : it->nodea;
				assert(other != node_invalid);
				if (index[other] == node_invalid) {
					call.other = other;
					node = other;
					goto new_node;
				} else if (stack_pos[other] != node_invalid) {
					u32 i = index[other];
					if (i < lowlink[node]) lowlink[node] = i;
				}
				goto next_edge;
			}
		}
		assert(false);
	prune:
		for (u32 i = 0; i < nodes.size(); ++i) {
			if (not in_main[i]) {
				auto& node = nodes[i];
				if (node.edge != edge_invalid) {
					auto range = node.iter(*this);
					for (auto it = range.begin(); it != range.end();) {
						if (it.is_nodea) {
							auto& nodeb = nodes[it->nodeb];
							if (nodeb.edge == it.edge) nodeb.edge = it->linkb;
							else {
								for (auto& edge : nodeb.iter(*this)) {
									if (edge.linka == it.edge) {
										const_cast<Edge&>(edge).linka = it->linkb;
										break;
									}
									if (edge.linkb == it.edge) {
										const_cast<Edge&>(edge).linkb = it->linkb;
										break;
									}
								}
							}
						} else {
							auto& nodea = nodes[it->nodea];
							if (nodea.edge == it.edge) nodea.edge = it->linka;
							else {
								for (auto& edge : nodea.iter(*this)) {
									if (edge.linka == it.edge) {
										const_cast<Edge&>(edge).linka = it->linka;
										break;
									}
									if (edge.linkb == it.edge) {
										const_cast<Edge&>(edge).linkb = it->linka;
										break;
									}
								}
							}
						}
						auto& edge = const_cast<Edge&>(*it);
						++it;
						edge.nodea =
							edge.nodeb = node_invalid;
					}
					node.edge = edge_invalid;
				}
			}
		}
	}
	for (u32 i = 0; i < (u32)edge_count; ++i) {
		auto& e = edges[i];
		// fix out-of-bound nodes
		if (e.nodea >= (u32)node_count) {
			e.nodea = node_invalid;
		}
		if (e.nodeb >= (u32)node_count) {
			e.nodeb = node_invalid;
		}
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


void Dist_cache::init(int facility_count_, Graph const* graph_) {
    facility_count = facility_count_;
    graph = graph_;

    size_max = facility_count + agents_per_team;
    buffer.resize(sizeof(u8) * 256 * 2 + sizeof(Graph_position) * size_max
        + sizeof(u16) * (size_max * size_max));
    
    std::memset(buffer.data(), 0xff, buffer.size());
    id_to_index1 = {(u8*)buffer.data(), 256};
    id_to_index2 = {(u8*)id_to_index1.end(), 256};
    positions = {(Graph_position*)id_to_index2.end(), size_max};
    distances = {(u16*)positions.end(), size_max * size_max};
    assert((char*)distances.end() == buffer.end());

    for (auto& i: id_to_index1) { i = 0xff; }
    size = 0;

	lookup_buffer.emplace_back<Lookups_t>();
	lookup_buffer.get<Lookups_t>().init(&lookup_buffer);
}
    
void Dist_cache::register_pos(u8 id, Pos pos) {
    auto pos_g = graph->pos(pos);
    int index = positions.index(pos_g);
    if (index == -1 or index >= size) {
        index = size++;
        positions[index] = pos_g;
        assert(size <= size_max);
    }
    narrow(id_to_index1[id], index);
}

void Dist_cache::calc_facilities() {
    assert(size == facility_count);
	for (u8 i = 0; i < size; ++i) {
		auto pos = positions[i];
		add_lookup(pos);
	}

    /*for (u8 a = 0; a < size; ++a) {
        m_dist(a, a) = 0;
        for (u8 b = a + 1; b < size; ++b) {
            m_dist(a, b) = (u16)(graph->dist_road(positions[a], positions[b]) / 1000);
            m_dist(b, a) = (u16)(graph->dist_road(positions[b], positions[a]) / 1000);
        }
        }*/
}

void Dist_cache::calc_agents() {
    /*for (u8 a = facility_count; a < size; ++a) {
        m_dist(a, a) = 0;
        for (u8 b = 0; b < a; ++b) {
            m_dist(a, b) = (u16)(graph->dist_road(positions[a], positions[b]) / 1000);
            m_dist(b, a) = (u16)(graph->dist_road(positions[b], positions[a]) / 1000);
        }
        } */   
}


    /*for (int i = 0; i < 16; ++i) {
        for (auto j: dist_cache.id_to_index1.subview(i*16, 16)) {
            jdbg > jup_printf("%4d", (int)j);
        }
        jdbg,0;
    }
        jdbg,0;
    for (int i = 0; i < 16; ++i) {
        for (auto j: dist_cache.id_to_index2.subview(i*16, 16)) {
            jdbg > jup_printf("%4d", (int)j);
        }
        jdbg,0;
    }
        jdbg,0;
    for (u8 a = 0; a < dist_cache.size; ++a) {
        for (u8 b = 0; b < dist_cache.size; ++b) {
            jdbg > jup_printf("%6d", (int)dist_cache.m_dist(a, b));
        }
        jdbg,0;
    }
    jdbg,0;*/

void Dist_cache::reset() {
    for (u8 a = 0; a < facility_count; ++a) {
        std::memset(
            distances.data() + a*size_max + facility_count,
            0xff,
            (size_max - facility_count) * sizeof(u16)
        );
    }
    std::memset(
        distances.data() + facility_count*size_max,
        0xff,
        size_max * (size_max - facility_count) * sizeof(u16)
    );

    size = facility_count;
    for (auto& i: id_to_index1) {
        if (i > size) i = 0xff;
    }
}

void Dist_cache::move_to(u8 id, u8 to_id) {
    id_to_index2[id] = id_to_index2[to_id];
}

void Dist_cache::load_positions() {
    std::memcpy(id_to_index2.data(), id_to_index1.data(), id_to_index2.size() * sizeof(u8));
}

u16 Dist_cache::lookup(u8 a_id, u8 b_id) {
	u8 a = id_to_index2[a_id];
	u8 b = id_to_index2[b_id];
	if (m_dist(a, b) == 0xffff) {
		u32 dist = 0xffffffff;
		if (a == b) {
			dist = 0;
		} else {
			auto s = positions[a], t = positions[b];
			u32 lid = lookup_invalid;

			if ((lid = get_lookup(s)) != lookup_invalid) {
				if (t.is_node()) {
					dist = lookup_distf(lid)[t.id];
				} else {
					auto const& edge = graph->edges()[t.id];
					if (edge.flags & 1) {
						dist = lookup_distf(lid)[edge.nodea] + (u32)(t.get_edge_pos() * edge.dist);
					} if (edge.flags & 2) {
						u32 d = lookup_distf(lid)[edge.nodeb] + (u32)((1.f - t.get_edge_pos()) * edge.dist);
						if (d < dist) {
							dist = d;
						}
					}
				}
			} else if ((lid = get_lookup(t)) != lookup_invalid) {
				if (s.is_node()) {
					dist = lookup_distb(lid)[s.id];
				} else {
					auto const& edge = graph->edges()[s.id];
					if (edge.flags & 2) {
						dist = lookup_distb(lid)[edge.nodea] + (u32)(s.get_edge_pos() * edge.dist);
					} if (edge.flags & 1) {
						u32 d = lookup_distb(lid)[edge.nodeb] + (u32)((1.f - s.get_edge_pos()) * edge.dist);
						if (d < dist) {
							dist = d;
						}
					}
				}
			}
			if (lid == lookup_invalid) {
				dist = graph->dist_road(positions[a], positions[b]);
			}
		}

		m_dist(a, b) = (u16)(dist / 1000);
	}
    return m_dist(a, b);
}

u16 Dist_cache::lookup_old(u8 a_id, u8 b_id) {
    u8 a = id_to_index1[a_id];
    u8 b = id_to_index1[b_id];
    if (m_dist(a, b) == 0xffff) {
        m_dist(a, b) = a == b ? 0 : (u16)(graph->dist_road(positions[a], positions[b]) / 1000);
    }
    return m_dist(a, b);
}

} /* end of namespace jup */
