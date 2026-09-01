# ZEngine Roadmap

This page tracks milestones, what has shipped, and what comes next. For detailed technical status see [Engine Architecture](engine-architecture.md), [Rendering Domain](rendering-domain.md), and the [design documents](https://github.com/JeanPhilippeKernel/RendererEngine/tree/develop/ZEngine/docs).

---

## Milestone: Stable Core (1.0.0)

**Status:** In progress · [GitHub milestone](https://github.com/JeanPhilippeKernel/RendererEngine/milestone/5)

Closes the rendering pipeline, wires the ECS to the renderer, adds the component reflection system, and stabilises the editor for daily use.

### Key feature groups

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
| **Memory** | 3 GB arena allocator, sub-arena budgets, scratch arena pair, `PaddedAtomic<T>`, `MemoryBudgetConfig`; `Array<T>` move-only; `HashMap`/`UnorderedHashMap` rewrite (PR [#615](https://github.com/JeanPhilippeKernel/RendererEngine/pull/615)); import/asset arena collapsed to single level — ImportPipeline watermark 84 % → 62 % |
| **VFS** | Full VFS stack (tickets 1–6): `VFSPath`, mount table, `VFSDiskBackend`, `VFSZipBackend`, `VFSScanner`, `VFSFileWatcher`; `.meta` sidecars; `AssetRegistry`; `VFSOpenFlags::Create` + `Truncate` (PR [#621](https://github.com/JeanPhilippeKernel/RendererEngine/pull/621)) |
| **Asset pipeline** | `GltfImporter` (fastgltf), `AssimpImporter`, `EnvironmentMapImporter`; `ImportCoordinator`; cook to `.zemesh` + `.zematerial`; material texture handles bound at draw time (PR [#634](https://github.com/JeanPhilippeKernel/RendererEngine/pull/634)) |
| **Rendering** | Render graph redesign — typed `RGResourceHandle` indices, `RGAccess`-driven automatic barriers, per-frame `RuntimeState`, all render targets transient and owned by the graph (PR [#641](https://github.com/JeanPhilippeKernel/RendererEngine/pull/641)); deferred PBR pipeline — 3-RT G-buffer (AlbedoAO, NormalRoughness, MetallicEmissive), LightingPass with Cook-Torrance BRDF, depth-based position reconstruction; stable viewport resize via swap-and-reuse slot; global packed vertex/index buffers (256 MB each); SkyboxPass + GridPass |
| **Editor UI** | Dark theme; dockable layout system; content browser; asset importer panel; `UIDispatcher` replaced with `MainThreadScheduler::Post`; `NSOpenPanel` main-thread deadlock fixed (PR [#645](https://github.com/JeanPhilippeKernel/RendererEngine/pull/645)) |
| **Engine** | Main + render thread split; `MainThreadScheduler` (lock-free MPSC); `FixedTimestepAccumulator`; `FrameRateCap` |
| **Platform** | macOS Dock icon, Windows taskbar icon, Linux window icon; `RecreationState` machine (PR [#623](https://github.com/JeanPhilippeKernel/RendererEngine/pull/623)); `OnClosed` dangling-pointer fix |
| **Stability** | Zero `vkDestroyDevice` validation errors on shutdown; arena-allocated Vulkan teardown pattern; `CrashHandler` signal fix (PR [#624](https://github.com/JeanPhilippeKernel/RendererEngine/pull/624)) |

---

## Future

Systems planned after 1.0.0 closes:

| System | Status | Notes |
|---|---|---|
| **Shadows** | Planned | Cascaded shadow maps, spot/point shadows — design doc at `shadows.md` |
| **Post-processing** | Planned | Bloom, ACES tone mapping, SSAO, FXAA — design doc at `post-processing.md` |
| **IBL / reflection** | Planned | Irradiance probes, specular cubemaps; base PBR (Cook-Torrance) already ships in 0.4.x |
| **Physics** | Planned | Jolt integration; rigid body, character controller, raycasting |
| **Animation** | Planned | Skeletal animation, `AnimationSampleSystem`, GPU skinning |
| **Scene serialization** | Planned | ECS-based YAML + binary formats with upgrade paths |
| **Scripting** | Planned | C++ DLL hot-reload, Lua 5.4 |
| **Audio** | Planned | miniaudio integration, spatial 3D audio |
| **Networking** | Planned | GGPO rollback, GNS transport, replication |
| **LOD** | Planned | Distance-based mesh switching |
| **GPU culling (advanced)** | Planned | Cluster-based deferred culling, draw call sorting — CPU frustum culling lands in 1.0.0 |
| **Lightmap baking** | Planned | Offline lightmap generation |
| **Texture compression** | Planned | KTX2 / BCn / ASTC |
| **UI system** | Planned | In-game immediate-mode UI over retained tree |
| **Text rendering** | Planned | MSDF font atlas, UTF-8 layout, localization |
| **Plugin SDK** | Planned | C++ plugin system, Python host |
