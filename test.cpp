#include "flat_data.hpp"
#include "debug.hpp"

namespace jup {

void test_flat_diff() {
    Buffer b;
    b.reserve(1024);
    auto& lst = b.emplace<Flat_array<Flat_array<u8>>>();
    lst.init(&b);
    auto& lst1 = lst.emplace_back(&b);
    auto& lst2 = lst.emplace_back(&b);
    auto& lst3 = lst.emplace_back(&b);
    lst1.init(&b);
    lst1.push_back(10, &b);
    lst1.push_back(20, &b);
    lst2.init(&b);
    lst2.push_back(30, &b);
    lst2.push_back(40, &b);
    lst3.init(&b);
    lst3.push_back(50, &b);
    lst3.push_back(60, &b);

    Diff_flat_arrays diff {&b};
    diff.register_arr(lst);
    diff.register_arr(lst1);
    diff.register_arr(lst2);
    diff.register_arr(lst3);

    diff.add(lst2, 99);
    diff.apply();
    jdbg < lst ,0;

    diff.remove(lst2, 0);
    diff.apply();
    jdbg < lst ,0;

}

} /* end of namespace jup */










