#include "agent.hpp"
#include <windows.h>

namespace jup {

Job const* currentJob = nullptr;
Buffer rbuf;
Flat_array<Item_stack> res;

Product const* getProductByID(Simulation const& sim, u8 id) {
	for (Product const& p : sim.products) {
		if (p.name == id) {
			return &p;
		}
	}
	return nullptr;
}

void getBaseMaterials(Simulation const& sim, Job_item i, Buffer *into) {

}

Action const& greedy_agent (Simulation const& sim, Perception const& perc) {
	jout << endl << BARRIER << endl << "Agent " << (int)sim.role.name << endl
		<< BARRIER << endl << "Storage:" << endl;

	for (Item_stack const& s : perc.self.items) {
		jout << (int)s.amount << "x " << (int)s.item << endl;
	}

	jout << BARRIER << endl;

	static Buffer actionBuffer;

	if (currentJob == nullptr) {
		if (perc.priced_jobs.begin() == perc.priced_jobs.end()) {
			return actionBuffer.emplace<Action_Skip>();
		}
		currentJob = &perc.priced_jobs[0];
		for (Job_item const& i : currentJob->items) {
			Product const* p = getProductByID(sim, i.item);
			assert(p);
			assert(i.item == p->name);
			if (p->assembled) {

			}
		}

	}

    return actionBuffer.emplace<Action_Skip>();    
}

}

