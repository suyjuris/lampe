#pragma once

#include "sockets.hpp"
#include "system.hpp"
#include "objects.hpp"

namespace jup {

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
        const char* name = nullptr;
        u8 id;
        u16 last_perception_id;
    };
    
public:
    Process proc;

    Server(const char* directory, const char* config = nullptr);

    void register_mothership(Mothership* mothership);
    bool register_agent(char const* name, char const* password = nullptr);
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
};

} /* end of namespace jup */


