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
constexpr auto DUMP_XML = "-d";
constexpr auto LOAD_CFGFILE = "--load";
    
}

struct Server_options {
    struct Agent_option {
        Buffer_view name, password;
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
};

class Server {
    struct Agent_data {
        Socket socket;
        Buffer_view name = nullptr;
        u8 id;
        u16 last_perception_id;
    };
    
public:
    Process proc;
    Server_options options;

    Server(Server_options const& op);
    ~Server();

    void register_mothership(Mothership* mothership);
    bool register_agent(Buffer_view name, Buffer_view password);
    void run_simulation();

    auto& agents() {
        return general_buffer.get<Flat_array<Agent_data>>(agents_offset);
    }

private:
    Buffer general_buffer;
	Buffer step_buffer;
    int agents_offset;
    int mothership_offset;
    Mothership* mothership;

    std::thread stdin_listener;
};

extern Server* server;

} /* end of namespace jup */


