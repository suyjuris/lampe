#pragma once

#include "sockets.hpp"
#include "system.hpp"
#include "objects.hpp"
#include "graph.hpp"
                    
namespace jup {

namespace cmd_options {

constexpr auto MASSIM_LOC = "-m";
constexpr auto CONFIG_LOC = "-c";
constexpr auto HOST_IP = "-i";
constexpr auto HOST_PORT = "-p";
constexpr auto ADD_AGENT = "-a";
constexpr auto ADD_DUMMY = "-u";
constexpr auto DUMP_XML = "-d";
constexpr auto LOAD_CFGFILE = "--load";
constexpr auto LAMPE_SHIP = "-s";
constexpr auto MASSIM_QUIET = "-q";
constexpr auto STATS_FILE = "--stats";

constexpr auto LAMPE_SHIP_TEST = "test";
constexpr auto LAMPE_SHIP_TEST2 = "test2";
constexpr auto LAMPE_SHIP_STATS = "stats";
constexpr auto LAMPE_SHIP_PLAY = "play";
    
}

struct Server_options {
    enum Ship: u8 {
        SHIP_TEST, SHIP_TEST2, SHIP_STATS, SHIP_PLAY
    };
    struct Agent_option {
        Buffer_view name, password;
        bool is_dumb = false;
    };
    
    Buffer_view massim_loc;
    Buffer_view config_loc;
    Buffer_view host_ip;
    Buffer_view host_port;
    bool use_internal_server = true;
    std::vector<Agent_option> agents;
    Buffer_view dump_xml;
    u8 ship = SHIP_TEST;
	Buffer_view statistics_file;
    Buffer _string_storage;
    bool massim_quiet = false;

    bool check_valid();
};

struct Mothership {
	virtual void init(Graph const* graph) = 0;
    virtual void on_sim_start(u8 agent, Simulation const& simulation, int sim_size) = 0;
    virtual void pre_request_action() = 0;
    virtual void pre_request_action(u8 agent, Percept const& perc, int perc_size) = 0;
    virtual void on_request_action() = 0;
    virtual void post_request_action(u8 agent, Buffer* into) = 0;
    virtual ~Mothership() {};
};

// The maximum number of maps
constexpr int max_map_number = 5;

class Server {
    struct Agent_data {
        Socket socket;
        Buffer_view name = nullptr;
        u8 id;
        bool is_dumb;
        u16 last_perception_id;
    };
    
public:
    Process proc;
    Server_options const& options;

    Server(Server_options const& op);
    ~Server();

    bool load_maps();

    void register_mothership(Mothership* mothership);
    bool register_agent(Server_options::Agent_option const& agent);
    void run_simulation();

    Flat_array<Agent_data>& agents() {
        return agent_buffer.get<Flat_array<Agent_data>>();
    }

    auto& graph(int i) {
        assert(0 <= i and i < (int)graphs.size());
        return graphs[i];
    }
    auto const& graph(int i) const {
        assert(0 <= i and i < (int)graphs.size());
        return graphs[i];
    }

private:
    Buffer agent_buffer;
    Buffer general_buffer;
	Buffer step_buffer;
    int mothership_offset;
    Mothership* mothership = nullptr;
    std::vector<Graph> graphs;

    std::thread stdin_listener;
};

extern Server* server;

} /* end of namespace jup */


