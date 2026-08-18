# ZEngine — Execution Plan

**Date:** 2026-06-25 (last updated: 2026-08-17)
**Team:** 2 engineers  
**Horizon:** 12 months (V1) + 4 months (next-year plans)  
**Based on:** All 50 validated design documents — 5 validation rounds, 0 blocking gaps

---

## How to read this document

Each sprint is two weeks. Each item lists its hard dependencies — work that **must
be complete before this item can start**. Items on the same sprint row run in parallel
across both engineers.

**A note on Tetragrama:** Tetragrama migration items are woven into the relevant
sprints. They are not a separate block. The single hard deadline is:
**Tetragrama must be migrated off `Rendering::Components::*` before Sprint 5**
when those headers are deleted. Missing this causes a build break.

---

## Month 1 — Foundation (Sprints 1–2)

### Sprint 1 (Days 1–10) — Unblock everything [COMPLETE]

Both engineers work in parallel from day 1. These are the absolute prerequisites —
nothing else can safely start until they are done.

**Engineer A — Allocator + Lifecycle**

| Task | Days | Hard dependency | Status |
|---|---|---|---|
| Fix allocator P0 bugs 1, 3, 4, 9, 13 (`memory-allocator-audit.md`) | 3 | None — start here | Done — PRs #497, #531 |
| Fix allocator remaining bugs 2, 5–8, 10–12, 14–16 | 2 | P0 bugs done | Done — PRs #497, #531 |
| Rewrite `Engine::Initialize` per `engine-lifecycle.md` §6 — **Sprint 1 scope only: Steps 6–10, 12, 22–25** (pre-conditions, Window, Device, AssetManager, AppRenderPipeline, RenderThread, MainThreadRun). Steps 11 (VFS), 13–21 (ECS/Physics/Audio/Network) are commented placeholders that increment `g_init_step` only — do not implement until their subsystem sprint. | 3 | Allocator bugs | Done — Window, Device, AssetManager, RenderPipeline, RenderThread wired |
| Add `CrashHandler::Install/Uninstall` to `Obelisk/EntryPoint.cpp` + fix typo | 0.5 | None | Done — PR #551 |
| `MemoryBudgetConfig::Default()` + `CreateBudgetedArena` helper | 1.5 | Allocator bugs | Done — `MemoryBudgetConfig`, `CreateBudgetedArena` in `MemoryManager.h` |

**Engineer B — VFS foundation + Build**

| Task | Days | Hard dependency | Status |
|---|---|---|---|
| VFS Ticket 1: `VFSPath`, `VFSResult<T>`, `IVFSFile`, `IVFSBackend`, `VFSDiskContext` | 2.5 | None — start here | Done — PR #530 |
| VFS Ticket 5: `.meta` sidecars, `MetaFileIO`, stable UUIDs | 2 | VFS T1 | Not done |
| `build-integration.md`: CMake structure, toolchain files, dependency strategy | 2 | None | Not done |
| Fix minor logging gaps: `LogMessage` struct, `LogEventFn` fn-pointer, `shared_mutex` | 1.5 | None | Done — `LogMessage`, `LogEventHandler` exist in `Logger.h` |

**Sprint 1 exit gate:** Allocator safe. Engine::Initialize wired (Steps 6–10, 12, 22–25). VFS T1 + logging done. CrashHandler live.
Remaining: VFS T5 (.meta sidecars) and CMake build-integration doc not yet done — carry to Sprint 2.

---

### Sprint 2 (Days 11–20) — ECS core + VFS mount layer [COMPLETE]

**Engineer A — ECS core**

| Task | Days | Hard dependency | Status |
|---|---|---|---|
| `EntityID`, `ComponentTypeID`, `ArchetypeMask` | 0.5 | Sprint 1 complete | Done |
| `ComponentStorage<T>` — dense array, generation check, swap-and-pop | 1.5 | Above | Done |
| `EntityRegistry` — generational free-list | 1 | Above | Done |
| `ECS::Scene` — CreateEntity, DestroyEntity, AddComponent, ForEach template | 1.5 | Above | Done |
| `Query<Ts...>` — cached mask | 0.5 | Scene | Done |
| `ECSTest.cpp` — 8 tests under ASan | 0.5 | Above | Done |
| Logging policy: channel enum, per-build CMake defines, hot-path macros | 1.5 | Sprint 1 | Done |
| Memory budget: register arenas with `MemoryProfiler::TrackArena` | 0.5 | Sprint 1 | Done |

**Engineer B — VFS mount + ECS components**

| Task | Days | Hard dependency | Status |
|---|---|---|---|
| VFS Ticket 2: `VFSMountTable`, `VFSContext`, `VFSDiskBackend`, `VFSZipBackend`, `VFSPakBackend` | 4 | VFS T1 | Done — `VFSMountTable`, `VFSContext`, `VFSDiskBackend`, `VFSZipBackend` merged in PR #550; `VFSPakBackend` pending |
| VFS Ticket 5: `.meta` sidecars, `MetaFileIO`, stable UUIDs (carried from Sprint 1) | 2 | VFS T1 | Done — PR #550 + VFS tickets series |
| `build-integration.md`: CMake structure, toolchain files (carried from Sprint 1) | 2 | None | Not started |
| Plain-data ECS components: `TransformComponent`, `NameComponent`, `UUIDComponent`, `MeshComponent`, `LightComponent`, `RigidBodyComponent` | 2 | ECS Scene (sync with A) | Partial — `TransformComponent` done; `MeshComponent`, `NameComponent` pending (issue #604); others pending |
| Tetragrama: update `LogUIComponent` to use `LogEventFn` function pointer | 0.5 | Logging fixes | Done |
| Tetragrama: verify no transitive include of `GraphicSceneEntity.h` | 0.5 | None | Done |

**Sprint 2 exit gate:** ECS Scene compiles and 8 tests pass. VFS T1+T2+T5 done. New plain-data components exist alongside old ones. [EXIT GATE MET — 2026-08-17]

---

## Month 2 — Simulation core (Sprints 3–4)

### Sprint 3 (Days 21–30) — Scheduler + Actor + VFS scanner [COMPLETE]

**Engineer A — Scheduler + Actor**

| Task | Days | Hard dependency | Status |
|---|---|---|---|
| `WorldTick` — `RegisterSystem`, `OrderBefore`, `Commit` (DAG + Kahn), `Tick` (wave dispatch) | 4 | ECS Scene | Done |
| `SchedulerTest.cpp` — 6 tests including conflict/cycle asserts | 1 | WorldTick | Done |
| `Actor` base class — `Create`, `Wrap`, `Detach`, component delegation | 2 | ECS Scene | Done |
| `ActorManager` — register, tick, shutdown | 1 | Actor | Done |
| `ActorTest.cpp` — create/destroy/ForEach visibility | 0.5 | Actor | Pending — file not yet created (issue #608) |
| `WorldCommands` deferred queue — `DeferCreateEntity`, `DeferDestroyEntity`, `Flush` | 1.5 | WorldTick | Done |

**Engineer B — VFS scanner + file watcher**

| Task | Days | Hard dependency | Status |
|---|---|---|---|
| VFS Ticket 3: `VFSScanner` async walk, `VFSDirectoryCache`, `VFSMemoryBackend` | 3.5 | VFS T2 | Done |
| VFS Ticket 4: `VFSFileWatcher` (inotify / FSEvents / RDCW), debounce | 3 | VFS T3 | Done |
| Tetragrama: migrate `ProjectViewUIComponent` from `directory_iterator` to `VFSScanner` | 2 | VFS T3 | Done |
| Tetragrama: migrate icon loading to `VFSPath::FromNative` | 0.5 | VFS T1 | Done |

**Sprint 3 exit gate:** Parallel system dispatch works. Actors visible to systems. VFS T1–T5 done. Tetragrama ProjectView on VFS. [EXIT GATE MET — 2026-08-17]

---

### Sprint 4 (Days 31–40) — Game loop + Input + migration Phase 2

**Engineer A — Game loop + Input**

| Task | Days | Hard dependency |
|---|---|---|
| `FixedTimestepAccumulator`, `FrameTimer`, `FrameRateCap`, enhanced `TimeStep` | 1.5 | Sprint 3 |
| `FramePacket` double-buffer (ping-pong, atomic index swap) | 1 | Above |
| `Scene::SnapshotTransforms`, `Scene::FillRenderableTransforms` | 1 | ECS Scene |
| `Engine::MainThreadRun` rewrite integrating all timing systems | 1.5 | All above |
| Input: `InputManager`, `RegisterAction`, `Poll`, `GetButton/Axis`, gamepad | 3 | Sprint 3 |
| `InputComponent` ECS struct + `InputSystem` with `SystemDeps` | 0.5 | WorldTick |
| Bind input to rollback `InputFrame` (feeds networking later) | 0.5 | Input |

**Engineer B — Migration phases 2+3 + Asset registry**

| Task | Days | Hard dependency |
|---|---|---|
| VFS Ticket 6: `AssetRegistry`, `AssetIndex`, `DependencyGraph`, hot-reload cascade | 5 | VFS T3+T4+T5 |
| Migration Phase 2: create `ECS::Components::*` namespace, `Actor` base | 2 | ECS Scene |
| **Tetragrama CRITICAL**: migrate `HierarchyViewUIComponent` + `InspectorViewUIComponent` off `Rendering::Components::*` | 2 | ECS Components |
| Migration Phase 3: grep audit + `GraphicScene3DSerializer` → `Actor::GetComponent` | 1 | Above |

**Sprint 4 exit gate:** Fixed timestep accumulator running. Input polling live.
ECS::Components namespace exists. Tetragrama migrated off old component headers.
Asset registry operational.

---

## Month 3 — Rendering + Simulation (Sprints 5–6)

### Sprint 5 (Days 41–50) — Dead code removal + Render resource manager

**Engineer A — Migration Phase 4+5 + Render resource manager**

| Task | Days | Hard dependency |
|---|---|---|
| Migration Phase 4: delete `GeometryComponent`, `ValidComponent`, `CameraComponent` | 0.5 | Sprint 4 |
| Migration Phase 5: delete `GraphicSceneEntity`, `#if 0` block, `entt` from CMake | 1.5 | Phase 3 done |
| Render resource manager: `RMHandle`, `UploadMesh`, `UploadTexture`, `UploadBuffer`, deferred deletion | 5 | Sprint 3 (RenderGraph) |
| Add `RenderResourceManager::EnqueueDeletion` (4 overloads) | 1 | RRM core |
| `RMM::GetBuffer` const + mutable split | 0.5 | RRM core |
| Tetragrama: `SceneViewportUIComponent` → `RenderGraph::GetFinalOutputHandle()` | 0.5 | RRM |
| Tetragrama: `EditorSceneSerializer` → new scene format (versioned, EntityID-based) | 1 | ECS Scene |

**Engineer B — Import pipeline + Shader pipeline**

| Task | Days | Hard dependency |
|---|---|---|
| Import pipeline: `ImportQueue`, `ImportCoordinator`, `IAssetImporter` migration | 3 | VFS T6 |
| Shader asset pipeline: `ShaderImporter`, `VFSIncludeResolver`, `ShaderCache`, hot-reload | 3 | Import pipeline + VFS T1 |
| `AssimpImporter`: add `ExtractSkeleton` (DFS pre-order), `ExtractAnimationClips` (30fps resample) | 2 | Import pipeline |
| Add math prerequisites: `Vec3<T> lerp`, `TRS()` to `Matrix.h` | 0.5 | None |

**Sprint 5 exit gate:** `entt` removed from codebase. RRM handles GPU resource lifetime.
Shader hot-reload works. Animation import wired.

---

### Sprint 6 (Days 51–60) — Physics + Audio

**Engineer A — Jolt Physics**

| Task | Days | Hard dependency |
|---|---|---|
| Jolt CMake submodule + allocator hooks | 0.5 | Sprint 1 |
| `RigidBodyComponent`, `ColliderComponent`, `CharacterControllerComponent` | 0.5 | ECS Components |
| `PhysicsWorld`: `Initialize`, `CreateBody`, `DestroyBody`, `Raycast`, contact listener | 3 | ECS Scene |
| `PhysicsSyncTransformToBodySystem`, `PhysicsStepSystem`, `PhysicsSyncBodyToTransformSystem`, `CharacterControllerSystem` | 2.5 | PhysicsWorld + WorldTick |
| Register all 4 systems with correct `OrderBefore` chain | 0.5 | Above |
| Physics tests: 7 test cases | 1 | Above |

**Engineer B — Audio + Cook pipeline**

| Task | Days | Hard dependency |
|---|---|---|
| miniaudio embed + allocator callbacks | 0.5 | Sprint 1 |
| `AudioSourceComponent`, `AudioListenerComponent` | 0.5 | ECS Components |
| `AudioManager`: `Initialize`, `LoadClip`, `Play`, `Stop`, pose pool | 2 | VFS T1 |
| `AudioListenerSystem`, `AudioSourceSystem`, command ring buffer, `Flush` | 2 | AudioManager + WorldTick |
| `PlayMusic`, `FadeMusic` streaming | 1 | AudioManager |
| Cook pipeline: `CookManifest`, `CookCoordinator`, topological sort, `PakWriter` atomic write | 4 | Import pipeline + VFS T6 |

**Sprint 6 exit gate:** Physics simulation running. Audio playing spatial sounds.
Cook pipeline producing ZPKG pak files.

---

## Month 4 — Animation + Rendering quality (Sprints 7–8)

### Sprint 7 (Days 61–70) — Animation + Scene serialization

**Engineer A — Animation system**

| Task | Days | Hard dependency |
|---|---|---|
| `AnimationHandles.h`, `SkeletonData`, `AnimationClip`, `BoneTransform` | 0.5 | Math prerequisites |
| `AnimationManager`: skeleton pool, clip pool, pose buffer pool | 1.5 | ECS Scene |
| `AnimationSampleSystem` — time advance, channel sampling, pose write | 2 | AnimationManager + WorldTick |
| `SkinningUploadSystem` — TRS forward pass, GPU bone matrix upload | 2 | RRM::UploadBuffer + WorldTick |
| Register both systems, `OrderBefore(anim_sample, skinning_upload)` | 0.5 | WorldTick |
| Animation tests: 6 cases | 0.5 | All above |
| Parent/child transform hierarchy: `ParentComponent`, `TransformHierarchySystem` | 2 | ECS Scene |

**Engineer B — Scene serialization + Shadows**

| Task | Days | Hard dependency |
|---|---|---|
| `ComponentSerializerRegistry`, `YAMLSceneSerializer`, `BinarySceneSerializer` | 4 | ECS Scene + VFS T1 |
| Atomic write protocol for scene files (temp + rename) | 0.5 | VFS T1 |
| `ComponentTypeRegistry` with stable string names (fixes non-serializable IDs) | 1 | ECS core |
| CSM shadow maps: `CascadeData`, `ShadowPassNode`, `ShadowPassSpec`, 4 passes | 3.5 | RRM + RenderGraph |
| Shadow depth shaders: vertex + PCF lookup GLSL | 1 | Above |

**Sprint 7 exit gate:** Animated characters skinning on GPU. Scenes save/load in both
formats. Shadow maps casting for directional light.

---

### Sprint 8 (Days 71–80) — Post-processing + Game loop integration

**Engineer A — Post-processing + Lighting pass**

| Task | Days | Hard dependency |
|---|---|---|
| Main lighting pass: descriptor sets, full-screen triangle, all shadow map inputs | 4 | RRM + Shadows |
| `PostProcessStack` + `PostProcessPassVtable` (function table, no virtual) | 1 | RenderGraph |
| Bloom (dual Kawase): `MakeBloomPass`, threshold + downsample + upsample + composite | 2 | PostProcessStack |
| ACES tone mapping pass | 1 | PostProcessStack |
| FXAA pass | 0.5 | PostProcessStack |
| Wire post-process chain in `Engine::RenderThreadRun` | 0.5 | All passes |

**Engineer B — Spot + Point shadows + SSAO**

| Task | Days | Hard dependency |
|---|---|---|
| Spot light shadow maps: `SpotShadowData`, perspective projection, 4 slots | 2 | CSM shadows |
| Point light cube shadow maps: `PointShadowData`, 6-face draw, 2 slots | 2 | CSM shadows |
| `ShadowUniformBuffer` fed to lighting pass | 0.5 | All shadows |
| SSAO pass: hemisphere kernel, noise texture, bilateral blur, occlusion term | 3 | Lighting pass |
| Color LUT grading pass (3D LUT, `.cube` import) | 1 | PostProcessStack |
| Vignette + chromatic aberration passes | 0.5 | PostProcessStack |

**Sprint 8 exit gate:** Full PBR lighting with shadows and post-processing running.
Visual quality comparable to a modern indie title.

---

## Month 5 — Game systems (Sprints 9–10)

### Sprint 9 (Days 81–90) — UI + Text rendering

**Engineer A — UI system**

| Task | Days | Hard dependency |
|---|---|---|
| `UITypes.h`, `UIWidgets.h` (Label, Button, Image, ProgressBar, Panel) | 1 | None |
| `UIDrawList`, `UIDrawCmd`, `PushColoredQuad/TexturedQuad/GlyphQuad` | 1.5 | Above |
| `UIContext`: `Begin/End`, all widget submission methods, `ProcessInput` | 3 | UIDrawList |
| `UILayout`: `Stack`, `Anchor`, `Inset`, `SplitHorizontal/Vertical` | 1 | UIContext |
| `UIScreenStack`, `UIScreen` virtual base (acceptable — cold path) | 1 | UIContext |
| `UIPassCallback` as `IRenderGraphCallbackPass`, wire into render-graph | 1 | RenderGraph |
| `UITextureRegistry`: texture slot management, descriptor set update | 1 | UIContext + RRM |
| `SetInputConsumed`, `IsInputConsumed` | 0.5 | UIContext |

**Engineer B — Text rendering + Localization**

| Task | Days | Hard dependency |
|---|---|---|
| `FontManager`: `LoadFont`, glyph metric pool, atlas texture handle | 2 | VFS T1 + RRM |
| `TextLayout::Build`: UTF-8 decoder, word wrap, kerning, tab stops | 2.5 | FontManager |
| SDF fragment shader (MSDF median + `fwidth` AA, outline) | 1 | FontManager |
| `TextRenderPass`: glyph batch, atlas bind, alpha blend | 1.5 | RenderGraph |
| `LocalizationManager`: CSV load, `Get(key)`, `SetLanguage` hot-swap | 2 | VFS T1 |
| `.zatlas` cook step via msdf-atlas-gen CLI | 0.5 | Cook pipeline |
| `Vec3<T> lerp` overload added to `MathUtils.h` (already in Phase 0 — verify) | 0.5 | None |

**Sprint 9 exit gate:** In-game UI rendering. Text with fonts. Localisation working.

---

### Sprint 10 (Days 91–100) — Scripting + Save system

**Engineer A — C++ DLL scripting**

| Task | Days | Hard dependency |
|---|---|---|
| `GameDLLLoader`: `Load`, `Unload`, `Reload`, platform dlopen/LoadLibrary | 2 | Sprint 3 |
| `ZGameContext` struct + `extern "C"` entry points | 0.5 | ECS + WorldTick |
| Actor factory pattern: `ActorTypeID`, `RegisterActorFactory`, `PrepareReload/Restore` | 2 | ActorManager |
| `WorldTick::BeginRebuild` + `Commit` cycle for system re-registration | 1 | WorldTick |
| Windows CRT `/MD` enforcement in game DLL CMake | 0.5 | Build integration |
| DLL hot-reload integration: step 2b drain before unload | 1 | All above |
| Engine version check: `EngineVersion.h` via configure_file | 0.5 | Build integration |
| Scripting tests: load, reload, system re-registration, symbol error | 2.5 | All above |

**Engineer B — Save system + Particles**

| Task | Days | Hard dependency |
|---|---|---|
| `ZSavHeader`, `ZSavRecordHeader`, `GameSaveData` key-value store | 1.5 | VFS T1 |
| `SaveManager`: `Save/Load/Delete/Autosave`, atomic write (temp + rename) | 2 | GameSaveData |
| `PlatformPaths::GetSaveDirectory` (Windows/Linux/macOS) | 1 | None |
| Steam Cloud sync in `SaveManager::Save/Load` | 0.5 | Steam integration |
| Settings save (separate `settings.zsav`) | 0.5 | SaveManager |
| `ParticleEmitterComponent`, `EmitterConfig`, `ParticlePool` (SOA) | 1 | ECS Components |
| `ParticleSpawnSystem`, `ParticleSimulateSystem`, `ParticleRenderSystem` | 2.5 | WorldTick + RRM |
| Billboard shader, depth sort via index array | 1 | ParticleRenderSystem |

**Sprint 10 exit gate:** Game logic reloads without restarting engine. Progress persists.
Particle effects running.

---

## Month 6 — Networking + Polish (Sprints 11–12)

### Sprint 11 (Days 101–110) — Networking core

**Engineer A — Transport + Session + Serialization**

| Task | Days | Hard dependency |
|---|---|---|
| Fill networking spec gaps: transport backends pseudocode, replication delta wire format, handshake protocol | 2 | None |
| `INetTransport` + `GNSTransport` (GameNetworkingSockets, Steam-gated) | 2 | None |
| `ENetTransport` (ENet, no Steam dependency) | 1 | INetTransport |
| `NetworkSession`: peer registry, broadcast, clock tick | 2 | INetTransport |
| `NetworkClock`: NTP two-way exchange, 8-sample median filter | 1 | NetworkSession |
| `NetBitWriter/Reader`: bit-pack, compressed float, smallest-3 quat | 2 | None |

**Engineer B — Replication + Profiling**

| Task | Days | Hard dependency |
|---|---|---|
| `NetReplicatedComponent`, `NetComponentDescriptor`, `NetReplicationRegistry` | 1.5 | ECS Components |
| `ReplicationSystem` (delta-compressed, dirty-flag) | 2 | NetworkSession + WorldTick |
| `ReplicationReceiveSystem` (apply + interpolate) | 1.5 | Replication |
| `NetRPC` flat dispatch table, send/broadcast helpers | 1.5 | NetworkSession |
| Profiling: Tracy CMake, `ZENGINE_PROFILE_SCOPE` macros, `GPUProfiler` VkQueryPool | 3 | RenderGraph |
| `DebugOverlay` (F3), `DebugConsole` (tilde) | 1.5 | UIContext |

**Sprint 11 exit gate:** Basic networking with replication and RPC. Profiling zones
visible in Tracy.

---

### Sprint 12 (Days 111–120) — Rollback + Prediction + Steam + Crash

**Engineer A — Rollback + Prediction**

| Task | Days | Hard dependency |
|---|---|---|
| `RollbackModule`: snapshot ring buffer, `BeginFrame`, `EndFrame`, `OnRemoteInput`, resimulate | 5 | NetworkSession + ECS Scene |
| `PredictionModule`: client prediction, server correction, reconciliation | 4 | NetworkSession + ECS Scene |
| Rollback tests: injected input delay, state match verification | 1 | Above |

**Engineer B — Lag comp + Steam + Crash**

| Task | Days | Hard dependency |
|---|---|---|
| `LagCompensator`: history ring buffer, `BeginRewind/EndRewind` | 3 | NetworkSession + ECS Scene |
| `NetRelevanceSystem`: spatial grid, per-peer sets, newly-relevant diff | 2 | NetworkSession + WorldTick |
| Steam integration: `SteamManager`, init, tick, achievements, cloud, overlay | 3 | Sprint 1 (Obelisk crash) |
| Crash handler: Windows SEH + MiniDumpWriteDump, Linux/macOS signal + backtrace | 2 | Obelisk fix done |

**Sprint 12 exit gate:** Multiplayer with rollback working. Steam overlay live.
Crash dumps written on crash.

---

## Month 7 — Tetragrama + V1 completion (Sprints 13–14)

### Sprint 13 (Days 121–130) — Tetragrama full migration

Both engineers converge on getting the editor fully functional on the new stack.

**Engineer A**

| Task | Days | Hard dependency |
|---|---|---|
| Tetragrama: `HierarchyViewUIComponent` — use `EntityID` selection, `WorldCommands` | 2 | ECS + WorldCommands |
| Tetragrama: `InspectorViewUIComponent` — component reflection API, display ECS fields | 2 | Reflection |
| ECS component reflection: `ComponentMeta`, `FieldDescriptor`, `EditorRegistry::Register<T>` | 3 | ECS Components |
| Tetragrama: animation inspector (SkeletonComponent, AnimatorComponent display) | 1.5 | Animation + Reflection |
| Tetragrama: scene play/pause/stop buttons wired to `WorldTick` | 1.5 | WorldTick |

**Engineer B**

| Task | Days | Hard dependency |
|---|---|---|
| Tetragrama: `EditorSceneSerializer` → new binary format (converter for old `.zescene`) | 3 | Scene serialization |
| Tetragrama: `AssetManager::LoadTextureFileAsAsset` VFSPath overload | 1 | VFS T1 |
| Tetragrama: `GraphicScene3DSerializer` → `Actor::GetComponent` + new component types | 2 | Migration Phase 3 |
| VFSPakBackend: runtime mounting of ZPKG archives | 2 | Cook pipeline + VFS T2 |
| Scene UUID validation: reject missing UUIDs in shipping builds | 1 | Scene serialization |
| Integration smoke tests: load scene, play simulation, save, reload | 1 | All above |

**Sprint 13 exit gate:** Editor and runtime on the same ECS/VFS/serialization stack.
Scenes round-trip correctly through the new binary format.

---

### Sprint 14 (Days 131–140) — V1 hardening

Final integration, CI gate, and packaging.

**Engineer A**

| Task | Days | Hard dependency |
|---|---|---|
| Networking spec minor gaps: NTP sample count, handshake details | 1 | Networking |
| Interest management grid tuning: `kMaxMountPoints`, capacity assertions | 0.5 | Networking |
| End-to-end multiplayer test: 2 clients, rollback under simulated packet loss | 2 | All networking |
| Input action map persistence: `SaveBindings/LoadBindings` to `settings.zsav` | 1 | Input + Save |
| Document 12 remaining minor gaps (0.5 days each — see migration-plan.md) | 3 | All systems |
| Obelisk: final validation that lifecycle matches new engine init sequence | 0.5 | All above |

**Engineer B**

| Task | Days | Hard dependency |
|---|---|---|
| CPack packaging: NSIS (Windows), AppImage (Linux), DMG + notarize (macOS) | 2 | Build integration |
| CI/CD: GitHub Actions matrix (3 OS × 3 build types), artifact upload | 2 | Build integration |
| Symbol upload: PDB → Sentry (Windows), DWARF (Linux), dSYM (macOS) | 1 | Crash handler |
| Asset cook CI step: headless cook on PR, manifest diff as artifact | 1 | Cook pipeline |
| Shader compilation CI: validate all GLSL compiles at PR time | 0.5 | Shader pipeline |
| Performance gate: no frame > 33ms on reference scene (debug overlay validates) | 1 | Profiling |
| Final VFS + import pipeline minor gaps | 1.5 | VFS + Import |
| Steam submission checklist: `steam_appid.txt`, store page, depot upload | 1 | Steam integration |

**Sprint 14 exit gate: V1 complete.** Engine ships singleplayer and multiplayer games.
Tetragrama editor fully migrated. Packaging works on all three platforms.

---

## Months 8–12 — Next-year plans (Sprints 15–26)

After V1 ships, the team moves to next-year plans. These are fully parallel — each
can start as soon as its V1 dependency is done.

### Dependency map for next-year plans

```
V1 complete
  ├── LOD system (5d)           → then → Culling system (11d) → Asset streaming (9d)
  ├── Light culling (7d)        → then → Deferred rendering (10d)
  ├── RRM + Shader pipeline     → then → Texture compression (8d)
  ├── Animation system          → then → Animation blend trees (10d)
  ├── VFS + Cook + Scripting    → then → Plugin system (15d)
  │                                  → then → Python plugin host (14d)
  │                                  → then → Plugin store engine side (8d)
  ├── Scene serialization       → then → Lightmap baking (12d)
  └── Lua: standalone           → Behavior tree (8d) [can start month 8]
```

### Sprint sequence (months 8–12)

| Sprints | Engineer A | Engineer B |
|---|---|---|
| 15–16 (Wk 1–4) | LOD system (7d) | Light culling (7d) + Texture compression start |
| 17–18 (Wk 5–8) | Culling system (11d) | Texture compression (8d) + Behavior tree (8d) |
| 19–20 (Wk 9–12) | Animation blend trees (10d) | Deferred rendering (10d) |
| 21–22 (Wk 13–16) | Asset streaming (9d) | Lightmap baking (12d) |
| 23–24 (Wk 17–20) | Plugin system (15d) | Lua scripting host (9d) |
| 25–26 (Wk 21–24) | Python plugin host (14d) | Plugin store — engine side (8d) |

**Month 12 exit gate:** Full next-year feature set complete. LOD, culling, light
culling, blend trees, texture compression, deferred rendering, lightmaps, plugin SDK.

---

## Dependency graph — critical path

```
Sprint 1: Allocator bugs ──────────────────────────────────────────────────────┐
Sprint 1: VFS T1 + T5 ─────────────────────────────────────────────────────┐   │
Sprint 2: ECS Scene ────────────────────────────────┐                      │   │
Sprint 3: WorldTick + Actor ─────────────────────┐  │                      │   │
Sprint 3: VFS T3 + T4 ───────────────────────┐   │  │                      │   │
Sprint 4: InputManager ──────────────────┐   │   │  │                      │   │
Sprint 4: VFS T6 + AssetRegistry ────┐   │   │   │  │                      │   │
Sprint 5: RRM ───────────────────┐   │   │   │   │  │                      │   │
Sprint 5: Import pipeline ───┐   │   │   │   │   │  │                      │   │
Sprint 6: Physics ───────┐   │   │   │   │   │   │  │                      │   │
Sprint 6: Audio ─────────┤   │   │   │   │   │   │  │                      │   │
Sprint 7: Animation ─────┤   │   │   │   │   │   │  │                      │   │
Sprint 7: Scene serial.──┤   │   │   │   │   │   │  │                      │   │
Sprint 8: Shadows ───────┤   │   │   │   │   │   │  │ ← All required for   │   │
Sprint 8: Post-process ──┤   │   │   │   │   │   │  │   first shippable    │   │
Sprint 9: UI + Text ─────┤   │   │   │   │   │   │  │   game               │   │
Sprint 10: Scripting ────┤   │   │   │   │   │   │  │                      │   │
Sprint 10: Save ─────────┤   │   │   │   │   │   │  │                      │   │
Sprint 11: Networking ───┤   │   │   │   │   │   │  │                      │   │
Sprint 12: Rollback ─────┤   │   │   │   │   │   │  │                      │   │
Sprint 12: Steam + Crash─┤   │   │   │   │   │   │  │                      │   │
Sprint 13: Tetragrama ───┘   │   │   │   │   │   │  │                      │   │
Sprint 14: V1 complete ──────┘   │   │   │   │   │  └── VFS chain ─────────┘   │
                                 │   │   │   │   └─── Simulation chain ─────────┘
                                 │   │   │   └─────── WorldTick gate
                                 │   │   └─────────── ECS Scene gate
                                 │   └─────────────── Actor gate
                                 └─────────────────── Asset gate
```

**The single most critical path:**
`Allocator bugs` → `ECS Scene` → `WorldTick` → `Physics + Audio + Animation`
→ `Lighting + Shadows` → `V1 shippable game`

Everything else is parallel to this path.

---

## Hard deadlines within V1

| Deadline | Why | Sprint | Status |
|---|---|---|---|
| Allocator P0 bugs done | Nothing else is safe to allocate | Sprint 1 | Done |
| ECS Scene compiles + tests pass | WorldTick, Actor, all systems gate on this | Sprint 2 | Done |
| Tetragrama migrated off `Rendering::Components::*` | Sprint 5 deletes those headers | **Before Sprint 5** | Pending — InspectorView + HierarchyView still reference old model |
| VFS T1–T6 done | Import pipeline, audio, animation, scene serialization all block on it | Sprint 4 | Done — all 6 tickets merged |
| WorldCommands deferred queue | Systems cannot spawn entities without it | Sprint 3 | Done — WorldCommands.h/.cpp implemented |
| `UploadBuffer` added to RRM | Animation skinning upload blocked | Sprint 5 | Done — UpdateBuffer implemented in RRM |
| Networking spec gaps filled (5d) | Implementation cannot start until spec is clean | Sprint 11 | Pending |

---

## What ships when

| Milestone | Sprint | What a game dev can do |
|---|---|---|
| Alpha 1 | Sprint 4 (end Month 2) | Entities move. Physics. Basic input. No rendering. |
| Alpha 2 | Sprint 8 (end Month 4) | Full 3D rendering with shadows and post-processing. |
| Beta 1 | Sprint 10 (end Month 5) | Singleplayer game with UI, text, save, particles, DLL scripting. |
| Beta 2 | Sprint 12 (end Month 6) | Multiplayer with rollback. Steam integration. Crash reports. |
| V1 Release | Sprint 14 (end Month 7) | Shippable on Steam. Tetragrama editor on new stack. |
| V1.1 | Month 8–9 | LOD, culling, light culling, texture compression. |
| V1.2 | Month 10–11 | Deferred rendering, blend trees, lightmap baking. |
| V1.3 | Month 12 | Plugin SDK. Python host. Plugin store engine side. |
