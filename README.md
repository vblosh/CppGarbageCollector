# CppGC

CppGC is a header-only, non-moving mark-and-sweep garbage collector for C++17.
Each collector owns a set of explicitly allocated objects and runs on the thread
that created it. Collection is manual by default, with an optional object-count
threshold that triggers collection before later allocations.

CppGC is an explicit managed-object system, not a conservative C++ heap scanner.
It does not inspect the native stack or automatically discover ordinary pointers.

## Requirements

- C++17 or newer
- CMake 3.14 or newer for the included build
- GoogleTest to build the unit tests

## Basic usage

Define a managed type by deriving from `cppgc::GCObject`. Use
`cppgc::GCMember<T>` for references between managed objects and enumerate those
members in a `trace()` override:

```cpp
#include "GarbageCollector.h"

class Node : public cppgc::GCObject
{
public:
    explicit Node(int value)
        : value(value)
    {}

    void trace(cppgc::TraceVisitor& visitor) const override
    {
        visitor.visit(next);
    }

    cppgc::GCMember<Node> next;
    int value;
};
```

`GCMember<T>` contains only its target pointer. Assignments are therefore plain
pointer stores. The collector validates managed targets when an object is created
and whenever its edges are traced.

If a base class declares managed edges, derived `trace()` overrides must call
`Base::trace(visitor)` before visiting their own members.

Create objects through `GarbageCollector::createInstance()` and keep externally
reachable objects in `GCObjectRootPtr<T>` roots:

```cpp
cppgc::GarbageCollector gc;
cppgc::GCObjectRootPtr<Node> root(gc);

root = gc.createInstance<Node>(1);
root->next = gc.createInstance<Node>(2);

gc.collect();       // both nodes survive
root.reset();
gc.collect();       // both nodes are reclaimed
```

An ordinary local pointer returned by `createInstance()` is not a root:

```cpp
Node* pointer = gc.createInstance<Node>(1);
gc.collect(); // pointer is now dangling unless another root reaches the node
```

## How collection works

The collector maintains an ownership set containing every object created by
that collector.

When `collect()` is called:

1. Every registered root is placed in an iterative work list.
2. Reachable objects are marked with the current collection epoch.
3. Each object's `trace()` override visits its managed and legacy raw edges.
4. Objects not marked in the current epoch are removed from the ownership set.
5. Unreachable objects are destroyed.

The traversal uses an explicit work list rather than recursive function calls,
so graph depth does not consume the native call stack. Epoch marking avoids a
full pass that resets Boolean mark flags and allows a later collection to recover
cleanly if tracing throws an exception.

Because reachability is traced from roots, unreachable cycles are reclaimed:

```text
root -> A -> B       survives

       A <-> B       reclaimed when no root reaches A or B
```

CppGC is non-moving: surviving object addresses do not change during collection.

## Roots

`GCObjectRootPtr<T>` registers itself when constructed and unregisters itself
when destroyed. Copying a root creates another registered root. `reset()` or
assignment from `nullptr` keeps the root registered but clears its object.

A root accepts only objects owned by its collector. Assigning an object from a
different collector throws `std::invalid_argument`.

A root may outlive its collector. Collector destruction detaches and clears all
registered roots before deleting its remaining objects, so later root destruction
does not access a dead collector.

`GarbageCollector` itself is neither copyable nor movable.

## Weak pointers

`GCObjectWeakPtr<T>` is a non-owning reference to a managed object. Unlike
`GCObjectRootPtr<T>`, it does not keep its target alive. The collector clears the
weak pointer when the target is reclaimed:

```cpp
cppgc::GarbageCollector gc;
cppgc::GCObjectRootPtr<Node> root(gc);
root = gc.createInstance<Node>(1);

cppgc::GCObjectWeakPtr<Node> weak(root);

gc.collect();              // root keeps the node alive
auto locked = weak.lock(); // promotes the live target to a registered root

root.reset();
locked.reset();
gc.collect();              // the node is reclaimed and weak is cleared

assert(weak.expired());
assert(weak.get() == nullptr);
```

A weak pointer can be constructed with a collector and later assigned a managed
pointer or root:

```cpp
cppgc::GCObjectWeakPtr<Node> weak(gc);
weak = gc.createInstance<Node>(1);
```

Use `lock()` when the object must remain alive while it is used. It returns an
empty `GCObjectRootPtr<T>` if the weak pointer has expired. `get()` only observes
the current target; the returned raw pointer is not a root and may become
dangling after a later collection.

`empty()` and `expired()` both report whether the target has been cleared, and
`reset()` clears it explicitly. Copying a weak pointer creates an independently
registered weak pointer to the same target.

Weak pointers, roots, and their targets must belong to the same collector.
Assigning a target or weak pointer from another collector throws
`std::invalid_argument`. A weak pointer may outlive its collector: collector
destruction detaches and clears it, after which `lock()` throws
`std::logic_error`.

When `GCObjectWeakPtr<T>` is stored as a field of a managed object, initialize it
with that object's collector. Do not pass weak fields to `TraceVisitor`; weak
references intentionally do not make their targets reachable.

## Managed members

`GCMember<T>` supports pointer assignment, `get()`, `operator->`, Boolean tests,
and `reset()`. It is a trivially copyable, pointer-sized value.

Each `GCMember` stores only its target pointer (8 bytes on 64-bit builds). There
is no intrusive per-object member registry; tracing metadata is declared once
per class in `trace()`.

Because a target-only member does not know its containing object, assigning a
foreign or stale target does not throw immediately. The next `collect()` detects
the invalid managed edge before dereferencing it, throws `std::invalid_argument`,
and leaves the collector usable after the edge is reset.

Raw pointer members remain supported for compatibility:

```cpp
class LegacyNode : public cppgc::GCObject
{
public:
    LegacyNode* next = nullptr;

    void trace(cppgc::TraceVisitor& visitor) const override
    {
        visitor.visitRaw(next);
    }
};
```

Raw members cannot reject cross-collector assignments automatically. They must
be passed explicitly to `visitor.visitRaw()`. During tracing, foreign
or stale raw edges are ignored because membership is checked before the target
is dereferenced. Prefer `GCMember<T>` for new code.

If both a base and derived class override `trace()`, the derived override must
call `Base::trace(visitor)` to preserve all base-class edges.

Edges assigned inside an object's constructor are validated after the object is
registered. If any initial edge points outside the collector, creation rolls back
and throws `std::invalid_argument`.

## Pointer-free and inherited types

When a class has no managed pointers, simply derive from `GCObject`. No tracing
override is needed:

```cpp
class ValueObject : public cppgc::GCObject
{};
```

A derived class can add `GCMember` fields normally and visit them in its
`trace()` override. Call the base implementation only when the base also traces
edges. Use standard C++ RTTI (`dynamic_cast` or `typeid`) if runtime type queries
are needed.

## Automatic collection threshold

Passing a positive threshold sets the minimum automatic-collection threshold:

```cpp
cppgc::GarbageCollector gc(10000);
```

The collector keeps two values:

- The configured minimum threshold, returned by `get_collection_threshold()`.
- The adaptive next threshold, returned by `get_next_collection_threshold()`.

Before creating a new object, `createInstance()` calls `collect()` when the
current object count is greater than or equal to the adaptive next threshold.
The new object is allocated after that collection, so it cannot be reclaimed
during its own creation.

After every successful collection, the next threshold is recomputed from the
number of surviving objects. It grows by 50% of the live count or at least 1,024
objects, whichever is greater, and never falls below the configured minimum.
The calculation saturates at `std::numeric_limits<size_t>::max()` rather than
overflowing. This prevents a mostly-live heap from collecting again before every
subsequent allocation.

Conceptually, the calculation is:

```text
growth = max(live_objects / 2, 1024)
next_threshold = max(configured_minimum, saturating_add(live_objects, growth))
```

A threshold of `0` disables automatic collection.

The threshold can be changed while the collector is idle:

```cpp
gc.set_collection_threshold(20000);
```

Changing the threshold resets both the configured minimum and the next trigger
to the supplied value. The next successful collection makes the trigger adaptive
again.

Automatic collection does not make local raw pointers into roots. Any object
that must survive a threshold-triggered collection must already be reachable
from a registered root.

## Runtime contract and exceptions

- Create, use, collect, and destroy a collector and its roots on the collector's
  owner thread. Cross-thread collector operations throw `std::logic_error` where
  an exception can be reported safely.
- Do not call `collect()`, allocate objects, or register new roots recursively
  while collection is active. Unsupported reentrant operations are rejected.
- Managed destructors must be non-throwing. This is enforced by
  `createInstance()` at compile time.
- Sweep order is unspecified. Managed destructors must not dereference other
  managed objects.
- Raw pointers and `GCMember` values become invalid when their target is
  collected. Use `GCObjectWeakPtr<T>` when a non-owning reference must be cleared
  automatically.
- The collector resets its internal state if tracing throws, and a later
  collection can be attempted again.

## Limitations

- No conservative stack scanning: reachability comes only from registered roots,
  and edges declared through `trace()`.
- No moving or compacting collection.
- No concurrent or parallel collection.
- No finalizer API.
- No automatic byte-based heap limit; the optional threshold counts objects.
- The ownership registry adds per-object storage and lookup overhead.
- Manually deleting an object returned by `createInstance()` is invalid and can
  cause a later double deletion.

## Build and test

The repository contains a `vcpkg.json` manifest for GoogleTest. Enable manifest
mode by passing the vcpkg toolchain during the first CMake configuration. For
the default Windows installation used by this project:

```powershell
$env:VCPKG_ROOT = "C:\src\vcpkg"

cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build
ctest --test-dir build --output-on-failure
```

The toolchain reads the manifest and installs dependencies into the build tree.
Use a fresh build directory when enabling or changing the toolchain because
CMake caches toolchain selection.

Enable AddressSanitizer and UndefinedBehaviorSanitizer with GCC or Clang:

```sh
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCPPGC_ENABLE_SANITIZERS=ON
```

The performance test (a heavier benchmark-style binary) is built by default.
To skip it (e.g. in CI or for faster configuration):

```sh
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCPPGC_BUILD_PERF_TESTS=OFF
```

The repository's CI builds and tests with both GCC and Clang sanitizers. The
unit suite covers cycles, deep graphs, root and collector lifetimes,
cross-collector references, weak pointers, reentrancy, thread affinity,
thresholds, exception recovery, inherited managed members, and randomized
graphs.
