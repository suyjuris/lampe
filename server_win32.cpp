
#include <cstring>
#include <initializer_list>
#include <io.h>

#include "server.hpp"
#include "messages.hpp"
#include "world.hpp"

namespace jup {

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

Server::Server(c_str directory, c_str config_par) {
    general_buffer.reserve(2048);

    int package_pattern = general_buffer.size();
    general_buffer.append(directory);
    general_buffer.append("\\target\\agentcontest-*.jar");
    general_buffer.append("", 1);
               
    int package = general_buffer.size();
    general_buffer.append("..\\target\\");
    if (!find_file_not_containing(general_buffer.data() + package_pattern,
                                  {"sources", "javadoc"}, &general_buffer)) {
        jerr << "Could not find server jar\n";
        assert(false);
    }
    jerr << "Using package: " << general_buffer.data() + package << '\n';

    int config;
    if (config_par) {
        config = general_buffer.size();
        general_buffer.append(config_par);
    } else {
        int config_pattern = general_buffer.size();
        general_buffer.append(directory);
        general_buffer.append("\\scripts\\conf\\2015*");
        general_buffer.append("", 1);

        config = general_buffer.size();
        general_buffer.append("conf\\");
        if (!find_file_not_containing(general_buffer.data() + config_pattern,
                                      {"random"}, &general_buffer)) {
            jerr << "Could not find config file\n";
            assert(false);
        }
    }
    jerr << "Using configuration: " << general_buffer.data() + config << '\n';

    int cmdline = general_buffer.size();
    general_buffer.append("java -ea -Dcom.sun.management.jmxremote -Xss2000k -X"
        "mx600M -DentityExpansionLimit=1000000 -DelementAttributeLimit=1000000 "
        "-Djava.rmi.server.hostname=TST -jar ");
    general_buffer.append(general_buffer.data() + package);
    general_buffer.append(" --conf ");
    general_buffer.append(general_buffer.data() + config);
    general_buffer.append("", 1);

    int dir = general_buffer.size();
    general_buffer.append(directory);
    general_buffer.append("\\scripts");
    general_buffer.append("", 1);
    
    proc.write_to_stdout = true;
    proc.init(general_buffer.data() + cmdline, general_buffer.data() + dir);
    
	proc.waitFor("[ NORMAL ]  ##   InetSocketListener created");

    general_buffer.reset();
    
    agents_offset = general_buffer.size();
    general_buffer.emplace_back<Flat_array<Agent_data>>();
    agents().init(&general_buffer);
}


bool Server::register_agent(Agent_callback const& agent, char const* name, char const* password) {
    assert(name);
    if (!password) {
        password = "1";
    }
    
    agents().push_back(Agent_data {}, &general_buffer);
    Agent_data& data = agents().back();

    data.agent = agent;
    data.name = name;
    data.id = agents().size();
    data.socket.init("localhost", "12300");
    if (!data.socket) { return false; }

    send_message(data.socket, Message_Auth_Request {name, password});

    // Put this onto the step_buffer, as the initialization of the agents() list
    // in the general_buffer has not been completed.
    auto& answer = get_next_message_ref<Message_Auth_Response>(data.socket, &step_buffer);
    if (!answer.succeeded) {
        // TODO: don't print the password onto the console, that's generally
        // dumb (maybe print stars instead)
        jerr << "Warning: Agent named " << name << " with password "
             << password << " could not log in\n";
        return false;
    }

    return true;
}

void Server::run_simulation() {
    proc.write_to_buffer = false;
    proc.write_to_stdout = true;
	proc.send("\n");
    
    int max_steps = -1;

	bool firstagent = true;

    for (Agent_data& i: agents()) {
        auto& mess = get_next_message_ref<Message_Sim_Start>(i.socket, &general_buffer);
        if (max_steps != -1) {
            assert(max_steps == mess.simulation.steps);
        } else {
            max_steps = mess.simulation.steps;
        }
        i.simulation = &mess.simulation;
		if (firstagent) {
			firstagent = false;
			world.seed_capital = mess.simulation.seed_capital;
			world.max_steps = mess.simulation.steps;
			world.simulation_id = mess.simulation.id;
			world.team_id = mess.simulation.team;
			world.products = &mess.simulation.products;
		}
    }

    for (int step = 0; step < max_steps; ++step) {
        if (step == max_steps - 1) {
            proc.write_to_buffer = true;
        }
        
        step_buffer.reset();

		firstagent = true;

        for (Agent_data& i: agents()) {

            auto& mess = get_next_message_ref<Message_Request_Action>(i.socket, &step_buffer);
            assert(mess.perception.simulation_step == step);

			if (firstagent) {
				firstagent = false;
				world.deadline = mess.perception.deadline;
				world.simulation_step = mess.perception.simulation_step;
				world.team = &mess.perception.team;
				world.charging_stations = &mess.perception.charging_stations;
				world.dump_locations = &mess.perception.dump_locations;
				world.shops = &mess.perception.shops;
				world.storages = &mess.perception.storages;
				world.workshops = &mess.perception.workshops;
				world.auction_jobs = &mess.perception.auction_jobs;
				world.priced_jobs = &mess.perception.priced_jobs;
				world.opponents = &step_buffer.get<Flat_array<Entity>>();
				for (Entity const& e : mess.perception.entities) {
					if (e.team != world.team_id) {
						world.opponents->push_back(e, &step_buffer);
					}
				}
				world.agents = &step_buffer.get<Flat_array<Agent>>();
			}

			// TODO: Agents aufbauen (aus mess.perception.self,
			// mess.perception.id und i.simulation->role)
			// und in world.agents einbinden
            
            Action const& action = i.agent(i.id, *i.simulation, mess.perception);
                 
            auto& answ = step_buffer.emplace_back<Message_Action>(
                mess.perception.id, (Action_Goto1&)action, &step_buffer
            );
            send_message(i.socket, answ);
        }
    }

    proc.waitFor("[ NORMAL ]  ##   ######################### new simulation run ----");
    
    for (Agent_data& i: agents()) {
        auto& mess = get_next_message_ref<Message_Sim_End>(i.socket, &general_buffer);
        jout << "The simulation has ended. Agent " << i.name << " has a ranking of "
             << (int)mess.ranking << " and a score of " << mess.score << '\n';
    }
}

} /* end of namespace jup */


