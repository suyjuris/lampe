#pragma once

#include "buffer.hpp"
#include "objects.hpp"
#include "server.hpp"

namespace jup {


struct Mothership_complex : Mothership {    
	void on_sim_start(u8 agent, Simulation const& simulation, int sim_size) override;
	void pre_request_action() override;
	void pre_request_action(u8 agent, Percept const& perc, int perc_size) override;
	void on_request_action() override;
	void post_request_action(u8 agent, Buffer* into) override;
}


} /* end of namespace jup */
