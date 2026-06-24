# ZEngine VFS — Ticket 3: Async Scanner and VFSMemoryBackend

**Module:** `ZEngine/Core/VFS/` + `Tetragrama/Components/`
**Standard:** C++20
**Status:** Ready for implementation
**Estimated effort:** 3–4 days (1 engineer)
**Depends on:** Ticket 1 (VFSPath, IVFSFile, IVFSBackend, IVFSContext), Ticket 2 (VFSContext, VFSDiskBackend)

---

## Table of Contents

1. [Motivation](#1-motivation)
2. [Scope](#2-scope)
3. [Directory Layout](#3-directory-layout)
4. [Ticket 1 Interface Reference](#4-ticket-1-interface-reference)
5. [3A — VFSMemoryBackend](#5-3a--vfsmemorybackend)
6. [3B — VFSDirectoryCache](#6-3b--vfsdirectorycache)
7. [3C — VFSScanner](#7-3c--vfsscanner)
8. [Concurrency Model](#8-concurrency-model)
9. [3D — ProjectViewUIComponent Migration](#9-3d--projectviewuicomponent-migration)
10. [Unit Tests](#10-unit-tests)
11. [Deliverables Checklist](#11-deliverables-checklist)

---

## 1. Motivation

Two concrete regressions this ticket fixes:

| File | Line | Problem |
|---|---|---|
| `ProjectViewUIComponent.cpp` | 106 | `std::filesystem::directory_iterator` called on the render/UI thread every frame during normal browsing |
| `ProjectViewUIComponent.cpp` | 183 | `std::filesystem::recursive_directory_iterator` called on UI thread during every search keystroke |

Both are synchronous filesystem scans on the UI thread. On a large project (thousands of assets) these stall the editor frame. After this ticket, the UI reads from an in-memory `VFSDirectoryCache` that is populated asynchronously by `VFSScanner` using the existing `ThreadPoolHelper`.

---

## 2. Scope

**In scope:**
- `VFSMemoryBackend` — in-memory `IVFSBackend` (scratch, tests, hot-reload staging)
- `VFSDirectoryCache` — thread-safe snapshot of the VFS directory tree; read by UI, written by scanner
- `VFSScanner` — async tree walker using `ThreadPoolHelper`
- `ProjectViewUIComponent` migration — remove all `directory_iterator` / `recursive_directory_iterator` calls

**Not in scope:** FileWatcher (Ticket 4+), `.meta` sidecars (Ticket 5+), asset registry (Ticket 6+), VFS write mutations via `IVFSContext` in popup handlers (deferred — popup handlers continue using `std::filesystem` for now).

---

## 3. Directory Layout

New files only (existing files modified in §9):

```
ZEngine/ZEngine/Core/VFS/
    VFSMemoryBackend.h          ← MemNode, VFSMemoryFile, VFSMemoryBackend
    VFSMemoryBackend.cpp
    VFSDirectoryCache.h         ← VFSDirectoryCache
    VFSDirectoryCache.cpp
    VFSScanner.h                ← ScanStats, VFSScanner
    VFSScanner.cpp

ZEngine/tests/Misc/             ← picked up by existing GLOB in tests/CMakeLists.txt
    VFSMemoryBackend_test.cpp
    VFSDirectoryCache_test.cpp
    VFSScanner_test.cpp
```

**CMake note.** `ZEngine/ZEngine/CMakeLists.txt` uses `GLOB_RECURSE` with `CONFIGURE_DEPENDS` — no manual source list changes needed. Add `<semaphore>` to `ZEngine/ZEngine/pch.h`.

---

## 4. Ticket 1 Interface Reference

Reproduced here so this document is self-contained for the implementing engineer.

```cpp
namespace ZEngine::Core::VFS
{
    enum class VFSError : uint32_t
    { OK=0, NotFound, AlreadyExists, PermissionDenied, InvalidPath,
      IOError, Unsupported, Cancelled, /* ... */ };

    template <typename T> struct VFSResult
    {
        static VFSResult Ok(T v);
        static VFSResult Fail(VFSError e);
        bool     Succeeded() const;
        bool     Failed()    const;
        VFSError Error()     const;
        T&       Value();
    };

    struct VFSPath
    {
        char     Buffer[MAX_FILE_PATH_COUNT] = {};   // 256
        uint16_t Length                      = 0;
        static VFSResult<VFSPath> Parse(cstring raw);
        cstring  CStr()   const;
        bool     IsPrefixOf(const VFSPath&) const;
        bool     operator==(const VFSPath&) const;
        VFSResult<VFSPath> Parent()   const;
        VFSResult<VFSPath> Append(cstring segment) const;
        VFSResult<VFSPath> Filename() const;   // last segment
        uint64_t Hash()   const;               // FNV-1a, pre-computed
    };

    enum class VFSOpenFlags : uint32_t
    { None=0, Read=1<<0, Write=1<<1, Create=1<<2, Append=1<<3, Truncate=1<<4 };

    enum class VFSEntryType : uint8_t { File=0, Directory=1 };

    struct VFSDirEntry
    {
        VFSPath      Path        = {};
        VFSEntryType Type        = VFSEntryType::File;
        uint64_t     SizeBytes   = 0;
    };

    enum class VFSBackendCaps : uint32_t
    { None=0, Read=1<<0, Write=1<<1, List=1<<2, MemoryMap=1<<3 };

    struct IVFSFile
    {
        virtual VFSResult<size_t>                   Read(std::span<uint8_t>, uint64_t offset) = 0;
        virtual VFSResult<size_t>                   Write(std::span<const uint8_t>, uint64_t offset) = 0;
        virtual VFSResult<void>                     Flush()     = 0;
        virtual VFSResult<void>                     Close()     = 0;
        virtual VFSResult<std::span<const uint8_t>> MemoryMap() = 0;
        virtual uint64_t                            Size()      = 0;
    };

    struct IVFSBackend
    {
        virtual VFSResult<IVFSFile*>                  Open(VFSPath relative, VFSOpenFlags) = 0;
        virtual void                                  Close(IVFSFile* file)                = 0;
        virtual VFSResult<Core::Containers::Array<VFSDirEntry>>
                                                      List(Core::Memory::ArenaAllocator*,
                                                           VFSPath dir) const             = 0;
        virtual VFSResult<void>                       CreateDir(VFSPath)                  = 0;
        virtual VFSResult<void>                       Remove(VFSPath)                     = 0;
        virtual VFSResult<void>                       Rename(VFSPath from, VFSPath to)    = 0;
        virtual VFSBackendCaps                        Capabilities() const                = 0;
    };

    struct IVFSContext
    {
        virtual VFSResult<IVFSFile*>                  Open(VFSPath absolute, VFSOpenFlags) = 0;
        virtual void                                  Close(IVFSFile* file)                = 0;
        virtual VFSResult<Core::Containers::Array<VFSDirEntry>>
                                                      List(Core::Memory::ArenaAllocator*,
                                                           VFSPath dir) const             = 0;
        virtual VFSResult<void>                       Mount(IVFSBackend*, VFSPath root,
                                                            int priority=0)              = 0;
        virtual VFSResult<void>                       Unmount(VFSPath root)              = 0;
    };
}
```

---

## 5. 3A — VFSMemoryBackend

### 5.1 `VFSMemoryBackend.h`

```cpp
// ZEngine/Core/VFS/VFSMemoryBackend.h
#pragma once
#include <Core/VFS/VFSError.h>
#include <Core/VFS/IVFSBackend.h>
#include <ZEngineDef.h>
#include <shared_mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace ZEngine::Core::VFS
{
    // -----------------------------------------------------------------------
    // MemNode — internal storage unit for one file or directory.
    //
    // Uses std::vector<uint8_t> intentionally: VFSMemoryBackend is a
    // scratch / test store, never on a hot rendering path. Arena allocation
    // is not used here because MemNode owns heap data (std::vector) whose
    // copy-on-rehash semantics are incompatible with the engine's arena Array.
    // -----------------------------------------------------------------------
    struct MemNode
    {
        enum class Kind : uint8_t { File = 0, Directory = 1 };

        Kind                 NodeKind = Kind::File;
        std::vector<uint8_t> Data     = {};   // empty for directories
    };

    // -----------------------------------------------------------------------
    // VFSMemoryFile — returned by VFSMemoryBackend::Open().
    //
    // Read mode  — holds shared_lock for its lifetime; MemoryMap() returns
    //              a zero-copy span directly into MemNode::Data.
    // Write mode — accumulates writes in m_write_buf; commits to MemNode
    //              on Flush() or Close() under a unique_lock.
    // -----------------------------------------------------------------------
    struct VFSMemoryFile final : public IVFSFile
    {
        VFSMemoryFile()  = default;
        ~VFSMemoryFile() override;

        VFSResult<size_t>                   Read(std::span<uint8_t> buf,
                                                  uint64_t offset)          override;
        VFSResult<size_t>                   Write(std::span<const uint8_t> buf,
                                                   uint64_t offset)         override;
        VFSResult<void>                     Flush()                         override;
        VFSResult<void>                     Close()                         override;
        VFSResult<std::span<const uint8_t>> MemoryMap()                     override;
        uint64_t                            Size()                          override;

    private:
        friend struct VFSMemoryBackend;

        MemNode*                             m_node           = nullptr;
        VFSOpenFlags                         m_flags          = VFSOpenFlags::None;
        bool                                 m_closed         = false;

        // Read path: holds shared_lock for entire file lifetime.
        std::shared_lock<std::shared_mutex>  m_read_lock;

        // Write path: staging buffer committed on Flush()/Close().
        std::vector<uint8_t>                 m_write_buf      = {};

        // Back-pointer to backend mutex for write-path commit.
        std::shared_mutex*                   m_backend_mutex  = nullptr;
    };

    // -----------------------------------------------------------------------
    // VFSMemoryBackend
    // -----------------------------------------------------------------------
    struct VFSMemoryBackend final : public IVFSBackend
    {
        VFSMemoryBackend();
        ~VFSMemoryBackend() override;

        // IVFSBackend
        VFSResult<IVFSFile*>  Open(VFSPath relative, VFSOpenFlags flags)     override;
        void                  Close(IVFSFile* file)                           override;
        VFSResult<Core::Containers::Array<VFSDirEntry>>
                              List(Core::Memory::ArenaAllocator* arena,
                                   VFSPath dir)                         const override;
        VFSResult<void>       CreateDir(VFSPath relative)                     override;
        VFSResult<void>       Remove(VFSPath relative)                        override;
        VFSResult<void>       Rename(VFSPath from, VFSPath to)                override;
        VFSBackendCaps        Capabilities()                            const override;

        // Direct population — used by scanner and tests.
        // Atomically creates or overwrites the file node at `path`.
        VFSResult<void>       WriteFile(VFSPath path,
                                        std::span<const uint8_t> data);

        bool                  Exists(VFSPath path) const;

    private:
        // Key: std::string(path.Buffer, path.Length) for simplicity.
        // MemNode* owned by the map (delete on Remove/destructor).
        std::unordered_map<std::string, MemNode*> m_nodes;
        mutable std::shared_mutex                  m_mutex;

        // Internal helpers — all called with appropriate lock already held.
        MemNode*       FindNode(const VFSPath& path);
        const MemNode* FindNode(const VFSPath& path) const;
        void           EnsureParentExists(const VFSPath& path);
        void           AddChildToParent(const VFSPath& child_path);
        void           RemoveChildFromParent(const VFSPath& child_path);
    };

} // namespace ZEngine::Core::VFS
```

### 5.2 Implementation Notes

#### `Open(Read)` algorithm

```
1. acquire shared_lock lock(m_mutex)
2. node = FindNode(relative)  →  if null: return Fail(NotFound)
3. assert node->NodeKind == Kind::File, else return Fail(IOError)
4. heap-allocate VFSMemoryFile file (new VFSMemoryFile)
5. file->m_node           = node
   file->m_flags          = flags
   file->m_backend_mutex  = &m_mutex
6. move lock into file->m_read_lock   ← transfers ownership; backend lock released
7. return Ok(file)
```

The shared_lock moves into the file handle. The backend's local `lock` variable is now unlocked; the file holds the shared_lock. This keeps the node's `Data` stable for the lifetime of the read-mode file.

#### `Open(Write | Create)` algorithm

```
1. acquire unique_lock lock(m_mutex)
2. if Create flag set AND node not found:
       insert new MemNode(Kind::File) into m_nodes
       call EnsureParentExists(relative)
       call AddChildToParent(relative)
3. node = FindNode(relative)  →  if still null: return Fail(NotFound)
4. heap-allocate VFSMemoryFile file
5. file->m_node          = node
   file->m_flags         = flags
   file->m_backend_mutex = &m_mutex
6. if Append flag: copy node->Data into file->m_write_buf
7. unique_lock released on scope exit
8. return Ok(file)
```

Writes accumulate in `m_write_buf`. The backend is NOT locked during writes — only on commit.

#### `VFSMemoryFile::Flush()` / `Close()`

```
if write mode:
    acquire unique_lock(*m_backend_mutex)
    m_node->Data = m_write_buf           // overwrite (truncate semantics)
    // unique_lock released
if Close():
    m_closed = true
    m_write_buf.clear()
    m_write_buf.shrink_to_fit()
    // read-mode: m_read_lock is released by destructor of shared_lock member
```

#### `MemoryMap()`

- Read mode: `return Ok(span<const uint8_t>(m_node->Data.data(), m_node->Data.size()))`
  Valid while `m_read_lock` is held (i.e. while the file is open and not Closed).
- Write mode: `return Fail(VFSError::Unsupported)`

#### `List(arena, dir)` algorithm

```
1. acquire shared_lock
2. find dir node, verify Kind::Directory, else Fail
3. count = 0
   for each (key, node) in m_nodes:
       candidate = VFSPath::Parse(key.c_str())
       if candidate.Parent() == dir: count++
4. Array<VFSDirEntry> result; result.init(arena, count)
5. for each (key, node) in m_nodes:
       candidate = VFSPath::Parse(key.c_str())
       if candidate.Parent() == dir:
           VFSDirEntry e
           e.Path      = candidate
           e.Type      = (node->Kind == Kind::File) ? File : Directory
           e.SizeBytes = node->Data.size()
           result.push_back(e)
6. return Ok(result)
```

#### `WriteFile(path, data)` algorithm

```
1. acquire unique_lock
2. node = FindNode(path)
   if not found:
       insert new MemNode(Kind::File)
       EnsureParentExists(path)
       AddChildToParent(path)
       node = FindNode(path)
3. node->Data.assign(data.begin(), data.end())
4. return Ok()
```

#### `EnsureParentExists(path)`

Walk up the parent chain. For each ancestor not yet in `m_nodes`, insert a `MemNode(Kind::Directory)` and call `AddChildToParent` for it. Stop when a parent already exists. This is always called under `unique_lock`.

#### `Remove(path)` algorithm

```
1. acquire unique_lock
2. node = FindNode(path)  →  Fail(NotFound) if absent
3. if Directory and any child exists in m_nodes: Fail(IOError)
4. RemoveChildFromParent(path)
5. delete node; m_nodes.erase(key)
6. return Ok()
```

#### `Rename(from, to)` algorithm

```
1. acquire unique_lock
2. from_node = FindNode(from)  →  Fail(NotFound) if absent
3. if FindNode(to) != null: Fail(AlreadyExists)
4. RemoveChildFromParent(from)
5. m_nodes[to_key] = from_node; m_nodes.erase(from_key)
6. AddChildToParent(to)
7. return Ok()
```

#### `Close(file)` (backend method)

```
file->Close()   // commits write buf if write mode
delete file
```

---

## 6. 3B — VFSDirectoryCache

### 6.1 `VFSDirectoryCache.h`

```cpp
// ZEngine/Core/VFS/VFSDirectoryCache.h
#pragma once
#include <Core/VFS/VFSError.h>
#include <Core/VFS/IVFSBackend.h>   // VFSDirEntry
#include <Core/Containers/Array.h>
#include <ZEngineDef.h>
#include <functional>
#include <shared_mutex>
#include <span>
#include <string>
#include <unordered_map>

namespace ZEngine::Core::VFS
{
    // -----------------------------------------------------------------------
    // VFSDirectoryCache
    //
    // Thread model:
    //   READERS  — UI thread (every frame); acquires shared_lock.
    //   WRITERS  — VFSScanner worker threads; acquires unique_lock.
    //   STALE    — FileWatcher (Ticket 4+); acquires unique_lock for Invalidate.
    //
    // Ownership: VFSDirEntry data is arena-allocated by VFSScanner.
    // The arena must outlive the cache entries it backs.
    // -----------------------------------------------------------------------
    struct VFSDirectoryCache
    {
        VFSDirectoryCache()  = default;
        ~VFSDirectoryCache() = default;

        // Called from UI thread every frame.
        // Returns empty span if directory not yet cached.
        // O(1) hash lookup + shared_lock acquire.
        std::span<const VFSDirEntry> GetListing(const VFSPath& dir) const;

        // Called by VFSScanner from a worker thread.
        // Moves the Array into the cache and marks the entry non-stale.
        void SetListing(const VFSPath& dir,
                        Core::Containers::Array<VFSDirEntry>&& entries);

        // Called by FileWatcher (Ticket 4+) or after a mutation.
        // Marks the entry stale. GetListing still returns old data (no blank pane).
        void Invalidate(const VFSPath& dir);

        // Returns true if entry is stale or has never been scanned.
        bool IsStale(const VFSPath& dir) const;

        // Clears all entries (project close / root remount).
        void Clear();

        // Number of cached directories (diagnostics / tests).
        size_t Size() const;

        // Iterates all cached directories.
        // Acquires shared_lock for the entire iteration.
        // visitor(dir_path, entries_span)
        void ForEachDir(
            std::function<void(const VFSPath&,
                               std::span<const VFSDirEntry>)> visitor) const;

    private:
        struct CacheEntry
        {
            Core::Containers::Array<VFSDirEntry> Entries = {};
            bool                                 Stale   = true;
        };

        // Key: std::string(dir.Buffer, dir.Length)
        std::unordered_map<std::string, CacheEntry> m_entries;
        mutable std::shared_mutex                   m_mutex;
    };

} // namespace ZEngine::Core::VFS
```

### 6.2 Implementation Notes

**`GetListing`** — acquires shared_lock, finds entry, captures `(data pointer, size)`, releases lock, returns span. The span is valid as long as no concurrent `SetListing`/`Clear` runs. Because the UI exclusively reads and the scanner writes between frame-synced drain points, this is safe in the engine's threading model.

**`SetListing`** — acquires unique_lock. `CacheEntry::Entries` is a `Core::Containers::Array<VFSDirEntry>` (arena-backed, move is a pointer copy). Assign the moved array, set `Stale = false`.

**`ForEachDir`** — acquires shared_lock, iterates `m_entries`, constructs a `VFSPath` from the string key, calls visitor with `(path, span)`. Called only when search buffer is non-empty — not every frame.

**`Core::Containers::Array<T>` move.** The engine's `Array<T>` is a struct with `T* m_data`, `size_t m_size`, `size_t m_capacity`, no destructor that frees. Move is a member-wise copy. The arena that backed the source array must remain alive; the cache does not take ownership of the arena.

---

## 7. 3C — VFSScanner

### 7.1 `VFSScanner.h`

```cpp
// ZEngine/Core/VFS/VFSScanner.h
#pragma once
#include <Core/VFS/VFSDirectoryCache.h>
#include <Core/VFS/IVFSContext.h>
#include <Core/Memory/Allocator.h>
#include <ZEngineDef.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <semaphore>   // C++20

namespace ZEngine::Core::VFS
{
    struct ScanStats
    {
        uint64_t FilesFound = 0;
        uint64_t DirsFound  = 0;
        uint64_t DurationMs = 0;
    };

    // -----------------------------------------------------------------------
    // VFSScanner
    //
    // Usage:
    //   scanner.SetOnScanComplete([](ScanStats s){ /* marshal to UI if needed */ });
    //   scanner.Scan(ctx, rootPath, &arena, &cache);
    //
    // Thread model:
    //   Scan()          → called from UI/engine thread; returns immediately.
    //   ScanDirectory() → runs on ThreadPool workers (up to MaxConcurrentDirLists).
    //   OnScanComplete  → fires on the last worker thread that finishes.
    // -----------------------------------------------------------------------
    struct VFSScanner
    {
        static constexpr int MaxConcurrentDirLists = 4;

        VFSScanner();
        ~VFSScanner();

        // Starts async scan. If already scanning, cancels the current scan first.
        void Scan(IVFSContext*            context,
                  VFSPath                 root,
                  Core::Memory::ArenaAllocator* arena,
                  VFSDirectoryCache*      cache);

        // Cancels in-flight scan. Completion callback will NOT fire.
        void Cancel();

        // True from Scan() call until last task finishes (or Cancel()).
        bool IsScanning() const;

        // Called before Scan(). Fires on last scanner worker thread.
        // Caller marshals to UI thread if needed.
        void SetOnScanComplete(std::function<void(ScanStats)> callback);

    private:
        struct ScanContext
        {
            IVFSContext*                    Context = nullptr;
            VFSPath                         Root    = {};
            Core::Memory::ArenaAllocator*   Arena   = nullptr;
            VFSDirectoryCache*              Cache   = nullptr;
        };

        void ScanDirectory(ScanContext ctx, VFSPath dir);
        void OnTaskComplete(bool cancelled);

        std::function<void(ScanStats)>        m_complete_callback;

        std::atomic<bool>                     m_is_scanning{false};
        std::atomic<bool>                     m_cancel_requested{false};
        std::atomic<int32_t>                  m_pending_tasks{0};
        std::atomic<uint64_t>                 m_files_found{0};
        std::atomic<uint64_t>                 m_dirs_found{0};
        std::chrono::steady_clock::time_point m_scan_start{};

        // Limits simultaneous IVFSContext::List() calls.
        std::counting_semaphore<MaxConcurrentDirLists> m_dir_semaphore{MaxConcurrentDirLists};
    };

} // namespace ZEngine::Core::VFS
```

### 7.2 Async Walk Algorithm

```
VFSScanner::Scan(context, root, arena, cache):
    if IsScanning():
        m_cancel_requested.store(true, release)
        while m_pending_tasks.load(acquire) > 0:
            std::this_thread::yield()

    m_cancel_requested.store(false, relaxed)
    m_files_found.store(0, relaxed)
    m_dirs_found.store(0, relaxed)
    m_pending_tasks.store(1, relaxed)        ← root task counted BEFORE Submit
    m_is_scanning.store(true, release)
    m_scan_start = steady_clock::now()

    ScanContext ctx = { context, root, arena, cache }

    ThreadPoolHelper::Submit([this, ctx]() {
        ScanDirectory(ctx, ctx.Root)
        OnTaskComplete(m_cancel_requested.load(relaxed))
    })


VFSScanner::ScanDirectory(ctx, dir):
    if m_cancel_requested.load(relaxed): return

    m_dir_semaphore.acquire()                ← blocks if 4 lists already in flight
    result = ctx.Context->List(ctx.Arena, dir)
    m_dir_semaphore.release()

    if result.Failed():
        ZENGINE_CORE_WARN("VFSScanner: List failed for {}", dir.CStr())
        return

    ctx.Cache->SetListing(dir, std::move(result.Value()))

    for each entry in result.Value():
        if m_cancel_requested.load(relaxed): return

        if entry.Type == File:
            m_files_found.fetch_add(1, relaxed)
        else:                                ← Directory
            m_dirs_found.fetch_add(1, relaxed)
            m_pending_tasks.fetch_add(1, acq_rel)   ← BEFORE Submit
            sub = entry.Path
            ThreadPoolHelper::Submit([this, ctx, sub]() {
                ScanDirectory(ctx, sub)
                OnTaskComplete(m_cancel_requested.load(relaxed))
            })


VFSScanner::OnTaskComplete(cancelled):
    remaining = m_pending_tasks.fetch_sub(1, acq_rel) - 1
    if remaining > 0: return

    m_is_scanning.store(false, release)
    if !cancelled && m_complete_callback:
        ScanStats stats
        stats.FilesFound = m_files_found.load()
        stats.DirsFound  = m_dirs_found.load()
        stats.DurationMs = duration_cast<milliseconds>(
                               steady_clock::now() - m_scan_start).count()
        m_complete_callback(stats)
```

### 7.3 Critical Concurrency Invariants

**`m_pending_tasks` protocol.** The counter is incremented **before** `ThreadPoolHelper::Submit()` for every new task — never inside the lambda. This prevents a race where `OnTaskComplete()` fires (decrement reaches zero) before all child tasks are enqueued from the current directory's result loop.

**`m_cancel_requested` check placement.** Checked at entry of `ScanDirectory` and after each entry in the directory result loop. Not checked inside `m_dir_semaphore.acquire()` (which is non-cancellable). For faster cancellation, replace with `m_dir_semaphore.try_acquire_for(5ms)` in a loop that also checks the flag.

**`m_is_scanning` reset.** Set to `false` inside `OnTaskComplete` when `remaining == 0`, before the callback fires. The callback may re-enter `Scan()` immediately — by the time the callback runs, `m_is_scanning` is already `false`, so `Scan()` does not spin.

---

## 8. Concurrency Model

```
Thread Actors
─────────────────────────────────────────────────────────────────────────
UI Thread        VFSDirectoryCache       VFSMemoryBackend
  (60+ Hz)       ─────────────────       ────────────────
  GetListing()   shared_lock             (not involved)
  ForEachDir()   shared_lock (full iter)

Scanner Workers  VFSDirectoryCache       IVFSContext (read)
  (ThreadPool)   ─────────────────       ──────────────────
  ScanDirectory  unique_lock/SetListing  List() — pread, no write
  WriteFile()    (not involved)          VFSMemoryBackend: unique_lock

FileWatcher      VFSDirectoryCache       (future ticket)
  (Ticket 4+)    unique_lock/Invalidate

Atomic state (no mutex needed):
  m_pending_tasks    atomic<int32_t>   — counter protocol (pre-increment)
  m_cancel_requested atomic<bool>      — cooperative cancellation flag
  m_is_scanning      atomic<bool>      — IsScanning() public predicate
  m_dir_semaphore    counting_semaphore<4> — backpressure on List() calls

Key invariants:
  1. UI never waits — GetListing() holds shared_lock only for pointer/size capture.
  2. Scanner holds unique_lock only during SetListing() — not during List().
  3. m_pending_tasks is pre-incremented before Submit() to prevent premature zero.
  4. m_dir_semaphore caps concurrent List() calls at 4 — prevents IOQueue flood.
```

---

## 9. 3D — ProjectViewUIComponent Migration

### 9.1 New Members (`ProjectViewUIComponent.h`)

```cpp
// REMOVE:
std::filesystem::path m_assets_directory;
std::filesystem::path m_current_directory;

// ADD (in private section):
ZEngine::Core::VFS::VFSPath            m_assets_vfs_root    = {};
ZEngine::Core::VFS::VFSPath            m_current_vfs_dir    = {};
ZEngine::Core::VFS::VFSDirectoryCache* m_directory_cache    = nullptr;  // non-owning
ZEngine::Core::VFS::VFSScanner*        m_scanner            = nullptr;  // non-owning
std::atomic<bool>                      m_scan_refresh_ready{false};

// Keep for popup handlers (still use std::filesystem for mutations in this ticket):
std::filesystem::path                  m_popup_target_path;
```

`m_directory_cache` and `m_scanner` are injected by the editor layer (`Editor.cpp` / `ImguiLayer`) via an extended `Initialize()` signature. The component does not own them.

### 9.2 `Initialize()` — Before / After

**Before:**
```cpp
m_assets_directory  = ParentLayer->CurrentApp->WorkingSpacePath;
m_current_directory = m_assets_directory;
```

**After:**
```cpp
ZENGINE_VALIDATE_ASSERT(m_scanner != nullptr,
    "VFSScanner not injected into ProjectViewUIComponent")
ZENGINE_VALIDATE_ASSERT(m_directory_cache != nullptr,
    "VFSDirectoryCache not injected into ProjectViewUIComponent")

auto ws = ParentLayer->CurrentApp->WorkingSpacePath.string();
auto parse_result = ZEngine::Core::VFS::VFSPath::Parse(ws.c_str());
ZENGINE_VALIDATE_ASSERT(parse_result.Succeeded(), "WorkingSpacePath is not a valid VFSPath")

m_assets_vfs_root = parse_result.Value();
m_current_vfs_dir = m_assets_vfs_root;

m_scanner->SetOnScanComplete([this](ZEngine::Core::VFS::ScanStats stats) {
    ZENGINE_CORE_INFO("VFS scan complete: {} files, {} dirs, {}ms",
        stats.FilesFound, stats.DirsFound, stats.DurationMs)
    m_scan_refresh_ready.store(true, std::memory_order_release);
});

auto* vfs_context = ParentLayer->CurrentApp->GetVFSContext();  // IVFSContext*
m_scanner->Scan(vfs_context, m_assets_vfs_root, &m_local_arena, m_directory_cache);
```

### 9.3 `RenderContentBrowser()` — Before / After

**Before (line 106):**
```cpp
for (const auto& entry : std::filesystem::directory_iterator(m_current_directory))
{
    ImGui::TableNextColumn();
    RenderContentTile(renderer, entry);
}
```

**After:**
```cpp
auto listing = m_directory_cache->GetListing(m_current_vfs_dir);
for (const auto& entry : listing)
{
    ImGui::TableNextColumn();
    RenderContentTile(renderer, entry);
}
```

### 9.4 `RenderContentTile()` — Signature Change

```cpp
// BEFORE:
void RenderContentTile(GraphicRenderer* renderer,
                       const std::filesystem::directory_entry& entry);

// AFTER:
void RenderContentTile(GraphicRenderer* renderer,
                       const ZEngine::Core::VFS::VFSDirEntry& entry);
```

**Key internal changes:**

```cpp
// Name
auto fn_result = entry.Path.Filename();
cstring name   = fn_result.Succeeded() ? fn_result.Value().CStr() : entry.Path.CStr();

// Icon
ImTextureID icon = (entry.Type == ZEngine::Core::VFS::VFSEntryType::Directory)
    ? (ImTextureID)m_directory_icon->Handle.Index
    : (ImTextureID)m_file_icon->Handle.Index;

// Drag-and-drop payload
std::string itemPath = entry.Path.CStr();
ImGui::SetDragDropPayload("CONTENT_BROWSER_FILE_DRAG_OP",
    itemPath.c_str(), (itemPath.length() + 1) * sizeof(char));

// Double-click navigate
if (entry.Type == ZEngine::Core::VFS::VFSEntryType::Directory)
{
    m_current_vfs_dir = entry.Path;
    secure_memset(m_search_buffer, 0, sizeof(m_search_buffer), sizeof(m_search_buffer));
}

// Context menu — convert to std::filesystem::path for popup handlers (deferred migration)
if (ImGui::BeginPopup("ItemContextMenu"))
{
    std::filesystem::path fp(entry.Path.CStr());
    entry.Type == ZEngine::Core::VFS::VFSEntryType::Directory
        ? RenderContextMenu(ContextMenuType::Folder, fp)
        : RenderContextMenu(ContextMenuType::File,   fp);
    ImGui::EndPopup();
}
```

### 9.5 `RenderFilteredContent()` — Before / After

**Before (line 183):**
```cpp
for (const auto& entry : std::filesystem::recursive_directory_iterator(m_assets_directory))
{
    if (entry.is_regular_file() || entry.is_directory())
    {
        std::string nameLower = entry.path().filename().string();
        if (Helpers::KMPSearch(scratch.Arena, nameLower.c_str(), searchTerm))
        {
            ImGui::TableNextColumn();
            RenderContentTile(renderer, entry);
        }
    }
}
```

**After:**
```cpp
char name_lower[MAX_FILE_PATH_COUNT] = {};

m_directory_cache->ForEachDir(
    [&](const ZEngine::Core::VFS::VFSPath& /*dir*/,
        std::span<const ZEngine::Core::VFS::VFSDirEntry> entries)
    {
        for (const auto& entry : entries)
        {
            auto fn_result = entry.Path.Filename();
            if (!fn_result.Succeeded()) continue;

            cstring raw_name = fn_result.Value().CStr();
            size_t  len      = Helpers::secure_strlen(raw_name);
            size_t  copy_len = (len < MAX_FILE_PATH_COUNT - 1) ? len : MAX_FILE_PATH_COUNT - 2;
            for (size_t i = 0; i < copy_len; ++i)
                name_lower[i] = static_cast<char>(::tolower(
                                     static_cast<unsigned char>(raw_name[i])));
            name_lower[copy_len] = '\0';

            if (Helpers::KMPSearch(scratch.Arena, name_lower, searchTerm))
            {
                ImGui::TableNextColumn();
                RenderContentTile(renderer, entry);
            }
        }
    });
```

### 9.6 `RenderDirectoryNode()` — Before / After

**Before:**
```cpp
void RenderDirectoryNode(const std::filesystem::path& directory);
```

**After:**
```cpp
void RenderDirectoryNode(const ZEngine::Core::VFS::VFSPath& directory);
```

**Implementation:**
```cpp
void ProjectViewUIComponent::RenderDirectoryNode(const ZEngine::Core::VFS::VFSPath& directory)
{
    auto listing = m_directory_cache->GetListing(directory);
    for (const auto& entry : listing)
    {
        if (entry.Type != ZEngine::Core::VFS::VFSEntryType::Directory)
            continue;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
        if (entry.Path == m_current_vfs_dir)
            flags |= ImGuiTreeNodeFlags_Selected;

        auto fn_result = entry.Path.Filename();
        cstring label  = fn_result.Succeeded() ? fn_result.Value().CStr() : entry.Path.CStr();

        // Unique popup id: "Dir_" + full path
        char popup_id[MAX_FILE_PATH_COUNT + 4] = {};
        popup_id[0] = 'D'; popup_id[1] = 'i'; popup_id[2] = 'r'; popup_id[3] = '_';
        Helpers::secure_strcpy(popup_id + 4, sizeof(popup_id) - 4, entry.Path.CStr());

        bool nodeOpen = ImGui::TreeNodeEx(label, flags);

        if (ImGui::IsItemClicked())
        {
            m_current_vfs_dir = entry.Path;
            secure_memset(m_search_buffer, 0, sizeof(m_search_buffer), sizeof(m_search_buffer));
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup(popup_id);

        if (ImGui::BeginPopup(popup_id))
        {
            RenderContextMenu(ContextMenuType::LeftPane,
                              std::filesystem::path(entry.Path.CStr()));
            ImGui::EndPopup();
        }

        if (nodeOpen)
        {
            RenderDirectoryNode(entry.Path);
            ImGui::TreePop();
        }
    }
}
```

Also update `RenderTreeBrowser()` to call `RenderDirectoryNode(m_assets_vfs_root)` and render the root label using `m_assets_vfs_root.Filename()`.

### 9.7 Refresh Button

Add to `Render()` alongside the back button:

```cpp
// In Render(), after RenderBackButton():
if (ImGui::Button("  Refresh  "))
{
    auto* vfs_context = ParentLayer->CurrentApp->GetVFSContext();
    m_scanner->Scan(vfs_context, m_assets_vfs_root, &m_local_arena, m_directory_cache);
}
```

Auto-refresh on scan complete (poll the atomic flag set by the callback):
```cpp
// At start of Render():
if (m_scan_refresh_ready.exchange(false, std::memory_order_acquire))
{
    // Cache is already populated. Nothing to do — next GetListing() reads fresh data.
    // Optionally: reset m_current_vfs_dir to root if it became invalid.
}
```

### 9.8 Popup Handlers (Deferred)

`HandleCreateFolderPopup`, `HandleCreateFilePopup`, `HandleDeleteFolderPopup`, `HandleRenameFilePopup`, and friends continue using `std::filesystem` for the actual mutation in this ticket — the VFS write path through `IVFSContext` is not required until a later ticket. After any mutation completes, trigger a rescan:

```cpp
// Add at the end of each successful mutation handler:
{
    auto* vfs_context = ParentLayer->CurrentApp->GetVFSContext();
    m_scanner->Scan(vfs_context, m_assets_vfs_root, &m_local_arena, m_directory_cache);
}
```

---

## 10. Unit Tests

All test files go in `ZEngine/tests/Misc/` (picked up by the existing `GLOB`).

### 10.1 `VFSMemoryBackend_test.cpp`

```
WriteFile_and_Read
  WriteFile("tex/logo.png", {0x89,0x50,0x4e,0x47})
  Open("tex/logo.png", Read) → Succeeded
  Read(buf, 0) → 4 bytes, match input

MemoryMap_ReturnsZeroCopySpan
  WriteFile("data.bin", 1024-byte pattern)
  Open("data.bin", Read) → file
  MemoryMap() → span of 1024 bytes
  span.data() points inside file (not a copy)

WriteFile_Overwrites_Existing
  WriteFile("a.txt", "hello")
  WriteFile("a.txt", "world")
  Open + read → "world"

Open_Write_Accumulates_And_Commits
  Open("b.txt", Write|Create) → file
  Write({1,2,3}, 0)
  Write({4,5},   3)
  Flush()
  Open("b.txt", Read) → read {1,2,3,4,5}

CreateDir_and_List
  CreateDir("assets/textures")
  WriteFile("assets/textures/a.png", {})
  WriteFile("assets/textures/b.png", {})
  List(arena, "assets/textures") → 2 entries

Remove_File
  WriteFile("tmp.bin", {0})
  Remove("tmp.bin") → Succeeded
  Open("tmp.bin", Read) → Failed(NotFound)

Rename_File
  WriteFile("old.txt", {1,2})
  Rename("old.txt", "new.txt") → Succeeded
  Open("old.txt", Read) → Failed(NotFound)
  Open("new.txt", Read) → Succeeded, 2 bytes

Capabilities
  Capabilities() == Read|Write|List|MemoryMap

ConcurrentReads_NoDeadlock
  WriteFile("shared.bin", 4096 bytes)
  8 threads: each Open(Read), Read all, Close
  All succeed; test completes within 500ms (timed_join)
```

### 10.2 `VFSDirectoryCache_test.cpp`

```
GetListing_EmptyCache_ReturnsEmptySpan
  VFSDirectoryCache cache
  cache.GetListing(somePath).empty() == true

SetListing_then_GetListing
  SetListing("assets/", {entry1, entry2})
  GetListing("assets/").size() == 2

IsStale_BeforeSet_True
  cache.IsStale("never/set") == true

IsStale_AfterSet_False
  SetListing("d/", {})
  cache.IsStale("d/") == false

Invalidate_MarksStale_KeepsData
  SetListing("d/", {entry1})
  Invalidate("d/")
  IsStale("d/") == true
  GetListing("d/").size() == 1   ← old data still accessible

Clear_EmptiesAllEntries
  SetListing × 3 different paths
  Clear()
  cache.Size() == 0

ConcurrentReadWrite_NoDeadlock
  4 writer threads: SetListing in a loop (200 iterations)
  4 reader threads: GetListing in a loop (200 iterations)
  Run for 300ms; no deadlock, no crash, no assertion failure
```

### 10.3 `VFSScanner_test.cpp`

These tests use a `MockVFSContext` that implements `IVFSContext::List()` with controlled responses.

```
Scan_EmptyRoot_FiresComplete
  MockContext: List("/") → empty Array
  scanner.SetOnScanComplete(capture stats)
  scanner.Scan(&mock, "/", &arena, &cache)
  Wait 100ms for callback
  stats.FilesFound == 0, stats.DirsFound == 0

Scan_TwoLevels_PopulatesCache
  MockContext:
    List("/root/")       → [dir:"/root/sub/"]
    List("/root/sub/")   → [file:"/root/sub/a.txt", file:"/root/sub/b.png"]
  scanner.Scan(&mock, "/root/", ...)
  Wait for complete
  cache.GetListing("/root/").size() == 1
  cache.GetListing("/root/sub/").size() == 2
  stats.FilesFound == 2, stats.DirsFound == 1

Cancel_StopsWalk_NoCallback
  MockContext: 50 directories, each with 5 sub-dirs (List() sleeps 2ms)
  scanner.Scan(...)
  sleep(10ms)
  scanner.Cancel()
  Wait 100ms
  scanner.IsScanning() == false
  complete callback was NOT invoked

ConcurrentDirLists_LimitedTo4
  MockContext: each List() atomically increments concurrent_count,
               sleeps 5ms, then decrements
  Verify max(concurrent_count) <= 4 across entire scan

Rescan_After_Cancel_StartsFresh
  Run scan, cancel it
  Rescan with same or different root
  complete callback fires for second scan
  second scan stats do not include counts from first scan
```

---

## 11. Deliverables Checklist

```
[ ] ZEngine/ZEngine/pch.h — add <semaphore>

[ ] VFSMemoryBackend.h — MemNode, VFSMemoryFile, VFSMemoryBackend declared
[ ] VFSMemoryBackend.cpp
      Open(Read)  — shared_lock moves into VFSMemoryFile
      Open(Write|Create) — creates node if Create flag; write_buf accumulation
      Close(file) — commits write_buf, deletes file
      List(arena, dir) — scans m_nodes for direct children
      CreateDir — inserts Directory node, ensures parent chain
      Remove — verifies empty dir, removes node and child-of-parent record
      Rename — moves node pointer, updates child records
      WriteFile — convenience; creates or overwrites
      Exists — shared_lock lookup
      Capabilities() == Read|Write|List|MemoryMap
[ ] VFSMemoryFile::MemoryMap() — zero-copy span (read mode only)
[ ] VFSMemoryFile::Flush()/Close() — unique_lock commit

[ ] VFSDirectoryCache.h — GetListing, SetListing, Invalidate, IsStale, Clear, Size, ForEachDir
[ ] VFSDirectoryCache.cpp — shared_mutex reader-writer pattern on all methods

[ ] VFSScanner.h — ScanStats, VFSScanner declared
[ ] VFSScanner.cpp
      Scan() — pre-increments m_pending_tasks before Submit; cancels prior scan
      ScanDirectory() — semaphore acquire/release around List(); cancel checks
      OnTaskComplete() — fires callback only when remaining==0 and !cancelled
      Cancel() — sets m_cancel_requested; does not join threads

[ ] ProjectViewUIComponent.h — new VFSPath members + injected pointer members
[ ] ProjectViewUIComponent.cpp
      Initialize() — VFSPath::FromNative + VFSScanner::Scan
      RenderContentBrowser() — GetListing replaces directory_iterator
      RenderFilteredContent() — ForEachDir replaces recursive_directory_iterator
      RenderDirectoryNode() — GetListing replaces directory_iterator
      RenderContentTile() — VFSDirEntry signature
      Refresh button added
[ ] VERIFY: grep -r "directory_iterator" Tetragrama/ → zero results

[ ] ZEngine/tests/Misc/VFSMemoryBackend_test.cpp  — 9 tests passing
[ ] ZEngine/tests/Misc/VFSDirectoryCache_test.cpp — 7 tests passing
[ ] ZEngine/tests/Misc/VFSScanner_test.cpp        — 5 tests passing
```
