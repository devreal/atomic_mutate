# C++ API for Mutating an Atomic Variable

## Motivation

C++ std::atomic (as of C++23) provides `fetch_op` and `compare_exchange` (CAS), where `op` is either `add`, `sub`, `and`, `or`, `xor`, `min`, or `max`. While this covers many of the operations widely used in applications, it still represents a limited set of operations.
Advanced operations (custom screening, conditional operations) are typically implemented using `compare_exchange`.
`compare_exchange` maps well to architectures that provide CAS instructions (x86).
However, on other architectures `compare_exchange` is emulated using specific load-store instructions, e.g., using a linked-load (LL) of the original value and performing a store-conditional (SC) of the result.
Moreover, newer instructions such as `CMPccXADD` (and related) [4] implement semaphore-style atomic operations, i.e., using a predicate to guard an atomic operation.

For example, consider code to conditionally add a reference to a counter if the counter is not zero:

```c++
std::atomic<int> a;
int v, n;
v = a.load();
do {
  if (v > 0) n = v+1;
  else break;
} while (!a.compare_exchange(v, n));
```

It is clear that the `compare_exchange` is *not* what the user intends to perform. It is merely a too to implement the intended update. In fact, the *intent* of the user is to perform an atomic mutation of the value, given a set of operations that perform the mutation. This proposal introduces a mechanism that allows the application to focus on the mutating operation:


```c++
std::atomic<int> a;
std::atomic_mutate(a,
    [&](int v) -> std::optional<int> {
        if (a > 0) return v+1;
        else return {};
    });
```

Incidentally, this pattern can be implemented using the x86 `_cmpccxadd` instruction.

## `std::atomic_mutate`

`std::atomic_mutate` takes a `std::atomic` variable
and a callable that accepts an rvalue of the atomic value type
and returns a value to store into the atomic variable.
The sequence of load-mutate-store must happen atomically
and may fail if another thread modifies the atomic variable
concurrently. The atomic_mutate may fail spuriosly.

`std::atomic_mutate` returns true if the operation succeeded and
false otherwise. In order to mutate a value unconditionally
the call to std::atomic_mutate should be repeated until
succesful. For example, the following use atomically increments
an integer and resets it to zero if it exceeds a threshold:

```C
std::atomic<int> a = 0;
std::atomic_mutate(a, [](int x){ return (x+1) % THRESHOLD; })
```

`std::atomic_mutate` does not return the result of the mutation.
If desired, the result can be extracted through a reference capture
of the provided callable:

```C
std::atomic<int> a = 0;
int v;
std::atomic_mutate(a, [&](int x){ v = (x+1) % THRESHOLD; return v; });
return v;
```

`std::atomic_mutate` may be implemented using either compare-exchange
or linked-load/store-conditional (LL/SC), enabling the use of LL/SC
without inline assembly on systems that support this or similar
instructions (arm, RISC-V, PowerPC).

The implementation of `std::atomic_mutate` using LL/SC is currently
enabled on arm64 and can be disabled by defining `ATOMIC_MUTATE_FORCE_CAS`.

### Memory Model

`std::atomic_mutate` defaults to sequential consistency for both the
load and the subsequent store. This can be changed by using the
`std::atomic_mutate_explicit` which takes two additional arguments
for the load and store memory synchronization.

For example, the following code performs the load with relaxed memory synchronization while performing a release when storing the result:

```C
std::atomic<int> a = 0;
int v;
while (!std::atomic_mutate_explicit(a, [&](int x){ v = (x+1) % THRESHOLD; return v; },
                                    std::memory_order_relaxed,
                                    std::memory_order_release))
{ }
return v;
```

### Callable

This prototype currently does not check for the restrictions that
apply to the use of LL/SC (e.g., prohibition of indirect branches
and other LL/SC instruction between the linked-load and store-conditional)
so care must be taken to adhere to these constraints. In the future,
the compiler should check the provided callable for compliance and
fall back to an implementation using compare-exchange.

Applications should not rely on side-effects caused by the callable, i.e.,
any modifications other than the one produced by the store of the result value.

The callable may return either a value convertible to the value type
of the `std::atomic` variable or a `std::optional`.
The latter case can be used to either provide a value to be stored into the
atomic variable or to abort the operation by returning an empty
optional.

## Evaluation

Below are performance numbers comparing the implementation of `atomic_mutate` implemented using CAS (similar to what applications would write today) and the implementation using LL/SC, on an Apple M3.

Using Clang-17:
```
➜  build git:(main) ✗ OMP_NUM_THREADS=4 ./arithmetic 100000001
test_inc_mod<i, 0> found 1 ; total 355150 [us] ; avg 0.0035515 [us]
test_inc_mod<x, 0> found 1 ; total 320940 [us] ; avg 0.0032094 [us]
test_inc_mod<f, 0> found 1 ; total 493764 [us] ; avg 0.00493764 [us]
test_inc_mod<d, 0> found 1 ; total 494753 [us] ; avg 0.00494753 [us]
test_inc_mod<i, 1> found 1 ; total 329356 [us] ; avg 0.00329356 [us]
test_inc_mod<x, 1> found 1 ; total 321244 [us] ; avg 0.00321244 [us]
test_inc_mod<f, 1> found 1 ; total 495530 [us] ; avg 0.0049553 [us]
test_inc_mod<d, 1> found 1 ; total 493675 [us] ; avg 0.00493675 [us]
➜  build git:(main) ✗ OMP_NUM_THREADS=4 ./arithmetic_cas 100000001
test_inc_mod<i, 0> found 1 ; total 282602 [us] ; avg 0.00282602 [us]
test_inc_mod<x, 0> found 1 ; total 251308 [us] ; avg 0.00251308 [us]
test_inc_mod<f, 0> found 1 ; total 497347 [us] ; avg 0.00497347 [us]
test_inc_mod<d, 0> found 1 ; total 497197 [us] ; avg 0.00497197 [us]
test_inc_mod<i, 1> found 1 ; total 250600 [us] ; avg 0.002506 [us]
test_inc_mod<x, 1> found 1 ; total 255571 [us] ; avg 0.00255571 [us]
test_inc_mod<f, 1> found 1 ; total 493935 [us] ; avg 0.00493935 [us]
test_inc_mod<d, 1> found 1 ; total 495301 [us] ; avg 0.00495301 [us]
```

The Clang compiler appears to elide the `std::optional` construction and uses a builtin for the CAS that maps to a native CAS instruction.

Using GCC-15:

```
➜  build git:(main) ✗ OMP_NUM_THREADS=4 ./arithmetic 100000001
test_inc_mod<i, 0> found 1 ; total 943217 [us] ; avg 0.00943217 [us]
test_inc_mod<x, 0> found 1 ; total 928642 [us] ; avg 0.00928642 [us]
test_inc_mod<f, 0> found 1 ; total 1.46788e+06 [us] ; avg 0.0146788 [us]
test_inc_mod<d, 0> found 1 ; total 1.33285e+06 [us] ; avg 0.0133285 [us]
test_inc_mod<i, 1> found 1 ; total 1.51832e+06 [us] ; avg 0.0151832 [us]
test_inc_mod<x, 1> found 1 ; total 1.5321e+06 [us] ; avg 0.015321 [us]
test_inc_mod<f, 1> found 1 ; total 1.83134e+06 [us] ; avg 0.0183134 [us]
test_inc_mod<d, 1> found 1 ; total 1.90179e+06 [us] ; avg 0.0190179 [us]
➜  build git:(main) ✗ OMP_NUM_THREADS=4 ./arithmetic_cas 100000001
test_inc_mod<i, 0> found 1 ; total 909594 [us] ; avg 0.00909594 [us]
test_inc_mod<x, 0> found 1 ; total 926245 [us] ; avg 0.00926245 [us]
test_inc_mod<f, 0> found 1 ; total 1.93353e+06 [us] ; avg 0.0193353 [us]
test_inc_mod<d, 0> found 1 ; total 1.49278e+06 [us] ; avg 0.0149278 [us]
test_inc_mod<i, 1> found 1 ; total 1.72186e+06 [us] ; avg 0.0172186 [us]
test_inc_mod<x, 1> found 1 ; total 1.67829e+06 [us] ; avg 0.0167829 [us]
test_inc_mod<f, 1> found 1 ; total 2.32625e+06 [us] ; avg 0.0232625 [us]
test_inc_mod<d, 1> found 1 ; total 2.16745e+06 [us] ; avg 0.0216745 [us]
```

With GCC, the results indicate

All code is available in this repository.


## Related Work

Thiago Macieira proposed a similar, albeit more restrictive, API for inclusion into Qt[1]. The main difference is that the set operations is restricted to a comparison, binary, and arithmetic operations.
The stated goal was to utilize advanced x86 instructions such as `_cmpccxadd_epi64`, which are available as intrinsic operations.
The PoC implementation here does not utilize these instructions but the proposed API might make it easier to compilers to identify patterns in the callable that match these instructions.

P3111 proposes `std::atomic::reduce`, which provides an atomic update without `fetch`, i.e., performing only the atomic update without returning the prior value. This enables optimizations such as vectorization and reordering. The API proposed here is orthogonal to P3111 and could be added here as well (e.g., `std::atomic::reduce_update`).

Rust provides `std::sync::Atomic*::fetch_update` with similar semantics as proposed here [3].

A similar proposal was discussed on the std-discussion mailing list [5].


And there's P2066  proposes an interface for using transactional memory, on systems that
support it. [6]

Jesus and Weiland's study suggests that the new arm atomic intrinsics do always provide performance benefits over LL/SC and may in some circumstances inhibit scaling [7].

P3330 is a proposal similar to the one here. It's status is unclear at the moment [8].


[1] https://codereview.qt-project.org/c/qt/qtbase/+/489704
[2] https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3111r2.html
[3] https://doc.rust-lang.org/std/sync/atomic/struct.AtomicUsize.html#method.fetch_update
[4] https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html
[5] https://lists.isocpp.org/std-discussion/2024/05/2541.php
[6] https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p2066r5.html
[7] Ricardo Jesus and Michèle Weiland. 2023. A Study on the Performance Implications of AArch64 Atomics. In High Performance Computing: 38th International Conference, ISC High Performance 2023, Hamburg, Germany, May 21–25, 2023, Proceedings. Springer-Verlag, Berlin, Heidelberg, 279–296. https://doi.org/10.1007/978-3-031-32041-5_15
[8] https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3330r0.html