#include "mutate.h"
#include <cassert>

void test_safe_ptr() {
    std::atomic_safe_ptr<int> p = nullptr;
    int tmp;
    std::atomic_mutate(p, [&](int* ip){ return &tmp; });
    assert(p.get() == &tmp);
}

int main() {
    test_safe_ptr();
}