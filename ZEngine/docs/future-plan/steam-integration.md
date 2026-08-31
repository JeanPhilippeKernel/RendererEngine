# ZEngine — Steam Integration

**Priority:** P2 — Required to distribute on Steam
**Status:** Design
**Depends on:** `save-system.md` (Steam Cloud), `audio-system.md` (Steam audio?)
**Blocks:** Steam distribution, achievements, cloud saves

**Goal**: Integrate the Steamworks SDK into ZEngine as a thin, optional C++ wrapper behind
a compile-time feature gate (`ZENGINE_STEAM`). When Steam is present and running, the
engine initialises the SDK, drives `SteamAPI_RunCallbacks` each frame, unlocks
achievements, and synchronises save data with Steam Cloud. When Steam is absent (non-Steam
build, dedicated server, or editor build without a Steam client), every `SteamManager`
call is a silent no-op and the game continues normally. No `new`/`delete`, no exceptions,
no RTTI — consistent with all other ZEngine subsystems.

---

## 1. Steamworks SDK Overview

The Steamworks SDK (distributed by Valve as a ZIP from the Steamworks partner portal)
provides a C++ interface (`steam_api.h`) to the following platform services:

| Service | Description |
|---|---|
| Steam Overlay | In-game browser/friends overlay triggered by Shift+Tab or `SteamFriends()->ActivateGameOverlay()` |
| Achievements | Per-user unlock state stored on Steam servers; shown as notifications in-overlay |
| Leaderboards | Global and friends-only ranked score boards |
| Cloud Saves | Per-user remote file storage (`ISteamRemoteStorage`); up to 100MB per user |
| DLC | Downloadable content; ownership checked via `ISteamApps()->BIsDlcInstalled()` |
| Matchmaking | Lobby / session browser via `ISteamMatchmaking` |
| App Ownership | `ISteamApps()->BIsSubscribed()` — is this Steam account subscribed to the game? |
| App Ticket | Encrypted app-ticket DRM (separate from ownership check) |
| P2P Networking | NAT-punched peer-to-peer via `ISteamNetworkingMessages` |
| Input | Steam Input abstraction over controller/keyboard/mouse |

### v1 Scope (this document)

| Feature | Status |
|---|---|
| SDK initialisation / shutdown | v1 |
| `SteamAPI_RunCallbacks` per frame | v1 |
| Steam Overlay (passive; pause while active) | v1 |
| Achievements (unlock, query) | v1 |
| Cloud saves (read / write / delete) | v1 |
| App ID configuration | v1 |
| App ownership sanity check | v1 |

### v2 Deferred

| Feature | Reason deferred |
|---|---|
| Leaderboards | Requires game design decisions (score schema) |
| DLC entitlement checks | Not needed until DLC is planned |
| Matchmaking / lobbies | Multiplayer feature; out of v1 scope |
| Steam Input | Separate input-system ticket |
| Encrypted App Ticket DRM | Revenue protection; implement before public launch |
| Rich Presence | Nice-to-have; low priority |

---

## 2. SteamManager — C++ Wrapper

`SteamManager` is a pure-static class (no instances, no heap allocation) that wraps the
Steamworks C-style API. It is the only translation unit in ZEngine that includes
`steam_api.h` directly; all other engine code interacts with Steam exclusively through
`SteamManager`.

```cpp
// ZEngine/Platform/Steam/SteamManager.h
#pragma once
#include <cstdint>
#include "Core/Types.h"   // cstring = const char*; uint32_t already provided by <cstdint>

namespace ZEngine::Platform {

    class SteamManager {
    public:
        SteamManager()                             = delete;
        SteamManager(const SteamManager&)          = delete;
        SteamManager& operator=(const SteamManager&) = delete;

        // Lifecycle

        /// Calls SteamAPI_RestartAppIfNecessary, then SteamAPI_Init.
        /// Returns true if Steam is running and the SDK initialised successfully.
        /// Must be called before any other SteamManager function.
        /// Logs error and returns false (does not abort) on failure.
        [[nodiscard]] static bool Initialize();

        /// Calls SteamAPI_Shutdown. Safe to call even if Initialize returned false.
        static void Shutdown();

        /// Calls SteamAPI_RunCallbacks. Must be called once per frame on the main thread.
        /// No-op if !IsAvailable().
        static void Tick();

        /// Returns true if Initialize() succeeded and the Steam client is running.
        static bool IsAvailable();

        // User info

        /// Returns the 64-bit Steam ID of the local user. Returns 0 if !IsAvailable().
        static uint64_t GetSteamID();

        /// Returns the Steam display name of the local user. Returns "" if !IsAvailable().
        static cstring GetPlayerName();

        // Achievements

        /// Unlocks a Steam achievement by its API name (e.g. "ACH_WIN_ONE_GAME").
        /// Idempotent — safe to call on an already-unlocked achievement.
        /// Returns true if the call was forwarded to the SDK (regardless of prior state).
        /// Returns false if !IsAvailable() or the API name is null.
        [[nodiscard]] static bool UnlockAchievement(cstring api_name);

        /// Returns true if the named achievement is already unlocked for the local user.
        /// Result is read from a local cache populated at Initialize() time.
        [[nodiscard]] static bool IsAchievementUnlocked(cstring api_name);

        /// Clears all achievement progress for the local user.
        /// DEV ONLY — must be compiled out in shipping builds (#ifndef ZENGINE_SHIPPING).
        static void ResetAllAchievements();

        // Cloud saves  (ISteamRemoteStorage)

        /// Writes `size` bytes from `data` to a Steam Cloud file named `filename`.
        /// Returns true on success. Returns false if !IsCloudEnabled() or on SDK error.
        [[nodiscard]] static bool CloudWrite(cstring filename, const void* data, uint32_t size);

        /// Reads a Steam Cloud file into `out_data` (caller-allocated, capacity `max_size`).
        /// Sets `out_size` to the number of bytes actually read.
        /// Returns true on success. Returns false if the file does not exist or on error.
        [[nodiscard]] static bool CloudRead(
            cstring  filename,
            void*    out_data,
            uint32_t max_size,
            uint32_t& out_size
        );

        /// Deletes a Steam Cloud file. Returns true on success.
        static bool CloudDelete(cstring filename);

        /// Returns true if the user has Steam Cloud enabled for this app.
        static bool IsCloudEnabled();

        // Overlay  (ISteamFriends)

        /// Opens a specific overlay panel. Valid dialog names:
        ///   "friends", "community", "players", "settings",
        ///   "achievements", "store"
        /// No-op if !IsAvailable() or the overlay is already active.
        static void OpenOverlay(cstring dialog);

        /// Returns true if the Steam Overlay is currently displayed.
        /// Game simulation should be paused while this returns true.
        static bool IsOverlayActive();

    private:
        static bool     s_Initialized;
        // Atomic: written by SteamOverlayActivated_t callback (main thread via RunCallbacks),
        // read by IsOverlayActive() which may be called from any thread.
        static std::atomic<bool> s_OverlayActive{false};

        // Local achievement cache: StringHash(api_name) → unlocked
        // Populated by RequestCurrentStats callback at Initialize() time.
        // Using ZEngine containers to avoid STL dependency in this header.
        // Defined in SteamManager.cpp to avoid including Core containers in this header.
    };

} // namespace ZEngine::Platform
```

### 2.1 Non-Steam Build Stubs

When `ZENGINE_STEAM` is not defined, a separate header provides inline no-ops so the
rest of the engine compiles without `#ifdef` guards scattered throughout:

```cpp
// ZEngine/Platform/Steam/SteamManager_Stub.h  (included when !ZENGINE_STEAM)
#pragma once
#include <cstdint>
#include "Core/Types.h"

namespace ZEngine::Platform {

    class SteamManager {
    public:
        static bool     Initialize()                                          { return false; }
        static void     Shutdown()                                            {}
        static void     Tick()                                                {}
        static bool     IsAvailable()                                         { return false; }
        static uint64_t GetSteamID()                                          { return 0; }
        static cstring  GetPlayerName()                                       { return ""; }
        static bool     UnlockAchievement(cstring)                            { return false; }
        static bool     IsAchievementUnlocked(cstring)                        { return false; }
        static void     ResetAllAchievements()                                {}
        static bool     CloudWrite(cstring, const void*, uint32_t)            { return false; }
        static bool     CloudRead(cstring, void*, uint32_t, uint32_t& s)      { s = 0; return false; }
        static bool     CloudDelete(cstring)                                  { return false; }
        static bool     IsCloudEnabled()                                      { return false; }
        static void     OpenOverlay(cstring)                                  {}
        static bool     IsOverlayActive()                                     { return false; }
    };

} // namespace ZEngine::Platform
```

The engine's CMake configuration chooses which header to expose based on the
`ZENGINE_STEAM` option:

```cmake
if(ZENGINE_STEAM)
    target_sources(ZEngine PRIVATE Platform/Steam/SteamManager.cpp)
    target_compile_definitions(ZEngine PUBLIC ZENGINE_STEAM=1)
    # Full header exposed via include path
else()
    # Stub header copied to the generated include path; no .cpp needed
endif()
```

---

## 3. Initialization Sequence

Steam must be initialised before any other engine subsystem that might display UI or play
audio, because the Steam Overlay hooks into the graphics API at `SteamAPI_Init` time.

### 3.1 steam_appid.txt

During development (before the game has a published App ID), create a file named
`steam_appid.txt` in the process working directory containing only the numeric App ID:

```
480
```

(480 is Valve's public test app "Spacewar"; replace with your own App ID before shipping.)

`SteamAPI_RestartAppIfNecessary` reads this file when the game is launched outside of
Steam. The file must NOT be shipped in the final build — Valve's build system strips it
automatically; CI should assert its absence in release packages.

**CRITICAL — Release build requirement:**
`steam_appid.txt` MUST NOT be present in any release package or installer.
Shipping this file leaks your development App ID and allows bypassing Steam ownership checks.

Add to `build-integration.md` CPack configuration:
```cmake
# Exclude development files from release packages
list(APPEND CPACK_IGNORE_FILES
    "/steam_appid\\.txt$"
    "\\.zplugin\\.tmp$"
)
```
Also add a CI check that fails the release build if `steam_appid.txt` is found in the
output directory:
```cmake
add_custom_command(TARGET ZRuntime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Checking for steam_appid.txt..."
    COMMAND ${CMAKE_COMMAND} -DFILE=$<TARGET_FILE_DIR:ZRuntime>/steam_appid.txt
            -P "${CMAKE_SOURCE_DIR}/cmake/CheckNoSteamAppId.cmake"
    COMMENT "Verifying steam_appid.txt is absent from release"
)
```

### 3.2 Initialisation Steps

```cpp
// ZEngine/Platform/Steam/SteamManager.cpp
#ifdef ZENGINE_STEAM
// Steamworks SDK 1.57+ required.
// Vendored at __externals/steamworks/ — see cmake/Dependencies.cmake.
// Must NOT be included in non-ZENGINE_STEAM builds (guarded by #ifdef ZENGINE_STEAM).
#include "steam/steam_api.h"
#endif

#include "Platform/Steam/SteamManager.h"
#include "Core/Log/Logger.h"
#include "Core/Containers/UnorderedHashMap.h"
#include "Core/Memory/ArenaAllocator.h"

namespace ZEngine::Platform {

bool SteamManager::s_Initialized  = false;
std::atomic<bool> SteamManager::s_OverlayActive{false};

// Achievement cache — allocated from a dedicated small arena on Initialize()
static Core::Containers::UnorderedHashMap<uint32_t /*StringHash*/, bool>* s_AchievementCache = nullptr;
static Core::Memory::ArenaAllocator                                        s_SteamArena;

bool SteamManager::Initialize() {
#ifdef ZENGINE_STEAM
    // Step 1: If not launched via Steam, relaunch through Steam and exit.
    // This sets the App ID from steam_appid.txt in development.
    if (SteamAPI_RestartAppIfNecessary(ZENGINE_STEAM_APP_ID)) {
        // The process will exit; the Steam client relaunches us.
        return false;
    }

    // Step 2: Initialise the Steamworks API.
    if (!SteamAPI_Init()) {
        ZENGINE_LOG_ERROR("[Steam] SteamAPI_Init failed. Steam client is not running.");
        return false;
    }

    // Step 3: Request current stats (includes achievement state).
    // The callback STEAM_CALLBACK(OnUserStatsReceived) populates s_AchievementCache.
    SteamUserStats()->RequestCurrentStats();

    s_Initialized = true;
    ZENGINE_LOG_INFO("[Steam] Initialised. User: %s (SteamID: %llu)",
        SteamFriends()->GetPersonaName(),
        SteamUser()->GetSteamID().ConvertToUint64()
    );
    return true;
#else
    return false;
#endif
}

void SteamManager::Shutdown() {
#ifdef ZENGINE_STEAM
    if (s_Initialized) {
        SteamAPI_Shutdown();
        s_Initialized = false;
    }
#endif
}

} // namespace ZEngine::Platform
```

### 3.3 Wire Into Engine::Initialize

```cpp
// ZEngine/Engine/Engine.cpp  (abbreviated)
bool Engine::Initialize(const EngineConfig& config) {
    // 1. Create window / Vulkan surface
    if (!m_Window.Initialize(config.WindowSpec)) return false;

    // 2. Steam (before asset loading; after window so overlay can hook Vulkan)
    if (!Platform::SteamManager::Initialize()) {
        if constexpr (ZENGINE_STEAM) {
            // Steam required for distribution builds — show message, exit
            m_Window.ShowMessageBox("Steam Required",
                "This game requires Steam to be running. Please launch it via Steam.");
            return false;
        }
        // Non-Steam build: continue without Steam
    }

    // 3. Asset loading, renderer init, scene init ...
    return true;
}
```

The `if constexpr` branch means non-Steam builds never reference the `ZENGINE_STEAM`
constant as a runtime bool — the dead-code is eliminated at compile time.

---

## 4. Frame Tick

`SteamAPI_RunCallbacks()` dispatches all pending Steamworks callbacks (overlay state
changes, cloud sync notifications, achievement store results, etc.) to their registered
C++ callback objects. It must be called on the main thread, once per frame, before any
game logic that might depend on updated Steam state.

```cpp
void SteamManager::Tick() {
#ifdef ZENGINE_STEAM
    if (!s_Initialized) return;
    SteamAPI_RunCallbacks();

    // Update overlay active state (polled; no direct callback for this).
    // Tick() must be called on the main thread only (Steamworks SDK is not thread-safe).
    // IsOverlayActive() is thread-safe and can be called from render or game threads.
    //
    // Explicit release ordering so that threads reading IsOverlayActive()
    // with acquire ordering see a consistent value.
    bool overlay = SteamUtils()->IsOverlayEnabled() && SteamUtils()->BOverlayNeedsPresent();
    s_OverlayActive.store(overlay, std::memory_order_release);
#endif
}
```

Wire into the main engine loop:

```cpp
// ZEngine/Engine/Engine.cpp
void Engine::MainThreadRun() {
    while (!m_Window.ShouldClose()) {
        // Frame start
        Platform::SteamManager::Tick();   // must be first; may update s_OverlayActive

        // Pause simulation while Steam overlay is active
        const bool simulationPaused =
            Platform::SteamManager::IsOverlayActive() || m_Window.IsMinimized();

        m_Input.Poll();

        if (!simulationPaused) {
            m_Scene.Update(m_FrameTimer.DeltaTime());
        }

        m_Renderer.RenderFrame();
        m_FrameTimer.Tick();
        // Frame end
    }
}
```

The overlay check must use `SteamManager::IsOverlayActive()` rather than a raw
`SteamUtils()` call so non-Steam builds compile cleanly (the stub returns `false`).

---

## 5. Achievements

### 5.1 Design Principles

Achievements are **game-defined**: the engine provides the `SteamManager::UnlockAchievement`
mechanism; the game code defines the `AchievementID` enum and the conditions for
unlocking.

Reasoning: the engine cannot know what constitutes a "win" or a "first kill" in any
particular game. Keeping achievement logic in game code prevents the engine from encoding
game-specific rules.

### 5.2 AchievementID Pattern

```cpp
// GameCode/Achievements.h  (game-layer, not engine-layer)
#pragma once

namespace MyGame {

    // String API names must match the names entered in the Steamworks partner portal.
    // Keep this enum and the kAchievementApiNames table in sync.
    enum class AchievementID : uint32_t {
        WinOneGame    = 0,
        KillFirstEnemy,
        ReachChapter2,
        CollectAllGems,
        Count
    };

    constexpr const char* kAchievementApiNames[] = {
        "ACH_WIN_ONE_GAME",
        "ACH_KILL_FIRST_ENEMY",
        "ACH_REACH_CHAPTER2",
        "ACH_COLLECT_ALL_GEMS",
    };

} // namespace MyGame
```

Game code unlocks achievements by calling:

```cpp
ZEngine::Platform::SteamManager::UnlockAchievement(
    MyGame::kAchievementApiNames[static_cast<uint32_t>(MyGame::AchievementID::WinOneGame)]
);
```

### 5.3 Local Cache

The `SteamManager` caches achievement unlock state in a `UnorderedHashMap<uint32_t, bool>`
(keyed by `StringHash(api_name)`) populated when the `UserStatsReceived_t` callback fires
after `RequestCurrentStats()`. `IsAchievementUnlocked` reads from this cache — it never
calls `SteamUserStats()->GetAchievement` on every query, avoiding per-frame Steamworks
overhead.

```cpp
bool SteamManager::IsAchievementUnlocked(cstring api_name) {
#ifdef ZENGINE_STEAM
    if (!s_Initialized || !api_name) return false;
    uint32_t hash = Core::StringHash(api_name);
    bool* cached  = s_AchievementCache->Find(hash);
    if (cached) return *cached;

    // Cache miss: query the SDK and populate
    bool unlocked = false;
    SteamUserStats()->GetAchievement(api_name, &unlocked);
    s_AchievementCache->Insert(hash, unlocked);
    return unlocked;
#else
    return false;
#endif
}

bool SteamManager::UnlockAchievement(cstring api_name) {
#ifdef ZENGINE_STEAM
    if (!s_Initialized || !api_name) return false;

    uint32_t hash = Core::StringHash(api_name);
    // Avoid redundant SDK calls for already-unlocked achievements
    if (IsAchievementUnlocked(api_name)) return true;

    SteamUserStats()->SetAchievement(api_name);
    SteamUserStats()->StoreStats(); // persist to Steam servers
    s_AchievementCache->Insert(hash, true); // update cache
    return true;
#else
    return false;
#endif
}
```

`StoreStats()` is called immediately after `SetAchievement` to flush the unlock to the
Steam backend. The SDK queues the network request internally; it does not block.

### 5.4 Dev Reset

```cpp
void SteamManager::ResetAllAchievements() {
#if defined(ZENGINE_STEAM) && !defined(ZENGINE_SHIPPING)
    if (!s_Initialized) return;
    SteamUserStats()->ResetAllStats(/*bAchievementsToo*/ true);
    SteamUserStats()->StoreStats();
    if (s_AchievementCache) s_AchievementCache->Clear();
    ZENGINE_LOG_WARN("[Steam] All achievements reset (dev only).");
#endif
}
```

This function must not exist in shipping builds. The `#ifndef ZENGINE_SHIPPING` guard
ensures the function body is compiled out; the declaration in the header is also guarded
by the same macro so calling it is a compile error in shipping mode.

---

## 6. Steam Cloud Saves

### 6.1 Integration Point

Cloud saves do not replace the local save system — they augment it. The save system
(defined in `save-system.md`) writes save data to local disk using the VFS. After a
successful local write, it optionally mirrors the data to Steam Cloud.

### 6.2 File Size Budget

Steam allows up to **100 MB** of total cloud storage per user per app. ZEngine enforces
a soft warning at **1 MB per save file** and a hard cap at **20 MB per save file** (to
stay well within the quota even with multiple slots).

```cpp
namespace ZEngine::Platform {
    constexpr uint32_t kSteamCloudWarnSizeBytes  = 1  * 1024 * 1024; //  1 MB
    constexpr uint32_t kSteamCloudHardSizeBytes  = 20 * 1024 * 1024; // 20 MB
}
```

### 6.3 CloudWrite

```cpp
bool SteamManager::CloudWrite(cstring filename, const void* data, uint32_t size) {
#ifdef ZENGINE_STEAM
    if (!s_Initialized || !IsCloudEnabled()) return false;
    if (size > kSteamCloudHardSizeBytes) {
        ZENGINE_LOG_ERROR("[Steam Cloud] Write rejected: size %u exceeds hard cap %u",
            size, kSteamCloudHardSizeBytes);
        return false;
    }
    if (size > kSteamCloudWarnSizeBytes) {
        ZENGINE_LOG_WARN("[Steam Cloud] Save file '%s' is %u bytes (>1MB); "
                         "consider compressing save data.", filename, size);
    }

    bool ok = SteamRemoteStorage()->FileWrite(filename, data, static_cast<int32>(size));
    if (!ok) {
        ZENGINE_LOG_ERROR("[Steam Cloud] FileWrite failed for '%s'", filename);
    }
    return ok;
#else
    return false;
#endif
}
```

### 6.4 CloudRead

```cpp
bool SteamManager::CloudRead(
    cstring  filename,
    void*    out_data,
    uint32_t max_size,
    uint32_t& out_size)
{
#ifdef ZENGINE_STEAM
    if (!s_Initialized) { out_size = 0; return false; }

    if (!SteamRemoteStorage()->FileExists(filename)) {
        out_size = 0;
        return false;
    }

    int32 fileSize = SteamRemoteStorage()->GetFileSize(filename);
    if (fileSize <= 0 || static_cast<uint32_t>(fileSize) > max_size) {
        ZENGINE_LOG_ERROR("[Steam Cloud] Read rejected: file '%s' is %d bytes, buffer is %u",
            filename, fileSize, max_size);
        out_size = 0;
        return false;
    }

    int32 bytesRead = SteamRemoteStorage()->FileRead(
        filename, out_data, static_cast<int32>(max_size)
    );
    out_size = static_cast<uint32_t>(bytesRead);
    return bytesRead > 0;
#else
    out_size = 0;
    return false;
#endif
}
```

### 6.5 Save System Integration Pattern

The save system calls Steam Cloud after a successful local write:

```cpp
// In SaveSystem::SaveGame(uint32_t slot, const SaveData& data)
bool SaveSystem::SaveGame(uint32_t slot, const SaveData& data) {
    // Step 1: Serialise to a scratch buffer (ArenaAllocator-backed)
    uint32_t size = Serialise(data, m_ScratchBuffer, m_ScratchBufferSize);
    if (size == 0) return false;

    // Step 2: Write to local disk via VFS
    char localPath[256];
    FormatSavePath(localPath, sizeof(localPath), slot);
    bool localOk = m_VFS.WriteFile(localPath, m_ScratchBuffer, size);
    if (!localOk) return false;

    // Step 3: Mirror to Steam Cloud (best-effort; do not fail save on cloud error)
    if (Platform::SteamManager::IsCloudEnabled()) {
        char cloudPath[256];
        FormatCloudSaveName(cloudPath, sizeof(cloudPath), slot);
        bool cloudOk = Platform::SteamManager::CloudWrite(cloudPath, m_ScratchBuffer, size);
        if (!cloudOk) {
            ZENGINE_LOG_WARN("[Save] Steam Cloud write failed for slot %u; local save intact.",
                slot);
        }
    }
    return true; // local save succeeded; cloud is best-effort
}

bool SaveSystem::LoadGame(uint32_t slot, SaveData& out_data) {
    char localPath[256];
    FormatSavePath(localPath, sizeof(localPath), slot);

    // Step 1: Prefer local file
    if (m_VFS.FileExists(localPath)) {
        uint32_t size = m_VFS.ReadFile(localPath, m_ScratchBuffer, m_ScratchBufferSize);
        return size > 0 && Deserialise(m_ScratchBuffer, size, out_data);
    }

    // Step 2: Fall back to Steam Cloud if local file is missing
    if (Platform::SteamManager::IsCloudEnabled()) {
        char cloudPath[256];
        FormatCloudSaveName(cloudPath, sizeof(cloudPath), slot);
        uint32_t cloudSize = 0;
        bool cloudOk = Platform::SteamManager::CloudRead(
            cloudPath, m_ScratchBuffer, m_ScratchBufferSize, cloudSize
        );
        if (cloudOk && cloudSize > 0) {
            ZENGINE_LOG_INFO("[Save] Loaded slot %u from Steam Cloud (no local file found).", slot);
            // Write to local disk so future loads are fast
            m_VFS.WriteFile(localPath, m_ScratchBuffer, cloudSize);
            return Deserialise(m_ScratchBuffer, cloudSize, out_data);
        }
    }

    return false; // no save found anywhere
}
```

---

## 7. Steam Overlay

### 7.1 How It Works

The Steam Overlay is a Valve-managed in-game layer that renders over the game's swap
chain. It hooks into the Vulkan present queue at `SteamAPI_Init` time. The overlay
becomes visible when the user presses Shift+Tab or when the game calls
`OpenOverlay("achievements")` (for example, at the end of a level).

### 7.2 Overlay Active Pause

While the overlay is visible, the user cannot interact with the game. ZEngine pauses
game simulation (world update, physics, AI) but continues rendering (so the frame
presented behind the overlay is valid). Input events are consumed by the overlay and
must not be forwarded to game systems.

The pause check is already wired into `Engine::MainThreadRun` (see section 4). The
game UI layer should additionally suppress button presses for one frame after the overlay
closes to avoid accidental UI interaction when the user presses a key to dismiss the
overlay.

### 7.3 Opening the Overlay Programmatically

```cpp
// Show achievements at end of game session
Platform::SteamManager::OpenOverlay("achievements");

// Direct user to the game's store page
Platform::SteamManager::OpenOverlay("store");
```

### 7.4 IsOverlayActive Implementation

```cpp
bool SteamManager::IsOverlayActive() {
#ifdef ZENGINE_STEAM
    return s_Initialized && s_OverlayActive.load(std::memory_order_acquire);
#else
    return false;
#endif
}
```

`s_OverlayActive` is updated in `Tick()` by polling `SteamUtils()->BOverlayNeedsPresent()`
(see section 4). A Steamworks `GameOverlayActivated_t` callback would be more correct but
requires a registered callback object; the polling approach is simpler for v1 and has
negligible overhead.

---

## 8. App Ownership Check

After `SteamAPI_Init` succeeds, verify that the authenticated Steam user owns this game.
This is not DRM (Valve's platform handles DRM separately via encrypted app tickets); it is
a **sanity check** to catch edge cases where a user is running a build they obtained
through unofficial means.

```cpp
// In Engine::Initialize, after SteamManager::Initialize():
#ifdef ZENGINE_STEAM
if (Platform::SteamManager::IsAvailable()) {
    if (!SteamApps()->BIsSubscribed()) {
        ZENGINE_LOG_ERROR("[Steam] App ownership check failed: user does not own this game.");
        m_Window.ShowMessageBox(
            "Ownership Error",
            "This game must be purchased through Steam. "
            "Please visit the Steam store page to get a copy."
        );
        return false;
    }
}
#endif
```

Notes:
- `BIsSubscribed()` returns true for free-to-play games regardless of purchase.
- Do NOT gate critical game features on this check at runtime; only check at startup.
- Do NOT use this as the sole DRM mechanism — use the App Ticket API before public launch
  if DRM is a requirement.

---

## 9. Build Integration

### 9.1 Directory Layout

```
ZEngine/
└── __externals/
    └── steamworks/
        ├── include/
        │   ├── steam_api.h
        │   ├── isteamuser.h
        │   ├── isteamfriends.h
        │   ├── isteamuserstats.h
        │   ├── isteamremotestorage.h
        │   └── ... (all Steamworks headers)
        └── lib/
            ├── win64/
            │   ├── steam_api64.lib      (import lib)
            │   └── steam_api64.dll      (runtime DLL)
            ├── linux64/
            │   └── libsteam_api.so
            └── osx/
                └── libsteam_api.dylib
```

The Steamworks SDK must NOT be committed to a public repository — it is under NDA.
Add `__externals/steamworks/` to `.gitignore` and provide a `scripts/fetch_steamworks.py`
that downloads it from an internal artifact store using developer credentials.

### 9.2 CMakeLists.txt Changes

```cmake
# ZEngine/CMakeLists.txt

option(ZENGINE_STEAM "Enable Steamworks SDK integration" OFF)

if(ZENGINE_STEAM)
    # Locate the vendored SDK
    set(STEAMWORKS_ROOT "${CMAKE_SOURCE_DIR}/__externals/steamworks")

    if(NOT EXISTS "${STEAMWORKS_ROOT}/include/steam_api.h")
        message(FATAL_ERROR
            "Steamworks SDK not found at ${STEAMWORKS_ROOT}.\n"
            "Run scripts/fetch_steamworks.py to download it."
        )
    endif()

    # Platform-specific library selection
    if(WIN32)
        set(STEAM_API_LIB "${STEAMWORKS_ROOT}/lib/win64/steam_api64.lib")
        set(STEAM_API_DLL "${STEAMWORKS_ROOT}/lib/win64/steam_api64.dll")
    elseif(UNIX AND NOT APPLE)
        set(STEAM_API_LIB "${STEAMWORKS_ROOT}/lib/linux64/libsteam_api.so")
    elseif(APPLE)
        set(STEAM_API_LIB "${STEAMWORKS_ROOT}/lib/osx/libsteam_api.dylib")
    endif()

    target_include_directories(ZEngine PRIVATE "${STEAMWORKS_ROOT}/include")
    target_link_libraries(ZEngine PRIVATE "${STEAM_API_LIB}")
    target_compile_definitions(ZEngine PUBLIC
        ZENGINE_STEAM=1
        ZENGINE_STEAM_APP_ID=${ZENGINE_STEAM_APP_ID}
    )

    # Copy the runtime DLL/SO to the output directory post-build (Windows + Linux)
    if(WIN32)
        add_custom_command(TARGET ZEngine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${STEAM_API_DLL}"
                "$<TARGET_FILE_DIR:ZEngine>/steam_api64.dll"
            COMMENT "Copying steam_api64.dll to output directory"
        )
    elseif(UNIX AND NOT APPLE)
        add_custom_command(TARGET ZEngine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${STEAM_API_LIB}"
                "$<TARGET_FILE_DIR:ZEngine>/libsteam_api.so"
            COMMENT "Copying libsteam_api.so to output directory"
        )
    endif()

    message(STATUS "[ZEngine] Steamworks integration ENABLED (App ID: ${ZENGINE_STEAM_APP_ID})")
else()
    message(STATUS "[ZEngine] Steamworks integration DISABLED (non-Steam build)")
endif()
```

Configure from the command line:

```sh
cmake -DZENGINE_STEAM=ON -DZENGINE_STEAM_APP_ID=123456 ..
```

### 9.3 CMake Preset for Steam Builds

```json
// CMakePresets.json (excerpt)
{
  "name": "steam-release",
  "displayName": "Steam Release Build",
  "inherits": "release",
  "cacheVariables": {
    "ZENGINE_STEAM":        "ON",
    "ZENGINE_STEAM_APP_ID": "123456",
    "ZENGINE_SHIPPING":     "ON"
  }
}
```

---

## 10. Non-Steam Builds

When `ZENGINE_STEAM` is not defined:

- `SteamManager.h` expands to `SteamManager_Stub.h` (inline no-ops, see section 2.1)
- No `steam_api.h` is included anywhere in the engine
- No `steam_api64.dll` / `libsteam_api.so` is linked or copied
- `SteamManager::IsAvailable()` returns `false`; all feature gates based on it are
  inactive at runtime
- The game must not hard-fail on any `SteamManager` call returning `false` — local saves
  still work via VFS, achievements are silently skipped, the overlay is never shown

Platforms that always build without Steam:
- Dedicated server builds (no display, no user identity needed)
- CI test builds (Steam client not available on CI runners)
- Console ports (use platform-native achievement / cloud SDKs instead)
- Offline devkit builds

The stub design means that non-Steam feature branches never need `#ifdef ZENGINE_STEAM`
guards in game code; the abstraction is complete at the `SteamManager` boundary.

---

## 11. Deliverables Checklist

- [ ] `__externals/steamworks/` fetch script (`scripts/fetch_steamworks.py`) and `.gitignore` entry
- [ ] `ZEngine/Platform/Steam/SteamManager.h` — full class declaration (section 2)
- [ ] `ZEngine/Platform/Steam/SteamManager_Stub.h` — inline no-ops for non-Steam builds (section 2.1)
- [ ] `ZEngine/Platform/Steam/SteamManager.cpp` — full implementation behind `#ifdef ZENGINE_STEAM` (sections 3, 5, 6, 7, 8)
- [ ] `CMakeLists.txt` updated with `ZENGINE_STEAM` option, include paths, library linkage, and DLL copy step (section 9.2)
- [ ] `CMakePresets.json` `steam-release` preset (section 9.3)
- [ ] `Engine::Initialize` wired: `SteamManager::Initialize()` called after window creation (section 3.3)
- [ ] `Engine::MainThreadRun` wired: `SteamManager::Tick()` called first each frame (section 4)
- [ ] Overlay pause logic in main loop: simulation halted while `IsOverlayActive()` (section 4)
- [ ] `SaveSystem::SaveGame` / `LoadGame` updated to mirror data to Steam Cloud (section 6.5)
- [ ] App ownership check (`BIsSubscribed()`) in `Engine::Initialize` with user-facing message (section 8)
- [ ] `steam_appid.txt` documented in `CONTRIBUTING.md`; absent from release packages (section 3.1)
- [ ] `ResetAllAchievements()` guarded by `#ifndef ZENGINE_SHIPPING`; callable from dev console (section 5.4)
- [ ] Integration test: launch game without Steam running, verify graceful "Steam Required" dialog or silent fallback depending on build type
- [ ] Integration test: unlock achievement, close and relaunch, verify `IsAchievementUnlocked` returns true from cache
- [ ] Integration test: write cloud save, delete local file, reload — verify cloud read fallback (section 6.5)
