#include "mutate.h"
#include <cassert>
#include <iostream>
#include <chrono>
#include <string>

#define MAX_VALUE 128
#define NUM_ITER 10000000ULL

template<typename T, bool UseOptional>
void atomic_inc_mod(std::atomic<T>& a) {
    // try until successful
    std::atomic_mutate_explicit(a,
                [&](T v){
                    T res;
                    if constexpr (std::is_integral_v<T>) {
                        res = (v+1) % MAX_VALUE;
                    } else {
                        if (v >= T(MAX_VALUE-1)) res = 0.0;
                        else res = v+1.0;
                    }
                    if constexpr (UseOptional) {
                        return std::optional<T>(res);
                    } else {
                        return res;
                    }
                }, std::memory_order_relaxed, std::memory_order_relaxed);
    //std::cout << "res = " << a.load() << std::endl;
}

template<typename T, bool UseOptional>
void test_inc_mod(uint64_t niter) {
    std::chrono::time_point<std::chrono::high_resolution_clock> beg, end;
    std::atomic<T> a = 0;
    beg = std::chrono::high_resolution_clock::now();
    #pragma omp parallel for
    for (uint64_t i = 0; i < niter; ++i) {
        atomic_inc_mod<T, UseOptional>(a);
    }
    end = std::chrono::high_resolution_clock::now();
    double elapsed = (std::chrono::duration_cast<std::chrono::microseconds>(end - beg).count());
    std::cout << "test_inc_mod<" << typeid(T).name() << ", " << UseOptional << "> found " << a.load() << " ; total " << elapsed
              << " [us] ; avg " << elapsed / niter << " [us]" << std::endl;
    assert(a.load() == (niter%MAX_VALUE));
}

int main(int argc, char* argv[]) {
    uint64_t niter = NUM_ITER;
    if (argc > 1) niter = std::stoull(argv[1]);
    test_inc_mod<int32_t, false>(niter);
    test_inc_mod<int64_t, false>(niter);
    test_inc_mod<float, false>(niter);
    test_inc_mod<double, false>(niter);

    test_inc_mod<int32_t, true>(niter);
    test_inc_mod<int64_t, true>(niter);
    test_inc_mod<float, true>(niter);
    test_inc_mod<double, true>(niter);
}