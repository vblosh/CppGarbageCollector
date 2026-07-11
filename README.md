# CppGC

CppGC is a header-only, single-thread-owned mark-and-sweep collector for C++17.
Collection is explicit by default and can optionally run before allocations at
an object-count threshold.

## Managed objects

Derive managed types from `cppgc::GCObject`. Use `cppgc::GCMember<T>` for
managed edges and pass the containing object to each member:

```cpp
class Node : public cppgc::GCObject
{
    DECLARE_GCOBJECT_CLASS(Node)

public:
    Node() : next(this) {}

    cppgc::GCMember<Node> next;
};

GCOBJECT_POINTER_MAP_BEGIN(Node)
GCPOINTER(Node, next)
GCOBJECT_POINTER_MAP_END(Node)
```

`GCMember<T>` rejects edges to objects owned by another collector. Raw pointer
members remain supported for compatibility, but they cannot validate assignment.
Every managed edge must appear in the pointer map.

## Allocation and roots

```cpp
cppgc::GarbageCollector gc;
cppgc::GCObjectRootPtr<Node> root(gc);
root = gc.createInstance<Node>();
root->next = gc.createInstance<Node>();

gc.collect();
root.reset();
gc.collect();
```

Roots register themselves with their collector. A root may outlive its collector;
it is detached and reset when the collector is destroyed.

Pass a positive threshold to enable collection before allocation:

```cpp
cppgc::GarbageCollector gc(10000);
```

## Runtime contract

- Use, destroy, and manage roots from the thread that created the collector.
- Do not call `collect()` recursively. Reentrant collection is rejected.
- Managed destructor order is unspecified. Destructors must not dereference
  other managed objects, and managed destructors must be non-throwing.
- Raw pointers returned by `createInstance()` become invalid when their object
  is collected.
