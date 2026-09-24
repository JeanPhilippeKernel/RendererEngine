# ZEngine Roadmap

This page tracks milestones, what has shipped, and what comes next. For detailed technical status see [Engine Architecture](engine-architecture.md), [Rendering Domain](rendering-domain.md), and the [design documents](https://github.com/JeanPhilippeKernel/RendererEngine/tree/develop/ZEngine/docs).

---

## Milestone: Stable Core (1.0.0)

**Status:** In progress · [GitHub milestone](https://github.com/JeanPhilippeKernel/RendererEngine/milestone/5)

Closes the rendering pipeline, wires the ECS to the renderer, adds the component reflection system, and stabilises the editor for daily use.

**Status correction, 2026-09-17:** core reflection, built-in metadata, ECS hierarchy,
and transform/light render synchronization have landed. The historical issue archive
below is not an open-work tracker. The remaining editor-critical work is persistent
scene serialization, EditorSession transactions, all-ECS multi-selection, validated GPU
picking, native gizmos, and the serialized arbitrary grid.

### Open issue alignment — audited 2026-09-17

| Workstream | Open tracker | Current documentation interpretation |
|---|---|---|
| Scene document | [#714](https://github.com/JeanPhilippeKernel/RendererEngine/issues/714)–[#719](https://github.com/JeanPhilippeKernel/RendererEngine/issues/719) | The issue breakdown is still open; [scene serialization](https://github.com/JeanPhilippeKernel/RendererEngine/blob/develop/ZEngine/docs/future-plan/scene-serialization.md) is the current schema/lifetime contract. |
| Selection and gizmo | [#298](https://github.com/JeanPhilippeKernel/RendererEngine/issues/298), [#672](https://github.com/JeanPhilippeKernel/RendererEngine/issues/672), [#788](https://github.com/JeanPhilippeKernel/RendererEngine/issues/788) | Current work must use global ECS targets and native ZUI/RenderGraph integration. The actor/ImGuizmo wording retained in #672 and #788 is historical. |
| Editor camera | [#783](https://github.com/JeanPhilippeKernel/RendererEngine/issues/783)–[#785](https://github.com/JeanPhilippeKernel/RendererEngine/issues/785), [#789](https://github.com/JeanPhilippeKernel/RendererEngine/issues/789)–[#794](https://github.com/JeanPhilippeKernel/RendererEngine/issues/794) | The navigation redesign is delivered. Closed #781 is not acceptance evidence: the source still contains its placeholder raycast, so that work needs a reopened or successor issue. |
| Render lifetime and visibility | [#753](https://github.com/JeanPhilippeKernel/RendererEngine/issues/753), [#663](https://github.com/JeanPhilippeKernel/RendererEngine/issues/663), [#312](https://github.com/JeanPhilippeKernel/RendererEngine/issues/312), [#314](https://github.com/JeanPhilippeKernel/RendererEngine/issues/314) | RRM and GPU frustum-culling foundations exist. The remaining work is device-level tests, indirect-count compaction, true memory aliasing, and transparent submission—not reimplementation of the foundation. |
| Assets, tools, and tests | [#735](https://github.com/JeanPhilippeKernel/RendererEngine/issues/735), [#750](https://github.com/JeanPhilippeKernel/RendererEngine/issues/750), [#602](https://github.com/JeanPhilippeKernel/RendererEngine/issues/602), [#423](https://github.com/JeanPhilippeKernel/RendererEngine/issues/423), [#510](https://github.com/JeanPhilippeKernel/RendererEngine/issues/510), [#318](https://github.com/JeanPhilippeKernel/RendererEngine/issues/318) | These are active hardening/backlog items. Their old component names do not override the current ZUI panel and import-coordinator contracts. |
| Math policy | [#796](https://github.com/JeanPhilippeKernel/RendererEngine/issues/796) | [Portable math/SIMD policy](https://github.com/JeanPhilippeKernel/RendererEngine/blob/develop/ZEngine/docs/future-plan/math-simd-policy.md) now records the required correctness baseline, measurements, ABI constraints, and ADR gate. |
| Generated project configuration | [#821](https://github.com/JeanPhilippeKernel/RendererEngine/issues/821) | Its requirement for RendererEngine to persist `project.json` conflicts with ZodiacEngineHub ownership. RendererEngine may display the effective read-only policy; schema/default/persistence work belongs in ZodiacEngineHub or a revised handoff. |

The remaining open issues are isolated maintenance/product backlog. They do not alter the
scene-authoring dependency order unless their owning design document is updated first.

### Historical issue archive

#### Render pipeline completeness

| Issue | Description | Assignee |
|---|---|---|
| [#642](https://github.com/JeanPhilippeKernel/RendererEngine/issues/642) | Wire ECS `TransformComponent`/`MeshComponent` into `RenderScene` | — |
| [#643](https://github.com/JeanPhilippeKernel/RendererEngine/issues/643) | Wire `LightComponent` into `LightArrayUBO` — data-driven lighting | — |
| [#644](https://github.com/JeanPhilippeKernel/RendererEngine/issues/644) | Frustum culling — `FrustumCullSystem` + `CulledComponent` | — |

#### Component reflection and inspector

All assigned to [@jnyfah](https://github.com/jnyfah). Dependencies flow left to right — #647 and #648 can start in parallel today.

```
#647 (GetComponentRaw) ──────────────────────────────┐
#648 (types) ──► #649 (registry) ──► #650 (register) ──► #651 (inspector loop)
                                                          #652 (Add Component — Phase 2)
```

| Issue | Description | Depends on |
|---|---|---|
| [#647](https://github.com/JeanPhilippeKernel/RendererEngine/issues/647) | `IComponentStorage::GetRaw` + `Scene::GetComponentRaw` | — |
| [#648](https://github.com/JeanPhilippeKernel/RendererEngine/issues/648) | `FieldType` / `FieldDescriptor` / `ComponentMeta` types | — |
| [#649](https://github.com/JeanPhilippeKernel/RendererEngine/issues/649) | `ComponentReflectionRegistry` | #648 |
| [#650](https://github.com/JeanPhilippeKernel/RendererEngine/issues/650) | Register all 8 built-in components | #648, #649 |
| [#651](https://github.com/JeanPhilippeKernel/RendererEngine/issues/651) | Inspector generic loop — replaces per-component blocks | #647, #649, #650 |
| [#652](https://github.com/JeanPhilippeKernel/RendererEngine/issues/652) | `AddComponentRaw` + "Add Component" button (Phase 2) | #651 |

---

## Milestone: First steps (0.4.0) — completed

[GitHub milestone](https://github.com/JeanPhilippeKernel/RendererEngine/milestone/4)

Established the editor foundation, asset pipeline, and rendering plumbing needed for scene-authoring workflows.

### Shipped in 0.4.x

| Area | What shipped |
|---|---|
| **ECS** | Custom sparse-set ECS: generational entity handles, `ComponentStorage<T>`, `WorldTick` DAG scheduler, `ActorManager`, `WorldCommands` deferred mutations; 8 component headers; `ActorTest.cpp`; duplicate-EntityID guard; per-system staging buffers (PR [#619](https://github.com/JeanPhilippeKernel/RendererEngine/pull/619), [#620](https://github.com/JeanPhilippeKernel/RendererEngine/pull/620), [#628](https://github.com/JeanPhilippeKernel/RendererEngine/pull/628)) |
| **Memory** | Named, independently reserved CPU owners validated against an 8 GiB configured-capacity limit (Default 7,604 MiB; Editor 8,132 MiB; Server 6,324 MiB), sub-arenas, scratch pairs, `PaddedAtomic<T>`, and `MemoryBudgetConfig`; `Array<T>` is move-only and `HashMap`/`UnorderedHashMap` were rewritten (PR [#615](https://github.com/JeanPhilippeKernel/RendererEngine/pull/615)). See `ZEngine/docs/memory-budget.md` for the current profile. |
| **VFS** | Full VFS stack (tickets 1–6): `VFSPath`, mount table, `VFSDiskBackend`, `VFSZipBackend`, `VFSScanner`, `VFSFileWatcher`; `.meta` sidecars; `AssetRegistry`; `VFSOpenFlags::Create` + `Truncate` (PR [#621](https://github.com/JeanPhilippeKernel/RendererEngine/pull/621)) |
| **Asset pipeline** | `GltfImporter` (fastgltf), `AssimpImporter`, `EnvironmentMapImporter`; `ImportCoordinator`; cook to `.zemesh` + `.zematerial`; material texture handles bound at draw time (PR [#634](https://github.com/JeanPhilippeKernel/RendererEngine/pull/634)) |
| **Rendering** | Render graph redesign — typed `RGResourceHandle` indices, `RGAccess`-driven automatic barriers, per-frame `RuntimeState`, all render targets transient and owned by the graph (PR [#641](https://github.com/JeanPhilippeKernel/RendererEngine/pull/641)); deferred PBR pipeline — 3-RT G-buffer (AlbedoAO, NormalRoughness, MetallicEmissive), LightingPass with Cook-Torrance BRDF, depth-based position reconstruction; stable viewport resize; global packed vertex/index buffers; environment background and a compatibility grid path. |
| **Editor UI** | ZUI retained-mode dockable editor, dark theme, content browser, asset importer panel; `UIDispatcher` replaced with `MainThreadScheduler::Post`; `NSOpenPanel` main-thread deadlock fixed (PR [#645](https://github.com/JeanPhilippeKernel/RendererEngine/pull/645)). |
| **Engine** | Main + render thread split; `MainThreadScheduler` (lock-free MPSC); `FixedTimestepAccumulator`; `FrameRateCap` |
| **Platform** | macOS Dock icon, Windows taskbar icon, Linux window icon; `RecreationState` machine (PR [#623](https://github.com/JeanPhilippeKernel/RendererEngine/pull/623)); `OnClosed` dangling-pointer fix |
| **Stability** | Zero `vkDestroyDevice` validation errors on shutdown; arena-allocated Vulkan teardown pattern; `CrashHandler` signal fix (PR [#624](https://github.com/JeanPhilippeKernel/RendererEngine/pull/624)) |

---

## Active production path

| Area | Status | Next evidence |
|---|---|---|
| Scene serialization | In progress | Stable schemas, UUID resolution, transactional load, deterministic atomic save, and cross-process tests. |
| Scene authoring | Planned | EditorSession history, all-ECS selection, stale-safe picking, native gizmo, and serialized arbitrary grid. |
| Render safety | In progress | Immutable whole-scene render snapshot, validation/reference images, and measured memory/hardware gates. |
| Sky/environment | In progress | Complete the sky issue sequence under its separate 384 MiB environment-lighting policy. |
| Geometry residency | Implemented foundation | Harden the existing geometry stream/eviction path before general asset streaming. |
| ZUI | Implemented foundation | Continue editor integration, text/capture, and docking validation through the active ZUI contracts. |

## Deferred systems

Physics, animation, audio, game scripting, networking, plugins, LOD, lightmap baking,
texture compression, and platform services remain designs. No backend, invented API,
or target version named in an old proposal is a product commitment. Their corrected
design documents record the decisions and acceptance gates required before work starts.
