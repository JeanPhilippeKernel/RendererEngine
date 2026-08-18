# Import Pipeline — Asset Import Coordination

**Priority:** P3  
**Status:** Mostly Implemented — ImportCoordinator, ImportQueue, ImportJob, ImportPriority all live; DependencyGraph and VFSScanner/FileWatcher wiring remain design  
**Depends on:** `vfs-ticket6-asset-registry.md`, `actor-ecs-architecture.md`  
**Blocks:** `animation-system.md` (AssimpImporter end-to-end), `render-resource-manager.md`

### What is already implemented (as of PR #597)

- `IAssetImporter` interface — `CanImport(ext)` + `Import(ctx, path, meta)` — live in `ZEngine/Importers/IAssetImporter.h`
- `AssimpImporter::ImportFile()` — editor path; cooks .fbx/.obj to `.zemesh` + `.zematerial` on disk, reports progress via `ImportCompleteCallback` / `ImportProgressCallback` / `ImportErrorCallback` / `ImportLogCallback` type aliases
- `GltfImporter::ImportFile()` — same editor path for .glb/.gltf via fastgltf; extracts embedded textures to `Assets/Textures/`
- `.zematerial` serialized as JSON (nlohmann/json); `.zetextures` eliminated — texture VFS paths inline in `.zematerial`
- `MetaFileData::SourcePath` written after every successful import for future reimport
- Lazy cook on save — `EditorSceneSerializer` detects in-memory meshes with no artifact and cooks before writing `.zescene`
- Named callback aliases: `ImportCompleteCallback`, `ImportProgressCallback`, `ImportErrorCallback`, `ImportLogCallback` in `AssetTypes.h`
- `ImportCoordinator` + `ImportQueue` — priority-driven queue with deduplication; `Tick()`, `Enqueue()`, `EnqueueBatch()`, `GetProgress()`
- `ImportJob` + `ImportPriority` — job struct with `DiagnosticMessage[256]`, `RequeueCount`, priority enum
- `EnvironmentMapImporter` — implements `IAssetImporter` for .hdr/.exr environment map files

### What is still design (this document)

- VFSScanner → `EnqueueBatch` wiring
- FileWatcher → `Enqueue(Immediate)` wiring

Note: ImportCoordinator, ImportQueue, ImportJob, and ImportPriority are implemented in `ZEngine/ZEngine/Importers/`. The DependencyGraph and VFSScanner/FileWatcher integration remain design.

**Goal**: Implement a priority-driven, thread-safe asset import pipeline inside
`ZEngine::Importers` that routes any source file to the correct importer, tracks progress
for editor UI, enforces dependency ordering (textures before materials before meshes), and
integrates cleanly with the existing VFS layer — all without exceptions and without
`new`/`delete` in the hot path.

---

## 1. `IAssetImporter` Interface

Every concrete importer (Assimp, STB-image, a custom shader compiler, etc.) implements
this interface. The interface is intentionally minimal: two pure-virtual methods and no
state.

```cpp
// ZEngine/Importers/IAssetImporter.h
#pragma once
#include <VFS/IVFSContext.h>
#include <VFS/VFSPath.h>
#include <VFS/VFSResult.h>
#include <VFS/Meta/MetaFileData.h>

namespace ZEngine::Importers {

    class IAssetImporter {
    public:
        virtual ~IAssetImporter() = default;

        // Returns true if this importer handles files with the given extension.
        // Extension is passed without the leading dot (e.g. "png", "fbx", "glsl").
        virtual bool CanImport(const char* extension) const = 0;

        // Performs the full import. Reads source data through ctx, applies meta
        // overrides (uuid, import settings), and writes the cooked asset back into ctx.
        // Returns VFSResult<void>; on failure the error string is populated.
        virtual VFS::VFSResult<void> Import(
            VFS::IVFSContext&        ctx,
            const VFS::VFSPath&      path,
            const VFS::MetaFileData& meta) = 0;
    };

}  // namespace ZEngine::Importers
```

**Design notes**:

- `CanImport` is called once per importer during routing (see Section 4 `Route()`). It
  must be stateless and cheap — a string comparison, nothing more.
- `Import` receives a fully resolved `MetaFileData` reference. The importer must never
  generate a UUID internally; it must read `meta.UUID`. This is the primary change from
  the legacy Assimp path (see Section 6).
- `VFS::VFSResult<void>` carries either success or an error string without throwing. The
  coordinator checks the result and records failures in the registry (Section 7).
- No state lives on the importer. A single `AssimpImporter` instance handles every `.fbx`
  and `.obj` asset concurrently via the thread pool. All per-import scratch state is on
  the stack or in pool-allocated buffers obtained from `IVFSContext`.
- The interface deliberately avoids dependency injection of `AssetRegistry`. The
  coordinator updates the registry after `Import` returns; the importer only cooks data.

---

## 2. `ImportJob` and `ImportPriority`

`ImportJob` is the unit of work that flows through the queue. It is a plain aggregate with
no virtual methods and no heap-allocated members except the `std::function` callback.

```cpp
// ZEngine/Importers/ImportJob.h
#pragma once
#include <VFS/VFSPath.h>
#include <VFS/Meta/MetaFileData.h>
#include <functional>
#include <cstdint>

namespace ZEngine::Importers {

    enum class ImportPriority : uint8_t {
        Background = 0,   // deferred processing; runs when the frame budget allows
        Normal     = 1,   // standard editor import triggered by VFSScanner
        Immediate  = 2    // user-initiated or dependency-resolved retry; runs next Tick
    };

    struct ImportCallback {
        void* Context = nullptr;
        void (*Fn)(void*, bool success) = nullptr;
    };

    struct ImportJob {
        VFS::VFSPath      Path;
        VFS::MetaFileData Meta;
        ImportPriority    Priority     = ImportPriority::Normal;
        ImportCallback    Callback;
        uint32_t          RequeueCount = 0;   // incremented on dependency stall; cap at 3
        char              DiagnosticMessage[256] = {};  // populated on failure
    };

}  // namespace ZEngine::Importers
```

**Design notes**:

- `ImportPriority` is a `uint8_t` enum so it fits in the heap comparator without padding
  waste. The three levels cover all observed editor workflows: background thumbnail
  generation (`Background`), normal project-open scan (`Normal`), and user double-click
  or manual re-import (`Immediate`).
- `RequeueCount` is the circular dependency guard. Every time a job is re-inserted
  because `DependenciesSatisfied` returns false, `RequeueCount` is incremented before
  re-enqueue. At `RequeueCount == 3` the coordinator stops requeueing and marks the asset
  `Failed` (see Section 8).
- `DiagnosticMessage[256]` is a fixed-size char array. No `std::string`, no allocation.
  The importer or coordinator writes the null-terminated message via `snprintf`. The
  editor reads it when displaying failure details.
- `ImportCallback` is a C-style struct `{ void* Context; void (*Fn)(void*, bool); }` — zero allocation, consistent with `ImportCompleteCallback` and other engine callback conventions. Callbacks are optional; the coordinator checks `if (job.Callback.Fn)` before invoking. For batch imports from `VFSScanner`, callbacks are typically null and progress is tracked via atomics (Section 5).
- `VFS::MetaFileData` is stored by value because jobs live in the queue between frames and
  the on-disk meta file may be rewritten while the job waits. The copy is made at enqueue
  time.

---

## 3. `ImportQueue` — Thread-Safe Priority Queue with Deduplication

The queue is a max-heap ordered by `ImportPriority`. A secondary hash map provides O(1)
deduplication by `VFSPath` hash so that rapidly-arriving filesystem events (e.g. a file
saved multiple times in quick succession) do not enqueue duplicate jobs.

```cpp
// ZEngine/Importers/ImportQueue.h
#pragma once
#include <Importers/ImportJob.h>
#include <Core/Containers/Array.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <mutex>
#include <cstdint>

namespace ZEngine::Importers {

    class ImportQueue {
    public:
        // Inserts job into the heap. If a job for the same path already exists and the
        // new job has higher priority, the existing entry is upgraded in-place.
        // If priority is equal or lower, the call is a no-op.
        void     Enqueue(ImportJob job);

        // Removes and returns the highest-priority job. Returns false if queue is empty.
        bool     TryPop(ImportJob& out_job);

        bool     IsEmpty() const;
        uint32_t Size()    const;

        // Returns true if a job for path is currently waiting in the queue.
        bool     Contains(const VFS::VFSPath& path) const;

    private:
        mutable std::mutex                                     m_mtx;
        Core::Containers::Array<ImportJob>                     m_heap;  // max-heap by Priority
        Core::Containers::UnorderedHashMap<uint64_t, uint32_t> m_index; // path hash → heap pos
    };

}  // namespace ZEngine::Importers
```

**Implementation notes**:

**`Enqueue(job)`**:
1. Acquire `m_mtx`.
2. Compute `hash = job.Path.Hash()`.
3. If `m_index.Contains(hash)`:
   - Look up `pos = m_index[hash]`.
   - If `job.Priority > m_heap[pos].Priority`, overwrite `m_heap[pos].Priority = job.Priority`
     and sift-up from `pos`. Update `m_index` entries displaced by the sift. Return.
   - Otherwise return (lower-or-equal priority, no-op).
4. Otherwise: append `job` to `m_heap`, record `m_index[hash] = m_heap.Size() - 1`,
   sift-up from the last position, updating `m_index` for every element swapped.

**`TryPop(out_job)`**:

```
TryPop(out_job):
  lock(m_mtx)
  if m_heap is empty: return false

  out_job = m_heap[0]
  m_index.Erase(out_job.Path.Hash())  // ← remove the popped entry from the index map

  if m_heap.Size() > 1:
    ImportJob back = m_heap.Back()
    m_heap.Pop()                          // remove back element

    m_heap[0] = back
    m_index[back.Path.Hash()] = 0        // ← set new position to 0

    // Sift-down: swap with smallest-priority child, updating m_index at each swap
    pos = 0
    while true:
      smallest = pos
      left  = 2 * pos + 1
      right = 2 * pos + 2
      if left  < m_heap.Size() && m_heap[left].Priority  > m_heap[smallest].Priority: smallest = left
      if right < m_heap.Size() && m_heap[right].Priority > m_heap[smallest].Priority: smallest = right
      if smallest == pos: break
      swap(m_heap[pos], m_heap[smallest])
      m_index[m_heap[pos].Path.Hash()]      = pos       // ← update moved element
      m_index[m_heap[smallest].Path.Hash()] = smallest  // ← update moved element
      pos = smallest
  else:
    m_heap.Pop()

  return true
```

**`Contains(path)`**:
1. Acquire `m_mtx` (shared lock acceptable in future; `std::shared_mutex` upgrade path
   is non-breaking because `Contains` is read-only).
2. Return `m_index.Contains(path.Hash())`.

**Heap invariant maintenance**: every sift-up or sift-down swap must also update
`m_index` for the displaced element. This keeps the `m_index[hash] → heap position`
mapping correct at all times. The cost is O(log N) updates to `m_index` per
`Enqueue`/`TryPop` — acceptable for queue sizes in the hundreds.

**Hash collision policy**: `VFSPath::Hash()` returns a `uint64_t` FNV-1a hash. Two
distinct paths sharing the same hash would be treated as duplicates. Given 64-bit FNV-1a
and typical project sizes (< 100 000 assets), the probability is negligible. A future
`VFSPath` equality check at lookup can guard against the theoretical collision at the cost
of one extra string compare.

---

## 4. `ImportCoordinator`

`ImportCoordinator` is the central dispatch object. It owns the queue, all registered
importers, and the in-flight progress counters. `Tick()` is called once per editor frame
to drain up to N jobs (configurable; default 4) to the engine thread pool.

```cpp
// ZEngine/Importers/ImportCoordinator.h
#pragma once
#include <Importers/ImportQueue.h>
#include <Importers/IAssetImporter.h>
#include <Core/Containers/Array.h>
#include <atomic>

namespace ZEngine::Importers {

    struct ImportProgress {
        uint32_t Total;
        uint32_t Completed;
        uint32_t Failed;
    };

    class ImportCoordinator {
    public:
        // Registers a concrete importer. Ownership is not transferred; caller must ensure
        // lifetime exceeds the coordinator. Importers are checked in registration order.
        void RegisterImporter(IAssetImporter* importer);

        // Enqueues a single asset for import. Reads meta from disk via MetaFileIO.
        // No-op if a job for the same path is already queued at equal or higher priority.
        // Returns the asset UUID resolved at enqueue time.
        uuids::uuid Enqueue(VFS::VFSPath    path,
                            ImportPriority  priority = ImportPriority::Normal,
                            ImportCallback  cb       = {});

        // Enqueues multiple paths at Normal priority. Suitable for VFSScanner batch.
        void EnqueueBatch(const Core::Containers::Array<VFS::VFSPath>& paths);

        // Called once per frame. Pops up to m_jobs_per_tick jobs whose dependencies are
        // satisfied and dispatches each to the ThreadPool. Jobs whose dependencies are
        // not yet satisfied are re-inserted with RequeueCount incremented.
        void Tick();

        // Returns a snapshot of current import counters. Thread-safe (atomic reads).
        ImportProgress GetProgress() const;

    private:
        Core::Containers::Array<IAssetImporter*> m_importers;
        ImportQueue                              m_queue;
        std::atomic<uint32_t>                    m_completed{0};
        std::atomic<uint32_t>                    m_failed{0};
        std::atomic<uint32_t>                    m_total{0};
        uint32_t                                 m_jobs_per_tick = 4;

        // Returns the first importer that CanImport(ext), or nullptr.
        IAssetImporter* Route(const char* ext) const;

        // Checks DependencyGraph to verify all upstream assets are AssetStatus::Ready.
        bool DependenciesSatisfied(const VFS::VFSPath& path) const;

        // Extracts the extension from path (without dot) into out_ext[16].
        static void ExtractExtension(const VFS::VFSPath& path, char out_ext[16]);
    };

}  // namespace ZEngine::Importers
```

**`Enqueue(path, priority, cb)` implementation**:
1. Read `MetaFileData meta = MetaFileIO::Read(path)`. If the `.meta` file does not exist,
   call `MetaFileIO::GenerateDefault(path)` to create it (assigns a fresh UUID, default
   import settings) and read again.
2. Construct `ImportJob{path, meta, priority, cb}`.
3. Call `m_queue.Enqueue(std::move(job))`.
4. Increment `m_total` with `fetch_add(1, std::memory_order_relaxed)`.

**`Tick()` implementation**:
1. For `i` in `[0, m_jobs_per_tick)`:
   a. `ImportJob job; if (!m_queue.TryPop(job)) break;`
   b. `if (!DependenciesSatisfied(job.Path))`:
      - If `job.RequeueCount >= 3`: write `"Circular dependency or missing upstream asset"`
        into `job.DiagnosticMessage`, call `AssetRegistry::SetStatus(job.Meta.UUID, AssetStatus::Failed)`,
        increment `m_failed`, decrement `m_total`, invoke `job.Callback` with `false`. Continue.
      - Otherwise: `job.RequeueCount++; m_queue.Enqueue(std::move(job));` Continue.
   c. Route: `ExtractExtension(job.Path, ext); IAssetImporter* imp = Route(ext);`
   d. If `imp == nullptr`: write `"No importer for extension"` into `job.DiagnosticMessage`,
      mark `Failed`, increment `m_failed`, decrement `m_total`, invoke callback. Continue.
   e. Dispatch to `ThreadPool::Submit([imp, job, this]() mutable { ... })`. Inside the lambda:
      - Call `VFS::VFSResult<void> result = imp->Import(ctx, job.Path, job.Meta);`
      - If success: `AssetRegistry::SetStatus(job.Meta.UUID, AssetStatus::Ready); m_completed.fetch_add(1);`
      - If failure: copy error into `job.DiagnosticMessage` via `snprintf`,
        `AssetRegistry::SetStatus(job.Meta.UUID, AssetStatus::Failed); m_failed.fetch_add(1);`
      - `m_total.fetch_sub(1, std::memory_order_relaxed);`
      - `if (job.Callback) job.Callback(job.Path, success);`

**`Route(ext)` implementation**:
- Linear scan over `m_importers`. Return the first `imp` where `imp->CanImport(ext) == true`.
- Linear scan is correct and fast for the expected importer count (< 20). No hash map
  needed; the scan runs O(20) per dispatched job, not per frame.

---

## 5. Progress Reporting

The editor progress bar queries `ImportCoordinator::GetProgress()` each frame.

```cpp
ImportProgress ImportCoordinator::GetProgress() const {
    return ImportProgress {
        .Total     = m_total.load(std::memory_order_relaxed),
        .Completed = m_completed.load(std::memory_order_relaxed),
        .Failed    = m_failed.load(std::memory_order_relaxed),
    };
}
```

**Design notes**:

- All three counters are `std::atomic<uint32_t>`. `GetProgress()` reads them with
  `memory_order_relaxed` — a consistent snapshot is not required; the editor bar updates
  every frame and momentary inaccuracy is invisible to the user.
- `Total` is incremented at `Enqueue` time and decremented when a job is finalized
  (success, failure, or cycle abort). This makes `Total - Completed - Failed` the count
  of in-flight or queued jobs, which the editor can display as "remaining".
- No mutex is held during `GetProgress()`. The three atomic reads are not jointly atomic,
  meaning a transient `Completed > Total` is theoretically possible in a race. This is
  acceptable; the UI clamps displayed values to `[0, 100]%`.
- For a precise end-of-import notification (e.g. to trigger a post-import script),
  `Tick()` checks `m_total.load() == 0` after its dispatch loop and fires a registered
  `OnBatchComplete` callback if present. This callback is not part of the v1 API but
  the hook site is reserved.

---

## 6. `AssimpImporter` Migration

**Before** — UUID generated ad-hoc inside the importer, ignoring meta:

```cpp
// Legacy AssimpImporter.cpp (before)
VFS::VFSResult<void> AssimpImporter::Import(
    VFS::IVFSContext& ctx,
    const VFS::VFSPath& path,
    const VFS::MetaFileData& /*meta*/)   // meta ignored
{
    UUID id = UUID::Generate();           // ← ad-hoc, changes every import
    AssetRegistry::Register(id, path);
    // ... load mesh data ...
    return VFS::VFSResult<void>::Ok();
}
```

**After** — UUID read from the `meta` parameter that `ImportCoordinator` populated from
the `.meta` file via `MetaFileIO`:

```cpp
// Updated AssimpImporter.cpp (after)
VFS::VFSResult<void> AssimpImporter::Import(
    VFS::IVFSContext& ctx,
    const VFS::VFSPath& path,
    const VFS::MetaFileData& meta)       // meta is now authoritative
{
    const UUID id = meta.UUID;           // ← stable identity from .meta file
    AssetRegistry::Register(id, path);
    // ... load mesh data ...
    return VFS::VFSResult<void>::Ok();
}
```

**Key change**: remove the `UUID::Generate()` call and replace it with `meta.UUID`. The
`.meta` file is created by `MetaFileIO::GenerateDefault` on first import and reused on
every subsequent import, so the UUID is stable across reimport, project reload, and
version control.

**Impact on other importers**: every importer that previously called `UUID::Generate()`
internally receives the same fix. A project-wide search for `UUID::Generate()` inside
`Importers/` should yield zero results after the migration.

---

## 7. Error Handling

**Failed asset state**:

When `IAssetImporter::Import` returns a failure `VFSResult`, the coordinator:
1. Copies the error string into `job.DiagnosticMessage` via
   `snprintf(job.DiagnosticMessage, 256, "%s", result.Error())`.
2. Calls `AssetRegistry::SetStatus(job.Meta.UUID, AssetStatus::Failed)`.
3. Increments `m_failed`.
4. Decrements `m_total`.
5. Invokes `job.Callback(job.Path, false)` if present.

The asset remains in the registry with `AssetStatus::Failed`. The editor can display the
`DiagnosticMessage` in the import log panel by querying the registry entry.

**No automatic retry**:

Failed jobs are not automatically re-enqueued. The only retry mechanism is a manual
`Enqueue(path, ImportPriority::Immediate)` call, which the editor triggers when the user
clicks "Reimport" in the asset inspector or when `FileWatcher` emits a `Modified` event
for a previously-failed asset (see Section 9).

Rationale: automatic retry loops hide real errors (missing texture, corrupted file, wrong
importer) and waste CPU time. A manual retry after the user has fixed the source file is
the correct workflow.

**`DiagnosticMessage` field**:

```cpp
struct ImportJob {
    // ...
    char DiagnosticMessage[256] = {};
};
```

Fixed-size, zero-initialized. Never heap-allocated. The coordinator and importers write
into it via `snprintf`. The editor reads it as a C-string. 256 bytes is sufficient for
file paths (≤ 200 characters in practice) plus a short error reason.

**`AssetStatus::Failed` persistence**:

`AssetRegistry` persists `Failed` status to the project cache on save. On next project
open, the editor shows the asset as failed without re-attempting import, prompting the
user to fix the source and reimport.

---

## 8. Dependency Ordering

**Type import order**: textures must be `AssetStatus::Ready` before materials that
reference them; materials must be `Ready` before meshes that reference them. The enforced
order is:

```
Textures  →  Materials  →  Meshes  →  (Scenes / Prefabs)
```

**`DependenciesSatisfied(path)` implementation**:

1. Look up the asset's dependency list in `DependencyGraph::GetDependencies(path)`.
   `DependencyGraph` is populated during source file parsing (a lightweight pre-pass
   performed by each importer before the full cook).
2. For each dependency `dep_path`:
   a. Resolve `dep_uuid = MetaFileIO::Read(dep_path).UUID`.
   b. Query `AssetRegistry::GetStatus(dep_uuid)`.
   c. If status is not `AssetStatus::Ready`, return false.
3. Return true if all dependencies are `Ready` (or if the dependency list is empty).

**Requeueing on unsatisfied dependencies**:

Inside `Tick()`, when `DependenciesSatisfied` returns false:
- If `job.RequeueCount < 3`: increment `RequeueCount`, re-enqueue the job (priority
  preserved), and continue to the next pop. The dependent asset will be processed in a
  later tick after its dependencies finish.
- If `job.RequeueCount >= 3`: the job has been stalled three times. This indicates a
  circular dependency or a permanently-missing upstream asset. Write
  `"Dependency cycle or unresolvable upstream: <dep_path>"` into `DiagnosticMessage`,
  mark the asset `Failed`, and do not re-enqueue.

**`RequeueCount` cap rationale**: three stalls are enough to absorb transient ordering
races (e.g. a texture and mesh enqueued simultaneously, mesh pops first) while reliably
catching true cycles. Raising the cap to 10 would mask cycles for too long; lowering it
to 1 would cause false failures on normal concurrent imports.

**`DependencyGraph` population**: each importer performs a cheap pre-scan (no full
decode) during `CanImport` or at the start of `Import` to record referenced paths. For
Assimp meshes this means reading `aiScene::mMaterials[*]->GetTexture()` paths and
registering them in the graph before returning from the material-parse phase.

---

## 9. Integration Points

### `VFSScanner::ScanCompleteCallback` → `EnqueueBatch`

When `VFSScanner` finishes scanning a directory (project open, folder add), it fires
`ScanCompleteCallback` with the list of discovered paths:

```cpp
// In VFSScanner setup (e.g. ProjectManager.cpp)
scanner.SetScanCompleteCallback([&coordinator](const Core::Containers::Array<VFS::VFSPath>& paths) {
    coordinator.EnqueueBatch(paths);
});
```

`EnqueueBatch` iterates the array and calls `Enqueue(path, ImportPriority::Normal)` for
each path. Paths already in the queue (from a prior partial scan) are silently deduplicated
by `ImportQueue::Enqueue`.

### `FileWatcher` `Modified`/`Stale` event → `Enqueue(Immediate)`

When `FileWatcher` detects that a watched asset has been modified on disk:

```cpp
// In FileWatcher event dispatch
watcher.OnModified([&coordinator](const VFS::VFSPath& path) {
    coordinator.Enqueue(path, ImportPriority::Immediate);
});
```

`Immediate` priority ensures the reimport surfaces to the top of the heap on the next
`Tick()`, giving the user near-real-time feedback when saving a texture in an external
editor while the engine editor is open.

A `Stale` event (meta file exists but cooked cache is older than the source) uses
`ImportPriority::Normal` rather than `Immediate`, since staleness is detected at project
open time and is not an interactive user action.

### Coordinator lifetime

`ImportCoordinator` is owned by the editor application layer (e.g. `EditorApp`) and
lives for the full application lifetime. It outlives the thread pool to ensure all
in-flight lambdas can safely access the atomic counters via captured `this`.

---

## 10. Hot-Reload: In-Place Registry Update

When `FileWatcher` fires a `Modified` or `Stale` event for an asset that is **already in
the registry** (i.e., it has been imported before), the coordinator must update the
existing registry record rather than inserting a new one. Creating a new `AssetHandle`
for an already-loaded asset would leave the old GPU resource orphaned and break all
scene references pointing at the previous handle.

### `AssetRegistry::UpdateRecord`

```cpp
// ZEngine/VFS/AssetRegistry.h — addition
// Replaces the AssetRecord for an existing UUID in-place.
// - Updates ArtifactPath, ImporterName, LastSourceSha256 from new_meta.
// - Sets status to Loading (caller is responsible for setting Ready/Failed after import).
// - The existing AssetHandle is preserved — all scene references remain valid.
// - Asserts if uuid is not already registered (use Register for new assets).
void AssetRegistry::UpdateRecord(const uuids::uuid& uuid, const VFS::MetaFileData& new_meta);
```

### Coordinator hot-reload flow

```
FileWatcher::Modified → ImportCoordinator::Enqueue(path, Immediate)
  ImportCoordinator::Dispatch(job):
    1. MetaFileIO::GetOrCreate → ImportStatus::Stale
    2. uuid = meta.AssetUUID                          ← same UUID as before
    3. AssetRegistry::UpdateRecord(uuid, meta)        ← status → Loading, handle preserved
    4. importer->Import(ctx, path, meta)              ← reimport to new artifact
    5a. Success → AssetRegistry::SetStatus(uuid, Ready)
        RenderResourceManager::ScheduleSwap(           ← swap GPU resource, handle unchanged
            registry.GetHandle(uuid), new_asset_handle)
    5b. Failure → AssetRegistry::SetStatus(uuid, Failed)
                  (old GPU resource remains bound — no visual corruption)
```

The key invariant: the `AssetHandle` never changes across a hot-reload. Only the
underlying GPU resource is swapped by `RenderResourceManager::ScheduleSwap`. This
means scene YAML references, component `MeshComponent::Handle` fields, and material
bindings all remain valid without any fixup.

---

## 11. Unit Tests

File: `ZEngine/tests/Importers/ImportPipelineTest.cpp`

### Test 1 — Enqueue + TryPop returns the same job

```cpp
TEST(ImportQueue, EnqueueTryPopReturnsSameJob)
{
    ImportQueue queue;
    ImportJob job;
    job.Path = VFS::VFSPath("assets/textures/wood.png");
    job.Priority = ImportPriority::Normal;

    queue.Enqueue(job);
    EXPECT_FALSE(queue.IsEmpty());
    EXPECT_EQ(queue.Size(), 1u);

    ImportJob out;
    bool popped = queue.TryPop(out);
    EXPECT_TRUE(popped);
    EXPECT_EQ(out.Path, job.Path);
    EXPECT_TRUE(queue.IsEmpty());
}
```

### Test 2 — Duplicate enqueue deduplicates

```cpp
TEST(ImportQueue, DuplicateEnqueueKeepsSizeOne)
{
    ImportQueue queue;
    ImportJob job;
    job.Path = VFS::VFSPath("assets/textures/wood.png");
    job.Priority = ImportPriority::Normal;

    queue.Enqueue(job);
    EXPECT_TRUE(queue.Contains(job.Path));

    queue.Enqueue(job);  // same path, same priority → no-op
    EXPECT_EQ(queue.Size(), 1u);
    EXPECT_TRUE(queue.Contains(job.Path));
}
```

### Test 3 — Immediate priority pops before Normal

```cpp
TEST(ImportQueue, ImmediatePopsBeforeNormal)
{
    ImportQueue queue;

    ImportJob normal_job;
    normal_job.Path     = VFS::VFSPath("assets/meshes/chair.fbx");
    normal_job.Priority = ImportPriority::Normal;

    ImportJob immediate_job;
    immediate_job.Path     = VFS::VFSPath("assets/textures/chair_diffuse.png");
    immediate_job.Priority = ImportPriority::Immediate;

    queue.Enqueue(normal_job);
    queue.Enqueue(immediate_job);
    EXPECT_EQ(queue.Size(), 2u);

    ImportJob first;
    queue.TryPop(first);
    EXPECT_EQ(first.Priority, ImportPriority::Immediate);
    EXPECT_EQ(first.Path, immediate_job.Path);
}
```

### Test 4 — Route by extension selects correct importer

```cpp
TEST(ImportCoordinator, RouteByExtensionSelectsCorrectImporter)
{
    MockPngImporter  png_imp;   // CanImport("png") == true
    MockFbxImporter  fbx_imp;   // CanImport("fbx") == true

    ImportCoordinator coordinator;
    coordinator.RegisterImporter(&png_imp);
    coordinator.RegisterImporter(&fbx_imp);

    // Use the exposed Route() test-accessor (friend or protected in test build)
    IAssetImporter* selected = coordinator.Route("fbx");
    EXPECT_EQ(selected, &fbx_imp);

    selected = coordinator.Route("png");
    EXPECT_EQ(selected, &png_imp);

    selected = coordinator.Route("wav");  // no importer registered
    EXPECT_EQ(selected, nullptr);
}
```

### Test 5 — Successful import updates AssetRegistry state to Ready

```cpp
TEST(ImportCoordinator, SuccessfulImportSetsStatusReady)
{
    FakeVFSContext ctx;
    MockPngImporter png_imp;  // Import() always returns VFSResult<void>::Ok()
    AssetRegistry registry;

    ImportCoordinator coordinator;
    coordinator.RegisterImporter(&png_imp);

    VFS::VFSPath path("assets/textures/logo.png");
    bool callback_fired = false;
    bool callback_success = false;

    coordinator.Enqueue(path, ImportPriority::Normal,
        [&](const VFS::VFSPath&, bool success) {
            callback_fired   = true;
            callback_success = success;
        });

    coordinator.Tick();  // dispatches job; block until thread pool drains in test

    EXPECT_TRUE(callback_fired);
    EXPECT_TRUE(callback_success);

    UUID uuid = MetaFileIO::Read(path).UUID;
    EXPECT_EQ(registry.GetStatus(uuid), AssetStatus::Ready);
}
```

### Test 6 — Failed import sets AssetStatus to Failed with message

```cpp
TEST(ImportCoordinator, FailedImportSetsStatusFailed)
{
    FakeVFSContext ctx;
    MockBrokenImporter broken_imp;  // Import() returns VFSResult<void>::Err("decode error")
    AssetRegistry registry;

    ImportCoordinator coordinator;
    coordinator.RegisterImporter(&broken_imp);

    VFS::VFSPath path("assets/textures/corrupt.png");
    bool callback_success = true;  // expect it flips to false
    ImportJob captured_job;

    coordinator.Enqueue(path, ImportPriority::Normal,
        [&](const VFS::VFSPath&, bool success) {
            callback_success = success;
        });

    coordinator.Tick();

    EXPECT_FALSE(callback_success);

    UUID uuid = MetaFileIO::Read(path).UUID;
    EXPECT_EQ(registry.GetStatus(uuid), AssetStatus::Failed);

    // Diagnostic message must be non-empty
    const char* diag = registry.GetDiagnosticMessage(uuid);
    EXPECT_GT(strlen(diag), 0u);
}
```

### Test 7 — `EnqueueBatch` enqueues multiple paths

```cpp
TEST(ImportCoordinator, EnqueueBatchEnqueuesAllPaths)
{
    ImportCoordinator coordinator;
    MockPngImporter png_imp;
    coordinator.RegisterImporter(&png_imp);

    Core::Containers::Array<VFS::VFSPath> paths;
    paths.PushBack(VFS::VFSPath("assets/textures/a.png"));
    paths.PushBack(VFS::VFSPath("assets/textures/b.png"));
    paths.PushBack(VFS::VFSPath("assets/textures/c.png"));

    coordinator.EnqueueBatch(paths);

    ImportProgress prog = coordinator.GetProgress();
    EXPECT_EQ(prog.Total, 3u);
    EXPECT_EQ(prog.Completed, 0u);
    EXPECT_EQ(prog.Failed, 0u);
}
```

### Test 8 — `DependenciesSatisfied` blocks mesh until texture is Ready

```cpp
TEST(ImportCoordinator, DependenciesSatisfiedBlocksMeshUntilTextureReady)
{
    FakeVFSContext ctx;
    AssetRegistry  registry;
    MockPngImporter  png_imp;
    MockFbxImporter  fbx_imp;

    ImportCoordinator coordinator;
    coordinator.RegisterImporter(&png_imp);
    coordinator.RegisterImporter(&fbx_imp);

    VFS::VFSPath tex_path("assets/textures/chair_diffuse.png");
    VFS::VFSPath mesh_path("assets/meshes/chair.fbx");

    // Register in the DependencyGraph: mesh depends on texture
    DependencyGraph::Register(mesh_path, {tex_path});

    // Enqueue texture at Normal, mesh at Immediate
    coordinator.Enqueue(tex_path,  ImportPriority::Normal);
    coordinator.Enqueue(mesh_path, ImportPriority::Immediate);

    // First Tick: mesh pops first (Immediate), but texture is not Ready → requeued
    coordinator.Tick();
    UUID mesh_uuid = MetaFileIO::Read(mesh_path).UUID;
    EXPECT_NE(registry.GetStatus(mesh_uuid), AssetStatus::Ready);

    // Simulate texture finishing (as if thread pool completed its job)
    UUID tex_uuid = MetaFileIO::Read(tex_path).UUID;
    registry.SetStatus(tex_uuid, AssetStatus::Ready);

    // Second Tick: texture is Ready → mesh proceeds and imports successfully
    coordinator.Tick();
    EXPECT_EQ(registry.GetStatus(mesh_uuid), AssetStatus::Ready);
}
```

---

## 12. Deliverables Checklist

- [ ] `ZEngine/Importers/IAssetImporter.h` — `CanImport(ext)` + `Import(ctx, path, meta)` interface; no UUID generation inside importers
- [x] `ZEngine/Importers/ImportJob.h` — `ImportPriority` enum, `ImportCallback` typedef, `ImportJob` struct with `DiagnosticMessage[256]` and `RequeueCount`
- [x] `ZEngine/Importers/ImportQueue.h` + `.cpp` — max-heap by priority, `m_index` deduplication map, `Enqueue` upgrades priority on duplicate, `TryPop` maintains heap invariant, all operations under `m_mtx`
- [x] `ZEngine/Importers/ImportCoordinator.h` + `.cpp` — `RegisterImporter`, `Enqueue`, `EnqueueBatch`, `Tick`, `GetProgress`, `Route`, `DependenciesSatisfied`
- [x] `Tick()` pops up to `m_jobs_per_tick` jobs per call and dispatches each to `ThreadPool`; never blocks the main thread
- [ ] `DependenciesSatisfied` queries `DependencyGraph`; stalled jobs requeued with `RequeueCount++`; at `RequeueCount == 3` asset is marked `Failed` with diagnostic
- [ ] `AssimpImporter` (and all other importers) remove internal `UUID::Generate()` calls and read `meta.UUID` instead
- [ ] `AssetRegistry::SetStatus(uuid, AssetStatus::Failed)` called on import failure; `DiagnosticMessage` stored and retrievable via `AssetRegistry::GetDiagnosticMessage(uuid)`
- [ ] No automatic retry; manual retry via `Enqueue(path, ImportPriority::Immediate)`
- [ ] `VFSScanner::ScanCompleteCallback` wired to `ImportCoordinator::EnqueueBatch`
- [ ] `FileWatcher::OnModified` wired to `Enqueue(path, ImportPriority::Immediate)`
- [ ] `FileWatcher::OnStale` wired to `Enqueue(path, ImportPriority::Normal)`
- [ ] `GetProgress()` returns `{Total, Completed, Failed}` via three `memory_order_relaxed` atomic reads; no mutex held
- [ ] `tests/Importers/ImportPipelineTest.cpp` — all 8 tests pass under AddressSanitizer and UBSanitizer
- [ ] Manual smoke test: open a project with 500 assets (mix of `.png`, `.fbx`, `.glsl`); verify all import to `AssetStatus::Ready` with no ASAN errors, progress bar reaches 100%, no duplicate imports in the log
