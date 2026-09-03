# ZEngine — Logging Policy

**Priority:** P0 — Without a policy, hot-path logging kills performance in production
**Status:** Implemented
**Modifies:** `Logger.h`, `Logger.cpp`, `LoggerConfiguration.h`, `LoggerDefinition.h`, `ConsolePanel.h/.cpp (corrected — see note below; doc originally named this LogUIComponent, which does not exist in the shipped code)`, `Obelisk/EntryPoint.cpp`, `Scripts/CMake/LoggingDefaults.cmake`

---

## 1. Current State Analysis

The existing `Logger.h` / `Logger.cpp` provides a working foundation. The following items are assessed:

**What is correct and should be kept:**

- **spdlog async logger with rotating_file_sink.** The async queue decouples the calling thread from I/O. This is production-grade behavior and should not be replaced.
- **`AddEventHandler` for the editor log panel.** The hook mechanism is the right design; its implementation needs repair (see gaps below), not removal.
- **`fmt::format` at all call sites via the `ZENGINE_CORE_*` macros.** `fmt::format` avoids locale-sensitive formatting, is zero-allocation for short messages on its internal buffer, and compiles format strings at compile time with the `FMT_COMPILE` path.

**Gaps that must be fixed:**

- **`LogEventHandler` is `std::function<void(LogMessage)>`.** `std::function` performs a heap allocation for any callable that captures state. In the editor, the log panel (`ConsolePanel`, not `LogUIComponent` — see naming correction below) registers a handler via `std::bind(&ConsolePanel::OnLog, this, ...)` which captures `this`; this allocation happens at startup but the type forces every call site that handles `LogEventHandler` values to go through a virtual dispatch. Replace with a plain function pointer + context pointer pair.
- **`AddEventHandler` has an unguarded write.** `s_log_event_handlers.insert` in `AddEventHandler` is called without holding `s_mutex`, making concurrent registration from two threads a data race. Both `AddEventHandler` and `RemoveEventHandler` must hold a `unique_lock` before mutating the map.
- **No channel system.** All subsystems share one logger. There is no way to disable `ECS` verbose logging in a shipping build without also silencing engine lifecycle messages. Channels are required.
- **No build-type level filtering.** Debug builds and Release builds produce the same verbosity. `TRACE` and `INFO` messages from the render graph in a shipping build add measurable overhead in the format and enqueue path even if spdlog drops them at the sink.
- **`Logger::Info/Warn/Error/Critical` take `std::string msg` by value.** The caller already constructed a `std::string` via `fmt::format`; passing by value causes a second copy of the string data into the function. The parameter should be `std::string_view`.
- **Event handler dispatch copies the entire handler map.** The current implementation takes a lock, copies `s_log_event_handlers` into a local, releases the lock, and then iterates the copy. For a map with one or two handlers this is a hidden allocation on every log call. Use a `shared_mutex` so concurrent reads do not copy.

---

## 2. Channel Taxonomy

Every subsystem that emits log messages declares one of the following channels. Channels are the unit of per-build-type filtering.

```cpp
enum class LogChannel : uint8_t {
    ENGINE   = 0,  // Engine::Initialize, shutdown, lifecycle
    ECS      = 1,  // Scene, WorldTick, Actor creation/destruction
    RENDER   = 2,  // RenderGraph, VulkanDevice, pipeline/shader compilation
    PHYSICS  = 3,  // PhysicsWorld, body creation, collision events
    AUDIO    = 4,  // AudioEngine, clip loading, voice management
    NETWORK  = 5,  // NetworkSession, packet dispatch, replication
    VFS      = 6,  // VFSPath, mount table, file I/O
    ASSET    = 7,  // AssetManager, import pipeline, cache hits/misses
    UI       = 8,  // UIContext, widget layout, UIRenderer
    GAME     = 9,  // game DLL code; all gameplay-layer logs use this channel
    COUNT    = 10,
};
```

The `Game` channel is reserved for code in the game DLL (or equivalent project layer). Engine subsystems must not use it. This separation lets a game team control their own log verbosity without affecting engine channels.

`LogChannel::COUNT` is not a valid channel; it exists to size arrays indexed by channel.

Enum values are uppercase so the `##channel` token in the `ZENGINE_LOG` macro pastes directly into a valid `LogChannel::ENGINE`, `LogChannel::ECS`, etc. without a name-mapping layer.

---

## 3. Level Policy Per Build Type

The following table defines the minimum level that is forwarded to any sink or event handler for each channel in each build configuration. Messages below the minimum are discarded at the call site with zero overhead when compile-time filtering is active (see Section 5).

| Channel | Debug   | RelWithDebInfo | Release |
|---------|---------|----------------|---------|
| Engine  | TRACE+  | INFO+          | WARN+   |
| ECS     | TRACE+  | WARN+          | ERR+    |
| Render  | TRACE+  | WARN+          | ERR+    |
| Physics | INFO+   | WARN+          | ERR+    |
| Audio   | INFO+   | WARN+          | ERR+    |
| Network | INFO+   | INFO+          | WARN+   |
| VFS     | INFO+   | WARN+          | ERR+    |
| Asset   | INFO+   | INFO+          | WARN+   |
| UI      | TRACE+  | WARN+          | ERR+    |
| Game    | TRACE+  | INFO+          | INFO+   |

`Game` retains `INFO+` in Release because gameplay engineers need actionable runtime diagnostics in shipped builds without a debug binary.

`Network` retains `INFO+` in RelWithDebInfo because packet loss events and session state changes are important for QA testing of networked builds.

These defaults are set at compile time via CMake defines (one per channel, e.g. `ZENGINE_LOG_LEVEL_ECS`) and are overridable per-channel by defining the corresponding CMake variable.

**Correction**: contrary to what this paragraph originally claimed, runtime log level adjustment
*is* supported — `k_min_level` (`Logger.cpp:36`) is a mutable `static int` array, not a
`constexpr` one, and `Logger::SetMinLevel(LogChannel, LogLevel)` / `Logger::SetMinLevelAllChannels(LogLevel)`
exist and mutate it directly. The comment above the array in the shipped code states it's mutable
specifically so tests can lower the gate at runtime. The fast path in `Logger::Log` is still a
simple integer comparison against `k_min_level[channel]` — the compile-time CMake defines set the
*initial* values, but nothing prevents changing them afterward.

The CMake snippet below sets a uniform baseline per build type, then applies the per-channel overrides that diverge from that baseline (matching the table above). The baseline is `TRACE` for Debug, `INFO` for RelWithDebInfo, and `WARN` for Release.

CMake configuration (in `CMakeLists.txt` or `cmake/LoggingDefaults.cmake`):

```cmake
# Step 1 — set uniform baseline per build type
foreach(CHANNEL IN ITEMS ENGINE ECS RENDER PHYSICS AUDIO NETWORK VFS ASSET UI GAME)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(ZENGINE_LOG_LEVEL_${CHANNEL} "WARN" CACHE STRING "")
    elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        set(ZENGINE_LOG_LEVEL_${CHANNEL} "INFO" CACHE STRING "")
    else()
        set(ZENGINE_LOG_LEVEL_${CHANNEL} "TRACE" CACHE STRING "")
    endif()
endforeach()

# Step 2 — apply per-channel overrides that differ from the baseline
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(ZENGINE_LOG_LEVEL_ECS     "ERR" CACHE STRING "" FORCE)
    set(ZENGINE_LOG_LEVEL_RENDER  "ERR" CACHE STRING "" FORCE)
    set(ZENGINE_LOG_LEVEL_PHYSICS "ERR" CACHE STRING "" FORCE)
    set(ZENGINE_LOG_LEVEL_AUDIO   "ERR" CACHE STRING "" FORCE)
    set(ZENGINE_LOG_LEVEL_VFS     "ERR" CACHE STRING "" FORCE)
    set(ZENGINE_LOG_LEVEL_UI      "ERR" CACHE STRING "" FORCE)
    set(ZENGINE_LOG_LEVEL_GAME    "INFO"  CACHE STRING "" FORCE)
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(ZENGINE_LOG_LEVEL_ECS     "WARN"  CACHE STRING "" FORCE)
    set(ZENGINE_LOG_LEVEL_RENDER  "WARN"  CACHE STRING "" FORCE)
    set(ZENGINE_LOG_LEVEL_PHYSICS "WARN"  CACHE STRING "" FORCE)
    set(ZENGINE_LOG_LEVEL_AUDIO   "WARN"  CACHE STRING "" FORCE)
    set(ZENGINE_LOG_LEVEL_VFS     "WARN"  CACHE STRING "" FORCE)
    set(ZENGINE_LOG_LEVEL_UI      "WARN"  CACHE STRING "" FORCE)
else()
    set(ZENGINE_LOG_LEVEL_PHYSICS "INFO"  CACHE STRING "" FORCE)
    set(ZENGINE_LOG_LEVEL_AUDIO   "INFO"  CACHE STRING "" FORCE)
    set(ZENGINE_LOG_LEVEL_NETWORK "INFO"  CACHE STRING "" FORCE)
    set(ZENGINE_LOG_LEVEL_VFS     "INFO"  CACHE STRING "" FORCE)
    set(ZENGINE_LOG_LEVEL_ASSET   "INFO"  CACHE STRING "" FORCE)
endif()

# Step 3 — emit compile definitions
foreach(CHANNEL IN ITEMS ENGINE ECS RENDER PHYSICS AUDIO NETWORK VFS ASSET UI GAME)
    target_compile_definitions(ZEngine PUBLIC
        ZENGINE_LOG_LEVEL_${CHANNEL}=ZENGINE_LOG_LEVEL_${ZENGINE_LOG_LEVEL_${CHANNEL}})
endforeach()
```

Valid values: `TRACE=0`, `INFO=1`, `WARN=2`, `ERR=3`, `CRITICAL=4`.
Individual channels can be overridden at configure time: `-DZENGINE_LOG_LEVEL_ECS=ERR`

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

For channels and levels that are enabled at compile time, `fmt::format` still executes even if the runtime level filter would suppress it. The per-channel, per-level convenience macros (Section 5) include a compile-time level guard that eliminates the `fmt::format` call entirely when the level is below the configured minimum for that channel:

```cpp
// The convenience macro already guards both channel and level at compile time:
ZENGINE_LOG_ECS_TRACE("entity count: {}", scene.AliveCount());
// In RelWithDebInfo (ECS minimum = WARN), the entire call including fmt::format
// is eliminated by the compiler. No runtime overhead remains.
```

Do not hand-write `#if` guards around log calls. Use the convenience macros; they encode the check correctly.

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

// Numeric level constants — used in compile-time comparisons below.
// These must match the LogLevel enum ordinals exactly; both are used to populate and
// compare against k_min_level[] in Logger::Log. Any mismatch causes runtime filtering
// to disagree with the compile-time guard. There is no DEBUG level (spdlog has one;
// ZEngine does not expose it).
#define ZENGINE_LOG_LEVEL_TRACE    0
#define ZENGINE_LOG_LEVEL_INFO     1
#define ZENGINE_LOG_LEVEL_WARN     2
#define ZENGINE_LOG_LEVEL_ERR      3
#define ZENGINE_LOG_LEVEL_CRITICAL 4

// Core dispatch macro.
// Guards: (1) channel enabled at compile time, (2) level at or above the
// configured minimum for this channel. Both checks are compile-time constants;
// the compiler eliminates the entire block — including fmt::format — when either
// check fails. No runtime overhead remains for filtered-out calls.
#define ZENGINE_LOG(channel, level, ...)                                                  \
    do {                                                                                  \
        if constexpr (ZENGINE_LOG_CHANNEL_##channel &&                                   \
                      (ZENGINE_LOG_LEVEL_##level >= ZENGINE_LOG_LEVEL_##channel))         \
        {                                                                                 \
            ::ZEngine::Logging::Logger::Log(                                              \
                ::ZEngine::Logging::LogChannel::channel,                                  \
                ::ZEngine::Logging::LogLevel::level,                                      \
                fmt::format(__VA_ARGS__)                                                  \
            );                                                                            \
        }                                                                                 \
    } while (false)

// Per-channel, per-level convenience macros.
// All levels are defined for every channel so callers can freely use any level
// without needing to know which levels are active — filtered levels compile to nothing.
#define ZENGINE_LOG_ENGINE_TRACE(...)    ZENGINE_LOG(ENGINE,  TRACE,    __VA_ARGS__)
#define ZENGINE_LOG_ENGINE_INFO(...)     ZENGINE_LOG(ENGINE,  INFO,     __VA_ARGS__)
#define ZENGINE_LOG_ENGINE_WARN(...)     ZENGINE_LOG(ENGINE,  WARN,     __VA_ARGS__)
#define ZENGINE_LOG_ENGINE_ERR(...)      ZENGINE_LOG(ENGINE,  ERR,      __VA_ARGS__)
#define ZENGINE_LOG_ENGINE_CRITICAL(...) ZENGINE_LOG(ENGINE,  CRITICAL, __VA_ARGS__)

#define ZENGINE_LOG_ECS_TRACE(...)       ZENGINE_LOG(ECS,     TRACE,    __VA_ARGS__)
#define ZENGINE_LOG_ECS_INFO(...)        ZENGINE_LOG(ECS,     INFO,     __VA_ARGS__)
#define ZENGINE_LOG_ECS_WARN(...)        ZENGINE_LOG(ECS,     WARN,     __VA_ARGS__)
#define ZENGINE_LOG_ECS_ERR(...)         ZENGINE_LOG(ECS,     ERR,      __VA_ARGS__)

#define ZENGINE_LOG_RENDER_TRACE(...)    ZENGINE_LOG(RENDER,  TRACE,    __VA_ARGS__)
#define ZENGINE_LOG_RENDER_INFO(...)     ZENGINE_LOG(RENDER,  INFO,     __VA_ARGS__)
#define ZENGINE_LOG_RENDER_WARN(...)     ZENGINE_LOG(RENDER,  WARN,     __VA_ARGS__)
#define ZENGINE_LOG_RENDER_ERR(...)      ZENGINE_LOG(RENDER,  ERR,      __VA_ARGS__)

#define ZENGINE_LOG_PHYSICS_INFO(...)    ZENGINE_LOG(PHYSICS, INFO,     __VA_ARGS__)
#define ZENGINE_LOG_PHYSICS_WARN(...)    ZENGINE_LOG(PHYSICS, WARN,     __VA_ARGS__)
#define ZENGINE_LOG_PHYSICS_ERR(...)     ZENGINE_LOG(PHYSICS, ERR,      __VA_ARGS__)

#define ZENGINE_LOG_AUDIO_INFO(...)      ZENGINE_LOG(AUDIO,   INFO,     __VA_ARGS__)
#define ZENGINE_LOG_AUDIO_WARN(...)      ZENGINE_LOG(AUDIO,   WARN,     __VA_ARGS__)
#define ZENGINE_LOG_AUDIO_ERR(...)       ZENGINE_LOG(AUDIO,   ERR,      __VA_ARGS__)

#define ZENGINE_LOG_NETWORK_INFO(...)    ZENGINE_LOG(NETWORK, INFO,     __VA_ARGS__)
#define ZENGINE_LOG_NETWORK_WARN(...)    ZENGINE_LOG(NETWORK, WARN,     __VA_ARGS__)
#define ZENGINE_LOG_NETWORK_ERR(...)     ZENGINE_LOG(NETWORK, ERR,      __VA_ARGS__)

#define ZENGINE_LOG_VFS_INFO(...)        ZENGINE_LOG(VFS,     INFO,     __VA_ARGS__)
#define ZENGINE_LOG_VFS_WARN(...)        ZENGINE_LOG(VFS,     WARN,     __VA_ARGS__)
#define ZENGINE_LOG_VFS_ERR(...)         ZENGINE_LOG(VFS,     ERR,      __VA_ARGS__)

#define ZENGINE_LOG_ASSET_INFO(...)      ZENGINE_LOG(ASSET,   INFO,     __VA_ARGS__)
#define ZENGINE_LOG_ASSET_WARN(...)      ZENGINE_LOG(ASSET,   WARN,     __VA_ARGS__)

#define ZENGINE_LOG_UI_TRACE(...)        ZENGINE_LOG(UI,      TRACE,    __VA_ARGS__)
#define ZENGINE_LOG_UI_WARN(...)         ZENGINE_LOG(UI,      WARN,     __VA_ARGS__)
#define ZENGINE_LOG_UI_ERR(...)          ZENGINE_LOG(UI,      ERR,      __VA_ARGS__)

#define ZENGINE_LOG_GAME_TRACE(...)      ZENGINE_LOG(GAME,    TRACE,    __VA_ARGS__)
#define ZENGINE_LOG_GAME_INFO(...)       ZENGINE_LOG(GAME,    INFO,     __VA_ARGS__)
#define ZENGINE_LOG_GAME_WARN(...)       ZENGINE_LOG(GAME,    WARN,     __VA_ARGS__)
#define ZENGINE_LOG_GAME_ERR(...)        ZENGINE_LOG(GAME,    ERR,      __VA_ARGS__)

// Backwards-compatible aliases — route to ENGINE channel, no source changes required.
// Note: ZENGINE_CORE_INFO calls are silenced in Release builds (ENGINE minimum = WARN+).
// See Section 12 for the migration audit step before upgrading.
#define ZENGINE_CORE_INFO(...)     ZENGINE_LOG(ENGINE, INFO,     __VA_ARGS__)
#define ZENGINE_CORE_TRACE(...)    ZENGINE_LOG(ENGINE, TRACE,    __VA_ARGS__)
#define ZENGINE_CORE_WARN(...)     ZENGINE_LOG(ENGINE, WARN,     __VA_ARGS__)
#define ZENGINE_CORE_ERROR(...)    ZENGINE_LOG(ENGINE, ERR,      __VA_ARGS__)
#define ZENGINE_CORE_CRITICAL(...) ZENGINE_LOG(ENGINE, CRITICAL, __VA_ARGS__)
```

The double compile-time guard — channel enabled AND level at or above the configured minimum — ensures that when ECS is at `WARN+`, any `ZENGINE_LOG_ECS_TRACE` or `ZENGINE_LOG_ECS_INFO` call is fully eliminated by the compiler, including the `fmt::format` call. No runtime overhead remains.

The `ZENGINE_LOG_LEVEL_##channel` token (e.g. `ZENGINE_LOG_LEVEL_ECS`) is emitted by the CMake step in Section 3 as a numeric constant. The comparison `>= ZENGINE_LOG_LEVEL_##level` is therefore an integer constant expression, which `if constexpr` can evaluate at compile time.

---

## 6. Updated Logger::Log Signature

The existing five per-level static functions are supplemented by one unified dispatch function:

```cpp
// New unified entry point
static void Log(LogChannel channel, LogLevel level, std::string_view msg);

// Deprecated shims — keep for source compatibility, remove after migration.
// These route to Log(LogChannel::ENGINE, ...)
static void Info    (std::string_view msg);  // was: std::string by value
static void Trace   (std::string_view msg);
static void Warn    (std::string_view msg);
static void Error   (std::string_view msg);
static void Critical(std::string_view msg);
```

`Log` performs a runtime level check against the minimum level configured for `channel` in the current build (from the level policy table in Section 3, a mutable array indexed by `LogChannel` — see the correction in §3, it is not `constexpr`). If the message passes, it is forwarded to the spdlog async queue and to any registered event handlers.

`LogLevel` replaces `LogMessageType` with explicit numeric values that match the compile-time constants in Section 5, and uses uppercase names so the `##level` paste in `ZENGINE_LOG` resolves to valid enum members. The existing `LogMessageType` had a different ordinal order (`Info=0, Trace=1, ...`) which would silently corrupt any serialised log records.

**Correction**: `using LogMessageType = LogLevel` was never added — there is zero reference to
`LogMessageType` anywhere in the shipped `Logging/` code. The migration went with a clean cut to
`LogLevel` instead of keeping a compatibility alias; any code that serialises or persists
`LogMessageType` as a raw integer must be audited and remapped, but there is no alias to lean on
during that audit — every call site had to be updated directly:

```cpp
enum class LogLevel : uint8_t {
    TRACE    = 0,
    INFO     = 1,  // NOTE: was LogMessageType::Info = 0 in the old enum; ordinals differ
    WARN     = 2,
    ERR      = 3,  // named ERR, not ERROR — ERROR is a macro on Windows (expands to 0),
                   // making LogLevel::ERROR a compile error at every token-paste site
    CRITICAL = 4,
};

// PLANNED, NOT SHIPPED: using LogMessageType = LogLevel;
// The alias was never added — see the correction above this code block.
```

---

## 7. Fix LogEventHandler: std::function to Plain Function Pointer

The current `using LogEventHandler = std::function<void(LogMessage)>` causes a heap allocation for every handler registration that captures state (e.g., `ConsolePanel` registers via `std::bind(&ConsolePanel::OnLog, this, ...)`).

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

Usage at the call site (e.g., `ConsolePanel`):

```cpp
// Before: std::bind capturing this — causes heap allocation in std::function
m_handler_cookie = Logger::AddEventHandler(std::bind(&ConsolePanel::OnLog, this, std::placeholders::_1));

// After: plain function pointer + context
static void OnLogMessage(void* ctx, const LogMessage& msg) {
    static_cast<ConsolePanel*>(ctx)->OnLog(msg);
}
m_handler_cookie = Logger::AddEventHandler({ OnLogMessage, this });
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

## 9. Fix Event Handler Map: Unguarded Write and Lock-Copy Pattern

Two bugs exist in the current implementation:

**Bug 1 — unguarded write in `AddEventHandler`:** `s_log_event_handlers.insert` is called without holding `s_mutex`. Concurrent calls to `AddEventHandler` from two threads (e.g. two subsystems initialising in parallel) are a data race. Fix: acquire `unique_lock` before every mutation.

**Bug 2 — map copy on every log call:** The current `Info/Warn/Error/Critical` implementations take a `unique_lock`, copy the entire handler map to a local, release the lock, then iterate the copy. For a map with one or two entries this is a hidden allocation on every log call. Fix: use `std::shared_mutex` so concurrent reads share the lock without copying.

```cpp
// In Logger.cpp — internal storage
static UnorderedHashMap<uint32_t, LogEventHandler> s_log_event_handlers;
static std::shared_mutex                           s_handler_mutex;

// In Logger::Log — invoking handlers (shared read, no copy)
{
    std::shared_lock read_lock(s_handler_mutex);
    for (auto& [id, handler] : s_log_event_handlers) {
        handler.Invoke(msg);
    }
}

// In Logger::AddEventHandler — mutation (exclusive write)
{
    std::unique_lock write_lock(s_handler_mutex);
    s_log_event_handlers.Insert(next_id, handler);
}

// In Logger::RemoveEventHandler — mutation (exclusive write)
{
    std::unique_lock write_lock(s_handler_mutex);
    s_log_event_handlers.Remove(handle);
}
```

Handler registration and removal happen at startup and shutdown, not in the hot path. The `shared_lock` in `Log` allows multiple threads to invoke handlers concurrently without copying the map. Handlers themselves must be thread-safe if the logger is used from multiple threads.

---

## 10. LogMessage Record Type and Production Retention

**Updated `LogMessage` struct:**

The current `LogMessage` carries a `float Color[4]` field that `ConsolePanel` uses to drive its ZUI text color (this doc's original wording said "ImGui text color" — the shipped panel is ZUI-based, not ImGui). This field must be retained. The updated struct adds `LogChannel` and renames `Type` to `Level` for consistency with `LogLevel`, while keeping `Color` so the editor log panel requires no changes to its rendering code:

```cpp
// LogMessage — the record type passed to event handlers and stored in the ring buffer.
// Fields ordered for minimal padding (40 bytes on 64-bit; was 48 bytes before reorder).
struct LogMessage {
    float       Color[4]    = {0.0f};  // offset  0 — RGBA; set by Logger::Log from LogLevel
    const char* Message     = nullptr; // offset 16 — points into a parallel arena String buffer
    uint64_t    TimestampNs = 0;       // offset 24 — std::chrono::steady_clock nanoseconds
    uint16_t    MessageLen  = 0;       // offset 32
    LogLevel    Level       = LogLevel::TRACE;   // offset 34
    LogChannel  Channel     = LogChannel::ENGINE; // offset 35
    // 4 bytes tail padding — total 40 bytes
};
```

Message storage uses two parallel arena-allocated ring arrays:
- `s_log_message_rb` — the `LogMessage` records (Color, TimestampNs, Level, Channel, pointer)
- `s_log_raw_string_rb` — one `Core::Containers::String` per slot, pre-allocated at 2048 bytes each

`LogMessage::Message` points into the corresponding `String` slot. The pointer is valid for the lifetime of that ring slot. Once the ring wraps and the slot is overwritten the pointer becomes invalid. Do not store `Message` pointers across frames or after calling `FlushRingBufferToCrashLog`.

`Logger::Log` derives `Color` from the `LogLevel` using the same mapping the current code uses (`INFO` = green, `WARN` = orange, `ERR`/`CRITICAL` = red, `TRACE` = grey). This keeps `ConsolePanel` working without modification after migration.

**Sink configuration (Release):**
- Only `ERR` and `CRITICAL` messages reach the rotating file sink. All other messages are dropped at the `Logger::Log` runtime level check before touching spdlog.
- The rotating file sink retains its existing configuration: max file size 5 MB, 3 rotations.

**In-memory ring buffer:**
- A ring buffer of 1000 `LogMessage` records is maintained in the arena. It is written by `Logger::Log` for all messages that pass the per-channel level filter, regardless of whether they reach the file sink. INFO and WARN messages appear in the ring buffer even in Release builds.
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
| Compile-time channel + level check (`if constexpr`) | 0 ns (eliminated when filtered) |
| Runtime level check (array lookup, compare) | ~1 ns |
| `fmt::format` for a short message (no heap for small strings) | ~150–250 ns |
| `Logger::Log` dispatch: `shared_lock` acquire (uncontended) | ~20 ns |
| Handler invocation (1 handler, function pointer call) | ~5 ns |
| spdlog async enqueue (lock-free MPSC queue write) | ~80–120 ns |
| **Total (approximate)** | **~260–400 ns** |

This is within the 1 microsecond budget with headroom. The dominant cost is `fmt::format`. For messages where formatting is expensive (e.g., formatting a matrix or a long string), the caller is responsible for placing the log call outside the hot path (see Section 4).

Messages that are filtered out cost only the compile-time `if constexpr` check — zero at runtime. The `fmt::format` call is not reached.

---

## 12. Migration Guide

Existing code that uses the `ZENGINE_CORE_*` macros requires no changes at the call site. The macros are redefined as aliases to the `ENGINE` channel:

```cpp
// Existing call — continues to compile and behave identically
ZENGINE_CORE_INFO("Shader {} compiled in {}ms", shader_name, elapsed_ms);

// Expands to:
ZENGINE_LOG(ENGINE, INFO, "Shader {} compiled in {}ms", shader_name, elapsed_ms);

// Which expands to (ENGINE channel enabled, level passes in Debug):
::ZEngine::Logging::Logger::Log(
    ::ZEngine::Logging::LogChannel::ENGINE,
    ::ZEngine::Logging::LogLevel::INFO,
    fmt::format("Shader {} compiled in {}ms", shader_name, elapsed_ms)
);
```

**Behavior change in Release:** After migration, all `ZENGINE_CORE_INFO` calls are silenced in Release builds because the ENGINE channel minimum is `WARN+`. This is intentional but is a silent behavior change — no compiler warning is emitted. Before upgrading, grep for `ZENGINE_CORE_INFO` (and `ZENGINE_CORE_TRACE`) in code paths that execute in Release and audit each call: if the message must appear in a shipped build, migrate it to `ZENGINE_LOG_ENGINE_WARN` or `ZENGINE_LOG_GAME_INFO` as appropriate.

Subsystems should be migrated to channel-specific macros opportunistically. There is no flag day. The backwards-compatible macros remain defined indefinitely; they are not scheduled for removal.

**Migration priority order (suggested):**

1. `Render` subsystem — highest per-frame log volume; muting in Release has the most impact
2. `ECS` subsystem — `ForEach` and `ComponentStorage` callers most likely to have hot-path violations
3. `VFS` / `Asset` — I/O paths; channel isolation useful for asset pipeline debugging
4. Remaining subsystems at the team's discretion

---

## 13. Deliverables Checklist

**Fix items (bugs / performance issues in existing code):**

- [x] Change `LogEventHandler` from `std::function<void(LogMessage)>` to `LogEventFn` + `void*` struct
- [x] Change `Logger::Log` parameter to `std::string_view` (the old per-level shims were removed entirely; the unified `Log(LogChannel, LogLevel, std::string_view)` replaces them)
- [x] Fix unguarded write in `AddEventHandler` — acquire `unique_lock` before `s_log_event_handlers.insert`
- [x] Replace `s_log_event_handlers` copy-on-log pattern with `std::shared_mutex` shared read lock
- [x] Update `ConsolePanel::AddEventHandler` call from `std::bind` to plain function pointer + context

**New functionality:**

- [x] Add `LogChannel` enum to `Logger.h`
- [x] Add `LogLevel` enum with ordinals matching Section 5 constants (`using LogMessageType = LogLevel` was planned but never added — zero references to `LogMessageType` exist in the shipped code; call sites were migrated directly instead)
- [x] Add `Logger::Log(LogChannel, LogLevel, std::string_view)` unified dispatch
- [x] Implement runtime level policy table (mutable `static int` array `k_min_level[LogChannel::COUNT]` — not `constexpr`, see correction in §3) — initialized from the CMake-emitted `ZENGINE_LOG_LEVEL_*` defines, mutable at runtime via `Logger::SetMinLevel`/`SetMinLevelAllChannels`; `Logger::Log` early-returns before touching spdlog, the ring buffer, or event handlers when the message level is below the channel minimum
- [x] Add per-channel `ZENGINE_LOG_CHANNEL_*` and `ZENGINE_LOG_LEVEL_*` defines to `LoggerDefinition.h`
- [x] Add `ZENGINE_LOG(channel, level, ...)` core macro with dual compile-time guard
- [x] Add all per-channel convenience macros listed in Section 5
- [x] Redefine `ZENGINE_CORE_*` macros as ENGINE-channel aliases (backwards compatible)
- [x] Add `Color` derivation in `Logger::Log` from `LogLevel` (keeps `ConsolePanel` working)
- [x] Implement in-memory ring buffer (arena-allocated, lock-free write; default size 1024 from `LoggerConfiguration::RingBufferSize`)
- [x] Implement `Logger::FlushRingBufferToCrashLog(std::string_view path)` and no-arg overload (uses `CrashLogDir` stored at `Initialize` time)
- [x] Wire `FlushRingBufferToCrashLog` into crash handler — done via `CrashHandler::SetPreCrashCallback` in `Obelisk/EntryPoint.cpp`
- [x] Emit per-channel level defines from CMake — two-step: baseline + per-channel overrides in `Scripts/CMake/LoggingDefaults.cmake`; `target_compile_definitions` emit in `ZEngine/ZEngine/CMakeLists.txt`

**Additional fixes surfaced by tests:**

- [x] Fix `Logger::Initialize` to treat `OutputDirectory` as absolute when it already is one — was unconditionally prepending `current_path()`, breaking any caller passing an absolute path
- [x] Fix `Logger::Dispose` to call `spdlog::drop()` per registered logger and reset `s_crash_log_dir` — allows `Initialize` to be called again cleanly (required for test teardown and engine hot-reload)

**Test cases:**

- [ ] `ZENGINE_LOG_CHANNEL_ECS = 0`: compile ECS log call, verify no code generated (check assembly or compile with `-Wunused-value`)
- [ ] Compile-time level filter: configure ECS channel minimum to WARN; call `ZENGINE_LOG_ECS_INFO`; verify the `fmt::format` call is absent in the generated assembly
- [ ] Runtime level filter: direct `Logger::Log(ECS, TRACE, ...)` call in Debug build with ECS min=WARN — verify handler not invoked
- [x] `LogEventHandler` registration: register handler with context pointer; emit log; verify `Fn` called with correct `ctx`, `Level`, `Channel`, and `Message`
- [x] `RemoveEventHandler`: register, remove, emit — verify handler not called
- [x] `AddEventHandler` thread safety: 8 threads register handlers concurrently; emit one message; verify all 8 handlers received it
- [x] `shared_mutex` correctness: 4 threads emit 200 messages concurrently while one handler counts — verify final count equals total emitted
- [ ] `std::string_view` safety: pass a temporary `fmt::format(...)` result; verify no dangling reference (the string is consumed before the temporary is destroyed)
- [x] Ring buffer wrap: emit `RingBufferSize + 16` messages; verify flushed file has at most `RingBufferSize` lines
- [x] `FlushRingBufferToCrashLog`: emit three messages at different levels; verify file has most-recent entry first
- [x] `FlushRingBufferToCrashLog` no-arg: verify file is written under `CrashLogDir`
- [x] `LogMessage.Color`: verify `Logger::Log` sets correct RGBA values for each `LogLevel`
- [x] Backwards compatibility: `ZENGINE_CORE_INFO("test")` routes to `LogChannel::ENGINE` / `LogLevel::INFO`
- [ ] Performance: add a Google Benchmark target (`BM_LogEngineInfo`) measuring 10,000 `ZENGINE_LOG_ENGINE_INFO` calls and reporting ns/op — not a unit test assertion, to avoid flakiness on loaded CI runners
