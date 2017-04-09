
#include <iostream>

#include "global.hpp"

#ifdef SOFTASSERT
#include <list>
#include "objects.hpp"
void assert(bool expr) {
	if (!expr) throw(std::list<std::pair<int,jup::Dump>>());
}
#endif

namespace jup {

std::ostream& jout = std::cout;
std::ostream& jerr = std::cerr;

bool program_closing = false;

void debug_break() {}

} /* end of namespace jup */

