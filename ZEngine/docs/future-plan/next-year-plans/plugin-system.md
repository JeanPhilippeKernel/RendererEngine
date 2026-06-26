# ZEngine — Plugin System

**Priority:** Next-year plan — enables third-party developers to extend the engine
**Status:** Design
**Depends on:** `actor-ecs-architecture.md`, `system-scheduler.md`, `render-graph-integration.md`, `import-pipeline.md`, `scripting.md` (DLL loading pattern)
**See also:** `python-plugin-host.md` (Python/Lua/C# plugins), `plugin-store.md` (publishing and marketplace)

---

## 1. Design Philosophy

ZEngine's plugin system enables external engine developers to extend the engine without
modifying its source code. Plugins are distributed as shared libraries with a JSON
descriptor and loaded at engine startup.

**Core principles:**

- **Stable C ABI.** The plugin interface is `extern "C"` — no C++ name mangling, no
  virtual tables across the boundary, no STL types in the API surface. This ensures
  binary compatibility across compiler versions and minor engine updates.
- **Same extension points as first-party systems.** A plugin can do anything the engine
  itself can: register ECS components and systems, inject RenderGraph passes, add asset
  importers, add editor panels. There is no "second tier" capability.
- **Versioned SDK.** The plugin API is versioned. Plugins declare the SDK version they
  were built against; the engine rejects plugins built against an incompatible version.
- **DOD-conformant.** No virtual dispatch in hot paths. Plugin-registered systems and
  passes follow the same function pointer + data patterns as first-party code.
- **Isolation.** Plugins use the engine's arena allocator via a provided allocator
  pointer. They must not call `new`/`delete` or `malloc`/`free` directly.

---

## 2. Plugin Package Format

A plugin is a directory or zip archive containing:

```
MyPlugin/
  MyPlugin.zplugin        — JSON descriptor (required)
  MyPlugin.dll            — Windows shared library
  MyPlugin.so             — Linux shared library
  MyPlugin.dylib          — macOS shared library
  Resources/              — optional: shaders, textures, default assets
  Docs/                   — optional: documentation
```

### 2.1 `.zplugin` descriptor

```json
{
  "name":          "MyPlugin",
  "version":       "1.2.0",
  "sdk_version":   "1.0",
  "author":        "Studio Name",
  "description":   "A NavMesh and pathfinding plugin for ZEngine.",
  "homepage":      "https://example.com/myplugin",
  "entry_point":   "ZPlugin_GetDescriptor",
  "dependencies":  [],
  "engine_version_min": "1.0.0",
  "engine_version_max": "2.0.0"
}
```

Fields:
- `sdk_version` — the Plugin SDK version this plugin was compiled against. Must match
  `ZPLUGIN_SDK_VERSION` in `PluginSDK.h`.
- `entry_point` — the exported C function the engine calls to retrieve the plugin
  descriptor. Default is `ZPlugin_GetDescriptor`.
- `dependencies` — array of other plugin names that must be loaded before this one.
- `engine_version_min/max` — semver range the plugin is compatible with.

### 2.2 Hosted plugins (Python, Lua, C#)

Plugin authors who prefer a higher-level language can write plugins without C++.
A **host plugin** is a first-party C++ plugin that embeds a language runtime and
re-exposes the Plugin SDK to that language.

When a `.zplugin` descriptor contains a `"host"` field, `PluginLoader` does NOT
call `dlopen` on a library. Instead it passes the descriptor to the named host
plugin which loads and runs the script:

```json
{
  "name":         "MyPythonPlugin",
  "host":         "ZPythonHost",
  "entry_module": "MyPythonPlugin"
}
```

ZEngine ships three host plugins:

| Host | Language | Use case |
|---|---|---|
| `ZPythonHost` | Python 3.11+ | Editor tools, quest logic, prototyping |
| `ZLuaHost` | Lua 5.4 | Lightweight game logic, scripted events |
| `ZSharpHost` | C# (.NET 8) | Enterprise studios, Unity-familiar developers |

See `python-plugin-host.md` for the full design.

**Performance note:** Hosted plugins carry the overhead of their language runtime
(Python: ~100-500x vs C++, Lua: ~10-50x, C#: ~2-5x with AOT). Use hosted plugins
for low-frequency systems. High-frequency systems (physics, animation, networking)
should use C++ plugins.

---

## 3. Plugin SDK

The Plugin SDK is a set of headers distributed with the engine. Plugin authors include
these headers and link against no engine library — only the headers are needed.

```
ZEngine/
  PluginSDK/
    PluginSDK.h           — top-level include; include only this
    PluginTypes.h         — SDK version, ZPluginDescriptor, ZPluginContext
    PluginECS.h           — ECS extension API
    PluginRenderGraph.h   — RenderGraph extension API
    PluginImporter.h      — Asset importer extension API
    PluginEditor.h        — Editor panel extension API
    PluginAllocator.h     — Arena allocator access (no new/delete)
```

### 3.1 SDK version

```cpp
// ZEngine/PluginSDK/PluginTypes.h
#pragma once
#include <stdint.h>
#include <stddef.h>

#define ZPLUGIN_SDK_VERSION_MAJOR 1
#define ZPLUGIN_SDK_VERSION_MINOR 0
#define ZPLUGIN_SDK_VERSION \
    ((ZPLUGIN_SDK_VERSION_MAJOR << 16) | ZPLUGIN_SDK_VERSION_MINOR)
```

### 3.2 `ZPluginContext`

Passed to every plugin callback. Carries all engine subsystem pointers the plugin
needs to operate. The plugin must not store this pointer beyond the call that provides
it — use the individual subsystem pointers instead.

```cpp
// ZEngine/PluginSDK/PluginTypes.h

typedef void*    ZArenaHandle;    // opaque ArenaAllocator*
typedef void*    ZSceneHandle;    // opaque ECS::Scene*
typedef void*    ZWorldTickHandle;// opaque ECS::WorldTick*
typedef void*    ZRenderGraphHandle; // opaque RenderGraph*
typedef void*    ZVFSHandle;      // opaque IVFSContext*
typedef void*    ZAssetRegistryHandle; // opaque AssetRegistry*
typedef void*    ZEditorHandle;   // opaque EditorContext*; null in non-editor builds

struct ZPluginContext {
    uint32_t         EngineMajor;
    uint32_t         EngineMinor;
    uint32_t         EnginePatch;

    ZArenaHandle     Arena;        // plugin's dedicated sub-arena
    ZSceneHandle     Scene;
    ZWorldTickHandle WorldTick;
    ZRenderGraphHandle RenderGraph;
    ZVFSHandle       VFS;
    ZAssetRegistryHandle AssetRegistry;
    ZEditorHandle    Editor;       // null in shipping builds (ZENGINE_EDITOR not set)
};
```

### 3.3 `ZPluginDescriptor`

The single struct a plugin exports. All extension registrations happen through the
function pointers in this struct.

```cpp
// ZEngine/PluginSDK/PluginTypes.h

struct ZPluginDescriptor {
    // SDK version this plugin was compiled against.
    // Engine rejects the plugin if this doesn't match ZPLUGIN_SDK_VERSION.
    uint32_t SDKVersion;

    // Plugin identity
    const char* Name;
    const char* Version;
    const char* Author;

    // Lifecycle — all may be null if not needed.
    // Initialize: called once after all plugins are loaded, before WorldTick::Commit().
    // Shutdown:   called once during engine shutdown, before subsystems are destroyed.
    void (*Initialize)(const ZPluginContext* ctx);
    void (*Shutdown)  (const ZPluginContext* ctx);

    // Extension registration — called during Initialize, before WorldTick::Commit().
    // Each pointer may be null if the plugin does not use that extension point.
    void (*RegisterECSSystems)   (const ZPluginContext* ctx);
    void (*RegisterRenderPasses) (const ZPluginContext* ctx);
    void (*RegisterImporters)    (const ZPluginContext* ctx);
    void (*RegisterEditorPanels) (const ZPluginContext* ctx);  // null in shipping builds
};
```

The single exported function every plugin must provide:

```cpp
// In the plugin .cpp:
extern "C" {
    const ZPluginDescriptor* ZPlugin_GetDescriptor() {
        static const ZPluginDescriptor desc = {
            .SDKVersion          = ZPLUGIN_SDK_VERSION,
            .Name                = "MyPlugin",
            .Version             = "1.2.0",
            .Author              = "Studio Name",
            .Initialize          = MyPlugin_Initialize,
            .Shutdown            = MyPlugin_Shutdown,
            .RegisterECSSystems  = MyPlugin_RegisterSystems,
            .RegisterRenderPasses = nullptr,
            .RegisterImporters   = nullptr,
            .RegisterEditorPanels = nullptr,
        };
        return &desc;
    }
}
```

---

## 4. ECS Extension API

Plugins register ECS component types and systems using the same `WorldTick` and
`ComponentTypeOf<T>` infrastructure as first-party code.

```cpp
// ZEngine/PluginSDK/PluginECS.h
#pragma once
#include <PluginSDK/PluginTypes.h>

// Register a component type with the ECS Scene.
// component_size: sizeof(component struct)
// component_align: alignof(component struct)
// Returns a stable ComponentTypeID valid for the process lifetime.
uint32_t ZPlugin_RegisterComponentType(ZSceneHandle scene,
                                       uint32_t     component_size,
                                       uint32_t     component_align,
                                       const char*  name);

// Register an ECS system.
// fn:         void (*)(void* scene_opaque, float dt, void* world_commands_opaque)
// read_mask:  ArchetypeMask of components this system reads
// write_mask: ArchetypeMask of components this system writes
// Returns a stable SystemID for use with ZPlugin_OrderBefore.
typedef void (*ZSystemFn)(void* scene, float dt, void* world_commands);
uint32_t ZPlugin_RegisterSystem(ZWorldTickHandle world_tick,
                                ZSystemFn        fn,
                                uint64_t         read_mask,
                                uint64_t         write_mask,
                                const char*      name);

// Declare that system A must complete before system B.
void ZPlugin_OrderBefore(ZWorldTickHandle world_tick,
                         uint32_t         system_id_a,
                         uint32_t         system_id_b);

// Component access helpers — let plugins add/get/remove/has components
// by the ID returned from ZPlugin_RegisterComponentType.
void  ZPlugin_AddComponentRaw   (ZSceneHandle, uint32_t entity_id, uint32_t gen,
                                  uint32_t type_id, const void* data);
void* ZPlugin_GetComponentRaw   (ZSceneHandle, uint32_t entity_id, uint32_t gen,
                                  uint32_t type_id);
void  ZPlugin_RemoveComponentRaw(ZSceneHandle, uint32_t entity_id, uint32_t gen,
                                  uint32_t type_id);
bool  ZPlugin_HasComponentRaw   (ZSceneHandle, uint32_t entity_id, uint32_t gen,
                                  uint32_t type_id);
```

**Usage example — NavMesh plugin:**

```cpp
static uint32_t s_NavAgentTypeID = 0;
static uint32_t s_PathfindingSystemID = 0;

void NavPlugin_RegisterSystems(const ZPluginContext* ctx) {
    // Register a new component type
    struct NavAgentComponent { float Speed; float StoppingDistance; uint32_t PathHandle; };
    s_NavAgentTypeID = ZPlugin_RegisterComponentType(
        ctx->Scene, sizeof(NavAgentComponent), alignof(NavAgentComponent), "NavAgentComponent");

    // Register a system — reads Transform + NavAgent, writes Transform
    uint64_t transform_bit = (uint64_t)1 << ZPlugin_GetComponentTypeBit(ctx->Scene, "TransformComponent");
    uint64_t nav_agent_bit = (uint64_t)1 << s_NavAgentTypeID;

    s_PathfindingSystemID = ZPlugin_RegisterSystem(
        ctx->WorldTick,
        NavPlugin_PathfindingSystem,
        transform_bit | nav_agent_bit,   // read mask
        transform_bit,                   // write mask
        "PathfindingSystem");

    // Order: pathfinding runs after physics sync, before render cull
    ZPlugin_OrderBefore(ctx->WorldTick, s_PathfindingSystemID, ZPlugin_GetSystemID(ctx->WorldTick, "RenderCullSystem"));
}
```

---

## 5. RenderGraph Extension API

Plugins inject custom passes into the RenderGraph. The pass must implement the
`IRenderGraphCallbackPass` interface and be registered before `RenderGraph::Compile()`
is called (during `Initialize`, before `WorldTick::Commit`).

```cpp
// ZEngine/PluginSDK/PluginRenderGraph.h
#pragma once
#include <PluginSDK/PluginTypes.h>

// Opaque handle to a declared RenderGraph resource
typedef uint32_t ZResourceHandle;

// Plugin render pass callbacks — mirror IRenderGraphCallbackPass
typedef void (*ZPassSetupFn)  (void* pass_data, void* resource_builder, void* resource_inspector);
typedef void (*ZPassCompileFn)(void* pass_data, void* device, void* pass_builder, void* res_inspector);
typedef void (*ZPassExecuteFn)(void* pass_data, void* device, void* res_inspector,
                                void* scene, void* pass, void* framebuffer, void* cmd_buf);

struct ZRenderPassDesc {
    const char*    Name;
    void*          PassData;       // opaque user data; passed to all callbacks
    ZPassSetupFn   Setup;
    ZPassCompileFn Compile;
    ZPassExecuteFn Execute;
    bool           Enabled;
};

// Register a custom render pass. Must be called during RegisterRenderPasses(),
// before RenderGraph::Compile().
void ZPlugin_RegisterRenderPass(ZRenderGraphHandle rg, const ZRenderPassDesc* desc);

// Resource declaration helpers — wrap RenderGraphResourceBuilder
void ZPlugin_CreateRenderTarget(void* builder, const char* name,
                                 uint32_t width, uint32_t height, uint32_t vk_format);
void ZPlugin_AttachBuffer      (void* builder, const char* name, void* storage_buffer_handle);

// Resource query helpers — wrap RenderGraphResourceInspector
void* ZPlugin_GetRenderTarget  (void* inspector, const char* name); // returns TextureHandle*
void* ZPlugin_GetStorageBuffer (void* inspector, const char* name); // returns StorageBufferSetHandle*
```

---

## 6. Asset Importer Extension API

Plugins register importers for new file formats. An importer receives a raw file
buffer and produces engine asset data.

```cpp
// ZEngine/PluginSDK/PluginImporter.h
#pragma once
#include <PluginSDK/PluginTypes.h>

// Called by the import coordinator when a file with a matching extension is found.
// arena:       allocate all output data here (the engine owns the arena)
// file_data:   raw file bytes
// file_size:   byte count
// uuid_gen:    opaque UUID generator; call ZPlugin_GenerateUUID to use it
// out_assets:  array of ZPluginAsset to populate (pre-allocated by engine)
// returns:     number of assets produced, or -1 on failure
typedef int32_t (*ZImporterFn)(void*       arena,
                                const char* source_path,
                                const void* file_data,
                                uint32_t    file_size,
                                void*       uuid_gen,
                                void*       out_assets,
                                uint32_t    max_assets);

struct ZImporterDesc {
    const char*  Name;               // importer display name
    const char** Extensions;         // null-terminated array e.g. {".spine", ".skel", nullptr}
    ZImporterFn  Import;
};

// Register a new importer. Must be called during RegisterImporters().
void ZPlugin_RegisterImporter(void* import_registry, const ZImporterDesc* desc);

// UUID helper — generate a stable UUID from the import context
void ZPlugin_GenerateUUID(void* uuid_gen, uint8_t out_uuid[16]);
```

---

## 7. Editor Panel Extension API

In editor builds (`ZENGINE_EDITOR` defined), plugins can add custom panels, inspector
overrides, and asset browser integrations. This API is a no-op in shipping builds.

```cpp
// ZEngine/PluginSDK/PluginEditor.h
#pragma once
#include <PluginSDK/PluginTypes.h>

// Custom panel callback — called every editor frame to draw the panel via ImGui
typedef void (*ZPanelDrawFn)(void* panel_data, void* imgui_ctx);

struct ZEditorPanelDesc {
    const char*   Name;          // panel title
    void*         PanelData;     // opaque user data
    ZPanelDrawFn  Draw;
    bool          DefaultVisible;
    bool          Dockable;
};

// Register a custom editor panel.
void ZPlugin_RegisterEditorPanel(ZEditorHandle editor, const ZEditorPanelDesc* desc);

// Register a component inspector — called when an entity with this component type
// is selected. The plugin draws editable fields via ImGui.
typedef void (*ZInspectorFn)(void* data, uint32_t component_type_id, void* component_data);
void ZPlugin_RegisterComponentInspector(ZEditorHandle editor,
                                         uint32_t      component_type_id,
                                         ZInspectorFn  inspector,
                                         void*         data);

// Register an asset browser integration — adds a custom icon, preview,
// and double-click handler for a given asset file extension.
typedef void (*ZAssetPreviewFn)(void* data, const char* asset_path, void* imgui_ctx);
void ZPlugin_RegisterAssetBrowserEntry(ZEditorHandle   editor,
                                        const char*     extension,
                                        ZAssetPreviewFn preview_fn,
                                        void*           data);
```

---

## 8. Plugin Allocator API

Plugins receive a dedicated sub-arena carved from the main engine arena. All plugin
allocations must go through this arena. Plugins must never call `new`, `delete`,
`malloc`, or `free`.

```cpp
// ZEngine/PluginSDK/PluginAllocator.h
#pragma once
#include <PluginSDK/PluginTypes.h>

// Allocate `size` bytes aligned to `alignment` from the plugin's arena.
void* ZPlugin_Alloc(ZArenaHandle arena, size_t size, size_t alignment);

// Resize a previous allocation (same semantics as ArenaAllocator::Resize).
void* ZPlugin_Resize(ZArenaHandle arena, void* old_ptr, size_t old_size,
                      size_t new_size, size_t alignment);

// Reset the plugin arena to its initial state.
// All previous allocations become invalid. Use for per-frame scratch.
void  ZPlugin_ClearArena(ZArenaHandle arena);

// Convenience macros (optional, not part of the stable ABI)
#define ZPLUGIN_ALLOC(arena, T)         ((T*)ZPlugin_Alloc(arena, sizeof(T), alignof(T)))
#define ZPLUGIN_ALLOC_ARRAY(arena, T, n) ((T*)ZPlugin_Alloc(arena, sizeof(T)*(n), alignof(T)))
```

Each plugin receives a dedicated arena of `PluginArenaSizeBytes` (configurable in the
`.zplugin` descriptor, default 16 MB). The engine carves this from the global arena
during plugin initialization.

---

## 9. Plugin Loader

`PluginLoader` is an engine-side subsystem that scans for, loads, initializes, and
shuts down plugins. It runs between `Engine::Initialize` and `WorldTick::Commit`.

```cpp
// ZEngine/Plugins/PluginLoader.h
#pragma once
#include <Core/Memory/Allocator.h>
#include <Core/Containers/Array.h>

namespace ZEngine::Plugins {

    struct PluginRecord {
        char           Name[128]    = {};
        char           Version[32]  = {};
        void*          LibHandle    = nullptr;  // dlopen / LoadLibrary handle
        const ZPluginDescriptor* Descriptor = nullptr;
        Core::Memory::ArenaAllocator Arena   = {};
        bool           Initialized  = false;
    };

    struct PluginLoader {
        // Scan `plugins_dir` for .zplugin descriptors and load matching libraries.
        // Must be called after Engine::Initialize, before WorldTick::Commit.
        void ScanAndLoad(Core::Memory::ArenaAllocator* arena,
                         const char* plugins_dir,
                         const ZPluginContext& ctx);

        // Call RegisterECSSystems / RegisterRenderPasses / RegisterImporters /
        // RegisterEditorPanels on all loaded plugins.
        void RegisterAll(const ZPluginContext& ctx);

        // Call Initialize on all loaded plugins (after registration, before Commit).
        void InitializeAll(const ZPluginContext& ctx);

        // Call Shutdown on all loaded plugins (during engine shutdown).
        void ShutdownAll(const ZPluginContext& ctx);

        // Unload all shared libraries (after Shutdown).
        void UnloadAll();

        [[nodiscard]] uint32_t            LoadedCount() const;
        [[nodiscard]] const PluginRecord* GetRecord(uint32_t index) const;

    private:
        Core::Containers::Array<PluginRecord> m_plugins;

        bool LoadPlugin(Core::Memory::ArenaAllocator* arena,
                        const char* descriptor_path,
                        const ZPluginContext& ctx);
        bool ValidateDescriptor(const ZPluginDescriptor* desc) const;
    };

}  // namespace ZEngine::Plugins
```

### 9.1 Scan algorithm

```
1. Enumerate all *.zplugin files in plugins_dir (and one level of subdirectories)
2. Parse JSON descriptor — validate sdk_version, engine_version_min/max
3. Resolve dependency order:
   a. Build a directed graph: plugin A → plugin B means "A depends on B" (B loads first).
   b. Perform topological sort (Kahn's algorithm):
      - Compute in-degree for each plugin.
      - Add all plugins with in-degree 0 to a work queue.
      - Process queue: for each plugin, reduce in-degree of its dependents.
      - If queue empties before all plugins are processed: CYCLE DETECTED.
   c. On cycle detection:
      - Identify all plugins involved: "Circular dependency detected: A → B → C → A"
      - Reject ALL plugins in the cycle with a CRITICAL log entry.
      - Other plugins outside the cycle continue to load normally.
   d. On missing hard dependency:
      - If plugin A depends on B and B is not in the load list:
        Reject plugin A with error: "Plugin 'A' requires 'B' which is not installed."
      - Other plugins unaffected.
   e. Return the sorted list (dependency order: dependencies before dependents).
4. For each plugin in dependency order:
   a. Load the shared library (dlopen / LoadLibrary)
   b. Resolve entry_point symbol → ZPluginDescriptor*
   c. Validate SDKVersion == ZPLUGIN_SDK_VERSION (major must match)
   d. Carve a sub-arena for the plugin from the global arena
   e. Store PluginRecord
5. Call RegisterAll (registers ECS types, render passes, importers, editor panels)
6. Call InitializeAll (plugin-specific initialization)
```

### 9.2 Version compatibility rules

- **SDK major version mismatch**: plugin rejected, error logged.
- **SDK minor version mismatch**: allowed (minor versions are backwards compatible).
- **Engine version out of range**: plugin rejected, warning logged.
- A rejected plugin does not prevent other plugins from loading.

---

## 10. Integration with Engine Lifecycle

`PluginLoader` runs between steps 15 and 16 of `engine-lifecycle.md`:

```
Step 15: GameApplication::OnInitialized()        — game DLL calls ZGame_Initialize
Step 15b: PluginLoader::ScanAndLoad(plugins_dir)  — NEW: scan Plugins/ directory
Step 15c: PluginLoader::RegisterAll()             — NEW: ECS types, render passes, importers
Step 15d: PluginLoader::InitializeAll()           — NEW: plugin-specific init
Step 16:  WorldTick::Commit()                     — DAG built AFTER all plugins registered
Step 17:  AppRenderPipeline::Initialize()         — render graph compiled AFTER all passes registered
```

Shutdown (in `engine-lifecycle.md` shutdown sequence, between steps 4 and 5):
```
Step 4b: PluginLoader::ShutdownAll()   — plugin cleanup before subsystems go down
Step 4c: PluginLoader::UnloadAll()     — dlclose / FreeLibrary
```

---

## 11. Plugin SDK Distribution

The Plugin SDK is distributed as:

```
ZEnginePluginSDK_v1.0/
  include/
    PluginSDK.h           — single include for plugin authors
    PluginTypes.h
    PluginECS.h
    PluginRenderGraph.h
    PluginImporter.h
    PluginEditor.h
    PluginAllocator.h
  templates/
    CMakeLists.txt        — starter CMake for a new plugin
    MyPlugin.cpp          — minimal plugin skeleton
    MyPlugin.zplugin      — descriptor template
  docs/
    GettingStarted.md
    APIReference.md
    BestPractices.md
  CHANGELOG.md
  LICENSE                 — MIT (same as engine)
```

### 11.1 Starter CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.25)
project(MyPlugin VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)

# Path to extracted ZEngine Plugin SDK
set(ZENGINE_PLUGIN_SDK_DIR "" CACHE PATH "Path to ZEnginePluginSDK directory")
if(NOT ZENGINE_PLUGIN_SDK_DIR)
    message(FATAL_ERROR "Set ZENGINE_PLUGIN_SDK_DIR to the Plugin SDK path")
endif()

add_library(MyPlugin SHARED MyPlugin.cpp)

target_include_directories(MyPlugin PRIVATE ${ZENGINE_PLUGIN_SDK_DIR}/include)

# No engine library to link — the plugin only uses the SDK headers.
# All engine functions are resolved at runtime through the ZPlugin_* API.

# Plugin descriptor output alongside the DLL
configure_file(MyPlugin.zplugin ${CMAKE_BINARY_DIR}/MyPlugin.zplugin COPYONLY)
```

---

## 12. Security and Stability Considerations

- **Plugins run in-process.** A crashing plugin crashes the engine. No sandboxing in v1.
  Document this clearly in the SDK: "ZEngine plugins are in-process shared libraries.
  The engine does not sandbox plugin code. Plugin authors are responsible for stability."
- **No internal engine headers.** Plugins must only include `PluginSDK.h`. They must not
  include any engine internal header (`Engine.h`, `VulkanDevice.h`, etc.). The SDK
  provides wrappers for everything a plugin needs.
- **Component type ID stability.** Component type IDs assigned by `ZPlugin_RegisterComponentType`
  are process-stable but not serialization-stable across engine versions. Plugins that
  serialize component data must use string-based type names, not numeric IDs.
- **Arena isolation.** Each plugin gets its own arena. A plugin that exhausts its arena
  cannot corrupt other plugins' memory (only its own).

---

## 13. File Layout

```
ZEngine/
  Plugins/
    PluginLoader.h
    PluginLoader.cpp

  PluginSDK/
    PluginSDK.h               (top-level include)
    PluginTypes.h
    PluginECS.h
    PluginRenderGraph.h
    PluginImporter.h
    PluginEditor.h
    PluginAllocator.h

  ECS/
    PluginECSBridge.h         (engine-side implementation of ZPlugin_Register* ECS calls)
    PluginECSBridge.cpp

  Rendering/
    PluginRenderBridge.h      (engine-side implementation of ZPlugin_Register* render calls)
    PluginRenderBridge.cpp
```

---

## 14. Deliverables Checklist

- [ ] `ZEngine/PluginSDK/PluginTypes.h` — `ZPluginContext`, `ZPluginDescriptor`, SDK version
- [ ] `ZEngine/PluginSDK/PluginECS.h` — component registration, system registration, component access
- [ ] `ZEngine/PluginSDK/PluginRenderGraph.h` — render pass registration, resource helpers
- [ ] `ZEngine/PluginSDK/PluginImporter.h` — importer registration
- [ ] `ZEngine/PluginSDK/PluginEditor.h` — editor panel, inspector, asset browser registration
- [ ] `ZEngine/PluginSDK/PluginAllocator.h` — `ZPlugin_Alloc`, `ZPlugin_Resize`, `ZPlugin_ClearArena`
- [ ] `ZEngine/PluginSDK/PluginSDK.h` — single include umbrella
- [ ] `ZEngine/Plugins/PluginLoader.h/.cpp` — scan, load, register, init, shutdown, unload
- [ ] `ZEngine/Plugins/PluginECSBridge.h/.cpp` — implements ZPlugin_Register* ECS calls
- [ ] `ZEngine/Plugins/PluginRenderBridge.h/.cpp` — implements ZPlugin_Register* render calls
- [ ] `engine-lifecycle.md` — add steps 15b/15c/15d/4b/4c to init/shutdown sequence
- [ ] `memory-budget.md` — add per-plugin arena (16MB default × max plugin count)
- [ ] Plugin SDK distribution package — headers + templates + docs
- [ ] `tests/Plugins/PluginLoaderTest.cpp`:
  - [ ] Load a minimal plugin from disk, verify descriptor resolved
  - [ ] Reject plugin with mismatched SDK major version
  - [ ] Reject plugin outside engine version range
  - [ ] Dependency ordering: plugin B depends on A; verify A initialized first
  - [ ] Plugin registers ECS component; verify type ID assigned and component accessible
  - [ ] Plugin registers render pass; verify pass appears in RenderGraph after Compile()
  - [ ] Plugin arena exhaustion: assert fired, other plugins unaffected
  - [ ] Plugin shutdown called in reverse init order
  - [ ] PluginLoader::ScanAndLoad: topological sort detects and reports dependency cycles
  - [ ] Unit test: plugin A depends on B, B depends on A → both rejected, clear error message
  - [ ] Unit test: plugin A depends on missing plugin B → A rejected, C (independent) loads normally
- [ ] `ZPythonHost` plugin — see `python-plugin-host.md`
- [ ] `ZLuaHost` plugin (future)
- [ ] `ZSharpHost` plugin (future)
- [ ] `PluginLoader` support for `"host"` field in `.zplugin` descriptor — delegate to host plugin instead of dlopen
