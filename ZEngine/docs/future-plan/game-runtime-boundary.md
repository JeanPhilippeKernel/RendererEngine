# ZEngine — Game / Runtime Boundary

**Status:** Design. The current repository builds one engine library, the
Tetragrama editor library, and the Obelisk executable; it does not yet expose
the proposed isolated game-DLL/plugin boundary.

## Current boundary

Obelisk selects and constructs a `GameApplication` (currently the editor when
`--launchEditor` is supplied), then calls its concrete lifecycle:

```text
GameApplication::Initialize(MemoryManager*)
  -> OnInitializing / OverrideWindowConfiguration
  -> Engine::Initialize
  -> project VFS mount and scan
  -> AppRenderPipeline::Initialize
  -> OnInitialized
GameApplication::Run()
GameApplication::Shutdown()
```

`GameApplication` owns application-level window configuration, current render
scene, camera controller, VFS backend, render pipeline, and virtual callbacks
for events, updates, pre/post-render work, UI, and closing hooks. The engine
currently calls `GameApplication::Update()` every main-loop iteration and
uses application-provided scene/camera state to create `RenderFrameState`.
This is an in-process C++ integration, not a stable ABI.

The project configuration path is passed by `--projectConfigFile`. The
engine uses it for renderer/project policy such as geometry streaming and the
environment-lighting memory gate. Generated project configuration is not the
authoritative store for user-authored scene state; the versioned scene document
owns entities, components, references, and shared scene settings.

## Target boundary

The future boundary should separate three roles without pretending that the
separation already exists:

| Owner | Stable responsibility | Must not expose |
|---|---|---|
| Obelisk host | process lifecycle, CLI, project selection, diagnostics | gameplay/editor internals |
| ZEngine runtime | platform, ECS, assets, simulation, rendering, persistence services | mutable renderer internals as a plugin ABI |
| Game/editor module | authored behaviors, schemas/codecs, tools, and render-snapshot contribution | direct Vulkan ownership or cross-thread mutable scene access |

The public extension surface must be an explicitly versioned C/C++ ABI or a
well-defined plugin API. It must use stable schema/component keys, semantic
serialization and undo codecs, opaque extension data retention, and capability
negotiation. Process-local `ComponentTypeID`, arena pointers, STL layouts,
and raw component bytes are not durable plugin or scene contracts.

The render boundary should accept immutable frame snapshots and declared
render-graph callback passes using the current `Register`, `Prepare`, and
`Execute` contract. Plugins/editors may contribute data and pass
declarations, but do not mutate ECS or renderer-owned resource state from the
render thread.

## Migration gates

1. First make the in-process boundary explicit: replace incidental globals and
   application-owned raw render pointers with documented service interfaces.
2. Complete scene serialization, EditorSession transactions, selection, gizmo,
   and immutable rendering snapshots. An external ABI before those data
   contracts exist would freeze unstable identities.
3. Define compatibility versioning, load/unload failure behavior, ownership,
   threading, and a capability registry. Add a minimal test module.
4. Move to a separate runtime/game target only after the API is tested in both
   editor and non-editor launch modes. Packaging, hot reload, scripting, and
   sandboxing are follow-on scope.
