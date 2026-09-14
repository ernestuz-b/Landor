# Managed Heap — Design Specification

## 1. Purpose

`ManagedHeap` is a fixed-storage, handle-based, compactable C++ object store intended primarily for embedded systems, while remaining useful on host systems.

The design targets systems where:

- total memory use must be bounded;
- the system allocator is undesirable or unavailable;
- fragmentation matters;
- object addresses may change;
- logical references must remain stable;
- deterministic behaviour is preferred;
- local object access should be essentially as fast as ordinary pointer access.

The application supplies one contiguous region of memory. `ManagedHeap` never grows outside it and does not internally depend on `malloc`, ordinary `new`, or allocating standard-library containers.

---

## 2. Fundamental execution model

A heap has one active execution owner.

Access to a particular `ManagedHeap` is synchronous.

There are no simultaneous operations on the same heap from multiple execution contexts. In particular there are no concurrent:

- allocations;
- releases;
- object accesses through resolved addresses;
- descriptor operations;
- object compaction operations.

The surrounding program may be multithreaded, but one heap must be owned and accessed synchronously.

A threaded application may, for example, give a heap to a dedicated storage/world/network thread and communicate with that owner through messages.

This is fundamental to practical relocation.

Object compaction requires a known point at which no execution path can still be using a physical address into the heap. Arbitrary concurrent access destroys that guarantee unless substantially more machinery such as locks, epochs, pinning, reader tracking, hazard mechanisms, or stop-the-world synchronization is introduced.

Those mechanisms are deliberately outside this design.

No atomic reference counters or internal locks are required.

---

## 3. Stable-address intervals

Objects never move during normal execution.

Relocation occurs only during explicit object-compaction points.

Execution therefore consists of stable-address intervals:

```text
stable-address interval

    allocate
    access
    destroy
    access
    allocate

            |
            v

      OBJECT COMPACTION

            |
            v

stable-address interval
```

During such an interval:

> A resolved object address remains valid until either that object is destroyed or the next object-compaction point occurs.

Ordinary allocation never secretly moves another live object.

Descriptor allocation never secretly moves a live object.

Object destruction never moves another object.

---

## 4. Basic interface

Normal use should be simple:

```cpp
std::byte storage[32768];

auto heap = managed_heap::create(storage);

auto object = heap.make<MyObject>(constructor_args...);
```

Managed references have smart-pointer-like ownership:

```cpp
auto a = heap.make<MyObject>();
auto b = a;                       // retain
```

For high-throughput local access:

```cpp
auto p = a.unwrap();

p->method1();
p->method2();
p->value += 1;
```

Arrays and ordinary objects are supported:

```cpp
auto a = heap.make<std::array<std::int16_t, 5>>();

auto p = a.unwrap();
(*p)[2] = 17;
```

---

## 5. Heap creation

The preferred form is:

```cpp
std::byte storage[32768];

auto heap = managed_heap::create(storage);
```

When the supplied storage is an array, its capacity should be deduced automatically.

For externally supplied storage whose extent cannot be deduced, a form such as:

```cpp
auto heap = managed_heap::create<32768>(location);
```

may be provided.

`location` has placement-like semantics: it identifies the memory region owned by the heap.

The public creation interface should remain simple.

Users should not normally be required to invent:

- numeric heap IDs;
- UUIDs;
- tag classes;
- other heap-identity boilerplate.

---

## 6. Heap domains

A descriptor identity has meaning only inside a particular heap.

Where practical, each independently created heap therefore has a distinct compile-time **heap domain**.

Conceptually:

```cpp
managed_ptr<T, HeapDomain>
```

The heap domain exists in the C++ type system and therefore need not occupy per-handle runtime storage.

In the common embedded case, a persistent managed reference may physically contain only one compact descriptor identifier.

The exact mechanism used to manufacture distinct heap domains is implementation-specific.

Modern C++ may provide convenient compile-time mechanisms.

Older toolchains may use template/preprocessor techniques.

The surface `create(...)` API should remain simple regardless.

---

## 7. Overall memory layout

One supplied memory region contains both metadata and object storage.

Metadata grows upward from `heap_base`.

Objects grow downward from `heap_top`.

```text
high address / heap_top
+-------------------------------------+
| live object reservations            |
| v                                   |
|                                     |
|              free space             |
|                                     |
| ^                                   |
| descriptor table                    |
| descriptor table                    |
| descriptor table                    |
+-------------------------------------+
low address / heap_base
```

Metadata is not permanently reserved for some theoretical maximum number of objects.

It grows as descriptor demand grows.

---

## 8. Descriptor metadata is a stack of tables

Descriptor metadata consists of a contiguous physical stack of equal-sized descriptor tables.

Conceptually:

```text
heap_base

[ descriptor table ]
[ descriptor table ]
[ descriptor table ]
[ descriptor table ]

metadata_top
```

There are no pointers linking these tables.

Their physical arrangement is implicit from:

- `heap_base`;
- descriptor-table size;
- number of physical tables.

Thus physical table `n` is found by arithmetic.

The metadata stack is always physically contiguous.

---

## 9. Descriptor-table contents

Every descriptor table contains:

- a stable logical table ID;
- a fixed number of descriptors;
- descriptor occupancy state;
- reference counts or equivalent per-entry lifetime state.

Conceptually:

```cpp
struct descriptor_table {
    table_id_type id;

    descriptor descriptors[N];
    refcount_type refcounts[N];
    occupancy_type occupancy;
};
```

The exact physical arrangement may be structure-of-arrays or another compact representation.

The important architectural property is:

> Physical table position and logical table identity are separate.

---

## 10. Table identity

Every live descriptor table has a stable logical `table_id`.

Moving a whole descriptor table within the metadata stack does not change its ID.

A handle therefore identifies a descriptor logically as:

```text
(table_id, entry_index)
```

This pair may be packed into a single economical integer representation.

For example, if a table contains a power-of-two number of entries:

```cpp
table_id = handle >> entry_bits;
entry    = handle & entry_mask;
```

The precise encoding is selected by configuration.

---

## 11. Descriptor resolution

To resolve a managed descriptor:

1. extract its `table_id`;
2. extract its entry index;
3. scan the small physical table stack for the matching table ID;
4. index directly into that table.

Conceptually:

```cpp
auto table_id = decode_table(handle);
auto entry    = decode_entry(handle);

for (auto & table : tables) {
    if (table.id == table_id)
        return table.descriptors[entry];
}
```

This lookup is deliberately simple.

No secondary ID-to-location index is required.

The cost is amortized particularly well by `unwrap()`, which resolves an object once and then uses its physical address directly for the rest of the scope.

---

## 12. Descriptor allocation policy

New descriptors preferentially occupy lower physical descriptor tables.

Allocation scans tables from `heap_base` upward and takes the first available descriptor slot.

Within a table, free entries are likewise selected in deterministic order.

A new descriptor table is created only when all existing tables are full.

This creates a persistent occupancy pressure toward low addresses:

```text
heap_base

[ dense/full ]
[ dense/full ]
[ moderately occupied ]
[ sparse ]

metadata_top
```

Normal object churn therefore tends to depopulate tables near `metadata_top`.

This policy is useful but correctness does not depend on workload churn eventually emptying the highest table, because descriptor tables can also be physically compacted.

---

## 13. Creating a descriptor table

If all current descriptor tables are full, the heap attempts to push another equal-sized table onto the metadata stack.

This requires sufficient contiguous free space immediately above `metadata_top`.

If that space does not exist, descriptor expansion fails cleanly.

It does not implicitly move live objects.

The application may later reach an explicit object-compaction point, enlarge the bottom free span, and retry.

---

## 14. Descriptor-table reclamation

When the final live descriptor in a table disappears, that table becomes empty.

Because every descriptor table has exactly the same physical size, reclaiming an empty table is simple.

If the empty table is already physically at the top of the metadata stack:

```text
[A]
[B]
[EMPTY]  <- top
```

it is simply popped.

If the empty table is inside the stack:

```text
[A]
[EMPTY]
[C]
[D]      <- physical top
```

the physical top table is copied/moved into the empty table's location:

```text
[A]
[D]
[C]
```

and the old top is popped.

The moved table retains its logical ID.

Therefore all existing managed handles referring to descriptors in that table remain valid.

No individual descriptor identity changes.

---

## 15. Descriptor-table compaction

Descriptor-table compaction is an evacuation operation over equal-sized blocks:

> When an interior descriptor table becomes empty, move the physical top table into that hole and reduce the metadata stack height by one table.

No table-link updates are needed.

No forwarding descriptors are required.

No reference counts are split.

No handles are rewritten.

No extra identity map exists.

The metadata stack remains contiguous at all times.

This is intentionally much simpler than object compaction.

---

## 16. Descriptor-table movement does not move objects

Moving a descriptor table changes only the location of heap metadata.

It does not change:

- the physical address of any managed object;
- the logical descriptor identity;
- the managed handle;
- the object's reference count.

Therefore descriptor-table compaction does **not** invalidate an already unwrapped object pointer.

For example:

```cpp
auto p = object.unwrap();

// descriptor tables may be reorganized internally here
// without changing p's object address

p->method();
```

remains conceptually safe, provided ordinary heap synchronization rules are respected.

Internal implementation code must not retain raw pointers to descriptors across operations that may rearrange descriptor tables.

---

## 17. Descriptor identity lifetime

A table ID remains stable for the lifetime of that logical descriptor table.

An entry index remains the logical descriptor identity for the lifetime of the object occupying it.

Once a descriptor entry is free, its identity may later be reused.

Likewise, once a descriptor table contains no live descriptors and has been removed, its table ID may eventually be recycled deterministically.

Under correct RAII use, no valid managed handle remains to a descriptor whose slot has been freed.

Generation counters are therefore not required for the fundamental design, though debug implementations may choose to add stale-handle detection.

---

## 18. Object descriptor

Each live object has one descriptor.

Conceptually:

```cpp
struct descriptor {
    offset_type offset;
    size_type   size;
    handle_type next;
};
```

where:

- `offset` identifies the object's physical base;
- `size` is its actual payload/object size;
- `next` identifies the descriptor of the next object lower in memory.

`next` is a logical descriptor identity, not a pointer into a descriptor table.

Therefore moving a whole descriptor table does not disturb the spatial object chain.

The descriptor itself needs no:

- C++ type tag;
- destructor function pointer;
- free-list record;
- physical descriptor-table pointer.

---

## 19. Compact representations

Internal representations should use the smallest practical type capable of representing the configured heap.

Heap-relative addresses use offsets from `heap_base` when this is more economical than native pointers.

Typical possibilities are:

### 32-bit target

```text
8-bit offset
16-bit offset
native pointer
```

### 64-bit target

```text
8-bit offset
16-bit offset
32-bit offset
native pointer
```

If a compact offset provides no useful saving, native pointers are used.

Likewise the implementation should choose economical types for:

- table IDs;
- descriptor handles;
- entry indices;
- reference counts;
- object sizes.

These choices are compile-time decisions.

---

## 20. Object size and reserved extent

The heap distinguishes:

```text
object size
reserved extent
```

The object size is the actual payload size.

For a typed object:

```cpp
sizeof(T)
```

The reserved extent is the physical amount of heap space consumed by that allocation after alignment.

For example:

```text
offset          = 100
object size     = 5
reserved extent = 8

[100,105) object
[105,108) alignment padding
[100,108) reservation
```

Alignment padding belongs to the reservation.

It is not free space.

It is not fragmentation.

All spatial calculations use the reserved extent.

If the reserved extent can be derived from stored object size and the heap's alignment policy, it need not consume another descriptor field.

---

## 21. Alignment

Every managed object is placed at a suitably aligned address.

A simple implementation may define one heap-wide guaranteed alignment.

Conceptually:

```cpp
reserved_size =
    align_up(object_size, heap_alignment);
```

Types requiring alignment unsupported by the configured heap are rejected.

Placement, free-space calculations and object compaction all operate on reserved extents rather than raw object sizes.

---

## 22. Spatial object chain

Live object descriptors form a singly linked spatial chain ordered by physical object position.

The root identifies the highest-address live object.

Each `next` moves toward lower addresses.

For consecutive objects `A` above `B`:

```cpp
A.offset >= B.offset + reserved_size(B);
```

Equality means the reservations touch exactly.

The free hole between them is:

```cpp
gap =
    A.offset -
    (B.offset + reserved_size(B));
```

Therefore:

```text
gap == 0    directly adjacent
gap > 0     free space
```

---

## 23. Free-space accounting

Free-space accounting is deliberately simpler than free-space geometry.

The metadata stack is contiguous, so:

```cpp
metadata_bytes =
    table_count * sizeof(descriptor_table);
```

The total bytes occupied by live objects are:

```cpp
reserved_object_bytes =
    sum(reserved_size(object))
```

Therefore total free space is:

```cpp
free_bytes =
    capacity
    - metadata_bytes
    - reserved_object_bytes;
```

or equivalently:

```cpp
free_bytes =
    (heap_top - metadata_top)
    - reserved_object_bytes;
```

No hole traversal is required to calculate total free memory.

The heap should therefore maintain a running:

```cpp
reserved_object_bytes
```

counter.

Updates are trivial:

```text
allocate object:
    reserved_object_bytes += reserved_size

destroy object:
    reserved_object_bytes -= reserved_size

move object:
    unchanged

push descriptor table:
    table_count += 1

pop descriptor table:
    table_count -= 1

move descriptor table:
    unchanged
```

This allows:

```cpp
free_space()
```

to be O(1).

In debug builds, the value may be independently recomputed from the heap geometry and asserted for consistency.

---

## 24. Free-space geometry

Total free bytes and usable contiguous free space are different concepts.

The important geometric quantities are:

```text
total_free
    all unused bytes

top_span
    free space above the highest live object

bottom_span
    contiguous free space immediately above metadata_top

largest_extent
    largest individual allocatable region

interior_hole_volume
    total free space strictly between live objects
```

For example:

```text
heap_top
[A]
[ 100-byte hole ]
[B]
[ 20-byte hole ]
[C]
[ 200-byte bottom span]
metadata_top
```

gives:

```text
total free      = 320
bottom span     = 200
largest extent  = 200
interior holes  = 120
```

An allocation requiring 250 contiguous bytes therefore fails even though `free_space() == 320`.

Likewise, creating another descriptor table depends on the bottom span, not on total free bytes.

Thus:

> Accounting is arithmetic; geometry is derived.

---

## 25. Free-space geometry is derived

The heap stores no free-list.

It stores no hole records.

All geometric free-space information is derived from:

- `heap_top`;
- `metadata_top`;
- the ordered spatial object chain.

The free regions are:

- the top free span;
- holes between adjacent objects;
- the bottom free span immediately above `metadata_top`.

Removing an object automatically creates a hole.

Removing adjacent objects automatically coalesces their free space.

There is no independent free-space structure that can become inconsistent with the live-object map.

---

## 26. Allocation

Object allocation performs a deterministic traversal of the spatial object chain.

Candidate regions are:

- top span;
- every interior hole;
- bottom span;
- the complete available object region when no objects exist.

Allocation uses best fit:

> Select the smallest candidate capable of holding the required reservation.

Ties are resolved deterministically.

No existing object is relocated during ordinary allocation.

If no suitable contiguous extent exists, allocation fails.

The caller may choose to perform explicit object compaction later and retry.

---

## 27. Typed object construction

The primary creation facility is:

```cpp
template<class T, class... Args>
auto make(Args&&... args);
```

For example:

```cpp
auto object = heap.make<Foo>(a, b, c);
```

Conceptually it:

1. determines object size and required reservation;
2. obtains a descriptor entry;
3. finds a suitable object extent;
4. establishes the descriptor;
5. constructs `T` directly in managed storage;
6. initializes its reference count to one;
7. increments `reserved_object_bytes`;
8. returns a typed managed reference.

Modern implementations may use facilities such as `std::construct_at`.

Older compilers may use equivalent placement construction.

---

## 28. Persistent managed references

Persistent ownership is conceptually:

```cpp
managed_ptr<T, HeapDomain>
```

The managed pointer contains logical descriptor identity rather than physical object address.

It remains valid when:

- the object moves;
- its descriptor table moves;
- surrounding objects are created or destroyed.

### Copy

Copying increments the reference count.

### Move

Moving transfers one owned reference.

### Reset/destruction

Releases one reference.

When the last reference disappears, the object is destroyed.

---

## 29. Final object destruction

When a descriptor's reference count reaches zero:

1. resolve the object's current address;
2. invoke `T`'s destructor exactly once;
3. unlink its descriptor from the spatial object chain;
4. subtract its reserved extent from `reserved_object_bytes`;
5. mark the descriptor entry free;
6. expose the object's reservation as free space;
7. if this made its descriptor table empty, perform descriptor-table reclamation.

Thus descriptor-table cleanup naturally follows ordinary object lifetime events.

Users normally do not explicitly call `free()`.

---

## 30. Fast scoped access: `unwrap()`

Persistent managed references are safe across relocation, but repeated descriptor resolution is undesirable inside hot processing code.

Therefore:

```cpp
auto p = object.unwrap();
```

creates a scope-bound resolved access object.

`unwrap()`:

1. increments the object's reference count;
2. resolves its descriptor;
3. resolves its current physical object address;
4. caches that address;
5. returns a short-lived access object.

Then:

```cpp
p->method1();
p->method2();
p->method3();
```

is effectively ordinary pointer access.

There is no descriptor resolution on each `->`.

The expected cost is approximately:

```text
unwrap:
    one retain
    one descriptor resolution

local work:
    direct pointer access
    direct pointer access
    direct pointer access

scope exit:
    one release
```

---

## 31. Scoped access must remain scoped

The access object returned by `unwrap()` is deliberately not another persistent managed pointer.

Its purpose is temporary local access:

```cpp
{
    auto p = object.unwrap();

    p->foo();
    p->bar();
}
```

It should be:

- non-copyable;
- non-copy-assignable;
- non-move-assignable;
- non-movable where the supported language version permits practical return-by-value implementation.

On older language versions, a restricted move operation may exist only if required to materialize the returned scope object.

Its normal interface consists primarily of:

```cpp
operator->()
operator*()
```

It owns one reference to the underlying object.

---

## 32. Scoped access owns the object

Because `unwrap()` retains the object:

```cpp
auto p = object.unwrap();

object.reset();

p->method();
```

remains valid.

The scoped access keeps the object alive until its own destruction.

At scope exit, it releases its reference.

---

## 33. Unsafe raw-pointer interoperability

Some ordinary C/C++ interfaces require actual pointers.

Examples include:

- datagram parsers;
- serialization code;
- protocol implementations;
- byte-processing libraries;
- legacy APIs.

For this reason raw-pointer extraction is supported, but only from an already-unwrapped scoped access object.

Conceptually:

```cpp
auto p = packet.unwrap();

parse_datagram(p.unsafe_ptr(), length);
```

A persistent `managed_ptr` does not expose a direct raw-pointer operation.

The path is deliberately:

```text
managed_ptr
     |
     | unwrap()
     v
scoped resolved access
     |
     | unsafe_ptr()
     v
raw pointer
```

The additional step is intentional friction.

---

## 34. Raw-pointer contract

A raw pointer obtained through the unsafe escape hatch:

- carries no ownership;
- is valid only while its originating scoped access remains alive;
- must not survive an object-compaction point;
- must not be retained by the callee;
- must not be stored in an object;
- must not be logged for later use;
- must not be queued;
- must not be captured asynchronously;
- must not become persistent state.

Its intended use is immediate synchronous interoperation:

```cpp
auto p = packet.unwrap();

parse_datagram(p.unsafe_ptr(), length);
```

The callee must finish with the pointer before returning.

C++ cannot completely enforce this rule.

The API therefore makes crossing this boundary deliberately conspicuous.

A historical failure mode motivating this restriction is apparently harmless infrastructure, such as a logger, retaining physical object pointers.

---

## 35. Object relocation versus destruction

Moving an object and destroying an object are fundamentally different operations.

During object compaction:

- the object's representation is relocated;
- its descriptor offset changes;
- its logical identity remains unchanged;
- its reference count remains unchanged;
- its destructor is not called.

Destruction occurs only when the final owning reference disappears.

Managed objects may therefore have meaningful non-trivial destructors.

---

## 36. Relocatability

Objects stored in the heap must be safe to relocate as object representations without invoking normal copy/move construction and without destroying the old representation.

This requirement is **relocatability**, not trivial destructibility.

Where the compiler provides a suitable standard relocatability check, the heap should enforce it statically.

Older embedded toolchains may not provide such a facility.

On those implementations:

> Safe relocatability is a programmer-supplied semantic guarantee.

The component remains usable.

Lack of a modern relocation trait should not unnecessarily restrict the heap to trivially destructible objects.

---

## 37. Object-compaction safe point

Object compaction is explicit.

Conceptually:

```cpp
heap.compact();
```

Calling it asserts that:

- all scoped resolved accesses have ended;
- no raw pointer extracted from them remains in use;
- no other execution context is accessing this heap.

Persistent managed references may remain alive.

They continue to identify the same logical objects after relocation.

Descriptor-table compaction is distinct and does not require this object-address safe point because it does not move objects.

---

## 38. Object-compaction goals

Object compaction may pursue several useful goals:

- enlarge the bottom free span next to descriptor metadata;
- reduce interior fragmentation;
- increase the largest contiguous free extent;
- approach complete packing.

Different strategies are useful for different goals.

Three policies are supported conceptually:

```cpp
enum class compaction_policy {
    evacuate,
    shift,
    hybrid
};
```

---

## 39. Evacuation compaction

Evacuation begins with the lowest-address live object: the object closest to `metadata_top`.

For that object:

1. search free extents starting from the top of the heap;
2. find a suitable higher-address extent;
3. move the object there;
4. update its descriptor;
5. allow its old reservation to merge directly into the bottom span;
6. repeat if permitted.

Conceptually:

```text
before:

high
[A]
[hole]
[B]
[hole]
[C]      <- lowest live object
[bottom free span]
[metadata]
low
```

Move `C` into a suitable higher hole:

```text
after:

high
[A]
[C]
[remaining hole]
[B]
[larger bottom free span]
[metadata]
low
```

Every successful evacuation of the current lowest object increases the contiguous space immediately above the metadata frontier.

This makes evacuation particularly useful when another descriptor table must be created.

---

## 40. Shift-close compaction

Shift-close attacks an interior hole by moving a contiguous run of objects across it.

Conceptually:

```text
A | hole | B | C | D
```

becomes:

```text
A | B | C | D | free
```

or its equivalent in the heap's address direction.

Shift-close is especially suitable when the objective is aggressive or complete packing.

Its cost may be higher because eliminating one hole can require moving several objects.

---

## 41. Hybrid object compaction

Evacuation and shift-close complement each other.

An evacuation may create a geometry in which a later shift is inexpensive.

A shift may create a region into which another low object can be evacuated.

Hybrid compaction therefore performs bounded sequences of both operations:

```text
some evacuations
      |
      v
re-evaluate geometry
      |
      v
some shifts
      |
      v
re-evaluate geometry
      |
      v
repeat while useful
and budget remains
```

The exact alternation is policy, not architecture.

---

## 42. Compaction configuration

Compaction has sensible compile-time defaults but is also tunable at runtime.

Conceptually:

```cpp
struct compaction_config {
    compaction_policy policy;

    std::size_t move_budget_bytes;
    std::size_t evacuation_attempts;
    std::size_t shift_attempts;
    std::size_t fragmentation_threshold;
};
```

For example:

```cpp
heap.compaction().policy =
    compaction_policy::hybrid;

heap.compaction().move_budget_bytes = 4096;
heap.compaction().evacuation_attempts = 4;
heap.compaction().shift_attempts = 2;
```

Exact API spelling remains open.

The important point is that policy changes do not alter heap layout or descriptor representation.

---

## 43. Search is cheap; movement is expensive

The compaction algorithms deliberately prefer additional descriptor-chain searching over unnecessary memory movement.

The principal bounded cost is therefore:

```text
bytes moved
```

not:

```text
descriptors inspected
```

A compactor may walk the object chain repeatedly while choosing a worthwhile move.

A separate search budget is unnecessary unless profiling later demonstrates a need.

---

## 44. Movement budget

Object compaction respects a byte-movement budget.

The cost of moving an object or contiguous run is the amount of data actually relocated.

If no useful move fits inside the remaining budget, the pass stops.

This gives the application deterministic control over compaction cost.

---

## 45. Progress criteria

Different object-compaction strategies naturally have different progress measures.

### Evacuation

A successful evacuation must increase the bottom contiguous free span.

### Shift-close

A successful shift must reduce interior fragmentation or move the heap toward a more packed arrangement.

### Hybrid

A successful hybrid pass must improve at least one useful geometry measure, such as:

- bottom-span size;
- interior-hole volume;
- largest contiguous extent.

The compactor must not repeatedly move objects while merely moving fragmentation around without useful progress.

---

## 46. Descriptor growth and object compaction

It is possible to have:

```text
enough total free bytes
but
not enough contiguous bottom space
to push another descriptor table
```

Creating the descriptor table does not implicitly relocate objects.

Instead descriptor growth fails cleanly.

At a later safe point the caller may choose evacuation or hybrid object compaction, enlarge the bottom span, and retry.

This keeps address movement explicit and predictable.

---

## 47. Determinism

Given identical:

- heap capacity;
- configuration;
- allocation sequence;
- reference-release sequence;
- descriptor-table events;
- object-compaction points;
- object-compaction parameters;

the resulting layout must be deterministic.

Runtime policy values are ordinary deterministic inputs.

The heap does not base layout decisions on:

- wall-clock time;
- randomness;
- system allocator state;
- nondeterministic container iteration.

---

## 48. Debug invariants

Debug builds should validate at least:

1. the metadata stack begins at `heap_base`;
2. all descriptor tables are contiguous and equal-sized;
3. `metadata_top` equals the end of the physical table stack;
4. every physical descriptor table has a valid logical table ID;
5. no two live tables share an ID;
6. every live handle resolves to exactly one table ID and valid occupied entry;
7. object reservations lie entirely above `metadata_top`;
8. object reservations do not exceed `heap_top`;
9. object reservations never overlap;
10. object bases obey alignment requirements;
11. adjacent reservations may have exactly zero gap;
12. every spatial-chain link resolves to a live descriptor;
13. every live descriptor has a nonzero reference count;
14. `reserved_object_bytes` equals the independently derived sum of live reserved extents;
15. `free_space()` equals `capacity - metadata_bytes - reserved_object_bytes`;
16. derived free regions agree with the spatial object chain;
17. metadata + object reservations + free space equal total heap capacity;
18. descriptor-table evacuation preserves the moved table's logical ID;
19. object compaction occurs only when no tracked scoped object access remains active.

Optional debugging may additionally:

- poison vacated object storage;
- poison removed descriptor tables;
- track compaction generations;
- instrument raw-pointer extraction;
- assert absence of active unwrapped access at object-compaction points.

---

## 49. Language-version policy

The implementation should exploit modern C++ where useful for:

- type safety;
- compile-time configuration;
- construction;
- relocatability checks;
- heap-domain generation;
- RAII ergonomics.

However, embedded compiler ecosystems often lag current language standards.

Therefore:

- newer compilers should provide stronger checking;
- older compilers should degrade gracefully;
- unavailable static guarantees become documented programmer obligations;
- the fundamental memory and compaction algorithms remain usable.

Modern C++ improves the implementation; it does not redefine the underlying design.

---

## 50. Deliberately open details

The architecture is substantially settled.

Local implementation decisions still include:

- number of descriptors per table;
- precise descriptor-table physical layout;
- table-ID width and recycling policy;
- descriptor-handle packing;
- exact compile-time heap-domain mechanism;
- reference-count width;
- exact heap alignment policy;
- final name of the unsafe raw-pointer escape hatch;
- default compaction policy;
- default evacuation/shift attempt counts;
- exact candidate-selection heuristics;
- optional debug instrumentation;
- optional future profiling-driven lookup accelerators.

These should not require changing the fundamental model.

---

## 51. Core model

`ManagedHeap` owns one fixed contiguous memory region under synchronous execution ownership.

Metadata is a dense physical stack of equal-sized descriptor tables growing upward from `heap_base`.

Each table carries a stable logical ID. Managed handles encode that table ID plus an entry index, so whole descriptor tables may move physically without invalidating handles.

Descriptor allocation prefers lower physical tables. When a table becomes empty, the current top table can be moved directly into its place and the metadata stack popped, making descriptor metadata self-compacting without pointers, forwarding records, split identities, or an auxiliary mapping structure.

Object reservations grow downward from `heap_top`.

Persistent managed references identify descriptors rather than physical object addresses.

`unwrap()` converts a persistent handle into a scope-bound, reference-owning resolved access object, resolving the object once and then providing ordinary pointer-speed access.

An explicit unsafe raw-pointer escape exists only after `unwrap()` for synchronous interoperability with APIs that genuinely require raw pointers.

Total free memory is calculated in O(1) from total capacity, current metadata consumption, and a running count of reserved object bytes. The spatial chain is walked only for geometric questions such as best-fit placement, largest contiguous extent, bottom span, or interior fragmentation.

Object addresses remain stable between explicit object-compaction points.

Object compaction supports:

- **evacuation**, moving the lowest object into a higher hole to grow the metadata-side free span;
- **shift-close**, moving contiguous runs to eliminate interior holes;
- **hybrid**, combining both strategies under a configurable movement budget.

Compaction configuration has compile-time defaults and may be tuned at runtime.

The central design principle is:

> **Logical identity remains stable while physical placement is allowed to change, but the implementation pays for indirection only where that indirection buys something useful.**
