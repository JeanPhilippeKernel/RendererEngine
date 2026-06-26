# ZEngine Plugin Example — NavMesh Plugin

**Purpose:** Shows what a third-party plugin developer writes from start to finish.
This example implements a NavMesh + pathfinding plugin using Recast/Detour (MIT).

---

## What the plugin provides

- `NavMeshComponent` — marks a static mesh as NavMesh geometry
- `NavAgentComponent` — makes an entity pathfind to a target
- `PathfindingSystem` — ECS system that updates agent paths each tick
- `NavMeshBuildSystem` — ECS system that bakes NavMesh geometry at scene load
- An asset importer for `.navmesh` pre-baked NavMesh files
- An editor panel showing the NavMesh debug overlay

---

## Directory layout

```
NavMeshPlugin/
  CMakeLists.txt
  NavMeshPlugin.zplugin
  src/
    NavMeshPlugin.cpp       — entry point + lifecycle
    NavMeshSystem.cpp       — PathfindingSystem + NavMeshBuildSystem
    NavMeshImporter.cpp     — .navmesh file importer
    NavMeshEditor.cpp       — editor panel (ZENGINE_EDITOR only)
    NavMeshWorld.h/.cpp     — wraps Recast/Detour internals
  shaders/
    navmesh_debug.vert
    navmesh_debug.frag
```

---

## NavMeshPlugin.zplugin

```json
{
    "name":               "NavMeshPlugin",
    "version":            "1.0.0",
    "sdk_version":        "1.0",
    "author":             "YourStudio",
    "description":        "Recast/Detour NavMesh and pathfinding for ZEngine.",
    "entry_point":        "ZPlugin_GetDescriptor",
    "dependencies":       [],
    "engine_version_min": "1.0.0",
    "engine_version_max": "2.0.0"
}
```

---

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.25)
project(NavMeshPlugin VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)

# ── SDK path (set by plugin author's build environment) ──────────────────────
set(ZENGINE_PLUGIN_SDK_DIR "" CACHE PATH "Path to ZEngine Plugin SDK")
if(NOT ZENGINE_PLUGIN_SDK_DIR)
    message(FATAL_ERROR "Set ZENGINE_PLUGIN_SDK_DIR")
endif()

# ── Recast/Detour (vendored, MIT) ─────────────────────────────────────────────
add_subdirectory(vendor/recastnavigation)

# ── Plugin shared library ─────────────────────────────────────────────────────
add_library(NavMeshPlugin SHARED
    src/NavMeshPlugin.cpp
    src/NavMeshSystem.cpp
    src/NavMeshImporter.cpp
    src/NavMeshWorld.cpp
    $<$<BOOL:${ZENGINE_EDITOR}>:src/NavMeshEditor.cpp>
)

target_include_directories(NavMeshPlugin PRIVATE
    ${ZENGINE_PLUGIN_SDK_DIR}/include
    vendor/recastnavigation/Recast/Include
    vendor/recastnavigation/Detour/Include
)

target_link_libraries(NavMeshPlugin PRIVATE Recast Detour)

# Windows: export all symbols needed by the engine
if(MSVC)
    set_property(TARGET NavMeshPlugin PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Copy descriptor alongside the DLL
configure_file(NavMeshPlugin.zplugin
    ${CMAKE_BINARY_DIR}/NavMeshPlugin.zplugin COPYONLY)
```

---

## NavMeshPlugin.cpp — entry point + lifecycle

```cpp
#include <PluginSDK.h>           // only engine header needed
#include "NavMeshWorld.h"
#include "NavMeshSystem.h"
#include "NavMeshImporter.h"
#ifdef ZENGINE_EDITOR
#include "NavMeshEditor.h"
#endif

// ── Component structs — plain data, no virtuals ───────────────────────────────

struct NavMeshComponent {
    uint32_t NavMeshHandle = UINT32_MAX; // handle into NavMeshWorld pool
    float    CellSize      = 0.3f;
    float    CellHeight    = 0.2f;
    float    AgentHeight   = 2.0f;
    float    AgentRadius   = 0.4f;
    float    AgentMaxClimb = 0.9f;
    float    AgentMaxSlope = 45.f;
    bool     NeedsRebuild  = true;
};

struct NavAgentComponent {
    uint32_t NavMeshHandle   = UINT32_MAX;
    float    TargetX         = 0.f;
    float    TargetY         = 0.f;
    float    TargetZ         = 0.f;
    float    Speed           = 5.f;
    float    StoppingDist    = 0.5f;
    bool     HasTarget       = false;
    bool     ReachedTarget   = false;
    // Current path — arena-allocated, reused across frames
    float*   PathPoints      = nullptr;
    uint32_t PathPointCount  = 0;
    uint32_t PathArenaOffset = 0;  // offset in plugin arena for PathPoints
};

// ── Plugin state — lives in plugin arena ─────────────────────────────────────

struct NavPluginState {
    NavMeshWorld*   World              = nullptr;
    ZArenaHandle    Arena              = nullptr;
    uint32_t        NavMeshTypeID      = 0;
    uint32_t        NavAgentTypeID     = 0;
    uint32_t        BuildSystemID      = 0;
    uint32_t        PathfindSystemID   = 0;
    uint32_t        TransformBit       = 0;
    uint32_t        NavMeshBit         = 0;
    uint32_t        NavAgentBit        = 0;
};

static NavPluginState* g_state = nullptr;

// ── Lifecycle ─────────────────────────────────────────────────────────────────

static void NavPlugin_Initialize(const ZPluginContext* ctx)
{
    g_state        = ZPLUGIN_ALLOC(ctx->Arena, NavPluginState);
    g_state->Arena = ctx->Arena;
    g_state->World = ZPLUGIN_ALLOC(ctx->Arena, NavMeshWorld);
    NavMeshWorld_Init(g_state->World, ctx->Arena);
}

static void NavPlugin_Shutdown(const ZPluginContext* ctx)
{
    if (g_state && g_state->World) {
        NavMeshWorld_Shutdown(g_state->World);
    }
    g_state = nullptr;
    // Arena memory freed automatically by engine when plugin arena is cleared
}

// ── ECS registration ──────────────────────────────────────────────────────────

static void NavPlugin_RegisterSystems(const ZPluginContext* ctx)
{
    // Register component types
    g_state->NavMeshTypeID = ZPlugin_RegisterComponentType(
        ctx->Scene,
        sizeof(NavMeshComponent), alignof(NavMeshComponent),
        "NavMeshComponent");

    g_state->NavAgentTypeID = ZPlugin_RegisterComponentType(
        ctx->Scene,
        sizeof(NavAgentComponent), alignof(NavAgentComponent),
        "NavAgentComponent");

    // Look up the TransformComponent bit (registered by the engine before plugins load)
    uint32_t transform_bit = ZPlugin_GetComponentTypeBit(ctx->Scene, "TransformComponent");

    g_state->TransformBit = (uint64_t)1 << transform_bit;
    g_state->NavMeshBit   = (uint64_t)1 << g_state->NavMeshTypeID;
    g_state->NavAgentBit  = (uint64_t)1 << g_state->NavAgentTypeID;

    // NavMeshBuildSystem — reads Transform + NavMesh, writes NavMesh
    g_state->BuildSystemID = ZPlugin_RegisterSystem(
        ctx->WorldTick,
        NavMeshBuildSystem,
        g_state->TransformBit | g_state->NavMeshBit,  // read
        g_state->NavMeshBit,                           // write
        "NavMeshBuildSystem");

    // PathfindingSystem — reads Transform + NavAgent, writes Transform + NavAgent
    g_state->PathfindSystemID = ZPlugin_RegisterSystem(
        ctx->WorldTick,
        PathfindingSystem,
        g_state->TransformBit | g_state->NavAgentBit,  // read
        g_state->TransformBit | g_state->NavAgentBit,  // write
        "PathfindingSystem");

    // Order: NavMesh must be built before pathfinding queries it
    ZPlugin_OrderBefore(ctx->WorldTick, g_state->BuildSystemID, g_state->PathfindSystemID);

    // Order: pathfinding writes Transform before render cull reads it
    ZPlugin_OrderBefore(ctx->WorldTick,
        g_state->PathfindSystemID,
        ZPlugin_GetSystemID(ctx->WorldTick, "RenderCullSystem"));
}

// ── Importer registration ─────────────────────────────────────────────────────

static void NavPlugin_RegisterImporters(const ZPluginContext* ctx)
{
    static const char* exts[] = { ".navmesh", nullptr };
    static const ZImporterDesc desc = {
        .Name       = "NavMesh Importer",
        .Extensions = exts,
        .Import     = NavMeshImporter_Import,
    };
    ZPlugin_RegisterImporter(ctx->AssetRegistry, &desc);
}

// ── Editor panel registration ─────────────────────────────────────────────────

#ifdef ZENGINE_EDITOR
static void NavPlugin_RegisterEditorPanels(const ZPluginContext* ctx)
{
    static const ZEditorPanelDesc panel = {
        .Name           = "NavMesh Debug",
        .PanelData      = g_state,
        .Draw           = NavMeshEditor_Draw,
        .DefaultVisible = false,
        .Dockable       = true,
    };
    ZPlugin_RegisterEditorPanel(ctx->Editor, &panel);

    // Inspector: when a NavMeshComponent entity is selected, show editable fields
    ZPlugin_RegisterComponentInspector(ctx->Editor,
        g_state->NavMeshTypeID,
        NavMeshEditor_InspectNavMesh,
        g_state);

    ZPlugin_RegisterComponentInspector(ctx->Editor,
        g_state->NavAgentTypeID,
        NavMeshEditor_InspectNavAgent,
        g_state);
}
#endif

// ── Descriptor — the single exported symbol ───────────────────────────────────

extern "C" {
    const ZPluginDescriptor* ZPlugin_GetDescriptor()
    {
        static const ZPluginDescriptor desc = {
            .SDKVersion           = ZPLUGIN_SDK_VERSION,
            .Name                 = "NavMeshPlugin",
            .Version              = "1.0.0",
            .Author               = "YourStudio",
            .Initialize           = NavPlugin_Initialize,
            .Shutdown             = NavPlugin_Shutdown,
            .RegisterECSSystems   = NavPlugin_RegisterSystems,
            .RegisterRenderPasses = nullptr,     // this plugin has no custom render passes
            .RegisterImporters    = NavPlugin_RegisterImporters,
#ifdef ZENGINE_EDITOR
            .RegisterEditorPanels = NavPlugin_RegisterEditorPanels,
#else
            .RegisterEditorPanels = nullptr,
#endif
        };
        return &desc;
    }
}
```

---

## NavMeshSystem.cpp — the ECS systems

```cpp
#include <PluginSDK.h>
#include "NavMeshWorld.h"

// Access g_state from the plugin
extern NavPluginState* g_state;

// ── NavMeshBuildSystem ────────────────────────────────────────────────────────
// Iterates all entities with NavMeshComponent + TransformComponent.
// If NeedsRebuild is true, rebuilds the NavMesh geometry for that entity.
// Called once per tick — in practice rebuilds are rare (only on scene load or
// when the mesh moves).

void NavMeshBuildSystem(void* scene_opaque, float dt, void* cmds_opaque)
{
    // The plugin iterates using the raw C API — no ForEach template available
    // across the DLL boundary.
    uint32_t entity_count = ZPlugin_GetEntityCount(scene_opaque);
    for (uint32_t i = 0; i < entity_count; ++i) {
        uint32_t id  = ZPlugin_GetEntityIDAt(scene_opaque, i);
        uint32_t gen = ZPlugin_GetEntityGenAt(scene_opaque, i);

        NavMeshComponent* nav = (NavMeshComponent*)ZPlugin_GetComponentRaw(
            scene_opaque, id, gen, g_state->NavMeshTypeID);
        if (!nav || !nav->NeedsRebuild) continue;

        // Get the mesh vertices for this entity
        void* mesh_comp = ZPlugin_GetComponentRaw(scene_opaque, id, gen,
            ZPlugin_GetComponentTypeID(scene_opaque, "MeshComponent"));
        if (!mesh_comp) continue;

        // Retrieve mesh vertex data via importer API
        void* mesh_data = ZPlugin_GetMeshData(scene_opaque, mesh_comp);
        if (!mesh_data) continue;

        // Build or rebuild the NavMesh in NavMeshWorld
        nav->NavMeshHandle = NavMeshWorld_Build(
            g_state->World,
            g_state->Arena,
            mesh_data,
            nav->CellSize,
            nav->CellHeight,
            nav->AgentHeight,
            nav->AgentRadius,
            nav->AgentMaxClimb,
            nav->AgentMaxSlope);

        nav->NeedsRebuild = false;
    }
}

// ── PathfindingSystem ─────────────────────────────────────────────────────────
// Iterates all entities with NavAgentComponent + TransformComponent.
// Requests a path if the target changed; moves the agent along the current path.

void PathfindingSystem(void* scene_opaque, float dt, void* cmds_opaque)
{
    uint32_t entity_count = ZPlugin_GetEntityCount(scene_opaque);
    for (uint32_t i = 0; i < entity_count; ++i) {
        uint32_t id  = ZPlugin_GetEntityIDAt(scene_opaque, i);
        uint32_t gen = ZPlugin_GetEntityGenAt(scene_opaque, i);

        NavAgentComponent* agent = (NavAgentComponent*)ZPlugin_GetComponentRaw(
            scene_opaque, id, gen, g_state->NavAgentTypeID);
        if (!agent || !agent->HasTarget || agent->ReachedTarget) continue;
        if (agent->NavMeshHandle == UINT32_MAX) continue;

        // Get current position from TransformComponent
        float pos[3];
        ZPlugin_GetTransformPosition(scene_opaque, id, gen, pos);

        // If we have no path yet, query one
        if (agent->PathPointCount == 0) {
            float start[3] = { pos[0], pos[1], pos[2] };
            float end[3]   = { agent->TargetX, agent->TargetY, agent->TargetZ };

            // Allocate path buffer from plugin arena
            uint32_t max_path_points = 256;
            agent->PathPoints = ZPLUGIN_ALLOC_ARRAY(g_state->Arena, float,
                                                     max_path_points * 3);

            agent->PathPointCount = NavMeshWorld_FindPath(
                g_state->World,
                agent->NavMeshHandle,
                start, end,
                agent->PathPoints,
                max_path_points);
        }

        if (agent->PathPointCount == 0) continue; // no path found

        // Move toward the next waypoint
        float* next = agent->PathPoints; // first waypoint
        float dx = next[0] - pos[0];
        float dz = next[2] - pos[2];
        float dist = sqrtf(dx*dx + dz*dz);

        if (dist < agent->StoppingDist) {
            // Advance to the next waypoint
            agent->PathPointCount--;
            if (agent->PathPointCount == 0) {
                agent->ReachedTarget = true;
                return;
            }
            // Shift path array forward (memmove is safe — plain bytes)
            memmove(agent->PathPoints, agent->PathPoints + 3,
                    agent->PathPointCount * 3 * sizeof(float));
        } else {
            float step = agent->Speed * dt;
            float new_pos[3] = {
                pos[0] + (dx / dist) * step,
                pos[1],
                pos[2] + (dz / dist) * step,
            };
            ZPlugin_SetTransformPosition(scene_opaque, id, gen, new_pos);
        }
    }
}
```

---

## NavMeshImporter.cpp — .navmesh file importer

```cpp
#include <PluginSDK.h>
#include "NavMeshWorld.h"
#include <string.h>

// Simple binary format: [uint32_t magic][uint32_t vertex_count][float vertices[]]
#define NAVMESH_MAGIC 0x4E564D53u  // 'NVMS'

int32_t NavMeshImporter_Import(void*       arena,
                                const char* source_path,
                                const void* file_data,
                                uint32_t    file_size,
                                void*       uuid_gen,
                                void*       out_assets,
                                uint32_t    max_assets)
{
    if (file_size < 8) return -1;

    const uint8_t* p     = (const uint8_t*)file_data;
    uint32_t magic       = *(const uint32_t*)p; p += 4;
    uint32_t vert_count  = *(const uint32_t*)p; p += 4;

    if (magic != NAVMESH_MAGIC) return -1;
    if (file_size < 8 + vert_count * 12u) return -1;

    // Produce one asset — a NavMesh binary blob
    ZPluginAsset* asset = (ZPluginAsset*)out_assets;
    ZPlugin_GenerateUUID(uuid_gen, asset->UUID);
    asset->Type     = ZPLUGIN_ASSET_TYPE_BLOB;
    asset->DataSize = vert_count * 12;
    asset->Data     = ZPlugin_Alloc(arena, asset->DataSize, 4);
    memcpy(asset->Data, p, asset->DataSize);

    return 1; // one asset produced
}
```

---

## NavMeshEditor.cpp — editor panel (compiled only with ZENGINE_EDITOR)

```cpp
#ifdef ZENGINE_EDITOR
#include <PluginSDK.h>
#include "NavMeshWorld.h"
#include <imgui.h>   // editor builds link ImGui

extern NavPluginState* g_state;

void NavMeshEditor_Draw(void* panel_data, void* /*imgui_ctx*/)
{
    NavPluginState* state = (NavPluginState*)panel_data;
    ImGui::Text("NavMesh World: %u meshes built",
                NavMeshWorld_GetCount(state->World));
    ImGui::Separator();
    if (ImGui::Button("Rebuild All")) {
        NavMeshWorld_InvalidateAll(state->World);
    }
    ImGui::Checkbox("Draw NavMesh Overlay",
                    &state->World->DebugDrawEnabled);
}

void NavMeshEditor_InspectNavMesh(void* data, uint32_t /*type_id*/,
                                   void* component_data)
{
    NavMeshComponent* nav = (NavMeshComponent*)component_data;
    ImGui::DragFloat("Cell Size",      &nav->CellSize,      0.01f, 0.1f, 1.f);
    ImGui::DragFloat("Cell Height",    &nav->CellHeight,    0.01f, 0.1f, 1.f);
    ImGui::DragFloat("Agent Height",   &nav->AgentHeight,   0.1f,  0.5f, 5.f);
    ImGui::DragFloat("Agent Radius",   &nav->AgentRadius,   0.05f, 0.1f, 2.f);
    ImGui::DragFloat("Agent MaxClimb", &nav->AgentMaxClimb, 0.05f, 0.f,  2.f);
    ImGui::DragFloat("Agent MaxSlope", &nav->AgentMaxSlope, 1.f,   0.f, 60.f);
    if (ImGui::Button("Rebuild")) nav->NeedsRebuild = true;
    ImGui::Text("Handle: %u", nav->NavMeshHandle);
}

void NavMeshEditor_InspectNavAgent(void* data, uint32_t /*type_id*/,
                                    void* component_data)
{
    NavAgentComponent* agent = (NavAgentComponent*)component_data;
    ImGui::DragFloat("Speed",         &agent->Speed,        0.1f, 0.f, 20.f);
    ImGui::DragFloat("Stopping Dist", &agent->StoppingDist, 0.1f, 0.f,  5.f);
    ImGui::Text("Has Target:     %s", agent->HasTarget    ? "yes" : "no");
    ImGui::Text("Reached Target: %s", agent->ReachedTarget? "yes" : "no");
    ImGui::Text("Path Points:    %u", agent->PathPointCount);
    if (ImGui::Button("Clear Path")) {
        agent->PathPointCount = 0;
        agent->ReachedTarget  = false;
    }
}
#endif
```

---

## Game code using the plugin

The plugin is invisible to game code at the C++ level — game code works through the
same ECS API it uses for first-party components.

```cpp
// In the game DLL (game code, NOT plugin code)

void PlayerActor::OnCreate() {
    AddComponent<TransformComponent>({});

    // NavAgentComponent is registered by the plugin.
    // The game DLL doesn't include NavMeshPlugin headers —
    // it accesses the component by name through the ECS reflection API.
    auto* agent = scene.GetComponentByName<void>(GetEntityID(), "NavAgentComponent");
    if (agent) {
        // Cast using the layout we know from the plugin's documentation
        auto* nav = reinterpret_cast<NavAgentComponent*>(agent);
        nav->Speed       = 4.f;
        nav->StoppingDist = 0.3f;
    }
}

void PlayerActor::OnTick(float dt) {
    // Or more idiomatically — if the game links against the plugin headers:
    auto* agent = scene.GetComponent<NavAgentComponent>(GetEntityID());
    if (agent && !agent->ReachedTarget) {
        // The pathfinding system handles movement automatically.
        // Game code only sets the target:
        agent->TargetX   = patrol_waypoint.x;
        agent->TargetY   = patrol_waypoint.y;
        agent->TargetZ   = patrol_waypoint.z;
        agent->HasTarget = true;
    }
}
```

---

## What the plugin developer experiences

```
1. Download ZEngine Plugin SDK (headers + CMake template)
2. Copy the template directory, rename to NavMeshPlugin
3. Write NavMeshPlugin.cpp, NavMeshSystem.cpp, NavMeshImporter.cpp
4. Build with CMake — produces NavMeshPlugin.dll + NavMeshPlugin.zplugin
5. Drop both files into any ZEngine project's Plugins/ folder
6. Launch the engine — plugin loads automatically, components appear in the inspector,
   systems are registered and ordered correctly in the scheduler DAG
```

No engine recompilation. No engine source code access needed. No engine headers
beyond `PluginSDK.h`.
