#include "mutate.h"
#include <cassert>
#include <iostream>

#define MAX_VALUE 128
#define NUM_ITER 100000

int32_t atomic_inc_mod(std::atomic<int32_t>& a) {
    int32_t res;
    // try until successful
    while (!std::atomic_mutate_explicit(a,
                [&](int32_t v){
                    res = (v+1) % MAX_VALUE;
                    return res;
                }, std::memory_order_relaxed, std::memory_order_relaxed))
    { }
    return res;
}

int64_t atomic_inc_mod(std::atomic<int64_t>& a) {
    int64_t res;
    // try until successful
    while (!std::atomic_mutate_explicit(a,
                [&](int64_t v){
                    res = (v+1) % MAX_VALUE;
                    return res;
                }, std::memory_order_relaxed, std::memory_order_relaxed))
    { }
    return res;
}

float atomic_inc_mod(std::atomic<float>& a) {
    float res;
    // try until successful
    while (!std::atomic_mutate_explicit(a,
                [&](float v){
                    if (v >= float(MAX_VALUE-1)) res = 0.0;
                    else res = v+1.0;
                    return res;
                }, std::memory_order_relaxed, std::memory_order_relaxed))
    { }
    return res;
}

double atomic_inc_mod(std::atomic<double>& a) {
    double res;
    // try until successful
    while (!std::atomic_mutate_explicit(a,
                [&](double v){
                    if (v >= double(MAX_VALUE-1)) res = 0.0;
                    else res = v+1.0;
                    return res;
                }, std::memory_order_relaxed, std::memory_order_relaxed))
    { }
    return res;
}
template<typename T>
void test_inc_mod() {
    std::chrono::time_point<std::chrono::high_resolution_clock> beg, end;
    std::atomic<T> a = 0;
    beg = std::chrono::high_resolution_clock::now();
    #pragma omp parallel for
    for (int i = 0; i < 100000; ++i) {
        auto res = atomic_inc_mod(a);
    }
    end = std::chrono::high_resolution_clock::now();
    double elapsed = (std::chrono::duration_cast<std::chrono::microseconds>(end - beg).count());
    std::cout << "test_inc_mod found " << a.load() << " total " << elapsed << " ; avg " << elapsed / MAX_VALUE+2 << std::endl;
    assert(a.load() == (100000%MAX_VALUE));
}

int main() {
    test_inc_mod<int32_t>();
    test_inc_mod<int64_t>();
    test_inc_mod<float>();
    test_inc_mod<double>();
}