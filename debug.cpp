#include "debug.hpp"

#include <iostream>

namespace jup {

Debug_ostream jdbg = Debug_ostream {std::cout};
Debug_tabulator tab = Debug_tabulator {};

} /* end of namespace jup */
