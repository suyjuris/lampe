#pragma once

#include "sockets.hpp"
#include "system.hpp"

namespace jup {

class Server {
public:
    Process proc;

    Server(const char* directory, const char* config = nullptr);

    void start_sim();
};

} /* end of namespace jup */


