#include <cstring>
#include <csignal>
#include <iostream>
#include <io.h>
#include <initializer_list>
#include <thread>

#include "server.hpp"
#include "messages.hpp"
#include "debug.hpp"

namespace jup {

Server* server;

/**
 * Finds the first file matching pattern, while containing none of the string in
 * antipattern, then append its name (including the zero) to the buffer. Returns
 * whether a file was found.
 */
bool find_file_not_containing(
        c_str pattern,
        std::initializer_list<c_str> antipattern,
        Buffer* into
) {
    assert(into);

    _finddata_t file;

    auto handle = _findfirst(pattern, &file);
    if (handle + 1 == 0) return 0;
    auto code = handle;
    while (true) {
        if (code + 1 == 0) return 0;

        bool flag = false;
        for (auto i: antipattern) {
            if (std::strstr(file.name, i)) {
                flag = true;
                break;
            }
        }
        if (!flag) break;
        
        code = _findnext(handle, &file);
    }

    into->append(file.name, std::strlen(file.name) + 1);
    _findclose(handle);
    return 1;
}

bool Server_options::check_valid() {
    using namespace cmd_options;
    if (use_internal_server) {
        if (not massim_loc) {
            jerr << "The internal server is used, but the location of massim is not specified."
                " You may want to use the " << MASSIM_LOC << " option.\n";
            return false;
        }
    } else {
        if (not host_ip) {
            jerr << "Connection to external server requested, but the host ip is not specified."
                " You may want to use the " << HOST_IP << " option.\n";
            return false;
        }
    }

    for (size_t i = 0; i + 1 < agents.size(); ++i) {
        for (size_t j = i + 1; j < agents.size(); ++j) {
            if (agents[i].name == agents[j].name) {
                jerr << "You are trying to connect two agents with the same name ("
                     << agents[i].name.c_str() << "). This would result in a crash, that someone ma"
                    "y or may not have debugged for quite a while longer than they needed to.\n";
                return false;
            }
        }
    }
    
    return true;
}

/**
 * Close the java process on termination. This is easier said than done.
 */
void handle_sigint(int sig) {
    if (sig == SIGINT) {
        jerr << "Caught interrupt, cleaning up...\n";
    } else if (sig == SIGABRT) {
        jerr << "abort() was called, cleaning up...\n";
    } else if (sig == SIGTERM) {
        jerr << "Caught a SIGTERM, cleaning up...\n";
    } else if (sig == SIGFPE) {
        jerr << "An arithmetic exception occurred, cleaning up...\n";
    } else {
        assert(false);
    }
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
    if (std::cin) {
        std::raise(SIGINT);
    }
}

Server::Server(Server_options const& op): options{op} {
    using namespace cmd_options;

    if (options.use_internal_server) {
        jout << "Running internal server...\n";
    } else {
        jout << "Connecting to external server, IP: " << options.host_ip.c_str() << ", Port: "
             << options.host_port.c_str() << "\n";
    }
    
    if (op.use_internal_server) {

        // Register signal handling
        assert(std::signal(SIGINT,  &handle_sigint) != SIG_ERR);
        assert(std::signal(SIGABRT, &handle_sigint) != SIG_ERR);
        assert(std::signal(SIGTERM, &handle_sigint) != SIG_ERR);
        assert(std::signal(SIGFPE,  &handle_sigint) != SIG_ERR);

        if (!is_debugged()) {
            stdin_listener = std::thread {&sigint_from_stdin};
        }

        // Start finding the configuration and stuff
        general_buffer.reserve(2048);

        int package_pattern = general_buffer.size();
        general_buffer.append(op.massim_loc);
        general_buffer.append("\\target\\agentcontest-*.jar");
        general_buffer.append("", 1);
               
        int package = general_buffer.size();
        general_buffer.append("..\\target\\");
        if (!find_file_not_containing(general_buffer.data() + package_pattern,
                                      {"sources", "javadoc"}, &general_buffer)) {
            jerr << "Could not find server jar. You specified the location:\n";
            jerr << "  " << op.massim_loc << '\n';
            assert(false);
        }
        jerr << "Using package: " << general_buffer.data() + package << '\n';

        int config;
        if (op.config_loc) {
            int config_pattern = general_buffer.size();
            general_buffer.append(op.massim_loc);
            general_buffer.append("\\scripts\\");
            config = general_buffer.size();
            general_buffer.append(op.config_loc);
            general_buffer.append("", 1);
            if (not file_exists(general_buffer.data() + config_pattern)) {
                jerr << "The configuration file does not exist. You specified the location:\n  "
                     << op.config_loc << "\nPlease make sure to specify the location relative to ma"
                     <<"ssim's scripts directory.\n";
                assert(false);
            }
        } else {
            int config_pattern = general_buffer.size();
            general_buffer.append(op.massim_loc);
            general_buffer.append("\\scripts\\conf\\2015*");
            general_buffer.append("", 1);

            config = general_buffer.size();
            general_buffer.append("conf\\");
            if (!find_file_not_containing(general_buffer.data() + config_pattern,
                                          {"random"}, &general_buffer)) {
                jerr << "Could not find config file using the default pattern. Use the "
                     << CONFIG_LOC << " option to specify a different path. The default pattern is:"
                     << "\n  " << general_buffer.data() + config_pattern << '\n';
                assert(false);
            }
        }
        jerr << "Using configuration: " << general_buffer.data() + config << '\n';

        int cmdline = general_buffer.size();
        general_buffer.append("java -ea -Dcom.sun.management.jmxremote -Xss2000k -Xmx600M -DentityE"
                              "xpansionLimit=1000000 -DelementAttributeLimit=1000000 -Djava.rmi.ser"
                              "ver.hostname=TST -jar ");
        general_buffer.append(general_buffer.data() + package);
        general_buffer.append(" --conf ");
        general_buffer.append(general_buffer.data() + config);
        general_buffer.append("", 1);

        int dir = general_buffer.size();
        general_buffer.append(op.massim_loc);
        general_buffer.append("\\scripts");
        general_buffer.append("", 1);
    
        proc.write_to_stdout = true;
        // TODO: Better error message for missing java
        proc.init(general_buffer.data() + cmdline, general_buffer.data() + dir);

        proc.waitFor("[ NORMAL ]  ##   InetSocketListener created");
                         
        general_buffer.reset();
    }

    agent_buffer.reserve(2048);
    agent_buffer.emplace_back<Flat_array<Agent_data>>();
    agents().init(&agent_buffer);
}

Server::~Server() {
    if (stdin_listener.joinable() and stdin_listener.get_id() != std::this_thread::get_id()) {
        cancel_blocking_io(stdin_listener);
        std::cin.setstate(std::ios_base::failbit);  
        stdin_listener.join();
    } else if (stdin_listener.joinable())  {
        stdin_listener.detach();
    }
}

void Server::register_mothership(Mothership* mothership_) {
    //mothership = mothership_;
}

bool Server::register_agent(Server_options::Agent_option const& agent) {
    assert(agent.name);
    assert(agent.password);
    
    agents().push_back(Agent_data {}, &agent_buffer);
    Agent_data& data = agents().back();

    data.name = agent.name;
    data.id = agents().size() - 1;
    data.is_dumb = agent.is_dumb;
    if (options.use_internal_server) {
        data.socket.init("localhost", "12300");
    } else {
        if (options.host_port) {
            data.socket.init(options.host_ip, options.host_port);
        } else {
            data.socket.init(options.host_ip, "12300");
        }
    }
    
    
    if (!data.socket) { return false; }

    send_message(data.socket, Message_Auth_Request {agent.name, agent.password});

    // Put this onto the step_buffer, as the initialization of the agents() list
    // in the general_buffer has not been completed.
    auto& answer = get_next_message_ref<Message_Auth_Response>(data.socket, &step_buffer);
    if (!answer.succeeded) {
        // TODO: don't print the password onto the console, that's generally
        // dumb (maybe print stars instead)
        jerr << "Warning: Agent named " << agent.name << " with password "
             << agent.password << " could not log in\n";
        return false;
    }

    return true;
}

void Server::run_simulation() {
    if (options.agents.size()) {
        // Make sure to have all the dumb agents at the end
        for (auto i: options.agents) {
            if (i.is_dumb) continue;
            if (not server->register_agent(i)) {
                jerr << "Error: Could not connect agent, exiting.\n";
                assert(false);
                return;
            }
        }
        for (auto i: options.agents) {
            if (not i.is_dumb) continue;
            if (not server->register_agent(i)) {
                jerr << "Error: Could not connect agent, exiting.\n";
                assert(false);
                return;
            }
        }
    } else {
        assert(false);
    }

    // Press ENTER to start the simulation
    if (options.use_internal_server) {
        proc.write_to_buffer = false;
        proc.write_to_stdout = true;
        proc.send("\n");
    } else {
        jout << "Connected.\n";
    }
    
    while (true) {
        delete mothership;
        mothership = new Mothership_simple;
        reset_messages();
        
        int max_steps = -1;

        for (int i = 0; i < agents().size(); ++i) {
            int mess_offset = general_buffer.size();
            u8 type = get_next_message(agents()[i].socket, &general_buffer);
            if (type == Message::BYE) {
                goto outer;
            } else if (type == Message::SIM_START) {
                if (i == 0)
                    jout << "Start of a new simulation.\n";
            } else {
                assert(false);
            }
            auto& mess = general_buffer.get<Message_Sim_Start>(mess_offset);
            
            mess.simulation.id = register_id(agents()[i].name);
            if (max_steps != -1) {
                assert(max_steps == mess.simulation.steps);
            } else {
                max_steps = mess.simulation.steps;
            }
            if (not agents()[i].is_dumb) {
                mothership->on_sim_start(agents()[i].id, mess.simulation,
                                         general_buffer.end() - (char*)&mess.simulation);
            }
        }
        
        for (int step = 0; step < max_steps; ++step) {
            if (options.use_internal_server and step == max_steps - 1) {
                proc.write_to_buffer = true;
            }

            if (not options.use_internal_server) {
                jout << "Currently in step " << step + 1 << '\n';
            }
        
            step_buffer.reset();
            mothership->pre_request_action();
        
            for (Agent_data& i: agents()) {
                auto& mess = get_next_message_ref<Message_Request_Action>(i.socket, &step_buffer);
                if (options.use_internal_server) {
                    assert(mess.perception.simulation_step == step);
                } else {
                    if (step != mess.perception.simulation_step) {
                        jerr << "Warning: Adjusting simulation step from " << step << " to "
                             << mess.perception.simulation_step << '\n';
                        step = mess.perception.simulation_step;
                    }
                }
                

                i.last_perception_id = mess.perception.id;
                if (not i.is_dumb) {
                    mothership->pre_request_action(i.id, mess.perception,
                                                   step_buffer.end() - (char*)&mess.perception);
                }
            }

            mothership->on_request_action();
        
            for (Agent_data& i: agents()) {
                int action_offset = step_buffer.size();
            
                if (i.is_dumb) {
                    step_buffer.emplace_back<Action_Skip>();
                } else {
                    mothership->post_request_action(i.id, &step_buffer);
                }

                // TODO: Fix the allocations
                step_buffer.reserve_space(256);
            
                auto& answ = step_buffer.emplace_back<Message_Action>(
                    i.last_perception_id, step_buffer.get<Action_Post_job1>(action_offset), &step_buffer
                    );
                send_message(i.socket, answ);
            }
        }
                    
        for (Agent_data& i: agents()) {
            auto& mess = get_next_message_ref<Message_Sim_End>(i.socket, &general_buffer);
            jout << "The match has ended. Agent " << i.name.c_str() << " has a ranking of "
                 << (int)mess.ranking << " and a score of " << mess.score << '\n';
        }

        if (options.use_internal_server) break;
    }

  outer:
    jout << "The tournament has ended.\n";    
}

} /* end of namespace jup */


