# Containers

ZEngine provides a small set of arena-backed containers. None of them call `malloc`/`new` — all memory comes from an explicit `ArenaAllocator*` passed at construction time. There are no default constructors that allocate and no implicit copies.

See also: [Memory Management](memory-management.md) · [Engine Architecture](engine-architecture.md)

---

## Table of Contents

- [Overview](#overview)
- [Array](#arrayt)
- [UnorderedHashMap](#unorderedhashmapk-v)
- [HashMap (ordered)](#hashmapk-v)
- [UnorderedHashSet / HashSet](#unorderedhashset-hashset)
- [String / StringView](#string-stringview)
- [Common Pitfalls](#common-pitfalls)

---

## Overview

| Container | Header | Purpose |
|---|---|---|
| `Array<T>` | `Core/Containers/Array.h` | Contiguous growable buffer; move-only |
| `UnorderedHashMap<K, V>` | `Core/Containers/UnorderedHashMap.h` | Open-addressing hash map; no ordering guarantee |
| `HashMap<K, V>` | `Core/Containers/HashMap.h` | Open-addressing hash map; insertion order preserved |
| `UnorderedHashSet<K>` | `Core/Containers/UnorderedHashSet.h` | Set backed by `UnorderedHashMap<K, bool>`; no ordering guarantee |
| `HashSet<K>` | `Core/Containers/HashSet.h` | Set backed by `HashMap<K, bool>`; insertion order preserved |
| `String` | `Core/Containers/Strings.h` | Arena-backed null-terminated string |
| `StringView` | `Core/Containers/Strings.h` | Non-owning view over a `String` or C string |

All containers share the same init pattern — call `init(arena, ...)` before any other method. Calling methods on an uninitialised container is caught by `ZENGINE_VALIDATE_ASSERT` in debug builds.

---

## Array\<T\>

`Array<T>` is the foundation for all other containers internally. It is **move-only**: `operator=(const Array&)` is deleted to prevent accidental shallow copies that would share a data pointer.

```cpp
Array<int> arr;
arr.init(&arena, 64);              // allocate 64 slots, m_size = 0

arr.push(42);                      // append by value
arr.push(std::move(val));          // append by move

auto& slot = arr.push_use({});     // emplace-default and return reference
slot.field  = 123;

arr[0];                            // bounds-checked (debug assert)
arr.size();                        // occupied element count
arr.capacity();                    // allocated slot count
arr.data();                        // raw pointer
```

**Three-argument init** — `init(arena, initial_capacity, initial_size)` sets `m_size = initial_size`
without constructing elements. The caller is responsible for writing valid data (e.g. via `memcpy`
or placement-new) before accessing any slot. Used internally by the hash maps.

```cpp
h.LocalTransforms.init(&arena, count, count);
secure_memcpy(h.LocalTransforms.data(), count * sizeof(Mat4f), src, count * sizeof(Mat4f));
```

`Array` can grow via `push` if `m_size == m_capacity` — it calls `ZResize` (arena realloc). On a
linear arena, if the block was the last allocation it extends in-place; otherwise it allocates a new
block and abandons the old one. Prefer pre-sizing to avoid wasted arena space.

---

## UnorderedHashMap\<K, V\>

Open-addressing hash map with **linear probing** and **power-of-2 capacity**.

### Capacity contract

`init(arena, slot_capacity)` rounds the requested capacity up to the next power of 2 (minimum 16)
and allocates the full block in one shot via `Array::init(arena, cap, cap)`. All entry state bytes
are zeroed immediately with `secure_memset` so every slot starts as `EntryState::Empty`.

**Rule: pass at least 2× the number of entries you intend to insert.** This keeps the initial load
at or below 50% and means a rehash is never needed in normal use.

```cpp
// node_count = 906 entries expected
UnorderedHashMap<uint32_t, uint32_t> map;
map.init(&arena, node_count * 2);    // capacity = next_pow2(1812) = 2048

map.insert(key, value);              // copy
map.insert(key, std::move(value));   // move
map[key] = value;                    // upsert (creates with V{} if absent)

if (auto* v = map.find(key))         // returns V* or nullptr
    use(*v);

map.contains(key);                   // bool
map.remove(key);                     // marks slot Deleted (tombstone)
map.clear();                         // resets all slots to Empty, size = 0
```

### Reserve

`reserve(n)` grows the map to `next_pow2(n)` if that exceeds the current capacity. It allocates a
new Array block and rehashes all occupied entries into it. The old block is abandoned in the arena —
call `reserve` sparingly and prefer up-front sizing via `init`.

### Key types

- Primitive types and POD structs: hashed with `rapidhash(&key, sizeof(K))`, compared with `==`.
- `const char*`: hashed with `rapidhash(key, strlen(key))`, compared with `secure_strcmp`. Two
  different pointers holding the same string content will match.

### Load guard

A `guard_load()` check fires `ZENGINE_VALIDATE_ASSERT` if inserting would push occupancy above 75%.
If this assertion triggers, increase the `init` capacity or call `reserve()` before inserting.

### Iteration

```cpp
for (auto [key, value] : map) { /* unordered */ }
for (const auto [key, value] : map) { /* const */ }
```

---

## HashMap\<K, V\>

Same open-addressing design as `UnorderedHashMap`, plus **insertion order is preserved** via a
doubly-linked list threaded through the slot array (`prev` / `next` indices; `size_type(-1)` = sentinel).

```cpp
HashMap<int, int> map;
map.init(&arena, 64);

map.insert(30, 300);
map.insert(10, 100);
map.insert(20, 200);

for (auto [k, v] : map)    // 30, 10, 20 — insertion order
    use(k, v);
```

### Removal and reinsertion

`remove(key)` marks the slot `Deleted` and unlinks the entry from the ordered list. If the same key
is inserted again later it is **appended at the tail** — it does not return to its original position.

```cpp
map.insert(1, 10);   map.insert(2, 20);   map.insert(3, 30);
map.remove(1);       // order: 2, 3
map.insert(1, 100);  // order: 2, 3, 1   (appended, not spliced back)
```

### sort_keys()

Relinks the insertion-order list so iteration yields keys in ascending order. The hash table itself
is unchanged — lookup is O(1) before and after. `sort_keys()` is idempotent.

```cpp
map.sort_keys();
for (auto [k, v] : map)    // 10, 20, 30 — sorted
    use(k, v);

map.insert(25, 250);       // appended at tail — order: 10, 20, 30, 25
```

### Initialization difference from UnorderedHashMap

`HashMap` uses a **per-slot default construction loop** rather than `memset(0)` because the
linked-list sentinels `prev` and `next` must be `size_type(-1)`, not zero. This means `HashMap::init`
is slightly slower than `UnorderedHashMap::init` for large capacities.

---

## UnorderedHashSet / HashSet

Both set types are thin wrappers over their corresponding map types, storing `bool` as the value.
All capacity rules, the power-of-2 rounding, the load guard, and the key-type handling are
**identical** to the underlying map — refer to the sections above.

```cpp
UnorderedHashSet<int> uset;
uset.init(&arena, count * 2);   // same 2× sizing rule as the map
uset.insert(42);
uset.contains(42);              // true
uset.remove(42);
for (const int& k : uset) { /* unordered */ }
```

```cpp
HashSet<int> oset;
oset.init(&arena, count * 2);
oset.insert(30); oset.insert(10); oset.insert(20);
for (const int& k : oset) { /* 30, 10, 20 — insertion order */ }
oset.sort_keys();
for (const int& k : oset) { /* 10, 20, 30 */ }
```

Because `capacity()` delegates to the underlying map, it returns the power-of-2 rounded value —
not the raw number passed to `init`. Use `EXPECT_GE(set.capacity(), n)` in tests rather than
equality.

---

## String / StringView

`String` is a thin arena-backed wrapper around a null-terminated char array.

```cpp
String s;
s.init(&arena, "hello");        // allocates strlen("hello")+1 bytes
s.init(&arena, other.c_str());  // copy from another string or literal
s.c_str();                      // const char*
s.empty();                      // bool
s == other;                     // content comparison (secure_strcmp)
```

`StringView` is a non-owning view — it holds a pointer and length with no arena interaction.

---

## Common Pitfalls

**Under-sized init triggers the load guard:**
```cpp
// BAD — 10 slots for 10 entries = 100% load
map.init(&arena, 10);
for (int i = 0; i < 10; ++i)
    map.insert(i, i);   // assertion at 8th insert (75% threshold)

// GOOD — 2× margin keeps load at 50%
map.init(&arena, 20);
```

**Rehash wastes arena space:**
Every `reserve()` call allocates a new block and abandons the old one — arena allocators cannot
free individual allocations. Pre-size with `init` so `reserve` is never needed.

**Wrong sizeof in memcpy:**
When copying element arrays, use `sizeof` of the actual element type, not the container struct.
A `sizeof(ContainerType)` instead of `sizeof(ElementType)` will silently overflow into adjacent
arena allocations. Always write `count * sizeof(ElementType)` explicitly.

**Scratch arena lifetime:**
Do not store a container in a long-lived struct while pointing its arena at a scratch sub-arena
obtained via `ZGetScratch`. The container's memory will be released when `ZReleaseScratch` is
called. Use the subsystem's permanent arena for any container that outlives a single function.
