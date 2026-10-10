# Design documents

This section preserves the engine's technical decision records. It is organized
by **status** so a reader can tell whether a page describes a current contract,
proposed work, or implementation history.

| Status | Meaning |
|---|---|
| Current reference | The implemented behavior documented in the [reference](../reference/index.md). |
| Proposal / RFC | Design work that is planned, under review, or incomplete. It is not a promise of shipped behavior. |
| Completed design | An implementation record retained for rationale and maintenance context. |
| Historical presentation | A point-in-time analysis; dates and examples may no longer be current. |

## Current program

- [Production execution plan](execution-plan.md)
- [CPU memory budget reference](memory-budget.md)
- [Scene serialization proposal](future-plan/scene-serialization.md)
- [Renderer resource-management proposal](future-plan/render-resource-manager.md)
- [Zodiac Engine Hub / Panzerfaust integration](future-plan/panzerfaust.md)

## Proposal / RFC documents

The [proposal index](../project/rfcs.md) groups active and longer-horizon work by
subsystem. The complete set is available here:

### Editor, runtime, and tooling

- [Animation system](future-plan/animation-system.md), [audio system](future-plan/audio-system.md), [input system](future-plan/input-system.md), [physics system](future-plan/physics-system.md), [networking](future-plan/networking.md), and [scripting](future-plan/scripting.md)
- [Scene serialization](future-plan/scene-serialization.md), [editor entity selection](future-plan/editor-entity-selection.md), [editor undo / redo](future-plan/editor-undo-redo.md), [editor play mode](future-plan/editor-play-mode.md), [editor grid](future-plan/editor-grid.md), and [3D gizmo pass](future-plan/gizmo-3d-pass.md)
- [Component reflection](future-plan/component-reflection.md), [game/runtime boundary](future-plan/game-runtime-boundary.md), [save system](future-plan/save-system.md), and [Panzerfaust integration](future-plan/panzerfaust.md)
- [Build integration](future-plan/build-integration.md), [cook pipeline](future-plan/cook-pipeline.md), [import pipeline](future-plan/import-pipeline.md), [shader asset pipeline](future-plan/shader-asset-pipeline.md), and [thumbnail generation](future-plan/thumbnail-generation.md)

### Rendering and memory

- [Compute pipeline](future-plan/compute-pipeline.md), [draw-call sorting](future-plan/draw-call-sorting.md), [post-processing](future-plan/post-processing.md), [render-graph integration](future-plan/render-graph-integration.md), [render-resource manager](future-plan/render-resource-manager.md), [shadows](future-plan/shadows.md), and [sky rendering](future-plan/sky-rendering.md)
- [GPU allocator rearchitecture](future-plan/gpu-allocator-rearchitecture.md), [memory budget](future-plan/memory-budget.md), [per-frame upload heap](future-plan/per-frame-upload-heap.md), [profiling](future-plan/profiling.md), and [math/SIMD policy](future-plan/math-simd-policy.md)
- [Particle system](future-plan/particle-system.md), [text rendering](future-plan/text-rendering.md), [UI docking and floating windows](future-plan/ui-docking-floating-windows.md), and [Steam integration](future-plan/steam-integration.md)

### Longer horizon

- [Animation blend trees](future-plan/next-year-plans/animation-blend-trees.md), [asset streaming](future-plan/next-year-plans/asset-streaming.md), [behavior trees](future-plan/next-year-plans/behavior-tree.md), [culling system](future-plan/next-year-plans/culling-system.md), [deferred rendering](future-plan/next-year-plans/deferred-rendering.md), and [light culling](future-plan/next-year-plans/light-culling.md)
- [Lightmap baking](future-plan/next-year-plans/lightmap-baking.md), [LOD system](future-plan/next-year-plans/lod-system.md), [Lua scripting](future-plan/next-year-plans/lua-scripting.md), [plugin example: navmesh](future-plan/next-year-plans/plugin-example-navmesh.md), [plugin store](future-plan/next-year-plans/plugin-store.md), [plugin system](future-plan/next-year-plans/plugin-system.md), [Python plugin host](future-plan/next-year-plans/python-plugin-host.md), and [texture compression](future-plan/next-year-plans/texture-compression.md)
- [Virtual geometry streaming](future-plan/virtual-geometry-streaming.md)

## Completed designs

- [Actor–ECS architecture](completed/actor-ecs-architecture.md), [asset manager](completed/asset-manager.md), [bindless descriptor architecture](completed/bindless-descriptor-architecture.md), [crash handler](completed/crash-handler.md), and [engine lifecycle](completed/engine-lifecycle.md)
- [Fly camera redesign](completed/fly-camera-redesign.md), [game loop](completed/game-loop.md), [logging policy](completed/logging-policy.md), [memory allocator audit](completed/memory-allocator-audit.md), and [migration plan](completed/migration-plan.md)
- [Obelisk / Tetragrama impact](completed/obelisk-tetragrama-impact.md), [PSO cache architecture](completed/pso-cache-architecture.md), [render-graph redesign](completed/render-graph-redesign.md), [rendering flow](completed/rendering-flow.md), and [system scheduler](completed/system-scheduler.md)
- [TLSF allocator integration](completed/tlsf-allocator-integration.md), [UI system](completed/ui-system.md), [VFS design](completed/vfs-design.md), [VFS mount backends](completed/vfs-ticket2-mount-backends.md), [VFS scanner and memory backend](completed/vfs-ticket3-scanner-memory-backend.md), [VFS file watcher](completed/vfs-ticket4-filewatcher.md), [VFS metadata and UUID](completed/vfs-ticket5-meta-uuid.md), and [VFS asset registry](completed/vfs-ticket6-asset-registry.md)

## Historical presentations

- [Memory architecture](presentations/memory-architecture.md)
- [System heap analysis](presentations/system-heap-analysis.md)
