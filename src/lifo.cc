
#include "mutate.h"
#include <iostream>
#include <chrono>

/* TODO: not sure whether this actually does something good */
void yield() { asm("yield"); }

#define NUM_ITER 1000000ULL


/**
 * Test case: simple lifo
 */

template<typename T>
struct lifo_elem {
    lifo_elem<T>* next = nullptr;
    T* ptr = nullptr;

    lifo_elem(T* p)
    : ptr(p)
    { }
};

/* a LIFO with free-list, implemented using std::atomic_safe_ptr
 * through the lifo_elem above */
template<typename T>
struct lifo {
private:
    using elem_t = lifo_elem<T>;
    std::atomic_safe_ptr<elem_t> head;
    std::atomic_safe_ptr<elem_t> free_list;

public:
    void push(T* value) {
        // try to take one from the free list
        elem_t* item = nullptr;
        /* try to get an element from the free-list */
        while (free_list.get(std::memory_order_relaxed) != nullptr &&
               !std::atomic_mutate_explicit(free_list,
                [&](elem_t* elem) -> std::optional<elem_t*> {
                    if (nullptr != elem) {
                        item = elem; // found an elem, replace head with next
                        return std::make_optional(elem->next);
                    }
                    return {}; // return empty
                }, std::memory_order_acquire, std::memory_order_relaxed))
        { yield(); }

        if (!item) {
            // allocate new
            item = new lifo_elem(value);
        }

        item->ptr = value;

        // TODO: need a _strong version of atomic_mutate?
        while (!std::atomic_mutate_explicit(head,
                [&](elem_t* elem) {
                    if (nullptr != elem) item->next = elem;
                    return item;
                }, std::memory_order_relaxed, std::memory_order_release))
        { yield(); }
    }

    T* pop() {
        elem_t* item = nullptr;
        /* try to get a pointer from the head */
        while (head.get(std::memory_order_relaxed) != nullptr &&
               !std::atomic_mutate_explicit(head,
                [&](elem_t* elem) -> std::optional<elem_t*> {
                    if (nullptr != elem) {
                        item = elem;
                        return std::make_optional(elem->next); // replace head with next item
                    }
                    return {}; // empty
                }, std::memory_order_acquire, std::memory_order_release))
        { yield(); }
        if (item) {
            /* put lifo element back on free list and return result */
            T* result = item->ptr;
            while (!std::atomic_mutate_explicit(free_list,
                    [&](elem_t* elem) {
                        if (elem) {
                            item->next = elem;
                        }
                        return item;
                    }, std::memory_order_relaxed, std::memory_order_release))
            { yield(); }
            return result;
        }
        // nothing to pop
        return nullptr;
    }
};


static lifo<int> l;


int main() {
    #pragma omp parallel
    {
        std::chrono::time_point<std::chrono::high_resolution_clock> beg, end;

        int val = 0;

        #pragma omp barrier
        beg = std::chrono::high_resolution_clock::now();
        #pragma omp for
        for (std::size_t i = 0; i < NUM_ITER; ++i) {
            l.push(&val);
            l.pop();
        }
        end = std::chrono::high_resolution_clock::now();
        #pragma omp single
        {
            double elapsed = (std::chrono::duration_cast<std::chrono::microseconds>(end - beg).count());
            std::cout << "total time " << elapsed << " [us] ; avg "
                      << elapsed / NUM_ITER << " [us]" << std::endl;
        }
    }


    return 0;
}
