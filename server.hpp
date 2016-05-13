#pragma once

#include "agent.hpp"
#include "sockets.hpp"
#include "system.hpp"

namespace jup {


class Server {
    struct Agent_data {
        Agent agent;
        Socket socket;
        const char* name = nullptr;
        Simulation* simulation = nullptr;
        u8 id;
    };
    
public:
    Process proc;

    Server(const char* directory, const char* config = nullptr);

    bool register_agent(Agent const& agent, char const* name, char const* password = nullptr);
    void run_simulation();

    auto& agents() {
        return general_buffer.get<Flat_array<Agent_data>>(agents_offset);
    }

private:
    Buffer general_buffer;
    Buffer step_buffer;
    int agents_offset;
};

} /* end of namespace jup */


