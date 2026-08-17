# Obelisk & Tetragrama — ZEngine Migration Impact Analysis

**Date:** 2026-06-25  
**Scope:** Full exploration of both projects; cross-referenced against migration-plan.md  

---

## What these projects are

### Obelisk
The executable entry point for the engine. It initializes the memory arena (3 GB),
logger, thread pool, parses CLI arguments, and launches either Tetragrama (editor) or
a standalone `GameApplication`. Three files total. Minimal logic — mostly wiring.

### Tetragrama
The ZEngine Editor. A full `GameApplication` subclass with an ImGui-based UI built
from 10 UI components, a binary scene serializer (`.zescene`), an editor camera
controller, a message-passing system, and deep integration with AssetManager,
importers, and the rendering pipeline.

**40+ source files across 8 directories:**
- `Layers/` — ImguiLayer (event callbacks, scene tree rendering)
- `Components/` — SceneViewport, HierarchyView, InspectorView, ProjectView, LogUI, Dockspace
- `Controllers/` — EditorCameraController
- `Serializers/` — EditorSceneSerializer (binary `.zescene` format)
- `Messengers/` — async message passing between UI components
- `Helpers/` — UIDispatcher, UIComponentDrawerHelper, SearchPatternAlgorithm

---

## What Tetragrama uses from ZEngine today

The full dependency surface (grouped):

**Core framework:** `GameApplication`, `Layer`, `CoreEvent`, `EventDispatcher`,
`IRenderable`, `IUpdatable`, `TimeStep`, `Coroutine`

**Memory:** `ArenaAllocator`, `MemoryManager`, `ZPushStructCtor`, `ZPushStruct`,
`ZGetScratch`, `ZReleaseScratch`, `ZMega`, `ZGiga`, `DEFAULT_ALIGNMENT`

**Containers:** `Array<T>`, `String`, `UnorderedHashMap<K,V>`

**Smart pointers:** `Ref<T>`, `WeakRef<T>`, `RefCounted`, `IntrusivePtr<T>`,
`ZDEFINE_PTR`, `CreateRef<T>`

**Rendering:** `RenderScene`, `GraphicScene`, `GraphicRenderer`, `ImGUIRenderer`,
`TextureHandle`, `CommandBuffer`, `RenderScene`, `MeshAllocation`, `SubMeshAllocation`,
`RenderPipeline`, `Device`, `SwapchainPtr`, `SceneRenderer`

**Input/Window:** `CoreWindow`, all `*Event` types, `IKeyboardEventCallback`,
`IMouseEventCallback`, `ITextInputEventCallback`, `IWindowEventCallback`,
`KeyCodeDefinition`, `Keyboard`, `Mouse`, `IDevice`

**Camera:** `FlyCameraController`, `FlyCamera`, `CameraControllerType`, `CameraSetting`

**Asset management:** `AssetManager` (singleton), `AssetHandle`, `AssetMesh`,
`AssetNodeHierarchy`, `AssetNodeRef`, `AssetTexture`, `AssetFileType`,
`AssetMeshFileHeader`, `IAssetImporter`, `AssimpImporter`, `EnvironmentMapImporter`,
`ImportConfiguration`, `AssetImporterOutput`

**Serialization:** `Serializer<T>` template, `SerializerCommonHelper`,
binary helpers (`WriteBinary`, `ReadBinary`, `WriteBinaryString`, etc.)

**Helpers:** `NodeHierarchy`, `NodeHierarchyHelper`, `ThreadPoolHelper`,
`ThreadSafeQueue`, `MeshHelper`, `secure_memcpy`, `secure_memset`, `secure_strlen`,
`secure_strcpy`

**Math:** `Mat4f`, `Vec3f`, `Vec4f`, `Matrix`, `MathUtils` (radians, degrees)

**Logging:** `Logger`, `LogMessage`, `ZENGINE_CORE_WARN/ERROR/INFO/CRITICAL`

---

## Phase-by-phase impact

### Confirmed conflict with engine-lifecycle.md — now resolved

The `engine-lifecycle.md` doc had a direct conflict with how Obelisk works:

| Issue | What engine-lifecycle.md said | What Obelisk actually does | Resolution |
|---|---|---|---|
| `Logger::Initialize` | Called inside `Engine::Initialize()` Step 2 | Called in `applicationEntryPoint` **before** `Engine::Initialize` | Engine asserts Logger is live; does NOT call it again |
| `ThreadPoolHelper::Initialize` | Called inside `Engine::Initialize()` Step 3 | Called in `applicationEntryPoint` **before** `Engine::Initialize` | Engine asserts ThreadPool is live; does NOT call it again |
| `MemoryManager::Instance()` | Engine called a singleton | MemoryManager lives on Obelisk's stack; no singleton exists | All `MemoryManager::Instance()` replaced with the `arena*` parameter |
| `CrashHandler::Install` | Called inside `Engine::Initialize()` Step 1 | Not called anywhere today | Obelisk must add it as the **first line** of `applicationEntryPoint` |
| Shutdown of Logger/ThreadPool/Memory | Shutdown order in engine-lifecycle §4 | Obelisk owns teardown after `app->Shutdown()` returns | Shutdown steps 15–18 moved to Obelisk scope in the doc |

The lifecycle doc has been updated to reflect the correct Obelisk-owns-pre-engine model.

**Required change to Obelisk (`EntryPoint.cpp`):**
```cpp
int applicationEntryPoint(int argc, char* argv[]) {
    CrashHandler::Install();    // ADD — must be absolute first line

    MemoryManager manager = {};
    manager.Initialize({.DefaultSize = ZGiga(3u)});
    auto arena = &manager.Allocator;

    Logger::Initialize(arena, logger_cfg);
    ThreadPoolHelper::Initialize();

    // ... existing CLI parsing, app creation, app->Initialize/Run/Shutdown ...

    Logger::Dispose();
    manager.Shutdown();         // FIX typo: Shutdowm → Shutdown
    CrashHandler::Uninstall();  // ADD
    return 0;
}
```

That is the **only** change Obelisk needs. Everything else is already correct.

---

### Phase 0 — Math prerequisites (Vec3 lerp, TRS)
**Obelisk:** No impact.  
**Tetragrama:** No impact.

---

### Phase 1 — ECS core + System scheduler
**Obelisk:** No impact.  
**Tetragrama:** No impact — Tetragrama does not use ECS directly today.

---

### Phase 2 — ECS Components + Actor layer (old Rendering::Components removed)

**Obelisk:** No impact.  
**Tetragrama:** Done — `Rendering/Components/` has been deleted.

`Rendering/Components/` no longer exists. `InspectorViewUIComponent` and
`HierarchyViewUIComponent` had their component display code commented out rather than
replaced inline. The old references to `Rendering::Components::TransformComponent`,
`LightComponent`, `NameComponent`, `UUIComponent`, and `MaterialComponent` are gone.

**Remaining work:**
- `HierarchyViewUIComponent.cpp` — write new component display panels against
  `ECS::Components::NameComponent` (pending that header landing)
- `InspectorViewUIComponent.cpp` — write new inspector panels against
  `ECS::Components::MeshComponent`, `TransformComponent`, etc. (pending those headers)
- `EditorSceneSerializer.cpp` — binary format must be updated to new ECS component type names

**Estimate:** 2 days (new panel writing, not search-and-replace)

---

### Phase 3 — Migrate Rendering::Components call sites

**Obelisk:** No impact.  
**Tetragrama:** Done — old headers are already deleted. Tetragrama has no remaining
references to `Rendering::Components::*`; the component display code has been
commented out and will be rewritten against the ECS component headers.

---

### Phase 4 — Animation system

**Obelisk:** No impact.  
**Tetragrama:** The editor will need to display animated entities. This is additive:
- `InspectorViewUIComponent` should show `SkeletonComponent`, `AnimatorComponent`
- `HierarchyViewUIComponent` should mark animated entities with an icon
- `EditorSceneSerializer` must serialize `AnimatorComponent` state

None of this is blocking — the editor works without animation support. It is an editor
feature gap to fill after the animation system is implemented.

**Required (editor additions, not blockers):** 2 days post-Phase 4

---

### Phase 5 — Dead code removal (entt, #if 0, GraphicSceneEntity)

**Obelisk:** No impact.  
**Tetragrama:** Done — `GraphicSceneEntity.h/.cpp` have been deleted.

`GraphicSceneEntity.h` and `GraphicSceneEntity.cpp` no longer exist.
`GraphicScene3DSerializer.h` had its include of `GraphicSceneEntity.h` removed as
part of the deletion.

`EditorSceneSerializer` is separate from `GraphicScene3DSerializer` and does not
depend on `GraphicSceneEntity` directly — it uses `EditorScene` which uses `RenderScene`.

**Verified:** `EditorSceneSerializer.cpp` has no transitive include of `GraphicSceneEntity.h`.

---

### Phase 6 — VFS Stack

**Obelisk:** No impact.  
**Tetragrama (ProjectViewUIComponent):** This component currently uses
`std::filesystem::directory_iterator` directly to scan the asset browser. It is listed
in `vfs-design.md` §7.3 as one of the two proof-of-concept migration call sites.

When VFS Ticket 3 (async scanner) is implemented, `ProjectViewUIComponent` must be
updated to use `VFSScanner` instead of raw `directory_iterator`. This is already
called out in both the migration plan and the VFS ticket docs.

**Required changes:**
- `ProjectViewUIComponent.h/.cpp` — replace `directory_iterator` with `VFSScanner`
  subscription; scan results arrive via callback, not synchronously on the render thread
- `ProjectViewUIComponent` currently loads thumbnail textures with
  `AssetManager::LoadTextureFileAsAsset(path)` — migrate to `VFSPath` + importer pipeline

**Estimate:** 2 days

---

## What Tetragrama needs that is NOT yet in the design docs

These are features Tetragrama uses today or will need, but are not covered in any
existing design document:

### N-1. Editor ↔ ECS bridge (selection, picking)

**Problem:** Today `HierarchyViewUIComponent` selects scene nodes by node index
(`int node`). After migration, the editor needs to select and inspect `EntityID`s.
There is no design for:
- How the editor stores the "selected entity"
- How the inspector discovers which components an entity has (requires reflection)
- How drag-drop in the hierarchy maps to `WorldCommands::DeferSetParent`

This requires a new `component-reflection.md` doc (planned in execution-plan.md Sprint 13).

**Required new doc:** `editor-entity-selection.md` or extend `component-reflection.md`  
**Estimate:** 3 days (doc + impl)

---

### N-2. Scene graph hierarchy in ECS (parent/child transforms)

**Problem:** `EditorScene` and `ImguiLayer` rely heavily on `NodeHierarchy`
(parent/child/sibling/depth relationships). The ECS design does not specify how
parent-child transform inheritance works — there is no `ParentComponent` or
`ChildrenComponent` and no `TransformInheritanceSystem`.

**Required:** Extend `actor-ecs-architecture.md` with a hierarchy section:
- `ParentComponent { EntityID Parent; }` 
- `TransformHierarchySystem` — propagates world transforms from root to leaves
- `WorldCommands::DeferSetParent(EntityID child, EntityID parent)`

**Estimate:** 1 day doc + 2 days impl

---

### N-3. Render pipeline introspection for editor viewport

**Problem:** `SceneViewportUIComponent` accesses:
```cpp
RenderPipeline->Device->SwapchainPtr->IdleFrameThreshold
RenderPipeline->SceneRenderer->GetFrameOutput()
```
The new `EngineContext` (from `engine-lifecycle.md`) gives the editor access to
`Device` and `RenderGraph`, but there is no specified API for "get the final rendered
frame as a texture handle" for display in an ImGui viewport.

**Required:** Add to `render-graph-integration.md`:
- `RenderGraph::GetFinalOutputHandle() → TextureHandle` — returns `"ldr_final"` after Execute
- Document that this is valid to call from the render thread after `Execute()` returns

**Estimate:** 0.5 days doc (trivial impl)

---

### N-4. Logger event handler for the log UI panel

**Problem:** `LogUIComponent` registers a `Logger::AddEventHandler` callback to
receive log messages in real time for the editor log panel. The new logging policy
(`logging-policy.md`) changed the handler from `std::function` to a plain function
pointer + context. `LogUIComponent` must adapt.

This is already handled by the logging policy doc, but `LogUIComponent` is not listed
in its migration guide.

**Required:** Add `LogUIComponent` to `logging-policy.md` §12 (migration guide):
```cpp
// Old:
Logger::AddEventHandler([this](LogMessage msg) { ... });
// New:
Logger::AddEventHandler({ .Fn = &LogUIComponent::OnLogMessage, .Ctx = this });
```

**Estimate:** 0.5 days

---

### N-5. Editor serializer must handle ECS entity IDs, not node ints

**Problem:** `EditorSceneSerializer` serializes scenes as binary files with node
indices (int-based). After migration, scenes use `EntityID` (uint32 index + uint32
generation). The binary format is incompatible. Additionally, `scene-serialization.md`
specifies a YAML + binary format, but does not mention Tetragrama's existing `.zescene`
format or provide a migration path.

**Required:** Add to `scene-serialization.md` §8 (migration):
- Acknowledge that the existing `.zescene` format is incompatible with the new binary format
- Specify a one-time conversion tool: old `.zescene` → new `.zscene` binary
- Version the new format as `SCENE_FORMAT_VERSION = 2`; old files have version 1

**Estimate:** 1 day doc + 2 days impl (converter tool)

---

## What Tetragrama constrains in the ZEngine design

These constraints are imposed by the editor and must not be violated by migration:

| Constraint | Imposed by | Impact on design |
|---|---|---|
| `ZPushStructCtor`, `ZMega`, `ZGiga` macros must remain | Used in 40+ files | Macros are stable; no change needed |
| `Ref<T>` / `WeakRef<T>` / `RefCounted` must remain | Messenger system, all components | `IntrusivePtr.h` is not being removed |
| `Array<T>::init(arena, capacity)` API | All 40+ files use init() pattern | Must not change to constructor pattern |
| `ThreadPoolHelper::Submit` must accept lambdas | UIDispatcher, DockspaceUIComponent | Submit API is stable |
| `Logger::AddEventHandler` must return a removable cookie | LogUIComponent | Handled in logging-policy.md |
| `AssetManager::Instance()` singleton must be accessible | EditorScene, HierarchyView, Dockspace | AssetManager remains a singleton |
| Event callback interfaces must remain virtual | ImguiLayer has 13 handlers | Input interfaces are not being removed |

---

## Required changes — complete checklist

### Obelisk (1 file to touch)

- [ ] `Obelisk/EntryPoint.cpp` — update memory initialization if MemoryManager API changes (low risk; macros are stable)

### Tetragrama (phased)

**Phase 2 (component migration) — Rendering/Components/ deleted:**
- [x] `Rendering/Components/` directory deleted; `GraphicSceneEntity.h/.cpp` deleted
- [x] `Components/HierarchyViewUIComponent.cpp` — old `Rendering::Components::*` references removed (code commented out)
- [x] `Components/InspectorViewUIComponent.cpp` — old component type references removed (code commented out)
- [ ] `Components/HierarchyViewUIComponent.cpp` — write new panels against `ECS::Components::NameComponent` (pending header)
- [ ] `Components/InspectorViewUIComponent.cpp` — write new panels against `ECS::Components::MeshComponent`, `TransformComponent`, etc. (pending headers)
- [ ] `Serializers/EditorSceneSerializer.cpp` — update component type names in binary format
- [ ] Add `ParentComponent` + `TransformHierarchySystem` (N-2 above) to enable hierarchy

**Phase 3 (header deletion) — already complete:**
- [x] No transitive include of `GraphicSceneEntity.h` in Tetragrama — verified
- [x] No transitive include of `Rendering/Components/TransformComponent.h` (old) — verified

**Before Phase 4 completes (VFS Ticket 3):**
- [ ] `Components/ProjectViewUIComponent.cpp` — migrate `directory_iterator` to `VFSScanner`
- [ ] `Components/ProjectViewUIComponent.cpp` — migrate texture loading to VFSPath + importer

**After ECS is live (new features for editor completeness):**
- [ ] `Components/LogUIComponent.cpp` — update `AddEventHandler` to function pointer pattern (N-4)
- [ ] `Components/SceneViewportUIComponent.cpp` — use `RenderGraph::GetFinalOutputHandle()` (N-3)
- [ ] `Components/HierarchyViewUIComponent.cpp` — use `EntityID` selection, WorldCommands (N-1)
- [ ] `Serializers/EditorSceneSerializer.cpp` — migrate to new scene serialization format (N-5)
- [ ] `Components/InspectorViewUIComponent.cpp` — use component reflection API (N-1)

**After animation system is live (editor additions):**
- [ ] `Components/InspectorViewUIComponent.cpp` — display SkeletonComponent, AnimatorComponent
- [ ] `Components/HierarchyViewUIComponent.cpp` — show animation icon on animated entities

---

## Summary

| Area | Impact level | Est. days | Phase |
|---|---|---|---|
| Obelisk | Minimal | 0.5 | Phase 0 verification |
| Tetragrama component migration | Medium | 2 | Before Phase 3 |
| Tetragrama hierarchy (ECS) | Medium | 3 | After Phase 1 |
| Tetragrama VFS / ProjectView | Medium | 2 | After VFS Ticket 3 |
| Tetragrama scene serializer format | High | 3 | After ECS + serialization |
| Tetragamma editor entity selection + reflection | High | 3 | After ECS + reflection |
| Tetragrama render viewport introspection | Low | 0.5 | After render graph integration |
| Tetragrama logging handler update | Low | 0.5 | After logging policy |
| **Total** | | **14.5 days** | |

These 14.5 days are distributed across migration phases and are included in the execution-plan.md sprint schedule.
They represent the cost of keeping the editor functional throughout the migration, not
new feature work. They should be distributed across the relevant phases — not treated
as a separate block of work.

**Previously the single most important constraint — now resolved:** Tetragrama has been
migrated off `Rendering::Components::*`. The old headers are deleted. Component display
code in `InspectorViewUIComponent` and `HierarchyViewUIComponent` is in commented-out
blocks; the remaining work is writing new panels against the ECS component headers
(pending `MeshComponent`, `NameComponent`, etc.).
