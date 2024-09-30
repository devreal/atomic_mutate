#include "mutate.h"
#include <cassert>
#include <iostream>

#define MAX_VALUE 128

int32_t atomic_inc_mod(std::atomic<int32_t>& a) {
    int32_t res;
    // try until successful
    while (!std::atomic_mutate(a,
                [&](int32_t v){
                    res = (v+1) % MAX_VALUE;
                    return res;
                }))
    { }
    return res;
}

int64_t atomic_inc_mod(std::atomic<int64_t>& a) {
    int64_t res;
    // try until successful
    while (!std::atomic_mutate(a,
                [&](int64_t v){
                    res = (v+1) % MAX_VALUE;
                    return res;
                }))
    { }
    return res;
}

float atomic_inc_mod(std::atomic<float>& a) {
    float res;
    // try until successful
    while (!std::atomic_mutate(a,
                [&](float v){
                    if (v >= float(MAX_VALUE-1)) res = 0.0;
                    else res = v+1.0;
                    return res;
                }))
    { }
    return res;
}

double atomic_inc_mod(std::atomic<double>& a) {
    double res;
    // try until successful
    while (!std::atomic_mutate(a,
                [&](double v){
                    if (v >= double(MAX_VALUE-1)) res = 0.0;
                    else res = v+1.0;
                    return res;
                }))
    { }
    return res;
}
template<typename T>
void test_inc_mod() {
    std::atomic<T> a = 0;
    #pragma omp parallel for
    for (int i = 0; i < MAX_VALUE+2; ++i) {
        atomic_inc_mod(a);
    }
    std::cout << "test_inc_mod found " << a.load() << std::endl;
    assert(a.load() == 2);
}

int main() {
    test_inc_mod<int32_t>();
    test_inc_mod<int64_t>();
    test_inc_mod<float>();
    test_inc_mod<double>();
}