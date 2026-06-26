# ZEngine — Logging Policy

**Priority:** P0 — Without a policy, hot-path logging kills performance in production
**Status:** Design — extends existing Logger.h/.cpp
**Modifies:** `Logger.h`, `LoggerDefinition.h`

---

## 1. Current State Analysis

The existing `Logger.h` / `Logger.cpp` provides a working foundation. The following items are assessed:

**What is correct and should be kept:**

- **spdlog async logger with rotating_file_sink.** The async queue decouples the calling thread from I/O. This is production-grade behavior and should not be replaced.
- **`AddEventHandler` for the editor log panel.** The hook mechanism is the right design; its implementation needs repair (see gaps below), not removal.
- **`fmt::format` at all call sites via the `ZENGINE_CORE_*` macros.** `fmt::format` avoids locale-sensitive formatting, is zero-allocation for short messages on its internal buffer, and compiles format strings at compile time with the `FMT_COMPILE` path.

**Gaps that must be fixed:**

- **`LogEventHandler` is `std::function<void(LogMessage)>`.** `std::function` performs a heap allocation for any callable that captures state. In the editor, the log panel registers a handler that captures `this`; this allocation happens at startup but the type forces every call site that handles `LogEventHandler` values to go through a virtual dispatch. Replace with a plain function pointer + context pointer pair.
- **No channel system.** All subsystems share one logger. There is no way to disable `ECS` verbose logging in a shipping build without also silencing engine lifecycle messages. Channels are required.
- **No build-type level filtering.** Debug builds and Release builds produce the same verbosity. `TRACE` and `INFO` messages from the render graph in a shipping build add measurable overhead in the format and enqueue path even if spdlog drops them at the sink.
- **`Logger::Info/Warn/Error/Critical` take `std::string msg` by value.** The caller already constructed a `std::string` via `fmt::format`; passing by value causes a second copy of the string data into the function. The parameter should be `std::string_view`.
- **Event handler dispatch copies the entire handler map.** The current implementation takes a lock, copies `s_log_event_handlers` into a local, releases the lock, and then iterates the copy. For a map with one or two handlers this is a hidden allocation on every log call. Use a `shared_mutex` so concurrent reads do not copy.

---

## 2. Channel Taxonomy

Every subsystem that emits log messages declares one of the following channels. Channels are the unit of per-build-type filtering.

```cpp
enum class LogChannel : uint8_t {
    Engine   = 0,  // Engine::Initialize, shutdown, lifecycle
    ECS      = 1,  // Scene, WorldTick, Actor creation/destruction
    Render   = 2,  // RenderGraph, VulkanDevice, pipeline/shader compilation
    Physics  = 3,  // PhysicsWorld, body creation, collision events
    Audio    = 4,  // AudioEngine, clip loading, voice management
    Network  = 5,  // NetworkSession, packet dispatch, replication
    VFS      = 6,  // VFSPath, mount table, file I/O
    Asset    = 7,  // AssetManager, import pipeline, cache hits/misses
    UI       = 8,  // UIContext, widget layout, UIRenderer
    Game     = 9,  // game DLL code; all gameplay-layer logs use this channel
    Count    = 10,
};
```

The `Game` channel is reserved for code in the game DLL (or equivalent project layer). Engine subsystems must not use it. This separation lets a game team control their own log verbosity without affecting engine channels.

`LogChannel::Count` is not a valid channel; it exists to size arrays indexed by channel.

---

## 3. Level Policy Per Build Type

The following table defines the minimum level that is forwarded to any sink or event handler for each channel in each build configuration. Messages below the minimum are discarded at the call site with zero overhead when compile-time filtering is active (see Section 5).

| Channel | Debug   | RelWithDebInfo | Release |
|---------|---------|----------------|---------|
| Engine  | TRACE+  | INFO+          | WARN+   |
| ECS     | TRACE+  | WARN+          | ERROR+  |
| Render  | TRACE+  | WARN+          | ERROR+  |
| Physics | INFO+   | WARN+          | ERROR+  |
| Audio   | INFO+   | WARN+          | ERROR+  |
| Network | INFO+   | INFO+          | WARN+   |
| VFS     | INFO+   | WARN+          | ERROR+  |
| Asset   | INFO+   | INFO+          | WARN+   |
| UI      | TRACE+  | WARN+          | ERROR+  |
| Game    | TRACE+  | INFO+          | INFO+   |

`Game` retains `INFO+` in Release because gameplay engineers need actionable runtime diagnostics in shipped builds without a debug binary.

`Network` retains `INFO+` in RelWithDebInfo because packet loss events and session state changes are important for QA testing of networked builds.

These defaults are set at compile time via CMake defines (one per channel, e.g. `ZENGINE_LOG_LEVEL_ECS`). They can be overridden per-channel by defining the corresponding CMake variable; they cannot be changed at runtime without a rebuild. Runtime log level adjustment is intentionally not supported to keep the fast path a simple compile-time constant comparison.

CMake configuration (in `CMakeLists.txt` or `cmake/LoggingDefaults.cmake`):

```cmake
foreach(CHANNEL IN ITEMS ENGINE ECS RENDER PHYSICS AUDIO NETWORK VFS ASSET UI GAME)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(ZENGINE_LOG_LEVEL_${CHANNEL} "WARN" CACHE STRING "")
    elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        set(ZENGINE_LOG_LEVEL_${CHANNEL} "INFO" CACHE STRING "")
    else()
        set(ZENGINE_LOG_LEVEL_${CHANNEL} "TRACE" CACHE STRING "")
    endif()
    target_compile_definitions(ZEngine PUBLIC
        ZENGINE_LOG_LEVEL_${CHANNEL}=ZENGINE_LOG_LEVEL_${ZENGINE_LOG_LEVEL_${CHANNEL}})
endforeach()
```

Valid values: `TRACE=0`, `DEBUG=1`, `INFO=2`, `WARN=3`, `ERROR=4`, `CRITICAL=5`.
Individual channels can be overridden: `-DZENGINE_LOG_LEVEL_ECS=ERROR`

---

## 4. Hot-Path Rules

Logging must not appear inside the following code paths. These paths execute per-entity or per-packet each frame; even a single `ZENGINE_LOG` call inside them will show up in a frame profiler.

**Forbidden logging locations:**

- `ECS::Scene::ForEach` inner loop (the lambda body passed to `ForEach`)
- `WorldTick::Tick` wave dispatch loop
- `ComponentStorage::Get`, `ComponentStorage::Has`, `ComponentStorage::Remove`
- `NetBitWriter` and `NetBitReader` methods
- `ParticleSimulateSystem` inner loop
- Any function that is called more than once per entity per frame, even if not in the above list

**Permitted alternatives:**

- Use `ZENGINE_VALIDATE_ASSERT(condition, message)` to catch invalid state. `VALIDATE_ASSERT` fires once when the condition is first violated (it is not re-entrant per call site) and does not log every frame.
- Accumulate error counters and log the count once at the end of the tick from a safe outer scope.
- Deferred diagnostic events: write a lightweight event record to a ring buffer during the hot path; a background thread or end-of-frame callback formats and logs it outside the hot path.

Violations of this rule are treated as bugs in code review. A linter check for `ZENGINE_LOG` inside `ForEach` lambdas should be added to the CI static analysis pass.

For channels and levels that ARE enabled at compile time, the `fmt::format` call
still executes even if the runtime level filter would suppress it. To avoid this
overhead, use the conditional form:

```cpp
// Only formats the string if ECS TRACE is compiled in:
#if ZENGINE_LOG_CHANNEL_ECS && (ZENGINE_LOG_LEVEL_ECS <= ZENGINE_LOG_LEVEL_TRACE)
    ZENGINE_LOG_ECS_TRACE("entity count: {}", scene.AliveCount());
#endif
```

Or more concisely, use the level-specific macros which already wrap this check.
The compiler eliminates the entire block (including `fmt::format`) in Release builds
where TRACE is disabled.

---

## 5. Updated Macros with Channel and Compile-Time Filtering

Replace `LoggerDefinition.h` with the following structure.

```cpp
// Per-channel compile-time enable/disable.
// These can be overridden by passing -DZENGINE_LOG_CHANNEL_ECS=0 to the compiler.
#ifndef ZENGINE_LOG_CHANNEL_ENGINE
#define ZENGINE_LOG_CHANNEL_ENGINE  1
#endif
#ifndef ZENGINE_LOG_CHANNEL_ECS
#define ZENGINE_LOG_CHANNEL_ECS     1
#endif
#ifndef ZENGINE_LOG_CHANNEL_RENDER
#define ZENGINE_LOG_CHANNEL_RENDER  1
#endif
#ifndef ZENGINE_LOG_CHANNEL_PHYSICS
#define ZENGINE_LOG_CHANNEL_PHYSICS 1
#endif
#ifndef ZENGINE_LOG_CHANNEL_AUDIO
#define ZENGINE_LOG_CHANNEL_AUDIO   1
#endif
#ifndef ZENGINE_LOG_CHANNEL_NETWORK
#define ZENGINE_LOG_CHANNEL_NETWORK 1
#endif
#ifndef ZENGINE_LOG_CHANNEL_VFS
#define ZENGINE_LOG_CHANNEL_VFS     1
#endif
#ifndef ZENGINE_LOG_CHANNEL_ASSET
#define ZENGINE_LOG_CHANNEL_ASSET   1
#endif
#ifndef ZENGINE_LOG_CHANNEL_UI
#define ZENGINE_LOG_CHANNEL_UI      1
#endif
#ifndef ZENGINE_LOG_CHANNEL_GAME
#define ZENGINE_LOG_CHANNEL_GAME    1
#endif

// Core dispatch macro — if constexpr eliminates the body at compile time when disabled.
#define ZENGINE_LOG(channel, level, ...)                                             \
    do {                                                                             \
        if constexpr (ZENGINE_LOG_CHANNEL_##channel) {                               \
            ::ZEngine::Logging::Logger::Log(                                         \
                ::ZEngine::Logging::LogChannel::channel,                             \
                ::ZEngine::Logging::LogLevel::level,                                 \
                fmt::format(__VA_ARGS__)                                              \
            );                                                                       \
        }                                                                            \
    } while (false)

// Per-channel, per-level convenience macros
#define ZENGINE_LOG_ENGINE_INFO(...)     ZENGINE_LOG(ENGINE,  Info,     __VA_ARGS__)
#define ZENGINE_LOG_ENGINE_WARN(...)     ZENGINE_LOG(ENGINE,  Warn,     __VA_ARGS__)
#define ZENGINE_LOG_ENGINE_ERROR(...)    ZENGINE_LOG(ENGINE,  Error,    __VA_ARGS__)
#define ZENGINE_LOG_ENGINE_CRITICAL(...) ZENGINE_LOG(ENGINE,  Critical, __VA_ARGS__)

#define ZENGINE_LOG_ECS_TRACE(...)       ZENGINE_LOG(ECS,     Trace,    __VA_ARGS__)
#define ZENGINE_LOG_ECS_INFO(...)        ZENGINE_LOG(ECS,     Info,     __VA_ARGS__)
#define ZENGINE_LOG_ECS_WARN(...)        ZENGINE_LOG(ECS,     Warn,     __VA_ARGS__)
#define ZENGINE_LOG_ECS_ERROR(...)       ZENGINE_LOG(ECS,     Error,    __VA_ARGS__)

#define ZENGINE_LOG_RENDER_TRACE(...)    ZENGINE_LOG(RENDER,  Trace,    __VA_ARGS__)
#define ZENGINE_LOG_RENDER_WARN(...)     ZENGINE_LOG(RENDER,  Warn,     __VA_ARGS__)
#define ZENGINE_LOG_RENDER_ERROR(...)    ZENGINE_LOG(RENDER,  Error,    __VA_ARGS__)

#define ZENGINE_LOG_NETWORK_INFO(...)    ZENGINE_LOG(NETWORK, Info,     __VA_ARGS__)
#define ZENGINE_LOG_NETWORK_WARN(...)    ZENGINE_LOG(NETWORK, Warn,     __VA_ARGS__)
#define ZENGINE_LOG_NETWORK_ERROR(...)   ZENGINE_LOG(NETWORK, Error,    __VA_ARGS__)

#define ZENGINE_LOG_VFS_INFO(...)        ZENGINE_LOG(VFS,     Info,     __VA_ARGS__)
#define ZENGINE_LOG_VFS_WARN(...)        ZENGINE_LOG(VFS,     Warn,     __VA_ARGS__)
#define ZENGINE_LOG_VFS_ERROR(...)       ZENGINE_LOG(VFS,     Error,    __VA_ARGS__)

#define ZENGINE_LOG_ASSET_INFO(...)      ZENGINE_LOG(ASSET,   Info,     __VA_ARGS__)
#define ZENGINE_LOG_ASSET_WARN(...)      ZENGINE_LOG(ASSET,   Warn,     __VA_ARGS__)

#define ZENGINE_LOG_UI_WARN(...)         ZENGINE_LOG(UI,      Warn,     __VA_ARGS__)
#define ZENGINE_LOG_UI_ERROR(...)        ZENGINE_LOG(UI,      Error,    __VA_ARGS__)

#define ZENGINE_LOG_GAME_TRACE(...)      ZENGINE_LOG(GAME,    Trace,    __VA_ARGS__)
#define ZENGINE_LOG_GAME_INFO(...)       ZENGINE_LOG(GAME,    Info,     __VA_ARGS__)
#define ZENGINE_LOG_GAME_WARN(...)       ZENGINE_LOG(GAME,    Warn,     __VA_ARGS__)
#define ZENGINE_LOG_GAME_ERROR(...)      ZENGINE_LOG(GAME,    Error,    __VA_ARGS__)

// Backwards-compatible aliases — route to ENGINE channel, no source changes required
#define ZENGINE_CORE_INFO(...)     ZENGINE_LOG(ENGINE, Info,     __VA_ARGS__)
#define ZENGINE_CORE_TRACE(...)    ZENGINE_LOG(ENGINE, Trace,    __VA_ARGS__)
#define ZENGINE_CORE_WARN(...)     ZENGINE_LOG(ENGINE, Warn,     __VA_ARGS__)
#define ZENGINE_CORE_ERROR(...)    ZENGINE_LOG(ENGINE, Error,    __VA_ARGS__)
#define ZENGINE_CORE_CRITICAL(...) ZENGINE_LOG(ENGINE, Critical, __VA_ARGS__)
```

The `if constexpr` guard ensures that when a channel is disabled (`ZENGINE_LOG_CHANNEL_ECS = 0`), the compiler eliminates the entire block including the `fmt::format` call. No runtime overhead remains.

---

## 6. Updated Logger::Log Signature

The existing five per-level static functions are supplemented by one unified dispatch function:

```cpp
// New unified entry point
static void Log(LogChannel channel, LogLevel level, std::string_view msg);

// Deprecated shims — keep for source compatibility, remove after migration
// These route to Log(LogChannel::Engine, ...)
static void Info    (std::string_view msg);  // was: std::string by value
static void Trace   (std::string_view msg);
static void Warn    (std::string_view msg);
static void Error   (std::string_view msg);
static void Critical(std::string_view msg);
```

`Log` performs a runtime level check against the minimum level configured for `channel` in the current build (from the level policy table in Section 3, encoded as a `constexpr` array indexed by `LogChannel`). If the message passes, it is forwarded to the spdlog async queue and to any registered event handlers.

`LogLevel` mirrors `LogMessageType` but with a more explicit name:

```cpp
enum class LogLevel : uint8_t {
    Trace    = 0,
    Info     = 1,
    Warn     = 2,
    Error    = 3,
    Critical = 4,
};
```

`LogMessageType` is kept as a `using` alias for `LogLevel` to avoid breaking any serialization or enum comparisons that reference it.

---

## 7. Fix LogEventHandler: std::function to Plain Function Pointer

The current `using LogEventHandler = std::function<void(LogMessage)>` causes a heap allocation for every handler registration that captures state (e.g., the editor log panel capturing `this`).

Replace with an explicit context pointer:

```cpp
// In Logger.h — replace the existing LogEventHandler typedef

using LogEventFn = void (*)(void* ctx, const LogMessage& msg);

struct LogEventHandler {
    LogEventFn Fn  = nullptr;
    void*      Ctx = nullptr;  // passed as first argument to Fn; lifetime managed by caller

    bool IsValid() const { return Fn != nullptr; }
    void Invoke(const LogMessage& msg) const { if (Fn) Fn(Ctx, msg); }
};
```

Registration:

```cpp
// Returns a handle that can be passed to RemoveEventHandler
static uint32_t AddEventHandler(LogEventHandler handler);
static void     RemoveEventHandler(uint32_t handle);
```

Usage at the call site (e.g., editor log panel):

```cpp
// Before: lambda capturing this — causes heap allocation in std::function
m_log_handle = Logger::AddEventHandler([this](LogMessage msg) { AppendToPanel(msg); });

// After: plain function pointer + context
static void OnLogMessage(void* ctx, const LogMessage& msg) {
    static_cast<EditorLogPanel*>(ctx)->AppendToPanel(msg);
}
m_log_handle = Logger::AddEventHandler({ OnLogMessage, this });
```

This is zero-allocation. The `LogEventHandler` struct is 16 bytes on 64-bit platforms (function pointer + `void*`).

`LogMessage` is passed by `const&` instead of by value to avoid copying the message string for each handler invocation.

---

## 8. Fix String Copy in Info/Warn/Error/Critical

Change all per-level function parameters from `std::string msg` (by value, forces a copy) to `std::string_view msg` (non-owning, zero copy):

```cpp
// Before
static void Info(std::string msg);

// After
static void Info(std::string_view msg);
```

`std::string_view` is safe here because the message string is only consumed synchronously within `Log` before being moved into the spdlog async queue. The spdlog enqueue call copies the string into its internal buffer; the view does not need to outlive the `Log` call.

All existing call sites that pass `fmt::format(...)` results are unaffected because `std::string` is implicitly convertible to `std::string_view`.

---

## 9. Fix Event Handler Map Copy

The current implementation acquires a unique lock, copies the entire handler map to a local variable, releases the lock, and then iterates the copy. This pattern exists to avoid holding the lock while invoking handlers (correct) but creates a hidden allocation per log call (incorrect).

The fix uses `std::shared_mutex` to allow concurrent reads with no copy:

```cpp
// In Logger.cpp — internal storage
static UnorderedHashMap<uint32_t, LogEventHandler> s_log_event_handlers;
static std::shared_mutex                           s_handler_mutex;

// In Logger::Log — invoking handlers
{
    std::shared_lock read_lock(s_handler_mutex);  // shared; concurrent reads allowed
    for (auto& [id, handler] : s_log_event_handlers) {
        handler.Invoke(msg);
    }
}

// In Logger::AddEventHandler — mutation
{
    std::unique_lock write_lock(s_handler_mutex);  // exclusive; blocks new readers
    s_log_event_handlers.Insert(next_id, handler);
}

// In Logger::RemoveEventHandler — mutation
{
    std::unique_lock write_lock(s_handler_mutex);
    s_log_event_handlers.Remove(handle);
}
```

Handler registration and removal happen at startup and shutdown, not in the hot path. The `shared_lock` in `Log` allows multiple threads to invoke handlers concurrently without copying the map. Handlers themselves must be thread-safe if the logger is used from multiple threads.

Note: `LogEventHandler::Invoke` calls through a plain function pointer — no virtual dispatch, no `std::function` overhead.

---

## 10. Production Retention

In Release builds, the logging subsystem enforces the following retention policy:

**Sink configuration (Release):**
- Only `ERROR` and `CRITICAL` messages reach the rotating file sink. All other messages are dropped at the `Logger::Log` runtime level check before touching spdlog.
- The rotating file sink retains its existing configuration: max file size 5 MB, 3 rotations.

**`LogMessage` record type:**

```cpp
// LogMessage — the record type stored in the ring buffer and passed to handlers.
// POD — trivially copyable, safe for ring buffer use.
struct LogMessage {
    LogLevel    Level      = LogLevel::Info;
    LogChannel  Channel    = LogChannel::Engine;
    uint64_t    TimestampNs = 0;       // std::chrono::steady_clock nanoseconds
    const char* Message    = nullptr;  // points into a static 2048-byte per-record buffer
    uint16_t    MessageLen = 0;
};
// The Message field is copied into a fixed buffer inside the ring buffer record.
// Do not store pointers to Message across frames.
```

**In-memory ring buffer:**
- A ring buffer of 1000 `LogMessage` records is maintained in the arena. It is written by `Logger::Log` for all messages that pass the per-channel level filter, regardless of whether they reach the file sink. This means INFO and WARN messages appear in the ring buffer even in Release builds.
- The ring buffer uses atomic index advancement so it is lock-free for the write path.
- On a normal shutdown, the ring buffer is not flushed to disk. It is only accessed by the crash handler.

**Crash handler integration:**
```cpp
// In CrashHandler::OnCrash (called by platform SEH/signal handler)
void CrashHandler::OnCrash(CrashContext& ctx)
{
    // Flush pending spdlog queue first (non-blocking, best effort)
    Logger::Flush();

    // Dump ring buffer to crash log file in reverse-chronological order
    Logger::FlushRingBufferToCrashLog(ctx.CrashLogPath);

    // ... write stack trace, minidump, etc.
}
```

`Logger::FlushRingBufferToCrashLog` writes the ring buffer contents directly to a file using platform file I/O, bypassing spdlog, because the spdlog thread pool may not be in a safe state after a crash.

---

## 11. Performance Overhead Budget

The constraint is: logging a message at or above the configured minimum level must take less than 1 microsecond of CPU time on the calling thread. The async flush to disk happens on the spdlog thread pool and is excluded from this budget.

Breakdown of the calling-thread cost for a typical `ZENGINE_LOG_ENGINE_INFO("Entity {} spawned", id)` call:

| Step | Estimated cost |
|------|---------------|
| `if constexpr` channel check | 0 ns (compile-time eliminated when disabled) |
| Runtime level check (array lookup, compare) | ~1 ns |
| `fmt::format` for a short message (no heap for small strings) | ~150–250 ns |
| `Logger::Log` dispatch: shared_lock acquire (uncontended) | ~20 ns |
| Handler invocation (1 handler, function pointer call) | ~5 ns |
| spdlog async enqueue (lock-free MPSC queue write) | ~80–120 ns |
| **Total (approximate)** | **~260–400 ns** |

This is within the 1 microsecond budget with headroom. The dominant cost is `fmt::format`. For messages where formatting is expensive (e.g., formatting a matrix or a long string), the caller is responsible for placing the log call outside the hot path (see Section 4).

Messages that are filtered out (level below minimum) cost only the `if constexpr` check (zero at runtime) plus the runtime level array lookup (~1 ns). The `fmt::format` call is not reached.

---

## 12. Migration Guide

Existing code that uses the `ZENGINE_CORE_*` macros requires no changes. The macros are redefined as aliases to the `ENGINE` channel:

```cpp
// Existing call — continues to compile and behave identically
ZENGINE_CORE_INFO("Shader {} compiled in {}ms", shader_name, elapsed_ms);

// Expands to:
ZENGINE_LOG(ENGINE, Info, "Shader {} compiled in {}ms", shader_name, elapsed_ms);

// Which expands to (ENGINE channel enabled, level passes):
::ZEngine::Logging::Logger::Log(
    ::ZEngine::Logging::LogChannel::ENGINE,
    ::ZEngine::Logging::LogLevel::Info,
    fmt::format("Shader {} compiled in {}ms", shader_name, elapsed_ms)
);
```

The only behavioral difference is that in a Release build the message is now filtered by the `Engine` channel policy (`WARN+`), so `ZENGINE_CORE_INFO` calls are silenced in Release. This is the intended behavior. If an `INFO` call contains information that must appear in Release builds, migrate it to `ZENGINE_LOG_ENGINE_WARN` or `ZENGINE_LOG_GAME_INFO` as appropriate.

Subsystems should be migrated to channel-specific macros opportunistically. There is no flag day. The backwards-compatible macros remain defined indefinitely; they are not scheduled for removal.

**Migration priority order (suggested):**

1. `Render` subsystem — highest per-frame log volume; muting in Release has the most impact
2. `ECS` subsystem — `ForEach` and `ComponentStorage` callers most likely to have hot-path violations
3. `VFS` / `Asset` — I/O paths; channel isolation useful for asset pipeline debugging
4. Remaining subsystems at the team's discretion

---

## 13. Deliverables Checklist

**Fix items (bugs / performance issues in existing code):**

- [ ] Change `LogEventHandler` from `std::function<void(LogMessage)>` to `LogEventFn` + `void*` struct
- [ ] Change `Logger::Info/Trace/Warn/Error/Critical` parameter from `std::string` to `std::string_view`
- [ ] Replace `s_log_event_handlers` copy-on-log pattern with `std::shared_mutex` shared read lock
- [ ] Add `Logger::RemoveEventHandler(uint32_t handle)` (currently missing)

**New functionality:**

- [ ] Add `LogChannel` enum to `Logger.h`
- [ ] Add `LogLevel` enum (rename/alias `LogMessageType`)
- [ ] Add `Logger::Log(LogChannel, LogLevel, std::string_view)` unified dispatch
- [ ] Implement compile-time level policy table (constexpr array `k_min_level[LogChannel::Count][3 build types]`)
- [ ] Add per-channel `ZENGINE_LOG_CHANNEL_*` defines to `LoggerDefinition.h`
- [ ] Add `ZENGINE_LOG(channel, level, ...)` core macro
- [ ] Add all per-channel convenience macros listed in Section 5
- [ ] Redefine `ZENGINE_CORE_*` macros as ENGINE-channel aliases (backwards compatible)
- [ ] Implement in-memory ring buffer (1000 entries, arena-allocated, lock-free write)
- [ ] Implement `Logger::FlushRingBufferToCrashLog(std::string_view path)`
- [ ] Wire `FlushRingBufferToCrashLog` into `CrashHandler::OnCrash`

**Test cases:**

- [ ] `ZENGINE_LOG_CHANNEL_ECS = 0`: compile ECS log call, verify no code generated (check assembly or compile with `-Wunused-value`)
- [ ] Runtime level filter: configure ECS channel minimum to WARN; emit ECS INFO; verify spdlog queue not touched (mock spdlog sink, assert no enqueue)
- [ ] `LogEventHandler` registration: register handler with context pointer; emit log; verify `Fn` called with correct `ctx` and `msg`
- [ ] `RemoveEventHandler`: register, remove, emit — verify handler not called
- [ ] `shared_mutex` correctness: register handler from thread A; emit log from thread B simultaneously — verify no data race (ThreadSanitizer)
- [ ] `std::string_view` safety: pass a temporary `fmt::format(...)` result; verify no dangling reference (the string is consumed before the temporary is destroyed)
- [ ] Ring buffer: emit 1100 messages; verify oldest 100 are overwritten; verify newest 1000 are retrievable via `FlushRingBufferToCrashLog`
- [ ] `FlushRingBufferToCrashLog`: write ring buffer contents to a temp file; verify file contains the expected messages in reverse-chronological order
- [ ] Backwards compatibility: `ZENGINE_CORE_INFO("test")` compiles and routes to ENGINE channel INFO — verified by inspecting the log output channel field in a test handler
- [ ] Performance: measure wall time of 10,000 `ZENGINE_LOG_ENGINE_INFO` calls; verify mean < 1 microsecond per call on a representative test machine
