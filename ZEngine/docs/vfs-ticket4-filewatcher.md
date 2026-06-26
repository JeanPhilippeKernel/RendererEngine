# Ticket 4 — VFSFileWatcher: Platform File-Watch + Debounce

**Priority:** P2 — Implement after Ticket 3  
**Status:** Ready for implementation  
**Depends on:** `vfs-ticket3-scanner-memory-backend.md`  
**Blocks:** `vfs-ticket5`, `vfs-ticket6`, `import-pipeline.md` (OnStale hook)

**Goal**: Deliver a cross-platform file-watcher that fires debounced `VFSWatchEvent` notifications
into the engine's existing callback system, driving `VFSDirectoryCache::Invalidate()` and
`VFSScanner::RequestScan()` without polling the filesystem from user code.

---

## 1. Public API

### 1.1 `VFSWatchEvent`

```cpp
// ZEngine/VFS/VFSWatchEvent.h
#pragma once
#include <Core/ZEngineDef.h>

namespace ZEngine::VFS
{
    enum class WatchEventKind : uint8_t
    {
        Created  = 0,
        Modified = 1,
        Deleted  = 2,
        Renamed  = 3,   // OldPath → NewPath
        Overflow = 4,   // kernel buffer overflowed; do full rescan
    };

    struct VFSWatchEvent
    {
        char          Path[MAX_FILE_PATH_COUNT]    = {};
        char          OldPath[MAX_FILE_PATH_COUNT] = {};  // only for Renamed
        WatchEventKind Kind                        = WatchEventKind::Created;
        bool          IsDirectory                  = false;
    };
}
```

### 1.2 `WatchHandle`

```cpp
// ZEngine/VFS/IVFSPlatformWatcher.h
#pragma once
#include <cstdint>

namespace ZEngine::VFS
{
    using WatchHandle = uint32_t;
    constexpr WatchHandle INVALID_WATCH_HANDLE = 0xFFFF'FFFFu;
}
```

### 1.3 `IVFSPlatformWatcher`

```cpp
// ZEngine/VFS/IVFSPlatformWatcher.h  (continued)
#include <functional>
#include <VFS/VFSWatchEvent.h>

namespace ZEngine::VFS
{
    using RawEventCallback = std::function<void(const VFSWatchEvent&)>;

    class IVFSPlatformWatcher
    {
    public:
        virtual ~IVFSPlatformWatcher() = default;

        // Add a directory to watch (non-recursive unless the platform makes it cheap)
        virtual WatchHandle AddWatch(const char* native_path, bool recursive) = 0;
        virtual void        RemoveWatch(WatchHandle handle)                   = 0;

        // Called once per frame (or from a dedicated watcher thread) to drain
        // pending OS events and fire cb for each one.
        virtual void Poll(const RawEventCallback& cb) = 0;

        // Start/stop a background thread that calls Poll internally
        virtual void StartThread() = 0;
        virtual void StopThread()  = 0;
    };
}
```

### 1.4 `VFSFileWatcher` (debounce layer)

```cpp
// ZEngine/VFS/VFSFileWatcher.h
#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <VFS/IVFSPlatformWatcher.h>
#include <VFS/VFSWatchEvent.h>

namespace ZEngine::VFS
{
    using WatchCallback = std::function<void(const VFSWatchEvent&)>;

    struct DebounceEntry
    {
        VFSWatchEvent                           Event;
        std::chrono::steady_clock::time_point   LastSeen;
    };

    class VFSFileWatcher
    {
    public:
        explicit VFSFileWatcher(
            IVFSPlatformWatcher*          platform,
            std::chrono::milliseconds     debounce_window = std::chrono::milliseconds{80});

        ~VFSFileWatcher();

        WatchHandle Watch(const char* native_path, bool recursive, WatchCallback cb);
        void        Unwatch(WatchHandle handle);

        // Called once per frame from the main/editor thread
        void Tick();

    private:
        void OnRawEvent(const VFSWatchEvent& ev);
        void EmitReady(std::chrono::steady_clock::time_point now);

        IVFSPlatformWatcher*          m_platform;
        std::chrono::milliseconds     m_window;

        std::mutex                                         m_mtx;
        std::unordered_map<std::string, DebounceEntry>     m_pending;   // key = Path
        std::unordered_map<WatchHandle, WatchCallback>     m_callbacks;
    };
}
```

---

## 2. Debounce Algorithm

```
Phase A – Absorb (background or poll thread):
  for each raw OS event E:
    lock m_mtx
    entry = m_pending[E.Path]
    entry.Event    = E          // last-write-wins within window
    entry.LastSeen = now()
    unlock m_mtx

Phase B – Emit (Tick(), called from editor/main thread, ~60 Hz):
  lock m_mtx
  now = steady_clock::now()
  for each (path, entry) in m_pending:
    if (now - entry.LastSeen) >= m_window:
      fire m_callbacks[entry.Event's WatchHandle](entry.Event)
      erase entry
  unlock m_mtx
```

Key invariant: an event fires exactly once, after the path has been "quiet" for `m_window` ms.
A burst of saves to the same file (common with IDE auto-save) collapses to one notification.

---

## 3. Platform Backends

### 3.1 macOS — `VFSFSEventsWatcher` (`VFSFSEventsWatcher.mm`)

FSEvents delivers directory-level events, so the backend walks modified directories to find
changed files (similar to how Xcode's source editor works).

```cpp
// ZEngine/VFS/Platform/VFSFSEventsWatcher.h
#pragma once
#if defined(__APPLE__)
#include <CoreServices/CoreServices.h>
#include <VFS/IVFSPlatformWatcher.h>
#include <unordered_map>
#include <atomic>

namespace ZEngine::VFS
{
    class VFSFSEventsWatcher final : public IVFSPlatformWatcher
    {
    public:
        VFSFSEventsWatcher();
        ~VFSFSEventsWatcher() override;

        WatchHandle AddWatch(const char* native_path, bool recursive) override;
        void        RemoveWatch(WatchHandle handle)                    override;
        void        Poll(const RawEventCallback& cb)                   override;
        void        StartThread()                                      override;
        void        StopThread()                                       override;

    private:
        static void FSEventsCallback(
            ConstFSEventStreamRef, void* ctx,
            size_t num, void* paths,
            const FSEventStreamEventFlags* flags,
            const FSEventStreamEventId*    ids);

        void RebuildStream();
        void DrainQueue(const RawEventCallback& cb);

        struct WatchEntry { char Path[MAX_FILE_PATH_COUNT]; bool Recursive; };
        std::unordered_map<WatchHandle, WatchEntry> m_watches;
        WatchHandle          m_next_handle = 1;

        FSEventStreamRef     m_stream      = nullptr;
        CFRunLoopRef         m_run_loop    = nullptr;
        std::thread          m_thread;
        std::atomic<bool>    m_running{false};

        // Staging queue filled by FSEventsCallback, drained in Poll/DrainQueue
        std::mutex                       m_queue_mtx;
        Core::Containers::Array<VFSWatchEvent> m_queue;
    };
}
#endif // __APPLE__
```

**Implementation notes (`.mm` file)**:
- Create stream with `kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer`
- Latency: `0.05` seconds (FSEvents batches within this window before calling back)
- Run the stream on a dedicated `CFRunLoop` on `m_thread`; `StartThread` creates the thread, `StopThread` calls `CFRunLoopStop` and joins
- `FSEventsCallback` pushes raw paths into `m_queue`; `Poll` drains `m_queue` and classifies events using `stat()` (exists → Created/Modified; ENOENT → Deleted)
- For `Renamed`: FSEvents fires two events (old path vanishes, new path appears) which the debounce layer naturally handles as Deleted + Created

### 3.2 Linux — `VFSInotifyWatcher`

```cpp
// ZEngine/VFS/Platform/VFSInotifyWatcher.h
#pragma once
#if defined(__linux__)
#include <VFS/IVFSPlatformWatcher.h>
#include <unordered_map>
#include <atomic>

namespace ZEngine::VFS
{
    class VFSInotifyWatcher final : public IVFSPlatformWatcher
    {
    public:
        VFSInotifyWatcher();
        ~VFSInotifyWatcher() override;

        WatchHandle AddWatch(const char* native_path, bool recursive) override;
        void        RemoveWatch(WatchHandle handle)                    override;
        void        Poll(const RawEventCallback& cb)                   override;
        void        StartThread()                                      override;
        void        StopThread()                                       override;

    private:
        int m_inotify_fd = -1;
        int m_pipe_fd[2] = {-1, -1};   // wake pipe for StopThread

        struct WatchEntry
        {
            char        Path[MAX_FILE_PATH_COUNT];
            int         WatchDescriptor;
            WatchHandle Handle;
        };
        std::unordered_map<int, WatchEntry>         m_wd_to_entry;
        std::unordered_map<WatchHandle, int>         m_handle_to_wd;
        WatchHandle m_next_handle = 1;

        std::thread       m_thread;
        std::atomic<bool> m_running{false};

        std::mutex                            m_queue_mtx;
        Core::Containers::Array<VFSWatchEvent> m_queue;
    };
}
#endif // __linux__
```

**Implementation notes**:
- `inotify_init1(IN_NONBLOCK | IN_CLOEXEC)` in constructor
- `AddWatch` calls `inotify_add_watch(fd, path, IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ONLYDIR)`
- Background thread: `poll({inotify_fd, POLLIN}, ...)` with wake pipe; reads `inotify_event` structs, pushes to `m_queue`
- `IN_MOVED_FROM` / `IN_MOVED_TO` with matching `cookie` → synthesize `WatchEventKind::Renamed`
- `IN_Q_OVERFLOW` → push one event with `Kind = WatchEventKind::Overflow`

### 3.3 Windows — `VFSRDCWatcher`

```cpp
// ZEngine/VFS/Platform/VFSRDCWatcher.h
#pragma once
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <VFS/IVFSPlatformWatcher.h>
#include <unordered_map>
#include <atomic>

namespace ZEngine::VFS
{
    class VFSRDCWatcher final : public IVFSPlatformWatcher
    {
    public:
        VFSRDCWatcher();
        ~VFSRDCWatcher() override;

        WatchHandle AddWatch(const char* native_path, bool recursive) override;
        void        RemoveWatch(WatchHandle handle)                    override;
        void        Poll(const RawEventCallback& cb)                   override;
        void        StartThread()                                      override;
        void        StopThread()                                       override;

    private:
        HANDLE m_iocp = INVALID_HANDLE_VALUE;

        struct WatchEntry
        {
            char        Path[MAX_FILE_PATH_COUNT];
            HANDLE      DirHandle;
            OVERLAPPED  Overlapped;
            BYTE        Buffer[32768];
            WatchHandle Handle;
            bool        Recursive;
        };

        std::unordered_map<WatchHandle, WatchEntry*> m_entries;
        WatchHandle   m_next_handle = 1;
        std::thread   m_thread;
        std::atomic<bool> m_running{false};

        std::mutex                            m_queue_mtx;
        Core::Containers::Array<VFSWatchEvent> m_queue;

        void ReissueRead(WatchEntry* e);
        void ProcessNotifications(WatchEntry* e, DWORD bytes);
    };
}
#endif // _WIN32
```

**Implementation notes**:
- `CreateIoCompletionPort` with one worker thread
- Each watched directory: `CreateFile(path, FILE_LIST_DIRECTORY, FILE_SHARE_*, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL)`
- `ReadDirectoryChangesW(handle, buffer, sizeof(buffer), recursive, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE, NULL, &overlapped, NULL)`
- IOCP thread: `GetQueuedCompletionStatus` loop; `FILE_ACTION_RENAMED_OLD_NAME` + `FILE_ACTION_RENAMED_NEW_NAME` → coalesce into `Renamed`

---

## 4. CMake Changes

```cmake
# ZEngine/ZEngine/CMakeLists.txt additions

if(APPLE)
    target_sources(zEngineLib PRIVATE
        VFS/Platform/VFSFSEventsWatcher.mm   # .mm must be listed explicitly
    )
    target_link_libraries(zEngineLib PRIVATE
        "-framework CoreServices"
    )
elseif(UNIX)
    target_sources(zEngineLib PRIVATE
        VFS/Platform/VFSInotifyWatcher.cpp
    )
elseif(WIN32)
    target_sources(zEngineLib PRIVATE
        VFS/Platform/VFSRDCWatcher.cpp
    )
endif()
```

Because the existing `CMakeLists.txt` uses `GLOB_RECURSE` for `.cpp` files, only the `.mm`
file needs an explicit entry. The Linux and Windows `.cpp` files will be auto-discovered —
but adding them explicitly is safer and makes the platform split obvious to reviewers.

---

## 5. Integration with Ticket 3 Components

```cpp
// VFSContext::InitWatcher() — called during editor startup
void VFSContext::InitWatcher()
{
#if defined(__APPLE__)
    m_platform_watcher = std::make_unique<VFSFSEventsWatcher>();
#elif defined(__linux__)
    m_platform_watcher = std::make_unique<VFSInotifyWatcher>();
#elif defined(_WIN32)
    m_platform_watcher = std::make_unique<VFSRDCWatcher>();
#endif
    m_file_watcher = std::make_unique<VFSFileWatcher>(m_platform_watcher.get());

    // Watch the project root; scanner already knows the subtree
    m_file_watcher->Watch(
        m_project_root_native.c_str(),
        /*recursive=*/true,
        [this](const VFSWatchEvent& ev) {
            m_directory_cache->Invalidate(ev.Path);
            m_scanner->RequestScan(VFSPath::FromNative(ev.Path).Value());
        });

    m_platform_watcher->StartThread();
}

// VFSContext::Tick() — called once per frame from the editor loop
void VFSContext::Tick()
{
    m_file_watcher->Tick();  // drains debounce window, fires callbacks
}
```

---

## 6. Unit Tests

File: `ZEngine/tests/VFS/VFSFileWatcherTest.cpp`

Add `VFS/*.cpp` to the glob in `tests/CMakeLists.txt`:
```cmake
file(GLOB TEST_SOURCES
    Memory/*.cpp
    Containers/*.cpp
    Maths/*.cpp
    Misc/*.cpp
    VFS/*.cpp)          # ADD THIS LINE
```

### Test 1 — Debounce collapses burst into one event
```cpp
TEST(VFSFileWatcher, DebounceBurstCollapses)
{
    MockPlatformWatcher mock;
    VFSFileWatcher watcher(&mock, std::chrono::milliseconds{50});

    int fired = 0;
    watcher.Watch("/project", true, [&](const VFSWatchEvent&) { ++fired; });

    // Simulate 10 rapid raw events for the same file
    for (int i = 0; i < 10; ++i)
        mock.InjectEvent({"/project/foo.glb", {}, WatchEventKind::Modified, false});

    // Tick before window expires — nothing fires
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    watcher.Tick();
    EXPECT_EQ(fired, 0);

    // Tick after window expires — exactly one fires
    std::this_thread::sleep_for(std::chrono::milliseconds{60});
    watcher.Tick();
    EXPECT_EQ(fired, 1);
}
```

### Test 2 — Two distinct files fire two events
```cpp
TEST(VFSFileWatcher, TwoDistinctFilesFireTwice)
{
    MockPlatformWatcher mock;
    VFSFileWatcher watcher(&mock, std::chrono::milliseconds{50});

    int fired = 0;
    watcher.Watch("/project", true, [&](const VFSWatchEvent&) { ++fired; });

    mock.InjectEvent({"/project/a.glb", {}, WatchEventKind::Modified, false});
    mock.InjectEvent({"/project/b.glb", {}, WatchEventKind::Modified, false});

    std::this_thread::sleep_for(std::chrono::milliseconds{60});
    watcher.Tick();
    EXPECT_EQ(fired, 2);
}
```

### Test 3 — Unwatch stops events
```cpp
TEST(VFSFileWatcher, UnwatchStopsEvents)
{
    MockPlatformWatcher mock;
    VFSFileWatcher watcher(&mock, std::chrono::milliseconds{10});

    int fired = 0;
    WatchHandle h = watcher.Watch("/project", true, [&](const VFSWatchEvent&) { ++fired; });

    watcher.Unwatch(h);

    mock.InjectEvent({"/project/foo.glb", {}, WatchEventKind::Created, false});
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    watcher.Tick();
    EXPECT_EQ(fired, 0);
}
```

### Test 4 — Overflow event triggers full rescan flag
```cpp
TEST(VFSFileWatcher, OverflowSetsKind)
{
    MockPlatformWatcher mock;
    VFSFileWatcher watcher(&mock, std::chrono::milliseconds{10});

    WatchEventKind received = WatchEventKind::Created;
    watcher.Watch("/project", true, [&](const VFSWatchEvent& ev) {
        received = ev.Kind;
    });

    mock.InjectEvent({"", {}, WatchEventKind::Overflow, false});
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    watcher.Tick();
    EXPECT_EQ(received, WatchEventKind::Overflow);
}
```

### Test 5 — Rename event preserves OldPath
```cpp
TEST(VFSFileWatcher, RenamePreservesOldPath)
{
    MockPlatformWatcher mock;
    VFSFileWatcher watcher(&mock, std::chrono::milliseconds{10});

    VFSWatchEvent received{};
    watcher.Watch("/project", true, [&](const VFSWatchEvent& ev) {
        received = ev;
    });

    VFSWatchEvent ev{};
    snprintf(ev.Path,    sizeof(ev.Path),    "/project/new.glb");
    snprintf(ev.OldPath, sizeof(ev.OldPath), "/project/old.glb");
    ev.Kind = WatchEventKind::Renamed;
    mock.InjectEvent(ev);

    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    watcher.Tick();
    EXPECT_STREQ(received.OldPath, "/project/old.glb");
    EXPECT_STREQ(received.Path,    "/project/new.glb");
}
```

### Test 6 — Multiple watches, correct callback routing
```cpp
TEST(VFSFileWatcher, MultipleWatchesRouteCorrectly)
{
    MockPlatformWatcher mock;
    VFSFileWatcher watcher(&mock, std::chrono::milliseconds{10});

    int fired_a = 0, fired_b = 0;
    watcher.Watch("/projectA", false, [&](const VFSWatchEvent&) { ++fired_a; });
    watcher.Watch("/projectB", false, [&](const VFSWatchEvent&) { ++fired_b; });

    mock.InjectEvent({"/projectA/x.png", {}, WatchEventKind::Created, false});
    mock.InjectEvent({"/projectB/y.png", {}, WatchEventKind::Created, false});

    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    watcher.Tick();
    EXPECT_EQ(fired_a, 1);
    EXPECT_EQ(fired_b, 1);
}
```

### Test 7 — Zero ticks, no spurious fires
```cpp
TEST(VFSFileWatcher, NoTickNoFire)
{
    MockPlatformWatcher mock;
    VFSFileWatcher watcher(&mock, std::chrono::milliseconds{10});

    int fired = 0;
    watcher.Watch("/project", true, [&](const VFSWatchEvent&) { ++fired; });

    mock.InjectEvent({"/project/foo.glb", {}, WatchEventKind::Modified, false});
    // Deliberately do NOT call watcher.Tick()
    EXPECT_EQ(fired, 0);
}
```

### Test 8 — `IsDirectory` flag propagated
```cpp
TEST(VFSFileWatcher, IsDirectoryFlagPropagated)
{
    MockPlatformWatcher mock;
    VFSFileWatcher watcher(&mock, std::chrono::milliseconds{10});

    bool got_dir = false;
    watcher.Watch("/project", true, [&](const VFSWatchEvent& ev) {
        got_dir = ev.IsDirectory;
    });

    mock.InjectEvent({"/project/newfolder", {}, WatchEventKind::Created, true});
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    watcher.Tick();
    EXPECT_TRUE(got_dir);
}
```

---

## 7. Deliverables Checklist

- [ ] `ZEngine/VFS/VFSWatchEvent.h`
- [ ] `ZEngine/VFS/IVFSPlatformWatcher.h`
- [ ] `ZEngine/VFS/VFSFileWatcher.h` + `VFSFileWatcher.cpp`
- [ ] `ZEngine/VFS/Platform/VFSFSEventsWatcher.h` + `VFSFSEventsWatcher.mm`
- [ ] `ZEngine/VFS/Platform/VFSInotifyWatcher.h` + `VFSInotifyWatcher.cpp`
- [ ] `ZEngine/VFS/Platform/VFSRDCWatcher.h` + `VFSRDCWatcher.cpp`
- [ ] CMake: `-framework CoreServices`, `.mm` explicit source entry
- [ ] `tests/VFS/VFSFileWatcherTest.cpp` (8 tests, MockPlatformWatcher)
- [ ] `VFSContext::InitWatcher()` and `VFSContext::Tick()` wired up
- [ ] Manual smoke test: rename a `.glb` in Finder → single notification within ~100 ms
