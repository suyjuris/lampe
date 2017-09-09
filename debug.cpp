#include "debug.hpp"

namespace jup {

Debug_ostream jdbg = Debug_ostream {std::cout};
Debug_tabulator tab = Debug_tabulator {};

bool debug_flag = false;

} /* end of namespace jup */
