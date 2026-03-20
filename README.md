# C++ API for Mutating an Atomic Variable


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
while (!std::atomic_mutate(a, [](int x){ return (x+1) % THRESHOLD; }))
{ <release CPU> }
```

`std::atomic_mutate` does not return the result of the mutation.
If desired, the result can be extracted through a reference capture
of the provided callable:

```C
std::atomic<int> a = 0;
int v;
while (!std::atomic_mutate(a, [&](int x){ v = (x+1) % THRESHOLD; return v; }))
{ }
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
the compiler could check the provided callable for compliance and
fall back to an implementation using compare-exchange.

The callable may return either a value convertible to the value type
of the `std::atomic` variable or a `std::optional`.
The latter case can be used to provide a value that is stored into the
atomic variable or to abort the transaction by returning an empty
optional.





