#pragma once

#include "flat_data.hpp"
#include "objects.hpp"

namespace jup {

struct Edge_iterator;
struct Graph;
struct Edge;

constexpr u32 node_invalid = 0xffffffff;
constexpr u32 edge_invalid = 0xffffffff;

struct Edge_iterator: public std::iterator<Edge, std::forward_iterator_tag> {
    Graph const* graph = nullptr;
	u32 edge = edge_invalid;
	bool is_nodea = false;

    Edge_iterator() {}
    Edge_iterator(Graph const* graph, u32 edge, bool is_nodea):
        graph{graph}, edge{edge}, is_nodea{is_nodea} {}
    
    Edge const& operator*() const;
	Edge const* operator->() const { return &**this; }
	Edge_iterator& operator++();

    bool operator==(Edge_iterator const& other) const {
        return edge == other.edge and is_nodea == other.is_nodea;
    }
	bool operator!=(Edge_iterator const& other) const {
        return not (*this == other);
    }
};

struct Node_range {
    Graph const* graph = nullptr;
    u32 node = node_invalid;
    u32 edge = edge_invalid;
    
	Edge_iterator begin() const;
	Edge_iterator end() const;
};

struct Node {
	u32 edge;
	Pos pos;

    Node_range const iter(Graph const& graph) const;
};

struct Edge {
	u32 nodea, nodeb, linka, linkb, dist, flags, geo, name;
};

struct u24 {
	u16 lb;
	u8 hb;
	
	u24() {}
	u24(u24 const& copy) : lb{ copy.lb }, hb{ copy.hb } {}
	u24(u32 const& val) : lb{ (u16)(val & 0xffff) }, hb{ (u8)((val & 0xff0000) >> 16) } { assert(val < 0x1000000); }
	u24 operator=(u32 val) { assert(val < 0x1000000); lb = (u16)(val & 0xffff); hb = (u8)((val & 0xff0000) >> 16); return *this; }
	operator u32() const { return ((u32)hb << 16) | lb; }
};

struct Graph_position {
	/**
	* id points to a node iff edge_pos == 0
	*/
	u24 id;
	u8 edge_pos;

	Graph_position() {}
	Graph_position(u24 id, u8 edge_pos) : id{ id }, edge_pos{ edge_pos } {}
	Graph_position(u24 id, float edge_pos) : id{ id } { set_edge_pos(edge_pos); }

	Pos pos(Graph const& graph) const;
	float get_edge_pos() const { assert(edge_pos); return (edge_pos - 0.5f) / 255.f; }
	void set_edge_pos(float val) {
		assert(val >= 0 && val <= 1);
		edge_pos = (u8)floor(val * 255.f);
		if (edge_pos < 255) ++edge_pos;
	}
};

struct Graph {
    using Nodes_t = Flat_array<Node, u32, u32>;
    using Edges_t = Flat_array<Edge, u32, u32>;
	using Geometry_t = Flat_array<Pos, u8, u8>;
	static constexpr int const geo_size = (sizeof(Geometry_t::Offset_t) + sizeof(Geometry_t::Size_t));
	static_assert(sizeof(Geometry_t::Type) == 2 * geo_size, "Geometry offset mismatch");
	using Route_t = Flat_array<u32, u16, u16>;


    /**
     * Reads the GraphHopper graph from node_filename and edge_filename into this graph. May be
     * called multiple times to re-initialize the graph.
     */ 
    void init(Buffer_view name, Buffer_view node_filename, Buffer_view edge_filename, Buffer_view geometry_filename);

    /**
     * Converts a lat, lon pair into a Pos
     */
    Pos get_pos(double lat, double lon) const;

	/**
	 * Returns the nearest node or edge
	 */
	Graph_position pos(Pos pos) const;

	/**
	 * Returns the distance of the shortest route between two positions
	 * Optionally writes that route into a buffer
	 */
	u32 dijkstra(Graph_position s, Graph_position t, Buffer* into = nullptr) const;
	u32 dijkstra(Pos s, Pos t, Buffer* into = nullptr) const { return dijkstra(pos(s), pos(t), into); };
    
    /**
     * Returns the actual coordinates from a position
     */
    std::pair<double, double> get_pos_back(Pos pos) const;

    Buffer_view const name() const { return {m_data.data() + name_offset, name_size}; }
    auto const& nodes() const { return m_data.get_c<Nodes_t>(node_offset); }
	auto const& edges() const { return m_data.get_c<Edges_t>(edge_offset); }
	auto const& geometry(u32 ofs) const { return m_data.get_c<Geometry_t>(geometry_offset + geo_size * ofs); }

    double map_min_lat = 0.0;
    double map_max_lat = 0.0;
    double map_min_lon = 0.0;
    double map_max_lon = 0.0;
    float map_scale_lat = 0.f;
    float map_scale_lon = 0.f;

    Buffer m_data;

    int node_offset = -1;
    int edge_offset = -1;
	int geometry_offset = -1;
	int name_offset = -1;

    int name_size = 0;

};

} /* end of namespace jup */
