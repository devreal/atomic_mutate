#pragma once

#include <atomic>
#include <optional>
#include <cstdint>

/* enable LL/SC on arm64 if the user didn't force CAS */
#if !defined(USE_LL_SC) && defined(__aarch64__) && !defined(ATOMIC_MUTATE_FORCE_CAS)
#define USE_LL_SC 1
#warning "Using LL/SC implementation of atomic_mutate on arm64"
#endif

namespace std {

    /** fwd decls */

    /**
     * std::atomic_mutate_explicit: atomically mutate the value of an atomic variable
     * using a user-provided function. The function is passed the current value of the
     * atomic variable, and returns either the new value to be stored, or std::nullopt
     * to indicate that the mutation should be aborted. The function may be invoked
     * multiple times if there are conflicting mutations, and the implementation should
     * retry the mutation until either it succeeds in updating the atomic variable,
     * or until it returns std::nullopt.
     *
     * The load_order and store_order parameters specify the memory ordering constraints for
     * the load and store operations performed by the implementation. The load_order
     * parameter specifies the memory ordering for the load operation that reads the current
     * value of the atomic variable, while the store_order parameter specifies the memory
     * ordering for the store operation that updates the atomic variable.
     */
    template<typename Fn, typename T>
    requires(std::is_invocable_r_v<T, Fn, std::add_rvalue_reference_t<T>>
          || std::is_invocable_r_v<std::optional<T>, Fn, std::add_rvalue_reference_t<T>>)
    inline void atomic_mutate_explicit(std::atomic<T>& a, Fn&& fn,
                                       std::memory_order load_order,
                                       std::memory_order store_order);

    /**
     * Non-explicit variant using std::memory_order_seq_cst for both load and
     * store ordering.
     */
    template<typename Fn, typename T>
    inline void atomic_mutate(std::atomic<T>& a, Fn&& fn);

#ifndef USE_LL_SC

    /**
     * Implementation of atomic_mutate using compare-exchange.
     */
    template<typename Fn, typename T>
    requires(std::is_invocable_r_v<T, Fn, std::add_rvalue_reference_t<T>>
          || std::is_invocable_r_v<std::optional<T>, Fn, std::add_rvalue_reference_t<T>>)
    inline void atomic_mutate_explicit(std::atomic<T>& a, Fn&& fn,
                                       std::memory_order load_order,
                                       std::memory_order store_order) {
        using R = std::invoke_result_t<Fn, std::add_rvalue_reference_t<T>>;
        T val;
        bool success;
        val = a.load(load_order);
        do {
            if constexpr (std::is_same_v<R, T>) {
                // if the function returns T, we can skip the optional wrapper
                auto newval = fn(val);
                success = a.compare_exchange_weak(val, newval,
                                                  std::memory_order_relaxed,
                                                  store_order);
            } else {
                std::optional<T> opt = fn(val);
                if (!opt) return;
                success = a.compare_exchange_weak(val, opt.value(),
                                                  std::memory_order_relaxed,
                                                  store_order);
            }
        } while (!success);
    }

#else  // USE_LL_SC

    namespace detail {

        /* dirty little helper: there is currently no way to get
         * a pointer to the underlying data of a std::atomic;
         * for ll/sc however we need access to that pointer,
         * so use reinterpret_cast to cast away the atomic.
         * This would not become a public interface. */
        template<typename T>
        T* atomic_get_ptr(std::atomic<T>& a) {
            return reinterpret_cast<T*>(&a);
        }


        template<typename T>
        inline T atomic_ll(const T* ptr) {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            T ret;
            if constexpr(sizeof(T) == 4) {
                __asm__ __volatile__("ldaxr    %w0, [%1]          \n" : "=&r"(ret) : "r"(ptr));
            } else {
                __asm__ __volatile__("ldaxr    %0, [%1]          \n" : "=&r"(ret) : "r"(ptr));
            }
            return ret;
        }

        template<typename T>
        inline bool atomic_sc(T* ptr, T newval) {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            int ret;

            if constexpr(sizeof(T) == 4) {
                __asm__ __volatile__("stlxr    %w0, %w2, [%1]     \n"
                                        : "=&r"(ret)
                                        : "r"(ptr), "r"(newval)
                                        : "cc", "memory");
            } else {

                __asm__ __volatile__("stlxr    %w0, %2, [%1]     \n"
                                        : "=&r"(ret)
                                        : "r"(ptr), "r"(newval)
                                        : "cc", "memory");
            }

            return (ret == 0);
        }

    } // namespace detail

    /**
     * Implementation of atomic_mutate using LL/SC.
     */
    template<typename Fn, typename T>
    requires(std::is_invocable_r_v<T, Fn, std::add_rvalue_reference_t<T>>
          || std::is_invocable_r_v<std::optional<T>, Fn, std::add_rvalue_reference_t<T>>)
    inline void atomic_mutate_explicit(std::atomic<T>& a, Fn&& fn,
                                       std::memory_order load_order,
                                       std::memory_order store_order) {
        using R = std::invoke_result_t<Fn, std::add_rvalue_reference_t<T>>;
        T* aptr = detail::atomic_get_ptr(a);
        // TODO: figure out what the memory ordering LL/SC is
        std::atomic_thread_fence(load_order);
        bool success = false;
        do {
            auto val = detail::atomic_ll(aptr);
            if constexpr (std::is_same_v<R, T>) {
                // if the function returns T, we can skip the optional wrapper
                T newval = fn(val);
                std::atomic_thread_fence(store_order);
                success = detail::atomic_sc(aptr, newval);
            } else {
                std::optional<T> opt = fn(val);
                if (!opt) return;
                std::atomic_thread_fence(store_order);
                success = detail::atomic_sc(aptr, opt.value());
            }
        } while (!success);
    }

#endif // USE_LL_SC


    /* explicit memory ordering */
    template<typename Fn, typename T>
    requires(std::is_invocable_r_v<T, Fn, std::add_rvalue_reference_t<T>>
          || std::is_invocable_r_v<std::optional<T>, Fn, std::add_rvalue_reference_t<T>>)
    inline void atomic_mutate_explicit(std::atomic<T>& a, Fn&& fn,
                                       std::memory_order load_order,
                                       std::memory_order store_order);

    /* non-explicit variants */
    template<typename Fn, typename T>
    inline void atomic_mutate(std::atomic<T>& a, Fn&& fn) {
        atomic_mutate_explicit(a, std::forward<Fn>(fn),
                               std::memory_order_seq_cst,
                               std::memory_order_seq_cst);
    }

} // namespace std
