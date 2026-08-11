# Ticket 6 — AssetRegistry: Multi-Index Store, Dependency Graph, and Hot-Reload Cascade

**Priority:** P3 — Implement after all VFS tickets 1–5 are live  
**Status:** Implemented  
**Module:** `ZEngine/VFS/Registry/` + `ZEngine/Managers/AssetManager`  
**Standard:** C++20  
**Estimated effort:** 5–7 days (1 engineer)  
**Depends on:** Ticket 1 (VFSPath), Ticket 3 (VFSDirectoryCache, VFSScanner), Ticket 4 (VFSFileWatcher, VFSWatchEvent), Ticket 5 (MetaFileData, MetaFileIO)  
**Blocks:** `import-pipeline.md`, `cook-pipeline.md`

---

## Table of Contents

1. [Motivation](#1-motivation)
2. [Scope](#2-scope)
3. [Directory Layout](#3-directory-layout)
4. [AssetRecord — the registry row](#4-assetrecord--the-registry-row)
5. [AssetIndex — multi-index store](#5-assetindex--multi-index-store)
6. [DependencyGraph — forward + reverse adjacency](#6-dependencygraph--forward--reverse-adjacency)
7. [AssetRegistry — facade](#7-assetregistry--facade)
8. [Hot-Reload Cascade Algorithm](#8-hot-reload-cascade-algorithm)
9. [AssetRegistry::Query API](#9-assetregistryquery-api)
10. [Integration with VFSScanner and VFSFileWatcher](#10-integration-with-vfsscanner-and-vfsfilewatcher)
11. [Migration Plan — AssetManager call sites](#11-migration-plan--assetmanager-call-sites)
12. [Unit Tests](#12-unit-tests)
13. [Deliverables Checklist](#13-deliverables-checklist)

---

## 1. Motivation

`AssetManager` currently uses two flat maps — `UUIDToHandle` and `HandleToUUID` — to correlate
assets between their stable identity (UUID) and their runtime slot (a packed `uint32_t`). This
design has four concrete problems:

| Problem | Where it hurts today |
|---|---|
| No path index — given a `VFSPath` you must scan all assets to find the matching UUID | `LoadAssetFile`, scene YAML serialization |
| No type index — listing all meshes requires iterating `UUIDToHandle` and filtering | `ProjectViewUIComponent`, renderer scene queries |
| No dependency tracking — reimporting `material.glb` cannot determine which meshes reference it | Future hot-reload |
| `AssetHandle = uint32_t` encodes type in the high bits but has no generation field, so a stale handle to a deleted asset silently aliases a new one at the same index | `GetAsset<T>` template specializations |

This ticket introduces `AssetRegistry`, which replaces `UUIDToHandle`/`HandleToUUID` with a
generational-handle-backed, multi-indexed record store and adds a `DependencyGraph` for
O(k) hot-reload cascade propagation.

The migration is incremental: `AssetManager` is not deleted in this ticket. It is given a new
field `Registry` of type `AssetRegistry*` and its existing methods are redirected one by one.

---

## 2. Scope

**In scope:**
- `AssetRecord` — the central registry row (UUID, type, VFSPath, MetaFileData snapshot, state machine)
- `AssetIndex` — multi-index store with lookups by UUID, VFSPath, AssetType, and name substring
- `DependencyGraph` — dual forward+reverse adjacency lists supporting O(k) cascade enumeration
- `AssetRegistry` — facade that owns `AssetIndex` + `DependencyGraph`, wired to VFS callbacks
- Hot-reload cascade algorithm (BFS walk of reverse edges with cycle protection)
- `AssetRegistry::Query` API — type filter, name pattern, extension filter
- Migration plan from `AssetManager::UUIDToHandle` / `HandleToUUID` to `AssetRegistry`
- Unit tests (10 minimum)

**Not in scope:**
- GPU resource invalidation during hot-reload (this is the renderer's responsibility; `AssetRegistry`
  only notifies via a callback)
- Persistent registry serialization to disk (all state is rebuilt from `.meta` files on startup)
- Shader variant registry (separate concern)
- Full replacement of `AssetManager` (this ticket migrates the index layer; the import pipeline
  remains in `AssetManager` for now)

---

## 3. Directory Layout

New files (existing files modified in §11):

```
ZEngine/ZEngine/VFS/Registry/
    AssetRecord.h           ← AssetRecord, AssetState
    AssetIndex.h            ← AssetIndex declaration
    AssetIndex.cpp          ← AssetIndex implementation
    DependencyGraph.h       ← DependencyGraph declaration
    DependencyGraph.cpp     ← DependencyGraph implementation
    AssetRegistry.h         ← AssetRegistry, QueryFilter, QueryResult
    AssetRegistry.cpp       ← AssetRegistry implementation

ZEngine/tests/VFS/
    AssetRegistryTest.cpp   ← 10 unit tests
```

**CMake note.** `ZEngine/ZEngine/CMakeLists.txt` uses `GLOB_RECURSE` with `CONFIGURE_DEPENDS`
for `.cpp` files — no manual source list additions needed for the new `.cpp` files. The new
`VFS/Registry/` subdirectory is automatically included.

---

## 4. AssetRecord — the registry row

### 4.1 `AssetState`

```cpp
// ZEngine/VFS/Registry/AssetRecord.h
#pragma once
#include <cstdint>

namespace ZEngine::VFS
{
    // Lifecycle state machine for a registered asset.
    //
    //   Unregistered ──Register──► Registered
    //   Registered   ──Import ──► Importing
    //   Importing    ──Done   ──► Loaded
    //   Loaded       ──Modify ──► Stale
    //   Stale        ──Import ──► Importing
    //   Any          ──Remove ──► (record erased)
    //
    enum class AssetState : uint8_t
    {
        Unregistered = 0,  // slot allocated but not yet populated
        Registered   = 1,  // UUID + path known; not yet imported
        Importing    = 2,  // import job is in-flight
        Loaded       = 3,  // GPU/CPU resources are live
        Stale        = 4,  // source changed; awaiting reimport
        Failed       = 5,  // last import attempt returned an error
    };
}
```

### 4.2 `AssetRecord`

```cpp
// ZEngine/VFS/Registry/AssetRecord.h  (continued)
#pragma once
#include <Core/VFS/VFSPath.h>
#include <VFS/Meta/MetaFileData.h>
#include <VFS/Registry/AssetRecord.h>
#include <Managers/AssetManager.h>   // AssetType, AssetHandle (uint32_t)
#include <uuid.h>
#include <cstdint>

namespace ZEngine::VFS
{
    // MAX_ASSET_NAME_LEN: name stored inline to avoid arena fragmentation on lookup.
    // Derived from VFSPath::Filename() on Register; updated on Rename.
    constexpr uint32_t MAX_ASSET_NAME_LEN = 128;

    struct AssetRecord
    {
        // ── Identity ─────────────────────────────────────────────────────────
        uuids::uuid                 UUID        = {};
        Managers::AssetType         Type        = Managers::AssetType::MESH;

        // ── Location ─────────────────────────────────────────────────────────
        VFSPath                     Path        = {};   // canonical VFS path
        char                        Name[MAX_ASSET_NAME_LEN] = {};  // basename, null-terminated

        // ── Meta snapshot ────────────────────────────────────────────────────
        // Copied from MetaFileData at registration time; updated on reimport.
        // The registry does NOT re-read the .meta file except during hot-reload.
        MetaFileData                Meta        = {};

        // ── Runtime handle ───────────────────────────────────────────────────
        // Generational handle into HandleManager<AssetRecord>.
        // Replaces the old AssetHandle (uint32_t) for registry purposes.
        // The old uint32_t handle is kept in LegacyHandle for backward compat
        // during the migration period (see §11).
        Managers::AssetManager::AssetHandle LegacyHandle = 0;

        // ── State ─────────────────────────────────────────────────────────────
        AssetState                  State       = AssetState::Unregistered;

        // ── Helpers ───────────────────────────────────────────────────────────
        bool IsValid()   const { return !UUID.is_nil(); }
        bool IsLoaded()  const { return State == AssetState::Loaded; }
        bool IsStale()   const { return State == AssetState::Stale;  }
    };
}
```

### 4.3 Design notes

- `AssetRecord` is a plain-old-data struct. It lives inside the arena-backed slot array of
  `AssetIndex` and is never heap-allocated individually.
- `Name[]` stores only the filename component (e.g. `"tank.glb"`), not the full path.
  This is populated by `VFSPath::Filename()` at registration and enables efficient
  substring search without touching the `Path` buffer on every comparison.
- `MetaFileData Meta` is a snapshot. It captures the `AssetUUID`, `ImporterName`,
  `LastSourceSha256`, and `ArtifactPath` from the last successful `.meta` read.
  The `Status` field in the snapshot is always `Unknown` — runtime state is in `AssetState`.
- `LegacyHandle` exists only during the migration period (§11). Once all `AssetManager`
  call sites use `AssetRegistry`, this field is removed.

---

## 5. AssetIndex — multi-index store

### 5.1 Overview

`AssetIndex` is the central store. It owns a `HandleManager<AssetRecord>` (slot array with
generational safety) and maintains four secondary lookup structures:

| Index | Key type | Value | Purpose |
|---|---|---|---|
| Primary | `Helpers::Handle<AssetRecord>` | `AssetRecord&` | Generational slot access |
| By UUID | `uuids::uuid` | `Helpers::Handle<AssetRecord>` | Fast UUID → record |
| By VFSPath | `VFSPath` (FNV-1a hash) | `Helpers::Handle<AssetRecord>` | Fast path → record |
| By AssetType | `Managers::AssetType` | `Array<Handle<AssetRecord>>` | Enumerate all of a type |

Name-substring search is a linear scan over the name field; no dedicated index is maintained
because it is an editor-only, infrequent operation and the record count is bounded.

### 5.2 `AssetIndex.h`

```cpp
// ZEngine/VFS/Registry/AssetIndex.h
#pragma once
#include <Core/Containers/Array.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <Core/Memory/Allocator.h>
#include <Helpers/HandleManager.h>
#include <Managers/AssetManager.h>
#include <VFS/Registry/AssetRecord.h>
#include <uuid.h>
#include <functional>
#include <shared_mutex>
#include <span>

namespace ZEngine::VFS
{
    // -------------------------------------------------------------------------
    // UUIDHasher — adapts uuids::uuid for UnorderedHashMap
    // -------------------------------------------------------------------------
    struct UUIDHasher
    {
        uint64_t operator()(const uuids::uuid& id) const noexcept
        {
            // FNV-1a over the 16 UUID bytes
            constexpr uint64_t FNV_BASIS = 14695981039346656037ULL;
            constexpr uint64_t FNV_PRIME = 1099511628211ULL;
            const auto bytes = id.as_bytes();
            uint64_t   h     = FNV_BASIS;
            for (auto b : bytes) { h ^= static_cast<uint8_t>(b); h *= FNV_PRIME; }
            return h;
        }
        bool operator()(const uuids::uuid& a, const uuids::uuid& b) const noexcept
        {
            return a == b;
        }
    };

    // -------------------------------------------------------------------------
    // VFSPathHasher — adapts VFSPath for UnorderedHashMap using VFSPath::Hash()
    // -------------------------------------------------------------------------
    struct VFSPathHasher
    {
        uint64_t operator()(const VFSPath& p) const noexcept { return p.Hash(); }
        bool     operator()(const VFSPath& a, const VFSPath& b) const noexcept { return a == b; }
    };

    // -------------------------------------------------------------------------
    // RegisterResult — returned by AssetIndex::Register
    // -------------------------------------------------------------------------
    enum class RegisterError : uint8_t
    {
        None             = 0,
        DuplicateUUID    = 1,  // UUID already present; use Update instead
        DuplicatePath    = 2,  // VFSPath already mapped to a different UUID
        SlotExhausted    = 3,  // HandleManager capacity reached
    };

    struct RegisterResult
    {
        Helpers::Handle<AssetRecord> Handle = {};
        RegisterError                Error  = RegisterError::None;

        bool IsOk() const { return Error == RegisterError::None && Handle.Valid(); }
    };

    // -------------------------------------------------------------------------
    // AssetIndex
    // -------------------------------------------------------------------------
    class AssetIndex
    {
    public:
        // Maximum registered assets. Sized for a mid-size game project.
        // Increase if needed; the slot array is arena-allocated at init time.
        static constexpr uint32_t MAX_ASSETS = 65536;

        AssetIndex()  = default;
        ~AssetIndex() = default;

        // Must be called before any other method.
        // arena: persistent arena (must outlive AssetIndex)
        void Initialize(Core::Memory::ArenaAllocator* arena);

        // ── Mutation ─────────────────────────────────────────────────────────

        // Register a new asset. Fails if UUID or VFSPath is already mapped.
        // Populates AssetRecord fields; sets State = Registered.
        RegisterResult Register(const uuids::uuid&       uuid,
                                Managers::AssetType      type,
                                const VFSPath&           path,
                                const MetaFileData&      meta);

        // Update an existing record (e.g. after reimport). Record must exist.
        // Only Meta, LegacyHandle, State may be updated; UUID and Type are immutable.
        bool Update(Helpers::Handle<AssetRecord> handle,
                    const MetaFileData&          new_meta,
                    AssetState                   new_state);

        // Transition State only (e.g. Registered → Importing, Loaded → Stale).
        bool SetState(Helpers::Handle<AssetRecord> handle, AssetState new_state);

        // Remove the record and all secondary index entries.
        // Returns false if handle is stale (already removed).
        bool Remove(Helpers::Handle<AssetRecord> handle);

        // Rename: update Path and Name; re-index by-path.
        bool Rename(Helpers::Handle<AssetRecord> handle, const VFSPath& new_path);

        // ── Lookup ───────────────────────────────────────────────────────────

        // Primary — O(1), generational check
        AssetRecord*       Access(Helpers::Handle<AssetRecord> handle);
        const AssetRecord* Access(Helpers::Handle<AssetRecord> handle) const;

        // By UUID — O(1) hash lookup
        Helpers::Handle<AssetRecord> FindByUUID(const uuids::uuid& uuid) const;

        // By VFSPath — O(1) hash lookup
        Helpers::Handle<AssetRecord> FindByPath(const VFSPath& path) const;

        // By AssetType — O(1) to retrieve span; O(n_type) to iterate
        // span is valid until the next Register/Remove call.
        std::span<const Helpers::Handle<AssetRecord>>
            FindByType(Managers::AssetType type) const;

        // Name substring — O(N) linear scan over all live records.
        // Appends matches to `out`. Returns match count.
        uint32_t FindByNameSubstring(const char* substr,
                                     Core::Containers::Array<Helpers::Handle<AssetRecord>>& out) const;

        // Returns true if the handle refers to a live, valid record.
        bool IsLive(Helpers::Handle<AssetRecord> handle) const;

        // ── Iteration ────────────────────────────────────────────────────────

        // Visit all live records. ForEach acquires shared_lock for the duration.
        // Do NOT call Register/Remove from within the visitor — deadlock.
        void ForEach(std::function<void(const AssetRecord&)> visitor) const;

        // ── Stats ─────────────────────────────────────────────────────────────
        uint32_t Count()    const;   // total live records
        uint32_t Capacity() const;   // MAX_ASSETS

    private:
        Helpers::HandleManager<AssetRecord>     m_handles   = {};

        // Secondary indices — keyed on immutable fields.
        // Access under m_mutex.
        Core::Containers::UnorderedHashMap<
            uuids::uuid,
            Helpers::Handle<AssetRecord>,
            UUIDHasher>                          m_by_uuid   = {};

        Core::Containers::UnorderedHashMap<
            VFSPath,
            Helpers::Handle<AssetRecord>,
            VFSPathHasher>                       m_by_path   = {};

        // Per-type lists. Indexed by static_cast<uint8_t>(AssetType).
        // 4 types defined (MESH=0, MATERIAL=1, TEXTURE=2, MESH_HIERARCHY=3).
        static constexpr uint8_t ASSET_TYPE_COUNT = 4;
        Core::Containers::Array<
            Helpers::Handle<AssetRecord>>        m_by_type[ASSET_TYPE_COUNT] = {};

        mutable std::shared_mutex                m_mutex     = {};

        // Arena passed in Initialize(); used for type-bucket arrays.
        Core::Memory::ArenaAllocator*            m_arena     = nullptr;
    };

} // namespace ZEngine::VFS
```

### 5.3 Implementation Notes

#### `Initialize`

```cpp
void AssetIndex::Initialize(Core::Memory::ArenaAllocator* arena)
{
    m_arena = arena;
    m_handles.Initialize(arena, MAX_ASSETS);
    // Pre-allocate type bucket arrays with generous reserve
    for (uint8_t i = 0; i < ASSET_TYPE_COUNT; ++i)
        m_by_type[i].init(arena, 0, MAX_ASSETS / ASSET_TYPE_COUNT);
}
```

#### `Register` algorithm

```
1. Acquire unique_lock(m_mutex)
2. If m_by_uuid.contains(uuid)     → return {.Error = DuplicateUUID}
3. If m_by_path.contains(path)     → return {.Error = DuplicatePath}
4. handle = m_handles.Create()
5. If !handle.Valid()              → return {.Error = SlotExhausted}
6. record = m_handles[handle]       ← reference into slot array
7. record.UUID         = uuid
8. record.Type         = type
9. record.Path         = path
10. copy basename into record.Name (VFSPath::Filename → strncpy)
11. record.Meta         = meta
12. record.LegacyHandle = 0          ← caller sets this separately
13. record.State        = AssetState::Registered
14. m_by_uuid[uuid]     = handle
15. m_by_path[path]     = handle
16. m_by_type[type_idx].push_back(handle)
17. return {handle, RegisterError::None}
```

#### `Remove` algorithm

```
1. Acquire unique_lock(m_mutex)
2. record = m_handles.Access(handle)
3. If record == nullptr → return false
4. m_by_uuid.erase(record->UUID)
5. m_by_path.erase(record->Path)
6. Erase handle from m_by_type[record->Type] (swap with back, pop)
7. m_handles.Remove(handle)           ← bumps generation; slot reusable
8. return true
```

**Swap-and-pop** is used in step 6 because type buckets are unsorted and `Array` does not support
O(1) middle removal. The order of handles in a type bucket is undefined — callers must not
depend on insertion order.

#### `FindByType` span safety

`FindByType` returns a `std::span` into `m_by_type[i].Data()`. The span is invalidated by any
`Register` or `Remove` call that touches the same type bucket. Callers that iterate the span
must hold `m_mutex` (as a shared_lock) for the entire iteration, or copy the handles out first.
The `AssetRegistry::Query` API (§9) always copies results into a caller-owned array, so it is
safe without an explicit lock on the caller side.

---

## 6. DependencyGraph — forward + reverse adjacency

### 6.1 Concepts

An **edge** `A → B` means "asset A depends on asset B". Examples:
- A mesh (`A`) depends on its material (`B`).
- A material (`A`) depends on its albedo texture (`B`).

When `B` is modified (hot-reload), all assets that depend on `B` — directly or transitively —
must be invalidated. This is the **reverse** graph: given `B`, find all `A` where `A → B`.

`DependencyGraph` maintains:
- `Forward`: for each asset UUID, the set of UUIDs it directly depends on.
- `Reverse`: for each asset UUID, the set of UUIDs that directly depend on it.

Both directions are needed:
- `Forward` is used to validate that dependencies are registered before a dependent.
- `Reverse` is used for hot-reload cascade enumeration (BFS, §8).

### 6.2 `DependencyGraph.h`

```cpp
// ZEngine/VFS/Registry/DependencyGraph.h
#pragma once
#include <Core/Containers/Array.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <Core/Memory/Allocator.h>
#include <VFS/Registry/AssetRecord.h>      // UUIDHasher
#include <uuid.h>
#include <functional>
#include <shared_mutex>
#include <span>

namespace ZEngine::VFS
{
    // -------------------------------------------------------------------------
    // AdjacencyList — arena-backed list of UUIDs.
    // Inline storage for small lists (up to INLINE_CAPACITY) avoids arena
    // allocations for the common case of a mesh with 1–3 materials.
    // -------------------------------------------------------------------------
    constexpr uint32_t DEP_INLINE_CAPACITY = 8;

    struct AdjacencyList
    {
        uuids::uuid InlineStorage[DEP_INLINE_CAPACITY] = {};
        uint32_t    InlineCount                        = 0;

        // Overflow into arena when inline capacity exceeded
        Core::Containers::Array<uuids::uuid> Overflow  = {};

        // Total count (inline + overflow)
        uint32_t Count() const
        {
            return InlineCount + static_cast<uint32_t>(Overflow.size());
        }

        bool Empty() const { return Count() == 0; }

        // Get element i. No bounds check in release builds.
        uuids::uuid operator[](uint32_t i) const
        {
            if (i < InlineCount)   return InlineStorage[i];
            return Overflow[i - InlineCount];
        }

        // Returns true if uuid is already present.
        bool Contains(const uuids::uuid& uuid) const;

        // Append uuid (if not already present). Returns false if already present.
        bool Append(const uuids::uuid& uuid, Core::Memory::ArenaAllocator* arena);

        // Remove uuid. Returns false if not found.
        bool Erase(const uuids::uuid& uuid);
    };

    // -------------------------------------------------------------------------
    // DependencyGraph
    // -------------------------------------------------------------------------
    class DependencyGraph
    {
    public:
        DependencyGraph()  = default;
        ~DependencyGraph() = default;

        void Initialize(Core::Memory::ArenaAllocator* arena);

        // ── Mutation ─────────────────────────────────────────────────────────

        // AddEdge(dependent, dependency): asset `dependent` uses asset `dependency`.
        // Both UUIDs must refer to registered assets (caller's responsibility).
        // Returns false if the edge already exists.
        bool AddEdge(const uuids::uuid& dependent, const uuids::uuid& dependency);

        // RemoveEdge: removes a single directed edge.
        // Returns false if no such edge existed.
        bool RemoveEdge(const uuids::uuid& dependent, const uuids::uuid& dependency);

        // RemoveAsset: removes all edges involving `uuid` (both as source and target).
        // Call this before AssetIndex::Remove so that stale UUIDs are not retained.
        void RemoveAsset(const uuids::uuid& uuid);

        // ── Query ─────────────────────────────────────────────────────────────

        // Direct dependencies of `uuid` (assets that `uuid` depends ON).
        // Returned span is valid only while m_mutex is held by caller as shared_lock.
        // Prefer CopyDependencies for use outside the lock.
        void CopyDependencies(const uuids::uuid& uuid,
                              Core::Containers::Array<uuids::uuid>& out) const;

        // Direct dependents of `uuid` (assets that depend ON `uuid`).
        void CopyDependents(const uuids::uuid& uuid,
                            Core::Containers::Array<uuids::uuid>& out) const;

        // Returns true if `dependent` directly depends on `dependency`.
        bool HasEdge(const uuids::uuid& dependent, const uuids::uuid& dependency) const;

        // ── Cascade enumeration (used by hot-reload, §8) ───────────────────

        // Fills `out` with all assets that must be invalidated when `changed`
        // is hot-reloaded, using BFS over the reverse (dependent) edges.
        // `changed` itself is included at index 0.
        // Detects cycles (an asset cannot depend on itself transitively) via
        // a visited set; cycles are silently broken (the second visit is skipped).
        void CollectCascade(const uuids::uuid& changed,
                            Core::Containers::Array<uuids::uuid>& out,
                            Core::Memory::ArenaAllocator* scratch) const;

        // ── Stats ──────────────────────────────────────────────────────────
        uint32_t EdgeCount()  const;   // total directed edges
        uint32_t NodeCount()  const;   // distinct UUIDs in the graph

    private:
        // Both maps are keyed by UUID.
        // m_forward[A] = list of B where A→B (A depends on B)
        // m_reverse[B] = list of A where A→B (B is depended on by A)
        Core::Containers::UnorderedHashMap<
            uuids::uuid, AdjacencyList, UUIDHasher>   m_forward = {};
        Core::Containers::UnorderedHashMap<
            uuids::uuid, AdjacencyList, UUIDHasher>   m_reverse = {};

        mutable std::shared_mutex                      m_mutex   = {};
        Core::Memory::ArenaAllocator*                  m_arena   = nullptr;
        uint32_t                                       m_edges   = 0;
    };

} // namespace ZEngine::VFS
```

### 6.3 Implementation Notes

#### `AdjacencyList::Append`

```cpp
bool AdjacencyList::Append(const uuids::uuid& uuid, Core::Memory::ArenaAllocator* arena)
{
    if (Contains(uuid)) return false;
    if (InlineCount < DEP_INLINE_CAPACITY)
    {
        InlineStorage[InlineCount++] = uuid;
        return true;
    }
    Overflow.push_back(uuid);   // arena-backed Array::push_back
    return true;
}
```

#### `AdjacencyList::Erase`

```
Scan InlineStorage for uuid.
  If found at i:
    If Overflow not empty:
      InlineStorage[i] = Overflow.back(); Overflow.pop_back()
    Else if i != InlineCount-1:
      InlineStorage[i] = InlineStorage[--InlineCount]
    Else:
      --InlineCount
    Return true.
Scan Overflow (swap-and-pop).
Return false if not found.
```

#### `DependencyGraph::AddEdge`

```
1. Acquire unique_lock(m_mutex)
2. Get or insert m_forward[dependent]
3. If Append(dependency) returns false (already exists) → return false
4. Get or insert m_reverse[dependency]
5. Append(dependent) to reverse list
6. ++m_edges
7. return true
```

#### `DependencyGraph::RemoveAsset`

```
1. Acquire unique_lock(m_mutex)
2. Forward cleanup:
   fwd = m_forward[uuid]
   For each dep in fwd:
     m_reverse[dep].Erase(uuid)
     --m_edges
   m_forward.erase(uuid)
3. Reverse cleanup:
   rev = m_reverse[uuid]
   For each src in rev:
     m_forward[src].Erase(uuid)
     --m_edges
   m_reverse.erase(uuid)
```

#### `DependencyGraph::CollectCascade`

See §8 for the full BFS algorithm listing. `CollectCascade` is the only method that uses a
scratch arena (for the BFS visited set / queue). The scratch arena must be provided by the
caller (typically `AssetRegistry`'s internal scratch sub-arena).

---

## 7. AssetRegistry — facade

### 7.1 Hot-Reload Callback

```cpp
// ZEngine/VFS/Registry/AssetRegistry.h
#pragma once
#include <Core/Memory/Allocator.h>
#include <VFS/Registry/AssetIndex.h>
#include <VFS/Registry/DependencyGraph.h>
#include <VFS/VFSFileWatcher.h>        // VFSWatchEvent
#include <VFS/VFSScanner.h>            // ScanStats (forward compat)
#include <VFS/Meta/MetaFileIO.h>
#include <functional>
#include <span>

namespace ZEngine::VFS
{
    // Called after the cascade BFS identifies all assets to reload.
    // `cascade` spans all UUIDs to invalidate, in BFS order (root first).
    // Fired on the main/editor thread (after VFSFileWatcher::Tick()).
    using HotReloadCallback = std::function<void(std::span<const uuids::uuid> cascade)>;
```

### 7.2 `AssetRegistry.h`

```cpp
    // -------------------------------------------------------------------------
    // AssetRegistry
    // -------------------------------------------------------------------------
    class AssetRegistry
    {
    public:
        AssetRegistry()  = default;
        ~AssetRegistry() = default;

        // arena: persistent arena (owns AssetIndex + DependencyGraph storage)
        // scratch_size: byte size of the scratch sub-arena (default 64 KiB)
        void Initialize(Core::Memory::ArenaAllocator* arena,
                        uint64_t scratch_size = 65536);

        // ── Registration ─────────────────────────────────────────────────────

        // Register asset from its MetaFileData (called by VFSScanner callback).
        // type is inferred from file extension if not provided explicitly.
        RegisterResult Register(const uuids::uuid&  uuid,
                                Managers::AssetType type,
                                const VFSPath&      path,
                                const MetaFileData& meta);

        // Convenience: register and immediately mark as Loaded.
        RegisterResult RegisterLoaded(const uuids::uuid&                  uuid,
                                      Managers::AssetType                  type,
                                      const VFSPath&                       path,
                                      const MetaFileData&                  meta,
                                      Managers::AssetManager::AssetHandle  legacy_handle);

        // Remove the record and purge its dependency edges.
        bool Remove(const uuids::uuid& uuid);
        bool Remove(Helpers::Handle<AssetRecord> handle);

        // ── Dependency management ─────────────────────────────────────────────

        // Declare that `dependent` uses `dependency`.
        bool AddDependency(const uuids::uuid& dependent, const uuids::uuid& dependency);

        bool RemoveDependency(const uuids::uuid& dependent, const uuids::uuid& dependency);

        // ── State transitions ─────────────────────────────────────────────────

        bool SetState(const uuids::uuid& uuid, AssetState new_state);

        bool UpdateMeta(const uuids::uuid& uuid,
                        const MetaFileData& new_meta,
                        AssetState          new_state = AssetState::Loaded);

        // ── Lookup (delegates to AssetIndex) ─────────────────────────────────

        AssetRecord*       FindByUUID(const uuids::uuid& uuid);
        const AssetRecord* FindByUUID(const uuids::uuid& uuid) const;

        AssetRecord*       FindByPath(const VFSPath& path);
        const AssetRecord* FindByPath(const VFSPath& path) const;

        // ── Query (see §9) ────────────────────────────────────────────────────

        struct QueryFilter
        {
            // Set to a valid type to restrict results.
            // Set to nullopt to return all types.
            std::optional<Managers::AssetType> Type     = std::nullopt;

            // Non-null: only return records where Name contains this substring.
            const char*                        NameLike = nullptr;

            // Non-null: only return records where VFSPath ends with this extension
            // (e.g. ".glb", ".png"). Comparison is case-insensitive.
            const char*                        Ext      = nullptr;

            // Only return records with this state.
            // Set to nullopt to return records of any state.
            std::optional<AssetState>          State    = std::nullopt;
        };

        struct QueryResult
        {
            Core::Containers::Array<Helpers::Handle<AssetRecord>> Handles;
            uint32_t                                              Count = 0;
        };

        // Execute a query. Results written into caller-supplied array.
        // arena: scratch arena for the result array (caller owns lifetime).
        QueryResult Query(const QueryFilter& filter,
                          Core::Memory::ArenaAllocator* arena) const;

        // ── Hot-reload ────────────────────────────────────────────────────────

        // Register the callback invoked when a cascade is computed.
        void SetHotReloadCallback(HotReloadCallback cb);

        // Called by VFSFileWatcher integration (§10) when a source asset changes.
        // Computes BFS cascade and fires the HotReloadCallback.
        // path: the VFSPath of the modified asset.
        void OnAssetModified(const VFSPath& path);

        // Called by VFSFileWatcher integration when an asset is deleted on disk.
        void OnAssetDeleted(const VFSPath& path);

        // Called by VFSFileWatcher integration when an asset is renamed on disk.
        void OnAssetRenamed(const VFSPath& old_path, const VFSPath& new_path);

        // ── VFSScanner integration (§10) ─────────────────────────────────────

        // Called per-file from the scanner's ScanComplete callback.
        // Reads the .meta file for path, registers the asset if not already known,
        // or updates it if the SHA changed (Stale).
        void OnScanFileDiscovered(IVFSContext& ctx,
                                  const VFSPath& path,
                                  Managers::AssetType type);

        // ── Stats / diagnostics ───────────────────────────────────────────────

        uint32_t RecordCount()    const;
        uint32_t EdgeCount()      const;

        // Write a DOT-format dependency graph to `out_buf` (for debugging).
        // out_buf: caller-supplied buffer; out_len: buffer capacity.
        // Returns number of bytes written (0 if buffer too small).
        uint32_t DumpGraphDOT(char* out_buf, uint32_t out_len) const;

    private:
        AssetIndex                   m_index         = {};
        DependencyGraph              m_graph         = {};
        HotReloadCallback            m_reload_cb     = nullptr;

        // Scratch sub-arena: used for BFS queue/visited in CollectCascade.
        // Created from the persistent arena passed to Initialize.
        Core::Memory::ArenaAllocator m_scratch       = {};

        // Helpers
        static Managers::AssetType   InferTypeFromExtension(const VFSPath& path);
        static bool                  ExtensionMatches(const VFSPath& path,
                                                       const char* ext);
    };

} // namespace ZEngine::VFS
```

### 7.3 Implementation Notes

#### `Initialize`

```cpp
void AssetRegistry::Initialize(Core::Memory::ArenaAllocator* arena, uint64_t scratch_size)
{
    m_scratch = arena->CreateSubArena(scratch_size);
    m_index.Initialize(arena);
    m_graph.Initialize(arena);
}
```

#### `RegisterLoaded`

```cpp
RegisterResult AssetRegistry::RegisterLoaded(
    const uuids::uuid& uuid, Managers::AssetType type,
    const VFSPath& path, const MetaFileData& meta,
    Managers::AssetManager::AssetHandle legacy_handle)
{
    RegisterResult result = m_index.Register(uuid, type, path, meta);
    if (!result.IsOk()) return result;

    AssetRecord* rec = m_index.Access(result.Handle);
    ZENGINE_VALIDATE_ASSERT(rec != nullptr, "Register succeeded but Access returned null")
    rec->LegacyHandle = legacy_handle;
    rec->State        = AssetState::Loaded;
    return result;
}
```

#### `Remove`

```cpp
bool AssetRegistry::Remove(const uuids::uuid& uuid)
{
    m_graph.RemoveAsset(uuid);                            // purge edges first
    auto h = m_index.FindByUUID(uuid);
    if (!h.Valid()) return false;
    return m_index.Remove(h);
}
```

#### `InferTypeFromExtension`

```cpp
Managers::AssetType AssetRegistry::InferTypeFromExtension(const VFSPath& path)
{
    auto fn = path.Filename();
    if (!fn.Succeeded()) return Managers::AssetType::MESH;

    cstring name = fn.Value().CStr();
    // Walk backwards to find extension
    const char* dot = nullptr;
    for (const char* p = name; *p; ++p) if (*p == '.') dot = p;

    if (!dot) return Managers::AssetType::MESH;

    if (std::strcmp(dot, ".png")  == 0 ||
        std::strcmp(dot, ".jpg")  == 0 ||
        std::strcmp(dot, ".jpeg") == 0 ||
        std::strcmp(dot, ".hdr")  == 0 ||
        std::strcmp(dot, ".ktx")  == 0 ||
        std::strcmp(dot, ".ktx2") == 0)
        return Managers::AssetType::TEXTURE;

    // .glb, .gltf, .fbx, .obj → could be MESH or MESH_HIERARCHY.
    // Default to MESH; the importer sets MESH_HIERARCHY after parsing.
    return Managers::AssetType::MESH;
}
```

---

## 8. Hot-Reload Cascade Algorithm

### 8.1 Trigger

Hot-reload is triggered by `VFSFileWatcher` when a source asset file receives a
`WatchEventKind::Modified` event. The chain is:

```
VFSFileWatcher::Tick()
    → VFSWatchEvent(Kind=Modified, Path="/project/textures/rock_albedo.png")
    → AssetRegistry::OnAssetModified(VFSPath("/project/textures/rock_albedo.png"))
    → CollectCascade(changed_uuid, ...)
    → HotReloadCallback(cascade)
```

### 8.2 `OnAssetModified`

```cpp
void AssetRegistry::OnAssetModified(const VFSPath& path)
{
    auto handle = m_index.FindByPath(path);
    if (!handle.Valid()) return;   // untracked file; ignore

    AssetRecord* rec = m_index.Access(handle);
    if (!rec) return;

    // Mark source asset Stale
    m_index.SetState(handle, AssetState::Stale);

    // Compute cascade
    m_scratch.Clear();    // reset scratch arena for BFS
    Core::Containers::Array<uuids::uuid> cascade;
    cascade.init(&m_scratch, 0, 64);

    m_graph.CollectCascade(rec->UUID, cascade, &m_scratch);

    // Mark all cascade members Stale
    for (uint32_t i = 1; i < cascade.size(); ++i)  // i=0 is rec itself, already marked
    {
        auto dep_h = m_index.FindByUUID(cascade[i]);
        if (dep_h.Valid())
            m_index.SetState(dep_h, AssetState::Stale);
    }

    // Fire callback
    if (m_reload_cb)
        m_reload_cb(std::span<const uuids::uuid>(cascade.Data(), cascade.size()));
}
```

### 8.3 `DependencyGraph::CollectCascade` — BFS over reverse edges

```cpp
void DependencyGraph::CollectCascade(
    const uuids::uuid& changed,
    Core::Containers::Array<uuids::uuid>& out,
    Core::Memory::ArenaAllocator* scratch) const
{
    // visited set — dynamic open-addressing hash set backed by scratch arena.
    // Initial capacity 256 (covers typical dependency chains). Doubles on 66% load.
    // Using the scratch arena means no heap allocation and automatic cleanup when
    // the caller resets the arena after CollectCascade returns.
    uint32_t    visited_capacity = 256;
    uint32_t    visited_count    = 0;
    uuids::uuid* visited = ZPushArray(scratch, uuids::uuid, visited_capacity);
    std::memset(visited, 0, visited_capacity * sizeof(uuids::uuid));  // nil = empty slot

    auto visited_rehash = [&]() {
        uint32_t new_cap = visited_capacity * 2;
        uuids::uuid* new_table = ZPushArray(scratch, uuids::uuid, new_cap);
        std::memset(new_table, 0, new_cap * sizeof(uuids::uuid));
        for (uint32_t i = 0; i < visited_capacity; ++i)
        {
            if (visited[i].is_nil()) continue;
            uint64_t h = UUIDHasher{}(visited[i]) % new_cap;
            for (uint32_t probe = 0; probe < new_cap; ++probe)
            {
                uint64_t slot = (h + probe) % new_cap;
                if (new_table[slot].is_nil()) { new_table[slot] = visited[i]; break; }
            }
        }
        visited          = new_table;
        visited_capacity = new_cap;
    };

    auto visited_contains = [&](const uuids::uuid& id) -> bool {
        uint64_t h = UUIDHasher{}(id) % visited_capacity;
        for (uint32_t probe = 0; probe < visited_capacity; ++probe)
        {
            uint64_t slot = (h + probe) % visited_capacity;
            if (visited[slot].is_nil()) return false;
            if (visited[slot] == id)   return true;
        }
        return false;
    };

    auto visited_insert = [&](const uuids::uuid& id) {
        // Grow before inserting if >66% full — keeps linear-probe performance stable.
        if (visited_count >= visited_capacity * 2 / 3)
            visited_rehash();
        uint64_t h = UUIDHasher{}(id) % visited_capacity;
        for (uint32_t probe = 0; probe < visited_capacity; ++probe)
        {
            uint64_t slot = (h + probe) % visited_capacity;
            if (visited[slot].is_nil() || visited[slot] == id)
            {
                visited[slot] = id;
                ++visited_count;
                return;
            }
        }
    };

    // BFS queue backed by scratch arena
    Core::Containers::Array<uuids::uuid> queue;
    queue.init(scratch, 0, 64);

    // Enqueue root
    out.push_back(changed);
    visited_insert(changed);
    queue.push_back(changed);

    std::shared_lock lock(m_mutex);

    while (!queue.empty())
    {
        uuids::uuid current = queue.back();
        queue.pop_back();

        auto it = m_reverse.find(current);
        if (it == m_reverse.end()) continue;   // no dependents

        const AdjacencyList& dependents = it->second;
        for (uint32_t i = 0; i < dependents.Count(); ++i)
        {
            const uuids::uuid& dep = dependents[i];
            if (!visited_contains(dep))
            {
                visited_insert(dep);
                out.push_back(dep);
                queue.push_back(dep);
            }
        }
    }
    // out[0] = changed (root); out[1..] = dependents in BFS order
}
```

**Cycle protection**: the visited set ensures that even if a cycle exists in the dependency graph
(which indicates an authoring error), the BFS terminates. If the visited table exceeds 66% load
factor (>~680 entries in a 1024-slot table, or >~1365 entries in the 2048-slot table used here),
`CollectCascade` returns `VFSResult<void>::Fail(VFSError::OutOfMemory)` immediately rather than
continuing with degraded cycle protection. The caller (e.g., `OnAssetModified`) must handle this
gracefully — log the error and skip the hot-reload cascade for this asset.

**Thread safety**: `CollectCascade` acquires `m_mutex` as `shared_lock` for the duration of the
BFS walk. `OnAssetModified` calls it after acquiring no other locks — no lock inversion with
`AssetIndex::m_mutex`.

**Complexity**: O(V + E) where V = cascade size, E = edges traversed. For typical game assets
(a material depending on 3–8 textures, a mesh depending on 1–4 materials), cascade sizes
are small (< 20 assets) and the walk is sub-microsecond.

---

## 9. AssetRegistry::Query API

### 9.1 Design

`Query` is an editor-side API — not on the hot rendering path. It is O(N) where N is the number
of live records, but N is typically small (thousands at most) and the operation is called at
interactive speeds (on keystrokes, not per-frame).

```cpp
QueryResult AssetRegistry::Query(const QueryFilter& filter,
                                  Core::Memory::ArenaAllocator* arena) const
{
    QueryResult result{};
    result.Handles.init(arena, 0, 64);

    if (filter.Type.has_value())
    {
        // Fast path: iterate only the type bucket
        auto type_handles = m_index.FindByType(filter.Type.value());

        // Copy type_handles before releasing internal shared_lock
        Core::Containers::Array<Helpers::Handle<AssetRecord>> type_copy;
        type_copy.init(arena, 0, static_cast<uint32_t>(type_handles.size()));
        for (auto& h : type_handles) type_copy.push_back(h);

        for (uint32_t i = 0; i < type_copy.size(); ++i)
        {
            const AssetRecord* rec = m_index.Access(type_copy[i]);
            if (!rec) continue;
            if (!PassesFilter(*rec, filter)) continue;
            result.Handles.push_back(type_copy[i]);
        }
    }
    else
    {
        // Full scan — used only when no type filter is specified
        m_index.ForEach([&](const AssetRecord& rec) {
            if (!PassesFilter(rec, filter)) return;
            auto h = m_index.FindByUUID(rec.UUID);
            if (h.Valid()) result.Handles.push_back(h);
        });
    }

    result.Count = static_cast<uint32_t>(result.Handles.size());
    return result;
}
```

### 9.2 `PassesFilter` predicate

```cpp
// File-local helper
static bool PassesFilter(const AssetRecord& rec, const AssetRegistry::QueryFilter& f)
{
    // State filter
    if (f.State.has_value() && rec.State != f.State.value())
        return false;

    // Name substring
    if (f.NameLike != nullptr && f.NameLike[0] != '\0')
    {
        // Case-insensitive substring search on rec.Name
        // KMPSearch or simple strstr; rec.Name is always null-terminated
        if (std::strstr(rec.Name, f.NameLike) == nullptr)
            return false;
    }

    // Extension filter
    if (f.Ext != nullptr && f.Ext[0] != '\0')
    {
        if (!AssetRegistry::ExtensionMatches(rec.Path, f.Ext))
            return false;
    }

    return true;
}
```

### 9.3 `ExtensionMatches`

```cpp
bool AssetRegistry::ExtensionMatches(const VFSPath& path, const char* ext)
{
    cstring raw = path.CStr();
    size_t  rlen = std::strlen(raw);
    size_t  elen = std::strlen(ext);
    if (elen == 0 || rlen < elen) return false;

    const char* suffix = raw + rlen - elen;
    for (size_t i = 0; i < elen; ++i)
        if (::tolower(static_cast<unsigned char>(suffix[i])) !=
            ::tolower(static_cast<unsigned char>(ext[i])))
            return false;
    return true;
}
```

### 9.4 Usage examples

```cpp
// All loaded textures
auto result = registry.Query(
    {.Type = Managers::AssetType::TEXTURE, .State = AssetState::Loaded},
    &scratch_arena);

// All .glb files regardless of type
auto result = registry.Query(
    {.Ext = ".glb"},
    &scratch_arena);

// All meshes whose name contains "tank"
auto result = registry.Query(
    {.Type = Managers::AssetType::MESH, .NameLike = "tank"},
    &scratch_arena);

// All stale assets (any type)
auto result = registry.Query(
    {.State = AssetState::Stale},
    &scratch_arena);
```

---

## 10. Integration with VFSScanner and VFSFileWatcher

### 10.1 VFSScanner integration

`VFSScanner` already calls `MetaFileIO::GetOrCreate` for each discovered asset file (Ticket 5).
In this ticket, `VFSScanner::ScanDirectory` additionally calls `AssetRegistry::OnScanFileDiscovered`
for asset-extension files.

```cpp
// In VFSScanner::ScanDirectory, after MetaFileIO::GetOrCreate succeeds:
if (meta.IsOk() && m_registry != nullptr)
{
    Managers::AssetType type = AssetRegistry::InferTypeFromExtension(entry.VFSPath);
    m_registry->OnScanFileDiscovered(*m_ctx, entry.VFSPath, type);
}
```

`AssetRegistry::OnScanFileDiscovered`:

```cpp
void AssetRegistry::OnScanFileDiscovered(
    IVFSContext& ctx, const VFSPath& path, Managers::AssetType type)
{
    // Read the .meta sidecar (already written by MetaFileIO::GetOrCreate in scanner)
    auto meta_result = MetaFileIO::Read(ctx, path);
    if (!meta_result.IsOk()) return;

    const MetaFileData& meta = meta_result.Value();

    // Already registered? → check for SHA change
    auto existing = m_index.FindByPath(path);
    if (existing.Valid())
    {
        AssetRecord* rec = m_index.Access(existing);
        if (rec &&
            std::strcmp(rec->Meta.LastSourceSha256, meta.LastSourceSha256) != 0)
        {
            // SHA changed → mark Stale
            m_index.SetState(existing, AssetState::Stale);
            m_index.Update(existing, meta, AssetState::Stale);
        }
        return;
    }

    // New asset → register it
    Register(meta.AssetUUID, type, path, meta);
}
```

`VFSScanner` gets a pointer to `AssetRegistry` injected at construction:

```cpp
// In VFSScanner.h — add member:
AssetRegistry* m_registry = nullptr;

// New setter:
void SetAssetRegistry(AssetRegistry* registry) { m_registry = registry; }
```

### 10.2 VFSFileWatcher integration

`VFSContext` (or `Editor`) wires the watcher's callback to the registry:

```cpp
// VFSContext::InitWatcher() — extend existing registration (Ticket 4):
m_file_watcher->Watch(
    m_project_root_native.c_str(),
    /*recursive=*/true,
    [this](const VFSWatchEvent& ev) {
        // Existing: invalidate cache and request rescan
        m_directory_cache->Invalidate(ev.Path);
        m_scanner->RequestScan(VFSPath::FromNative(ev.Path).Value());

        // New: notify registry
        if (m_registry == nullptr) return;
        VFSPath vfs_path = VFSPath::Parse(ev.Path).Value();
        switch (ev.Kind)
        {
        case WatchEventKind::Modified:
            m_registry->OnAssetModified(vfs_path);
            break;
        case WatchEventKind::Deleted:
            m_registry->OnAssetDeleted(vfs_path);
            break;
        case WatchEventKind::Renamed:
        {
            VFSPath old_path = VFSPath::Parse(ev.OldPath).Value();
            m_registry->OnAssetRenamed(old_path, vfs_path);
            break;
        }
        default:
            break;
        }
    });
```

`AssetRegistry::OnAssetDeleted`:

```cpp
void AssetRegistry::OnAssetDeleted(const VFSPath& path)
{
    auto handle = m_index.FindByPath(path);
    if (!handle.Valid()) return;
    AssetRecord* rec = m_index.Access(handle);
    if (!rec) return;

    // Fire cascade so dependents can respond (e.g. show placeholder)
    m_scratch.Clear();
    Core::Containers::Array<uuids::uuid> cascade;
    cascade.init(&m_scratch, 0, 16);
    m_graph.CollectCascade(rec->UUID, cascade, &m_scratch);

    if (m_reload_cb && !cascade.empty())
        m_reload_cb(std::span<const uuids::uuid>(cascade.Data(), cascade.size()));

    // Remove from registry
    Remove(rec->UUID);
}
```

`AssetRegistry::OnAssetRenamed`:

```cpp
void AssetRegistry::OnAssetRenamed(const VFSPath& old_path, const VFSPath& new_path)
{
    auto handle = m_index.FindByPath(old_path);
    if (!handle.Valid()) return;
    m_index.Rename(handle, new_path);
}
```

---

## 11. Migration Plan — AssetManager call sites

The migration is structured as four incremental phases. Each phase can be reviewed and merged
independently without breaking the existing import pipeline.

### Phase 1 — Add `AssetRegistry` to `AssetManager` (non-breaking)

Add `AssetRegistry* Registry = nullptr;` to `AssetManager`. Wire its initialization in
`AssetManager::Initialize`:

```cpp
// AssetManager.h — add field:
ZEngine::VFS::AssetRegistry* Registry = nullptr;

// AssetManager.cpp — Initialize:
static ZEngine::VFS::AssetRegistry s_registry;
s_registry.Initialize(&instance->Arena);
instance->Registry = &s_registry;
```

Nothing else changes. All existing `UUIDToHandle` / `HandleToUUID` paths continue to work.

### Phase 2 — Redirect `RegisterAsset` to write into both old maps and the registry

```cpp
// AssetManager.cpp — RegisterAsset (MODIFIED):
AssetManager::AssetHandle AssetManager::RegisterAsset(
    AssetType type, const uuids::uuid& uuid, uint32_t asset_id)
{
    AssetHandle h = CreateHandle(asset_id, type);

    // Existing maps — kept during migration
    Instance()->UUIDToHandle.insert({uuid, h});
    Instance()->HandleToUUID.insert({h, uuid});

    // NEW: registry
    if (Instance()->Registry != nullptr)
    {
        VFS::VFSPath path = {};      // filled in Phase 3
        VFS::MetaFileData meta = {}; // filled in Phase 3
        meta.AssetUUID = uuid;

        Instance()->Registry->RegisterLoaded(uuid, type, path, meta, h);
    }

    return h;
}
```

### Phase 3 — Pass `VFSPath` and `MetaFileData` through the importer pipeline

Extend `AssetImporterOutput` to carry `VFSPath` and `MetaFileData` (populated by the scanner
in Ticket 5). Then in `RegisterAsset`, pass them to `RegisterLoaded`.

Before:
```cpp
struct AssetImporterOutput
{
    const char* FilePath = nullptr;
    // ...
};
```

After:
```cpp
struct AssetImporterOutput
{
    const char*              FilePath = nullptr;
    VFS::VFSPath             VFSAssetPath = {};    // NEW
    VFS::MetaFileData        Meta         = {};    // NEW
    // ...
};
```

### Phase 4 — Remove `UUIDToHandle` / `HandleToUUID` from `AssetManager`

Once all call sites of `UUIDToHandle` and `HandleToUUID` have been redirected to
`AssetRegistry::FindByUUID` / `AssetRegistry::FindByPath`, remove the two maps from
`AssetManager`:

```cpp
// REMOVE from AssetManager:
Core::Containers::UnorderedHashMap<uuids::uuid, AssetHandle>  UUIDToHandle;
Core::Containers::UnorderedHashMap<AssetHandle, uuids::uuid>  HandleToUUID;
```

**Call site redirections in Phase 4:**

| Old pattern | New pattern |
|---|---|
| `AssetManager::Instance()->UUIDToHandle.at(id)` | `Registry->FindByUUID(id)->LegacyHandle` |
| `AssetManager::Instance()->HandleToUUID.at(h)` | `Registry->FindByUUID(uuid)->LegacyHandle == h` check |
| `UUIDToHandle.contains(id)` | `Registry->FindByUUID(id) != nullptr` |
| `GetAsset<T, uuids::uuid>(id)` template | unchanged (uses `UUIDToHandle` internally; redirect in Phase 4) |

### Migration validation checklist

After Phase 4 is complete, add a debug-mode assertion in `AssetManager::RegisterAsset` that
`UUIDToHandle` and `HandleToUUID` are never written to, then remove the assertion and the maps
in the final cleanup commit.

```
grep -r "UUIDToHandle\|HandleToUUID" ZEngine/ZEngine/  → zero results
```

---

## 12. Unit Tests

File: `ZEngine/tests/VFS/AssetRegistryTest.cpp`

Add `VFS/*.cpp` to the glob in `tests/CMakeLists.txt`:

```cmake
file(GLOB TEST_SOURCES
    Memory/*.cpp
    Containers/*.cpp
    Maths/*.cpp
    Misc/*.cpp
    VFS/*.cpp)    # covers MetaFileIOTest + AssetRegistryTest
```

All tests use `MemoryVFSContext` (from Ticket 5) and a stack-allocated `ArenaAllocator` of
sufficient size (4 MiB is ample for all registry tests).

### Test 1 — UUID lookup returns correct record

```cpp
TEST(AssetRegistry, UUIDLookupReturnsCorrectRecord)
{
    Core::Memory::ArenaAllocator arena;
    arena.Initialize(4 * 1024 * 1024);

    VFS::AssetRegistry registry;
    registry.Initialize(&arena);

    uuids::uuid uuid = uuids::uuid_random_generator{}();
    VFSPath     path = VFSPath::Parse("/project/mesh.glb").Value();

    VFS::MetaFileData meta{};
    meta.AssetUUID = uuid;

    auto result = registry.Register(uuid, Managers::AssetType::MESH, path, meta);
    ASSERT_TRUE(result.IsOk());

    const VFS::AssetRecord* rec = registry.FindByUUID(uuid);
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->UUID, uuid);
    EXPECT_EQ(rec->Type, Managers::AssetType::MESH);
    EXPECT_STREQ(rec->Path.CStr(), "/project/mesh.glb");
}
```

### Test 2 — VFSPath lookup returns correct record

```cpp
TEST(AssetRegistry, PathLookupReturnsCorrectRecord)
{
    Core::Memory::ArenaAllocator arena;
    arena.Initialize(4 * 1024 * 1024);

    VFS::AssetRegistry registry;
    registry.Initialize(&arena);

    uuids::uuid uuid = uuids::uuid_random_generator{}();
    VFSPath     path = VFSPath::Parse("/project/textures/diffuse.png").Value();

    VFS::MetaFileData meta{};
    meta.AssetUUID = uuid;

    registry.Register(uuid, Managers::AssetType::TEXTURE, path, meta);

    const VFS::AssetRecord* rec = registry.FindByPath(path);
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->UUID, uuid);
    EXPECT_EQ(rec->Type, Managers::AssetType::TEXTURE);
}
```

### Test 3 — Type query returns only matching records

```cpp
TEST(AssetRegistry, TypeQueryReturnsOnlyMatchingType)
{
    Core::Memory::ArenaAllocator arena;
    arena.Initialize(4 * 1024 * 1024);

    VFS::AssetRegistry registry;
    registry.Initialize(&arena);

    // Register 2 meshes and 1 texture
    for (int i = 0; i < 2; ++i)
    {
        uuids::uuid uuid = uuids::uuid_random_generator{}();
        char path_buf[64];
        snprintf(path_buf, sizeof(path_buf), "/project/mesh%d.glb", i);
        VFSPath     path = VFSPath::Parse(path_buf).Value();
        VFS::MetaFileData meta{}; meta.AssetUUID = uuid;
        registry.Register(uuid, Managers::AssetType::MESH, path, meta);
    }
    {
        uuids::uuid uuid = uuids::uuid_random_generator{}();
        VFSPath     path = VFSPath::Parse("/project/tex.png").Value();
        VFS::MetaFileData meta{}; meta.AssetUUID = uuid;
        registry.Register(uuid, Managers::AssetType::TEXTURE, path, meta);
    }

    VFS::AssetRegistry::QueryResult meshes = registry.Query(
        {.Type = Managers::AssetType::MESH}, &arena);
    EXPECT_EQ(meshes.Count, 2u);

    VFS::AssetRegistry::QueryResult textures = registry.Query(
        {.Type = Managers::AssetType::TEXTURE}, &arena);
    EXPECT_EQ(textures.Count, 1u);
}
```

### Test 4 — Dependency registration, forward and reverse lookup

```cpp
TEST(AssetRegistry, DependencyRegistrationForwardAndReverse)
{
    Core::Memory::ArenaAllocator arena;
    arena.Initialize(4 * 1024 * 1024);

    VFS::AssetRegistry registry;
    registry.Initialize(&arena);

    uuids::uuid mesh_uuid = uuids::uuid_random_generator{}();
    uuids::uuid mat_uuid  = uuids::uuid_random_generator{}();
    uuids::uuid tex_uuid  = uuids::uuid_random_generator{}();

    auto reg = [&](uuids::uuid id, Managers::AssetType t, const char* p) {
        VFSPath path = VFSPath::Parse(p).Value();
        VFS::MetaFileData meta{}; meta.AssetUUID = id;
        registry.Register(id, t, path, meta);
    };
    reg(mesh_uuid, Managers::AssetType::MESH,     "/p/mesh.glb");
    reg(mat_uuid,  Managers::AssetType::MATERIAL, "/p/mat.glb");
    reg(tex_uuid,  Managers::AssetType::TEXTURE,  "/p/tex.png");

    // mesh → mat → tex
    EXPECT_TRUE(registry.AddDependency(mesh_uuid, mat_uuid));
    EXPECT_TRUE(registry.AddDependency(mat_uuid,  tex_uuid));

    // Forward: mesh depends on mat
    Core::Containers::Array<uuids::uuid> deps;
    deps.init(&arena, 0, 8);
    // Access via DependencyGraph directly (through registry's graph accessor if exposed,
    // or via the registry's internal graph — white-box test)
    // Here we verify indirectly via cascade:
    Core::Containers::Array<uuids::uuid> cascade;
    cascade.init(&arena, 0, 8);

    // tex changes → cascade: tex, mat, mesh
    // (requires access to graph; expose a const getter if needed)
    // For a grey-box test, trigger OnAssetModified and capture the callback:
    std::vector<uuids::uuid> fired;
    registry.SetHotReloadCallback([&](std::span<const uuids::uuid> c) {
        for (auto& u : c) fired.push_back(u);
    });

    VFSPath tex_path = VFSPath::Parse("/p/tex.png").Value();
    registry.OnAssetModified(tex_path);

    ASSERT_EQ(fired.size(), 3u);
    EXPECT_EQ(fired[0], tex_uuid);   // root first
    // mat and mesh in BFS order — both must be present
    EXPECT_NE(std::find(fired.begin(), fired.end(), mat_uuid),  fired.end());
    EXPECT_NE(std::find(fired.begin(), fired.end(), mesh_uuid), fired.end());
}
```

### Test 5 — Cascade walk: diamond dependency produces each node once

```cpp
TEST(AssetRegistry, CascadeDiamondNoDuplicates)
{
    // Diamond: mesh → matA → tex
    //          mesh → matB → tex
    // Modifying tex should cascade to: tex, matA, matB, mesh (each exactly once)

    Core::Memory::ArenaAllocator arena;
    arena.Initialize(4 * 1024 * 1024);

    VFS::AssetRegistry registry;
    registry.Initialize(&arena);

    auto mk = [&](const char* p, Managers::AssetType t) -> uuids::uuid {
        uuids::uuid id = uuids::uuid_random_generator{}();
        VFSPath path = VFSPath::Parse(p).Value();
        VFS::MetaFileData meta{}; meta.AssetUUID = id;
        registry.Register(id, t, path, meta);
        return id;
    };

    uuids::uuid tex  = mk("/p/tex.png",  Managers::AssetType::TEXTURE);
    uuids::uuid matA = mk("/p/matA.glb", Managers::AssetType::MATERIAL);
    uuids::uuid matB = mk("/p/matB.glb", Managers::AssetType::MATERIAL);
    uuids::uuid mesh = mk("/p/mesh.glb", Managers::AssetType::MESH);

    registry.AddDependency(matA, tex);
    registry.AddDependency(matB, tex);
    registry.AddDependency(mesh, matA);
    registry.AddDependency(mesh, matB);

    std::vector<uuids::uuid> fired;
    registry.SetHotReloadCallback([&](std::span<const uuids::uuid> c) {
        for (auto& u : c) fired.push_back(u);
    });

    registry.OnAssetModified(VFSPath::Parse("/p/tex.png").Value());

    // Exactly 4 unique UUIDs; no duplicates
    ASSERT_EQ(fired.size(), 4u);
    std::sort(fired.begin(), fired.end());
    EXPECT_EQ(std::unique(fired.begin(), fired.end()), fired.end());
}
```

### Test 6 — Duplicate registration returns DuplicateUUID error

```cpp
TEST(AssetRegistry, DuplicateUUIDRegistrationFails)
{
    Core::Memory::ArenaAllocator arena;
    arena.Initialize(4 * 1024 * 1024);

    VFS::AssetRegistry registry;
    registry.Initialize(&arena);

    uuids::uuid uuid = uuids::uuid_random_generator{}();
    VFSPath     path = VFSPath::Parse("/p/mesh.glb").Value();
    VFS::MetaFileData meta{}; meta.AssetUUID = uuid;

    auto r1 = registry.Register(uuid, Managers::AssetType::MESH, path, meta);
    ASSERT_TRUE(r1.IsOk());

    // Same UUID, different path
    VFSPath path2 = VFSPath::Parse("/p/other.glb").Value();
    auto r2 = registry.Register(uuid, Managers::AssetType::MESH, path2, meta);
    EXPECT_FALSE(r2.IsOk());
    EXPECT_EQ(r2.Error, VFS::RegisterError::DuplicateUUID);

    // Same path, different UUID
    uuids::uuid uuid2 = uuids::uuid_random_generator{}();
    VFS::MetaFileData meta2{}; meta2.AssetUUID = uuid2;
    auto r3 = registry.Register(uuid2, Managers::AssetType::MESH, path, meta2);
    EXPECT_FALSE(r3.IsOk());
    EXPECT_EQ(r3.Error, VFS::RegisterError::DuplicatePath);

    // Total count still 1
    EXPECT_EQ(registry.RecordCount(), 1u);
}
```

### Test 7 — Remove erases all indices and dependency edges

```cpp
TEST(AssetRegistry, RemovePurgesAllIndices)
{
    Core::Memory::ArenaAllocator arena;
    arena.Initialize(4 * 1024 * 1024);

    VFS::AssetRegistry registry;
    registry.Initialize(&arena);

    uuids::uuid mesh_uuid = uuids::uuid_random_generator{}();
    uuids::uuid mat_uuid  = uuids::uuid_random_generator{}();

    auto reg = [&](uuids::uuid id, Managers::AssetType t, const char* p) {
        VFSPath path = VFSPath::Parse(p).Value();
        VFS::MetaFileData meta{}; meta.AssetUUID = id;
        registry.Register(id, t, path, meta);
    };
    reg(mesh_uuid, Managers::AssetType::MESH,     "/p/mesh.glb");
    reg(mat_uuid,  Managers::AssetType::MATERIAL, "/p/mat.glb");
    registry.AddDependency(mesh_uuid, mat_uuid);

    EXPECT_EQ(registry.RecordCount(), 2u);
    EXPECT_EQ(registry.EdgeCount(),   1u);

    registry.Remove(mat_uuid);

    EXPECT_EQ(registry.RecordCount(), 1u);
    EXPECT_EQ(registry.EdgeCount(),   0u);   // edge purged by RemoveAsset
    EXPECT_EQ(registry.FindByUUID(mat_uuid), nullptr);
    EXPECT_EQ(registry.FindByPath(VFSPath::Parse("/p/mat.glb").Value()), nullptr);

    // Type query should no longer include mat
    VFS::AssetRegistry::QueryResult mats = registry.Query(
        {.Type = Managers::AssetType::MATERIAL}, &arena);
    EXPECT_EQ(mats.Count, 0u);
}
```

### Test 8 — Query by name substring

```cpp
TEST(AssetRegistry, QueryByNameSubstring)
{
    Core::Memory::ArenaAllocator arena;
    arena.Initialize(4 * 1024 * 1024);

    VFS::AssetRegistry registry;
    registry.Initialize(&arena);

    auto reg = [&](const char* p, Managers::AssetType t) {
        uuids::uuid id = uuids::uuid_random_generator{}();
        VFSPath path = VFSPath::Parse(p).Value();
        VFS::MetaFileData meta{}; meta.AssetUUID = id;
        registry.Register(id, t, path, meta);
    };
    reg("/p/tank_body.glb",    Managers::AssetType::MESH);
    reg("/p/tank_turret.glb",  Managers::AssetType::MESH);
    reg("/p/jeep.glb",         Managers::AssetType::MESH);
    reg("/p/tank_albedo.png",  Managers::AssetType::TEXTURE);

    // Name substring "tank" → 3 results (tank_body, tank_turret, tank_albedo)
    auto result = registry.Query({.NameLike = "tank"}, &arena);
    EXPECT_EQ(result.Count, 3u);

    // Name + type filter → only mesh tanks (2)
    auto result2 = registry.Query(
        {.Type = Managers::AssetType::MESH, .NameLike = "tank"}, &arena);
    EXPECT_EQ(result2.Count, 2u);

    // Name not found → 0
    auto result3 = registry.Query({.NameLike = "helicopter"}, &arena);
    EXPECT_EQ(result3.Count, 0u);
}
```

### Test 9 — Query by extension

```cpp
TEST(AssetRegistry, QueryByExtension)
{
    Core::Memory::ArenaAllocator arena;
    arena.Initialize(4 * 1024 * 1024);

    VFS::AssetRegistry registry;
    registry.Initialize(&arena);

    auto reg = [&](const char* p, Managers::AssetType t) {
        uuids::uuid id = uuids::uuid_random_generator{}();
        VFSPath path = VFSPath::Parse(p).Value();
        VFS::MetaFileData meta{}; meta.AssetUUID = id;
        registry.Register(id, t, path, meta);
    };
    reg("/p/a.glb",    Managers::AssetType::MESH);
    reg("/p/b.gltf",   Managers::AssetType::MESH);
    reg("/p/c.png",    Managers::AssetType::TEXTURE);
    reg("/p/d.PNG",    Managers::AssetType::TEXTURE);   // uppercase extension

    auto glb  = registry.Query({.Ext = ".glb"},  &arena);
    auto png  = registry.Query({.Ext = ".png"},  &arena);
    auto gltf = registry.Query({.Ext = ".gltf"}, &arena);

    EXPECT_EQ(glb.Count,  1u);
    EXPECT_EQ(gltf.Count, 1u);
    EXPECT_EQ(png.Count,  2u);   // case-insensitive; .png matches .PNG
}
```

### Test 10 — SetState transitions propagate; cascade fires only Stale assets

```cpp
TEST(AssetRegistry, StateTransitionAndCascadeMarksStale)
{
    Core::Memory::ArenaAllocator arena;
    arena.Initialize(4 * 1024 * 1024);

    VFS::AssetRegistry registry;
    registry.Initialize(&arena);

    uuids::uuid tex  = uuids::uuid_random_generator{}();
    uuids::uuid mat  = uuids::uuid_random_generator{}();

    auto reg = [&](uuids::uuid id, Managers::AssetType t, const char* p) {
        VFSPath path = VFSPath::Parse(p).Value();
        VFS::MetaFileData meta{}; meta.AssetUUID = id;
        registry.Register(id, t, path, meta);
    };
    reg(tex, Managers::AssetType::TEXTURE,  "/p/tex.png");
    reg(mat, Managers::AssetType::MATERIAL, "/p/mat.glb");
    registry.AddDependency(mat, tex);

    // Initial state: both Registered
    EXPECT_EQ(registry.FindByUUID(tex)->State, VFS::AssetState::Registered);
    EXPECT_EQ(registry.FindByUUID(mat)->State, VFS::AssetState::Registered);

    // Simulate load
    registry.SetState(tex, VFS::AssetState::Loaded);
    registry.SetState(mat, VFS::AssetState::Loaded);

    // Trigger hot-reload on tex
    int cb_count = 0;
    registry.SetHotReloadCallback([&](std::span<const uuids::uuid>) { ++cb_count; });
    registry.OnAssetModified(VFSPath::Parse("/p/tex.png").Value());

    EXPECT_EQ(cb_count, 1);

    // Both should now be Stale
    EXPECT_EQ(registry.FindByUUID(tex)->State, VFS::AssetState::Stale);
    EXPECT_EQ(registry.FindByUUID(mat)->State, VFS::AssetState::Stale);
}
```

---

## 13. Deliverables Checklist

```
[ ] ZEngine/VFS/Registry/AssetRecord.h
      AssetState enum (6 values)
      AssetRecord struct (UUID, Type, Path, Name[128], Meta, LegacyHandle, State)

[ ] ZEngine/VFS/Registry/AssetIndex.h
      UUIDHasher struct (FNV-1a over uuid bytes)
      VFSPathHasher struct (delegates to VFSPath::Hash())
      RegisterError enum (4 values)
      RegisterResult struct
      AssetIndex class (MAX_ASSETS = 65536)
        Initialize(arena)
        Register(uuid, type, path, meta) → RegisterResult
        Update(handle, meta, state) → bool
        SetState(handle, state) → bool
        Remove(handle) → bool
        Rename(handle, new_path) → bool
        Access(handle) → AssetRecord* (2 overloads)
        FindByUUID → Handle
        FindByPath → Handle
        FindByType → span<Handle>
        FindByNameSubstring → uint32_t
        IsLive(handle) → bool
        ForEach(visitor)
        Count() / Capacity()

[ ] ZEngine/VFS/Registry/AssetIndex.cpp
      Initialize: m_handles.Initialize; m_by_type[i] pre-alloc
      Register: unique_lock; duplicate checks; slot alloc; 4 index writes
      Remove: unique_lock; 3 index erases; swap-and-pop from type bucket; m_handles.Remove
      Rename: unique_lock; erase old path index; update record; insert new path index
      FindByUUID / FindByPath: shared_lock; hash lookup
      FindByType: shared_lock; return span
      FindByNameSubstring: shared_lock; linear scan
      ForEach: shared_lock; iterate HandleManager slots 0..Head

[ ] ZEngine/VFS/Registry/DependencyGraph.h
      AdjacencyList struct (inline 8 + arena overflow)
      DependencyGraph class
        Initialize(arena)
        AddEdge / RemoveEdge / RemoveAsset
        CopyDependencies / CopyDependents / HasEdge
        CollectCascade(changed, out, scratch)
        EdgeCount / NodeCount

[ ] ZEngine/VFS/Registry/DependencyGraph.cpp
      AdjacencyList::Contains / Append / Erase
      AddEdge: unique_lock; dual insert (forward + reverse); ++m_edges
      RemoveEdge: unique_lock; dual erase; --m_edges
      RemoveAsset: unique_lock; forward + reverse cleanup; edge counter
      CollectCascade: shared_lock; BFS with inline visited table; populate out

[ ] ZEngine/VFS/Registry/AssetRegistry.h
      HotReloadCallback typedef
      QueryFilter struct (Type, NameLike, Ext, State)
      QueryResult struct (Handles array, Count)
      AssetRegistry class
        Initialize(arena, scratch_size=65536)
        Register / RegisterLoaded / Remove(uuid) / Remove(handle)
        AddDependency / RemoveDependency
        SetState(uuid, state) / UpdateMeta
        FindByUUID / FindByPath (2 overloads each)
        Query(filter, arena) → QueryResult
        SetHotReloadCallback(cb)
        OnAssetModified / OnAssetDeleted / OnAssetRenamed
        OnScanFileDiscovered
        RecordCount / EdgeCount
        DumpGraphDOT(buf, len) → uint32_t

[ ] ZEngine/VFS/Registry/AssetRegistry.cpp
      Initialize: m_scratch = arena->CreateSubArena; m_index.Initialize; m_graph.Initialize
      Register / RegisterLoaded: delegate to m_index; set LegacyHandle if provided
      Remove(uuid): m_graph.RemoveAsset → m_index.Remove
      OnAssetModified: FindByPath; SetState(Stale); CollectCascade; mark all Stale; fire cb
      OnAssetDeleted: cascade fire; Remove
      OnAssetRenamed: Rename in index only
      OnScanFileDiscovered: MetaFileIO::Read; Update or Register
      Query: type-bucket fast path or ForEach full scan; PassesFilter predicate
      InferTypeFromExtension: extension → AssetType mapping
      ExtensionMatches: case-insensitive suffix compare
      DumpGraphDOT: iterate m_graph, write "digraph{...}" DOT syntax

[ ] VFSScanner.h — add AssetRegistry* m_registry and SetAssetRegistry()
[ ] VFSScanner.cpp — call registry->OnScanFileDiscovered after MetaFileIO::GetOrCreate

[ ] VFSContext.cpp / Editor wiring — extend Watch callback to call
    OnAssetModified / OnAssetDeleted / OnAssetRenamed

[ ] AssetManager.h — add AssetRegistry* Registry field
[ ] AssetManager.cpp
    Phase 1: Initialize s_registry in AssetManager::Initialize
    Phase 2: RegisterAsset writes to both old maps and registry (dual-write)
    Phase 3: AssetImporterOutput extended with VFSPath + MetaFileData fields

[ ] tests/VFS/AssetRegistryTest.cpp — 10 tests:
    1.  UUIDLookupReturnsCorrectRecord
    2.  PathLookupReturnsCorrectRecord
    3.  TypeQueryReturnsOnlyMatchingType
    4.  DependencyRegistrationForwardAndReverse
    5.  CascadeDiamondNoDuplicates
    6.  DuplicateUUIDRegistrationFails
    7.  RemovePurgesAllIndices
    8.  QueryByNameSubstring
    9.  QueryByExtension
    10. StateTransitionAndCascadeMarksStale

[ ] Manual smoke tests:
    - Start editor, observe all assets registered (log RecordCount > 0)
    - Modify a texture file in Finder → watcher fires → cascade logged for dependent materials
    - Delete a material → remove propagates, dependent mesh shows placeholder state
    - Rename a mesh file → FindByPath with old path returns null; new path returns record
    - Query "tank" in asset browser → AssetRegistry::Query used; no directory_iterator

[ ] Verify grep -r "UUIDToHandle\|HandleToUUID" ZEngine/ZEngine/ after Phase 4 → zero results
```
