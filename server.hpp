#pragma once

#include "sockets.hpp"
#include "system.hpp"
#include "objects.hpp"

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
    
}

struct Server_options {
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

    Buffer _string_storage;

    bool check_valid();
};

struct Mothership {
    virtual void on_sim_start(u8 agent, Simulation const& simulation, int sim_size) = 0;
    virtual void pre_request_action() = 0;
    virtual void pre_request_action(u8 agent, Perception const& perc, int perc_size) = 0;
    virtual void on_request_action() = 0;
    virtual void post_request_action(u8 agent, Buffer* into) = 0;
    virtual ~Mothership() {};
};

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

    void register_mothership(Mothership* mothership);
    bool register_agent(Server_options::Agent_option const& agent);
    void run_simulation();

    Flat_array<Agent_data>& agents() {
        return agent_buffer.get<Flat_array<Agent_data>>();
    }

private:
    Buffer agent_buffer;
    Buffer general_buffer;
	Buffer step_buffer;
    int mothership_offset;
    Mothership* mothership = nullptr;

    std::thread stdin_listener;
};

extern Server* server;

} /* end of namespace jup */


