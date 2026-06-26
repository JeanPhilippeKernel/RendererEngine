# ZEngine — Plugin Store

**Priority:** Next-year plan — enables plugin developers to publish and monetize their work
**Status:** Design
**Depends on:** `plugin-system.md`, `python-plugin-host.md`, `steam-integration.md`

---

## 1. Overview

The ZEngine Plugin Store is a hosted marketplace where plugin developers publish plugins
(C++, Python, Lua, or C#) and engine users discover, install, and update them. It is
accessible from inside the editor and from a web portal.

**Goals:**
- Plugin developers can publish, version, and optionally monetize their plugins
- Engine users can browse, install, and update plugins with one click from the editor
- The engine auto-downloads and installs plugin updates in the background
- Free and paid plugins are both supported
- The store is language-agnostic — C++, Python, Lua, and C# plugins are all first-class

**What the store is not:**
- Not a game asset marketplace (textures, meshes, audio) — that is a separate product
- Not an app store with curation gatekeeping — plugins are published directly after
  automated validation, with a reporting system for bad actors

---

## 2. Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    ZEngine Editor                           │
│   Plugin Store Panel  ←→  StoreClient (HTTP + VFS)          │
└──────────────────────────┬──────────────────────────────────┘
                           │ HTTPS REST API
┌──────────────────────────▼──────────────────────────────────┐
│                    Store Backend (cloud)                     │
│   Plugin Registry   Payment Service   CDN (plugin binaries) │
└─────────────────────────────────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────┐
│                    Plugin Developer                          │
│   CLI tool: zplugin publish / zplugin yank                  │
└─────────────────────────────────────────────────────────────┘
```

The store backend is a hosted service operated by the ZEngine team. Plugin binaries
are served from a CDN. The editor's `StoreClient` communicates with the backend over
HTTPS REST. Authentication uses API keys for developers and OAuth2 for end users.

---

## 3. Plugin Package for Store Submission

A store-ready plugin extends the standard `.zplugin` descriptor with store metadata:

```json
{
    "name":               "NavMeshPlugin",
    "version":            "1.2.0",
    "sdk_version":        "1.0",
    "author":             "YourStudio",
    "description":        "Recast/Detour NavMesh and pathfinding for ZEngine.",
    "homepage":           "https://yourstudio.com/navmesh",
    "license":            "MIT",
    "engine_version_min": "1.0.0",
    "engine_version_max": "2.0.0",
    "dependencies":       [],

    "store": {
        "display_name":   "NavMesh Plugin",
        "category":       "AI",
        "tags":           ["navigation", "pathfinding", "AI", "navmesh"],
        "price_usd":      0,
        "thumbnail":      "store/thumbnail.png",
        "screenshots":    ["store/screen1.png", "store/screen2.png"],
        "changelog":      "CHANGELOG.md",
        "support_email":  "support@yourstudio.com",
        "repository":     "https://github.com/yourstudio/navmesh-plugin"
    }
}
```

**Store-specific fields:**
- `category` — one of: `AI`, `Rendering`, `Physics`, `Audio`, `Networking`,
  `Scripting`, `Tools`, `Import/Export`, `UI`, `Utilities`
- `price_usd` — 0 for free; any positive value for paid (minimum $1.00)
- `thumbnail` — 512×512 PNG shown in the store browser
- `screenshots` — up to 6 images shown on the plugin detail page
- `changelog` — markdown file with per-version release notes

---

## 4. Plugin Developer Workflow

### 4.1 Publishing a plugin

```bash
# Install the CLI tool (distributed as a standalone binary)
# https://store.zengine.dev/cli

# Authenticate with the store
zplugin login

# Validate the plugin package (checks descriptor, validates binary exports,
# runs automated compatibility tests against the current engine SDK)
zplugin validate ./NavMeshPlugin/

# Publish — uploads binaries to CDN, registers metadata in the store
zplugin publish ./NavMeshPlugin/

# Output:
# Validating NavMeshPlugin 1.2.0...
#   [ok] Descriptor valid
#   [ok] Windows binary: NavMeshPlugin.dll — exports ZPlugin_GetDescriptor
#   [ok] Linux binary:   NavMeshPlugin.so  — exports ZPlugin_GetDescriptor
#   [ok] macOS binary:   NavMeshPlugin.dylib — exports ZPlugin_GetDescriptor
#   [ok] SDK version 1.0 compatible
#   [ok] Screenshot dimensions valid
# Publishing NavMeshPlugin 1.2.0...
#   Uploading Windows binary... done (2.3 MB)
#   Uploading Linux binary...   done (2.1 MB)
#   Uploading macOS binary...   done (2.4 MB)
#   Registering metadata...     done
# Published: https://store.zengine.dev/plugins/navmesh-plugin
```

### 4.2 Updating a plugin

```bash
# Bump version in NavMeshPlugin.zplugin, rebuild, then:
zplugin publish ./NavMeshPlugin/

# Previous versions remain available for users pinned to them.
# Users on auto-update receive the new version automatically.
```

### 4.3 Yanking a broken release

```bash
# Yanked versions are hidden from new installs but remain available
# for users who already have them pinned.
zplugin yank NavMeshPlugin 1.2.0 --reason "Critical crash on Linux"
```

### 4.4 Developer dashboard

A web portal at `https://store.zengine.dev/dashboard` shows:
- Download counts per version per platform
- Revenue and payout history (for paid plugins)
- User reviews and ratings
- Crash reports submitted by users (if the engine's crash handler is enabled)
- Active support tickets

---

## 5. Store REST API

The `StoreClient` in the editor calls these endpoints. All responses are JSON.
Authentication: `Authorization: Bearer <api_key>` for developer calls;
`Authorization: Bearer <user_oauth_token>` for install/purchase calls.

```
GET  /v1/plugins?category=AI&q=navmesh&page=1&per_page=20
     → { plugins: [PluginListing], total: int, page: int }

GET  /v1/plugins/:name
     → PluginDetail

GET  /v1/plugins/:name/versions
     → { versions: [VersionInfo] }

GET  /v1/plugins/:name/:version/download/:platform
     → 302 redirect to CDN URL
     platform: windows | linux | macos

POST /v1/plugins/:name/install
     body: { version: "1.2.0", project_id: "..." }
     → { download_url: string, checksum_sha256: string }

GET  /v1/plugins/installed?project_id=...
     → { installed: [InstalledPlugin] }

POST /v1/plugins/:name/purchase
     body: { payment_method_id: string }
     → { license_key: string, receipt_url: string }

POST /v1/plugins/:name/review
     body: { rating: 1-5, text: string }
     → { review_id: string }

GET  /v1/search?q=string&engine_version=1.0.0
     → { results: [PluginListing] }
```

**`PluginListing` response shape:**
```json
{
    "name":          "NavMeshPlugin",
    "display_name":  "NavMesh Plugin",
    "version":       "1.2.0",
    "author":        "YourStudio",
    "description":   "Recast/Detour NavMesh and pathfinding for ZEngine.",
    "category":      "AI",
    "tags":          ["navigation", "pathfinding"],
    "price_usd":     0,
    "rating":        4.7,
    "review_count":  142,
    "download_count": 8430,
    "thumbnail_url": "https://cdn.zengine.dev/plugins/navmesh/thumb.png",
    "platforms":     ["windows", "linux", "macos"],
    "updated_at":    "2027-03-15T14:22:00Z"
}
```

---

## 6. `StoreClient` — In-Editor Integration

`StoreClient` is an engine module (editor builds only) that calls the REST API,
manages downloads, and drives the Plugin Store panel in the editor.

```cpp
// ZEngine/Editor/Store/StoreClient.h
#pragma once
#include <Core/Containers/Array.h>
#include <Core/Containers/String.h>

namespace ZEngine::Editor::Store {

    struct PluginListing {
        Core::Containers::String Name;
        Core::Containers::String DisplayName;
        Core::Containers::String Version;
        Core::Containers::String Author;
        Core::Containers::String Description;
        Core::Containers::String Category;
        float                    Rating       = 0.f;
        uint32_t                 DownloadCount = 0;
        uint32_t                 PriceCents   = 0;   // 0 = free
        bool                     IsInstalled  = false;
        bool                     HasUpdate    = false;
    };

    // Callback types — plain function pointers, no std::function
    using SearchResultFn  = void (*)(void* ctx, const Array<PluginListing>& results, bool success);
    using InstallResultFn = void (*)(void* ctx, const char* plugin_name, bool success, const char* error);
    using ProgressFn      = void (*)(void* ctx, const char* plugin_name, float progress_0_1);

    struct StoreClient {
        void Initialize(ArenaAllocator* arena, const char* api_base_url, const char* user_token);
        void Shutdown();

        // Async search — calls result_fn on the main thread when complete
        void Search(const char* query, const char* category,
                    SearchResultFn result_fn, void* ctx);

        // Async install — downloads binary to Plugins/ directory, calls result_fn
        void Install(const char* plugin_name, const char* version,
                     InstallResultFn result_fn, ProgressFn progress_fn, void* ctx);

        // Async update — same as install but replaces existing version
        void Update(const char* plugin_name,
                    InstallResultFn result_fn, ProgressFn progress_fn, void* ctx);

        // Uninstall — removes plugin files from Plugins/ directory
        void Uninstall(const char* plugin_name);

        // Check all installed plugins for available updates (called at editor startup)
        void CheckForUpdates(void (*on_updates_found)(void* ctx, uint32_t count), void* ctx);

        // Authentication
        bool IsAuthenticated() const;
        void Authenticate(const char* api_key);
        void SignOut();
    };

}  // namespace ZEngine::Editor::Store
```

### 6.1 Download and installation flow

```
User clicks "Install" on NavMeshPlugin 1.2.0
  │
  ▼
StoreClient::Install("NavMeshPlugin", "1.2.0", ...)
  │
  ├── POST /v1/plugins/NavMeshPlugin/install
  │     → { download_url, checksum_sha256 }
  │
  ├── Download binary for current platform to:
  │     <project>/Plugins/NavMeshPlugin/NavMeshPlugin.dll  (Windows)
  │     <project>/Plugins/NavMeshPlugin/NavMeshPlugin.zplugin
  │
  ├── Verify SHA-256 checksum
  │     if mismatch → delete files, call result_fn(false, "Checksum mismatch")
  │
  ├── Write to <project>/Plugins/NavMeshPlugin/NavMeshPlugin.zplugin
  │
  └── Call result_fn(true, nullptr)
        → Editor shows "Restart required to activate NavMeshPlugin"
```

### 6.2 Auto-update

At editor startup, `StoreClient::CheckForUpdates()` queries the store for all installed
plugin versions. If a newer version is available and the project has `"auto_update": true`
in its plugin config, it downloads and installs the update in the background. The editor
shows a notification: "NavMeshPlugin updated to 1.3.0".

---

## 7. Editor Plugin Store Panel

The Plugin Store panel is accessible from the editor menu: `Window → Plugin Store`.

It has four tabs:

| Tab | Contents |
|---|---|
| Browse | Search bar + category filter + plugin grid with thumbnail, name, rating, price |
| Installed | List of installed plugins with version, update badge, enable/disable toggle, uninstall button |
| Updates | Plugins with available updates; "Update All" button |
| My Plugins | For plugin developers: published plugins, download stats, revenue |

The panel uses `UIContext` (from `ui-system.md`) for rendering, not ImGui, so it is
available in both editor and in-game builds (e.g. a game that ships a modding panel).

---

## 8. Monetization

### 8.1 Revenue share
- ZEngine takes 20% of plugin revenue; the developer receives 80%
- Free plugins pay nothing
- Payouts are issued monthly via Stripe to verified developer accounts

### 8.2 Licensing model
- **Free / Open Source:** plugin is free; source may or may not be public
- **Paid — perpetual:** one-time purchase; valid for all engine versions within `engine_version_max`
- **Paid — subscription:** monthly fee; enables access to updates beyond the current engine version
- **Free trial:** full plugin, time-limited (developer sets trial duration)

### 8.3 License enforcement
License keys are validated against the store backend at plugin load time. In offline
scenarios, licenses are cached locally for up to 30 days. The engine never hard-blocks
a plugin that cannot reach the store — it logs a warning and continues.

---

## 9. Automated Validation

`zplugin validate` runs these checks before publish is allowed:

| Check | What it verifies |
|---|---|
| Descriptor schema | All required fields present and valid types |
| Binary exports | All three platform binaries export `ZPlugin_GetDescriptor` |
| SDK version | `SDKVersion` in the descriptor matches the binary's compiled version |
| Engine version range | `engine_version_min <= engine_version_max` |
| No banned symbols | Binary does not import `malloc`, `free`, `new`, `delete` (enforced by nm/dumpbin) |
| Size limit | Each binary < 100 MB; total package < 500 MB |
| Screenshot dimensions | Thumbnails 512×512; screenshots 1920×1080 or 2560×1440 |
| Virus scan | ClamAV scan on all binaries |
| Dependency resolution | All listed `dependencies` exist in the store |

The "no banned symbols" check enforces that plugins use arena allocation via the SDK,
not the system heap. This prevents a class of memory bugs caused by mixing allocators.

---

## 10. Plugin Discovery and Search

Plugins are indexed by name, tags, author, and description. Search is full-text.
Ranking factors:

1. Text relevance score
2. Download count (logarithmic weight)
3. Average rating
4. Recency of last update
5. Engine version compatibility with the current project's engine version

Plugins built against an incompatible SDK version are hidden from search results for
that project, with a "show incompatible" toggle.

---

## 11. Versioning and Compatibility

The store enforces compatibility at install time:
- If the project's engine version is outside `[engine_version_min, engine_version_max]`,
  the install is blocked with a clear error message
- The store always shows the latest compatible version for each project's engine version
- Old versions remain permanently available (never deleted) for reproducibility

Plugin authors are responsible for maintaining compatibility. The store sends automated
emails when a new engine version is released, notifying authors that their
`engine_version_max` may need updating.

---

## 12. File Layout

```
ZEngine/
  Editor/
    Store/
      StoreClient.h/.cpp         — REST API client
      StoreInstaller.h/.cpp      — download + checksum + file placement
      StorePanel.h/.cpp          — editor UI panel

  Tools/
    zplugin/                     — CLI tool (separate CMake target, standalone binary)
      main.cpp
      ValidateCommand.cpp
      PublishCommand.cpp
      YankCommand.cpp
      LoginCommand.cpp
```

The store backend is a separate cloud service and is not part of the engine repository.

---

## 13. Deliverables Checklist

**CLI tool (`zplugin`):**
- [ ] `zplugin login` — OAuth2 device flow, stores token in ~/.zengine/credentials
- [ ] `zplugin validate <path>` — all automated checks from §9
- [ ] `zplugin publish <path>` — upload binaries + metadata to store backend
- [ ] `zplugin yank <name> <version>` — yank a release with a reason
- [ ] `zplugin list` — list developer's published plugins

**Editor integration:**
- [ ] `StoreClient` — Search, Install, Update, Uninstall, CheckForUpdates
- [ ] `StoreInstaller` — async download + SHA256 verify + file placement
- [ ] Plugin Store Panel — Browse, Installed, Updates, My Plugins tabs
- [ ] Auto-update check on editor startup
- [ ] "Restart required" notification after install/update

**Store backend (cloud service — separate repo):**
- [ ] Plugin registry database (PostgreSQL)
- [ ] Binary CDN upload/download (S3-compatible)
- [ ] REST API (§5 endpoints)
- [ ] Payment processing (Stripe)
- [ ] Automated validation pipeline (§9)
- [ ] Developer dashboard web portal
- [ ] Email notifications for engine version releases

**Plugin descriptor:**
- [ ] `store` block added to `.zplugin` schema
- [ ] `PluginLoader` reads and validates store metadata (ignored at runtime — store metadata is for the store only)

**Tests:**
- [ ] `StoreClient` unit tests with a mock server
- [ ] `StoreInstaller` checksum mismatch test
- [ ] `zplugin validate` tests for each validation rule
- [ ] Install → load → verify plugin active end-to-end test
