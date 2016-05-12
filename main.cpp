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
 * Close the java process on termination
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
    
	Socket sock {"localhost", "12300"};
	assert(sock);
	send_message(sock, Message_Auth_Request {"a1", "1"});

    Buffer buffer;
    {
        buffer.reset();
        auto& mess = get_next_message_ref<Message_Auth_Response>(sock, &buffer);
        if (mess.succeeded) {
            jout << "Connected to server.\n";
        } else {
            jout << "Invalid authentification.\n";
            return 1;
        }
    }

    server->start_sim();

    int simulation_steps;
    {
        auto& mess = get_next_message_ref<Message_Sim_Start>(sock, &buffer);
        simulation_steps = mess.simulation.steps;
    }

    for (int step = 0; step < simulation_steps; ++step) {
        buffer.reset();
        auto& mess = get_next_message_ref<Message_Request_Action>(sock, &buffer);
        assert(mess.perception.simulation_step == step);
        
		auto& answ = buffer.emplace_back<Message_Action>
			( mess.perception.id, Action_Skip {}, &buffer );
		send_message(sock, answ);
    }

    delete server;
    
	return 0;
}
