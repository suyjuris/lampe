#include <csignal>
#include <iostream>
#include <thread>

#include "global.hpp"
#include "sockets.hpp"
#include "messages.hpp"
#include "system.hpp"
#include "server.hpp"

using namespace jup;

Server* server;

std::thread::id main_thread_id;


/**
 * Close the java process on termination. This is easier said than done.
 */
void handle_sigint(int sig) {
    //if (std::this_thread::get_id() != main_thread_id) return;
    assert(sig == SIGINT or sig == SIGABRT or sig == SIGTERM);
    jerr << "Caught interrupt, cleaning up...\n";
    program_closing = true;
    std::signal(SIGABRT, SIG_DFL);
    delete server;
    stop_abort_from_printing();
    std::abort();
}

/**
 * Throw a SIGINT when something is entered into stdin, workaround for mintty's
 * inability to properly Ctrl-C.
 */
void sigint_from_stdin() {
    std::cin.get();
    std::raise(SIGINT);
}

int main(int argc, c_str const* argv) {    
    if (argc <= 1) {
        jerr << "Not enough arguments.\n  Usage: " << argv[0] << " path [config]\n"
            "\n"
            " - path ist the path of the massim directory\n"
            " - config is the path of the configuration file relative to path.\n";
        return 1;
    }

    main_thread_id = std::this_thread::get_id();

    // Register signal handling
    assert(std::signal(SIGINT,  &handle_sigint) != SIG_ERR);
    assert(std::signal(SIGABRT, &handle_sigint) != SIG_ERR);
    assert(std::signal(SIGTERM, &handle_sigint) != SIG_ERR);

    std::thread stdin_listener;
    if (!is_debugged()) {
        stdin_listener = std::thread {&sigint_from_stdin};
    }
    
    c_str config = 0;
    if (argc > 2) {
        config = argv[2];
    }
    server = new Server {argv[1], config};
    
	init_messages();
	Socket_context socket_context;

    for (auto i: {"a1", "a2", "a3", "a4", "b1", "b2", "b3", "b4"}) {
        if (!server->register_agent(&greedy_agent, i)) {
            jerr << "Error: Could not connect agent, exiting.\n";
            return 1;
        }
    }
    server->run_simulation();

    delete server;
    return 0;
}
