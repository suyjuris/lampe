#pragma once

#include "flat_data.hpp"
#include "objects.hpp"

namespace jup {

struct Edge_iterator;
struct Graph;
struct Edge;

constexpr u32 node_invalid = 0xffff;
constexpr u32 edge_invalid = 0xffff;

struct Edge_iterator: public std::iterator<Edge, std::forward_iterator_tag> {
    Graph const* graph = nullptr;
	u32 edge = edge_invalid;
	bool is_nodea = false;

    Edge_iterator() {}
    Edge_iterator(Graph const* graph, u32 edge, bool is_nodea):
        graph{graph}, edge{edge}, is_nodea{is_nodea} {}
    
    Edge const& operator*() const;
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

    Node_range const iter(Graph const& graph);
};

struct Edge {
	u32 nodea, nodeb, linka, linkb, dist;
};


struct Graph {
    using Nodes_t = Flat_array<Node, u32, u32>;
    using Edges_t = Flat_array<Edge, u32, u32>;

    /**
     * Reads the GraphHopper graph from node_filename and edge_filename into this graph. May be
     * called multiple times to re-initialize the graph.
     */ 
    void init(Buffer_view name, Buffer_view node_filename, Buffer_view edge_filename);

    /**
     * Converts a lat, lon pair into a Pos
     */
    Pos get_pos(double lat, double lon) const;
    
    /**
     * Returns the actual coordinates from a position
     */
    std::pair<double, double> get_pos_back(Pos pos) const;

    Buffer_view const name() const { return {m_data.data() + name_offset, name_size}; }
    auto const& nodes() const { return m_data.get_c<Nodes_t>(node_offset); }
    auto const& edges() const { return m_data.get_c<Edges_t>(edge_offset); }

    double map_min_lat = 0.0;
    double map_max_lat = 0.0;
    double map_min_lon = 0.0;
    double map_max_lon = 0.0;
    float map_scale_lat = 0.f;
    float map_scale_lon = 0.f;

    Buffer m_data;

    int node_offset = -1;
    int edge_offset = -1;
    int name_offset = -1;

    int name_size = 0;

};

} /* end of namespace jup */
