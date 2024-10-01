#pragma once

#include <atomic>
#include <optional>
#include <cstdint>

/* enable LL/SC on arm64 if the user didn't force CAS */
#if !defined(USE_LL_SC) && defined(__aarch64__) && !defined(ATOMIC_MUTATE_FORCE_CAS)
#define USE_LL_SC 1
#endif

namespace std {

#ifndef USE_LL_SC

    /**
     * Implementation of atomic_mutate using compare-exchange.
     */
    template<typename Fn, typename T>
    requires(std::is_invocable_v<Fn, std::add_rvalue_reference_t<T>>)
    inline bool atomic_mutate_explicit(std::atomic<T>& a, Fn&& fn,
                                       std::memory_order load_order,
                                       std::memory_order store_order) {
        auto val    = a.load(load_order);
        std::optional<T> opt = fn(val);
        if (!opt) return false;
        return std::atomic_compare_exchange_weak_explicit(&a, &val, opt.value(),
                                                        std::memory_order_relaxed,
                                                        store_order);
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
                                        : "r"(ptr), "r"(&newval)
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
     * Implementation of atomic_mutate using compare-exchange.
     */
    template<typename Fn, typename T>
    requires(std::is_invocable_v<Fn, std::add_rvalue_reference_t<T>>)
    inline bool atomic_mutate_explicit(std::atomic<T>& a, Fn&& fn,
                                       std::memory_order load_order,
                                       std::memory_order store_order) {
        T* aptr = detail::atomic_get_ptr(a);
        std::atomic_thread_fence(load_order);
        auto val = detail::atomic_ll(aptr);
        std::optional<T> opt = fn(val);
        if (!opt) return false;
        std::atomic_thread_fence(store_order);
        return detail::atomic_sc(aptr, opt.value());
    }

#endif // USE_LL_SC

    // fwd-decl
    template<typename T>
    struct atomic_safe_ptr;

    // fwd-decl
    template<typename Fn, typename T>
    inline bool atomic_mutate_explicit(atomic_safe_ptr<T>& a, Fn&& fn,
                                       std::memory_order load_order,
                                       std::memory_order store_order);

    /**
     * Implementation of std::atomic_safe_ptr
     * Wraps a pointer and a value to handle the ABA
     * problem in compare-exchange.
     *
     * TODO: should atomic_safe_ptr be owning?
     */
    template<typename T>
    struct atomic_safe_ptr {
    private:
        union data {
            struct { // anonymous struct
                T* ptr = nullptr;
                intptr_t __fluff = 0;
            };
            // TODO: handle 32bit archs
            // atomic access to the ptr+fluff
            __int128_t __aba;
            data() = default;

            data(const __int128_t& aba)
            : __aba(aba)
            { }

            data(T* ptr)
            : ptr(ptr)
            , __fluff(0)
            { }
        };
        std::atomic<data> u;

        template<typename Fn, typename S>
        friend bool atomic_mutate_explicit(atomic_safe_ptr<S>&, Fn&&,
                                        std::memory_order,
                                        std::memory_order);

    public:
        atomic_safe_ptr() = default;

        atomic_safe_ptr(const __int128_t& val)
        : u(data{val})
        { }

        atomic_safe_ptr(T* ptr)
        : u(ptr)
        { }

        atomic_safe_ptr update(T* ptr) {
            data d = u.load(std::memory_order_relaxed);
            d.ptr = ptr;
            return {d.__aba};
        }

        T* get(std::memory_order load_order = std::memory_order_seq_cst) const {
            return u.load(load_order).ptr;
        }
    };

#ifndef USE_LL_SC

    /**
     * Implementation of atomic_mutate using compare-exchange.
     */
    template<typename Fn, typename T>
    inline bool atomic_mutate_explicit(atomic_safe_ptr<T>& a, Fn&& fn,
                                       std::memory_order load_order,
                                       std::memory_order store_order) {
        typename atomic_safe_ptr<T>::data val = a.u.load(load_order);
        std::optional<T*> opt   = fn(val.ptr);
        if (!opt) return false;
        __int128_t newval = a.update(opt.value()).u.load(std::memory_order_relaxed).__aba;
        return std::atomic_compare_exchange_weak_explicit(&a.u, &val, newval,
                                                    std::memory_order_relaxed,
                                                    store_order);
    }

#else

    /**
     * Implementation of atomic_mutate using compare-exchange.
     */
    template<typename Fn, typename T>
    inline bool atomic_mutate_explicit(atomic_safe_ptr<T>& a, Fn&& fn,
                                       std::memory_order load_order,
                                       std::memory_order store_order) {
        // no need to care about the ABA problem when using LLSC
        std::atomic_thread_fence(load_order);
        auto val = detail::atomic_ll(&detail::atomic_get_ptr(a.u)->ptr);
        std::optional<T*> opt = fn(val);
        if (!opt) return false;
        std::atomic_thread_fence(store_order);
        return detail::atomic_sc(&detail::atomic_get_ptr(a.u)->ptr, opt.value());
    }

#endif

    /* non-explicit variants */
    template<typename Fn, typename T>
    inline bool atomic_mutate(std::atomic<T>& a, Fn&& fn) {
        return atomic_mutate_explicit(a, std::forward<Fn>(fn),
                                      std::memory_order_seq_cst,
                                      std::memory_order_seq_cst);
    }

    /* non-explicit variants */
    template<typename Fn, typename T>
    inline bool atomic_mutate(std::atomic_safe_ptr<T>& a, Fn&& fn) {
        return atomic_mutate_explicit(a, std::forward<Fn>(fn),
                                      std::memory_order_seq_cst,
                                      std::memory_order_seq_cst);
    }

} // namespace std
