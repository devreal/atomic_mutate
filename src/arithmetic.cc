#include "mutate.h"
#include <cassert>
#include <iostream>
#include <chrono>

#define MAX_VALUE 128
#define NUM_ITER 1000000

template<typename T>
T atomic_inc_mod(std::atomic<T>& a) {
    T res;
    // try until successful
    while (!std::atomic_mutate_explicit(a,
                [&](T v){
                    if constexpr (std::is_integral_v<T>) {
                        res = (v+1) % MAX_VALUE;
                    } else {
                        if (v >= T(MAX_VALUE-1)) res = 0.0;
                        else res = v+1.0;
                    }
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
    for (int i = 0; i < NUM_ITER; ++i) {
        auto res = atomic_inc_mod(a);
    }
    end = std::chrono::high_resolution_clock::now();
    double elapsed = (std::chrono::duration_cast<std::chrono::microseconds>(end - beg).count());
    std::cout << "test_inc_mod found " << a.load() << " ; total " << elapsed
              << " [us] ; avg " << elapsed / NUM_ITER << " [us]" << std::endl;
    assert(a.load() == (NUM_ITER%MAX_VALUE));
}

int main() {
    test_inc_mod<int32_t>();
    test_inc_mod<int64_t>();
    test_inc_mod<float>();
    test_inc_mod<double>();
}