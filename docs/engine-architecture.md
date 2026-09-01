# Engine Architecture

Authoritative reference for ZEngine's overall architecture — threads, communication, and how all subsystems fit together. Update it when a significant architectural change lands in `develop`.

See also: [Rendering Domain](rendering-domain.md) · [Memory Management](memory-management.md) · [Asset Manager](asset-manager.md) · [ZUI System](zui-system.md)

---

## Table of Contents

- [Big Picture](#big-picture)
- [Thread Model](#thread-model)
- [Main Thread — Per-Frame Work](#main-thread-per-frame-work)
- [Render Thread — Per-Frame Work](#render-thread-per-frame-work)
- [Cross-Thread Communication](#cross-thread-communication)
- [ECS and Simulation](#ecs-and-simulation)
- [Asset Pipeline](#asset-pipeline)
- [Virtual File System (VFS)](#virtual-file-system-vfs)
- [Initialization Order](#initialization-order)
- [Shutdown Order](#shutdown-order)

---

## Big Picture

```mermaid
flowchart TD
    main["Main Thread\nECS simulation\nInput / VFS tick\nImportCoordinator\nApp::Update / UI"]
    render["Render Thread\nRRM::BeginFrame\nRenderGraph::Execute\nSwapchain present\nRRM::EndFrame"]
    scheduler["MainThreadScheduler\n512 MPSC slots"]
    pool["ThreadPool workers\nGltfImporter\nAssimpImporter\nImportCoordinator jobs"]

    main -->|"mailbox write (lock-free)"| render
    pool -->|"Post(ctx, fn)"| scheduler
    scheduler -->|"Drain() — step 9 each frame"| main
    pool -->|"IngestMesh / IngestTextures"| main
```

**Key design rules:**
- Main thread owns all ECS state, input, and UI logic.
- Render thread owns all Vulkan command recording and submission.
- Neither thread blocks on the other within a frame.
- Background workers never touch Vulkan — GPU work is enqueued to `RenderResourceManager` and drained on the render thread.

---

## Thread Model

| Thread | Count | Launched by | Joins at |
|---|---|---|---|
| Main thread | 1 | OS entrypoint (`main()`) | Process exit |
| Render thread | 1 | `Engine::Run()` | `Engine::Deinitialize()` |
| ThreadPool workers | `hw_concurrency - 1` | `ThreadPoolHelper::Initialize()` | `ThreadPoolHelper::Shutdown()` |

The render thread is launched **before** `MainThreadRun()` begins and joined **first** in teardown — before any GPU resource is touched.

---

## Main Thread — Per-Frame Work

`Engine::MainThreadRun()` — `ZEngine/ZEngine/Engine.cpp`

```
loop (until s_close_requested):

  1. PollEvent()                    GLFW platform event pump; events feed ZUIContext
  2. VFSContext::Tick()             Drain FileWatcher debounce queue
  3. skip frame if minimized
  4. frame_timer.End()             Measure raw delta (clamped 250 ms)
  5. accumulator.Accumulate(dt)

  6. Fixed simulation (60 Hz, max 5 catch-up steps):
       WorldTick::Tick(scene, fixed_dt, cmds)   ECS systems in DAG order
       WorldCommands::Flush(scene)              Deferred structural mutations
       ActorManager::Tick(fixed_dt)             Actor OnTick() callbacks
       Scene::SnapshotTransforms()              PreviousPosition ← Position
       accumulator.ConsumeStep()

  7. alpha = accumulator.Alpha()    Interpolation factor for renderer

  8. ImportCoordinator::Tick()      Dispatch pending import jobs to ThreadPool

  9. MainThreadScheduler::Drain()   Execute callbacks posted by background threads

 10. g_app->OnUpdate(raw_dt)       Subclass update (Editor: viewport hover → camera gate)
     CameraController→Update(dt)   After OnUpdate so hover state is current

 11. Build RenderPayload (if mailbox slot available):
       BeginOverlayFrame → ZUIBeginFrame(ctx, dt)
       OnRenderUI() → ZUILayer::Render() — build ZUI box tree
       EndOverlayFrame → ZUIEndFrame (ZUILayoutSolve + ZUIInteractionPass)
       FillOverlayPayload → ZUIRenderer::PreparePayload (DFS → draw list)
       SyncECSToRenderScene(alpha) + PrepareScene
       MailBoxBufferHead.store(next, release)

 12. FrameRateCap::Wait()          Applied unconditionally — including when mailbox is
                                    full (buffer-full continue also calls Wait so the
                                    main loop never spins at 100k+ Hz)
```

---

## Render Thread — Per-Frame Work

`Engine::RenderThreadRun()` — `ZEngine/ZEngine/Engine.cpp`

```
loop (until s_request_terminate):

  1. Mailbox read (lock-free)         Wait for payload; re-use last if none

  2. Swapchain::AcquireNextImage      Block on fence for in-flight slot

  3. RRM::BeginFrame(frame_index)
       FlushPendingUploads:
         ├─ ResetGeometryBuffersInternal  (if scene-reload flag is set)
         ├─ BeginBatchUpload
         ├─ DoUploadMesh × N           All mesh uploads in ONE GPU submission
         ├─ EndBatchUpload
         └─ DoUploadTexture × M        Per-texture timeline upload

  4. AppRenderPipeline::Tick (if InstancesDirty)
       GetMeshOffsets → SubMeshAllocation per submesh
       Upload TransformSB, DrawDataSB, VkDrawIndirectCommand[]

  5. RenderGraph::Execute
       DepthPrePass  → DrawIndirect   (all scene meshes, depth only)
       SkyboxPass    → DrawIndexed    (builtin cube)
       GridPass      → DrawIndexed    (builtin quad + push constants)
       GbufferPass   → DrawIndirect   (all scene meshes, full G-buffer)
       LightingPass  → Draw(3)        (full-screen deferred lighting triangle)

  6. ZUIRenderer::Submit              Upload ZUIDrawVtx/Idx, scissor/tex batches,
                                      DrawIndexed via ZUIPass (targets swapchain)

  7. Swapchain::Present               Submit + present, advance timeline semaphore

  8. RRM::EndFrame(frame_index)       Drain DeferredFreeQueue for this slot

  9. render_timer.End()               Sample wall-clock delta (includes vsync wait)
     g_engine_ctx→SmoothedDeltaTime = render_timer.SmoothedDelta()
                                       Written here so FPS display reflects true GPU rate
```

The render thread never writes ECS state. It reads only from the `RenderPayload` and `RenderScene::MeshInstance[]` (seqlock snapshot).

---

## Cross-Thread Communication

Three distinct channels, each with a different mechanism.

### 1. Main → Render: Lock-free mailbox

```mermaid
sequenceDiagram
    participant Main as Main Thread
    participant Ring as RenderPayload[3] ring buffer
    participant Render as Render Thread

    Main->>Ring: PrepareScene + OnRenderUI → RenderPayload[next]
    Main->>Ring: MailBoxBufferHead.store(next, release)
    Render->>Ring: tail = MailBoxBufferHead.load(acquire)
    alt head != tail
        Render->>Ring: consume payload[tail]
    else head == tail
        Render->>Render: re-use last payload (drop frame)
    end
```

`MailBoxBufferHead` is a `PaddedAtomic<uint32_t>` index into a 3-slot ring. No mutex, no blocking.

### 2. Background → Main: MainThreadScheduler

```mermaid
sequenceDiagram
    participant Worker as ThreadPool Worker
    participant MTS as MainThreadScheduler
    participant Main as Main Thread (step 9)

    Worker->>MTS: Post(ctx, fn) — fetch_add claims slot
    Worker->>MTS: write ctx + fn
    Worker->>MTS: ready[slot].store(true, release)
    Main->>MTS: Drain() — exchange write_cursor to 0
    loop for each claimed slot
        Main->>MTS: spin on ready[i].load(acquire)
        Main->>Main: fn(ctx)
        Main->>MTS: ready[i].store(false)
    end
```

512 arena-allocated slots, lock-free MPSC, no heap, C-style `Post(void* ctx, void (*fn)(void*))`.

**Current users:** `AssetImporterUIComponent` — posts `TriggerScan()` after import completes or fails.

### 3. Asset thread → Render thread: RRM pending queue

```mermaid
sequenceDiagram
    participant Asset as Asset Thread
    participant AM as AssetManager
    participant RRM as RenderResourceManager
    participant GPU as GPU

    Asset->>AM: IngestMesh(mesh, hier)
    AM->>AM: AssetRegistry::SetState(Loaded)
    AM->>RRM: OnAssetReady callback
    RRM->>RRM: m_pending.push (m_pending_mutex)
    Note over RRM: Next BeginFrame on render thread
    RRM->>RRM: FlushPendingUploads — drain m_pending[]
    RRM->>GPU: DoUploadMesh → vkCmdCopyBuffer
```

`m_pending[MAX_PENDING=256]` is a fixed array protected by `m_pending_mutex`. All Vulkan work stays on the render thread.

---

## ECS and Simulation

**Files:** `ZEngine/ZEngine/ECS/`

### Two-tier object model

```mermaid
flowchart TD
    AM["ActorManager\nMAX_ACTORS = 1024\narena-backed HandleManager"]
    A["Actor (Tier 1)\nC++ object with vtable\nOwns EntityID\nOnCreate / OnTick / OnDestroy"]
    EID["EntityID\nindex (uint32) + generation (uint32)\nIsValid() = generation != 0"]
    SC["Scene\nEntityRegistry (MAX 65536)\nUnorderedHashMap<ComponentTypeID, IComponentStorage*>"]
    CS["ComponentStorage<T>\nsparse-set\nm_sparse[] + m_dense[] + m_dense_ids[]"]
    WT["WorldTick\nDAG scheduler\nMAX_SYSTEMS = 64"]
    WC["WorldCommands\ndeferred mutations\ninitial cap 256 commands"]

    AM -->|"Create<T>() allocates Actor\nassigns EntityID"| A
    A -->|"owns"| EID
    EID -->|"indexes"| SC
    SC -->|"stores components in"| CS
    WT -->|"drives"| SC
    WT -->|"writes deferred mutations to"| WC
    WC -->|"Flush() applies to"| SC
```

| Tier | Type | Used for | Limit |
|---|---|---|---|
| 1 | `Actor` — C++ object with vtable, owns an `EntityID` | Player, camera, lights, scripted objects | 1 024 |
| 2 | Raw `EntityID` — data only, no vtable | Foliage, particles, projectiles, bulk objects | 65 536 |

Both tiers share the same `ComponentStorage<T>` sparse-set arrays. `Scene::ForEach<Ts...>()` and `Query<Ts...>` visit all alive entities regardless of tier.

---

### Components

All components live in `ECS/Components/`, namespace `ZEngine::ECS::Components`. A `ComponentTypeID` (`uint32_t`) is assigned lazily at the first call to `ComponentTypeOf<T>()` via an atomic counter. Up to 64 component types are supported (one bit per type in the 64-bit `ArchetypeMask`).

| Component | Key fields | Notes |
|---|---|---|
| `TransformComponent` | `Position`, `Rotation`, `Scale`, `PreviousPosition` (all `Vec3f`) | Fits one cache line (`sizeof ≤ 64`). `PreviousPosition` used for fixed-timestep interpolation. Distinct from `Rendering::Components::TransformComponent`. |
| `MeshComponent` | `uuids::uuid MeshUUID`, `uint32_t RenderInstanceId` | `RenderInstanceId = UINT32_MAX` = not yet registered in `RenderScene`. The bridge to update this is not yet implemented (see ECS → Render gap below). |
| `CameraComponent` | `FovY`, `Near`, `Far`, `AspectRatio`, `bool IsMain` | Exactly one entity should have `IsMain = true`. |
| `LightComponent` | `Type` (Directional/Point/Spot), `Intensity`, `Range`, `SpotAngle`, `Color[3]` | Range and SpotAngle unused for Directional lights. |
| `MaterialComponent` | `uuids::uuid MaterialUUID` | Per-instance material override. Absent = use mesh's baked material UUIDs. |
| `NameComponent` | `char Value[128]` | Display name for Outliner and debug. |
| `RigidBodyComponent` | `MotionType` (Static/Kinematic/Dynamic), `Mass`, `Friction`, `Restitution`, `uint32_t BodyID` | `BodyID = UINT32_MAX` = inactive. Physics integration (Jolt) planned Sprint 6. |
| `UUIDComponent` | `uuids::uuid Value` | Stable cross-session identity for scene serialization. |

---

### Scene

`Scene` owns the entity registry and all component storages. All arena allocations go through the `ArenaAllocator*` provided at `Initialize`.

```cpp
// Entity lifecycle
EntityID CreateEntity();
void     DestroyEntity(EntityID id);   // removes from all storages
bool     IsAlive(EntityID id) const;

// Component access (all template, O(1) via sparse-set)
template<T> void  AddComponent(EntityID id, T component);
template<T> T*    GetComponent(EntityID id);
template<T> bool  HasComponent(EntityID id) const;
template<T> void  RemoveComponent(EntityID id);

// Query — visits all alive entities that have all Ts
template<typename... Ts, typename Fn>
void ForEach(Fn&& fn);   // Fn signature: void(EntityID, Ts&...)

// Fixed-timestep interpolation support
void SnapshotTransforms();                              // PreviousPosition ← Position
void FillRenderableTransforms(float alpha,             // lerp Position by alpha
    Array<RenderableTransform>& out);
```

**`ForEach` iteration model:** iterates entity slots in the `EntityRegistry`, checks the `ArchetypeMask` against `(ComponentTypeOf<T1> | ComponentTypeOf<T2> | ...)`, and skips non-matching entities. Does not iterate component dense arrays directly. `Query<Ts...>` is a thin wrapper that pre-computes the mask at construction.

---

### Actor

`Actor` is the Tier-1 entity class. It cannot be instantiated directly — only via `ActorManager::Create<T>()`.

```mermaid
sequenceDiagram
    participant App
    participant AM as ActorManager
    participant Scene

    App->>AM: Create<PlayerActor>()
    AM->>Scene: CreateEntity() → EntityID
    AM->>AM: arena-alloc PlayerActor, assign EntityID + Scene*
    AM->>App: actor->OnCreate()   ← override to AddComponent()
    AM-->>App: ActorHandle

    App->>AM: Destroy(handle)
    AM->>App: actor->OnDestroy()  ← override to clean up
    AM->>Scene: DestroyEntity(id)
    AM->>AM: explicit destructor, remove handle
```

**Virtual interface:**
```cpp
virtual void OnCreate()       {}   // called once after entity assigned; add components here
virtual void OnDestroy()      {}   // called before entity destroyed
virtual void OnTick(float dt) {}   // called every frame by ActorManager::Tick()
```

**Component helpers (inline on the Actor base):**
```cpp
template<T> void AddComponent(T c);
template<T> T*   GetComponent();
template<T> bool HasComponent() const;
template<T> void RemoveComponent();
```

No default components are attached by the base class. A typical `OnCreate()` override looks like:
```cpp
void PlayerActor::OnCreate() {
    AddComponent(TransformComponent{});
    AddComponent(MeshComponent{ .MeshUUID = uuid });
    AddComponent(NameComponent{ .Value = "Player" });
}
```

---

### WorldTick — System Scheduler

`WorldTick` builds a dependency DAG from registered systems (`SystemFn = void(*)(Scene&, float, WorldCommands&)`) and executes them in topological wave order. It ships no built-in systems — application code registers all systems.

```mermaid
flowchart TD
    R["RegisterSystem(fn, deps)\ndeps: ReadMask, WriteMask, UsesCommands"]
    OB["OrderBefore(a, b)\ndeclare explicit ordering edge\nonly needed when masks conflict"]
    C["Commit()\nBuildEdges — detect mask conflicts\nTopologicalSort (Kahn)\nAssert: no cycles, no unresolved conflicts\nPre-allocate per-system staging WorldCommands"]
    W0["Wave — single system\nruns inline on main thread\nwrites to caller's WorldCommands"]
    W1["Wave — multiple systems\ndispatch to ThreadPool workers\neach writes to its own staging buffer"]
    B["Barrier\n100-spin yield then cv.wait_for (30s)"]
    M["Merge staging buffers\n(in wave order)\nfix up SpawnCallbackIndex offsets"]
    F["WorldCommands::Flush(scene)\napply SpawnEntity / DestroyEntity\nAddComponent / RemoveComponent"]

    R --> OB --> C
    C --> W0 --> W1 --> B --> M --> F
    F -->|"next Tick()"| W0
```

**Conflict rule:** systems A and B conflict if any of these overlap: `A.WriteMask & B.ReadMask`, `A.WriteMask & B.WriteMask`, `A.ReadMask & B.WriteMask`. Conflicting systems must have an explicit `OrderBefore` edge or `Commit()` asserts.

---

### WorldCommands — Deferred Mutations

Structural scene mutations (`AddComponent`, `DestroyEntity`, etc.) cannot happen inside a parallel wave — they would race with other workers reading the same sparse-set arrays. Every mutation is buffered in `WorldCommands` and applied atomically after the wave barrier.

```mermaid
flowchart TD
    PW["Parallel wave\nN workers each have own staging[i]"]
    S0["worker 0\nstaging[0].SpawnEntity(callback)\nstaging[0].AddComponent(id, comp)"]
    S1["worker 1\nstaging[1].DestroyEntity(id)\nstaging[1].RemoveComponent<T>(id)"]
    B["Barrier — all workers complete"]
    M["Main thread: Merge staging[0..N]\nfix SpawnCallbackIndex offsets"]
    F["WorldCommands::Flush(scene)\napply in command order\nSpawnCallbacks invoked with new EntityID"]

    PW --> S0 & S1 --> B --> M --> F
```

**Supported command kinds:**

| Command | What it does in `Flush()` |
|---|---|
| `SpawnEntity(callback)` | Calls `scene.CreateEntity()`; invokes optional `SpawnCallback(EntityID)` with the new id |
| `DestroyEntity(id)` | Deduplicates; guards for already-dead entity |
| `AddComponent<T>(id, T)` | Copies component (≤ 256 bytes) into fixed buffer; calls `scene.AddComponent` |
| `RemoveComponent<T>(id)` | Stores type-erased apply-fn pointer; calls `scene.RemoveComponent<T>` |

Staging buffers are pre-allocated at `Commit()` (arena-backed, initial cap 256 commands per system) and reused every frame via `Clear()` — no heap after the first exercised frame.

---

### ECS → Render Bridge (gap — issue #604)

The connection between ECS state and the render pipeline is **partially implemented but not wired**.

```mermaid
flowchart LR
    TC["TransformComponent\n(ECS)"]
    MC["MeshComponent\nRenderInstanceId = UINT32_MAX"]
    FRT["Scene::FillRenderableTransforms(alpha)\n→ Array<RenderableTransform>"]
    RS["RenderScene::MeshInstance[]\nId, MeshUUID, Transform, Name"]
    DP["DrawScene → DrawIndirect"]

    TC -->|"exists"| FRT
    MC -.->|"RenderInstanceId never set\nno bridge system"| RS
    FRT -.->|"output never consumed\nno system reads it"| RS
    RS --> DP
```

**What exists:**
- `Scene::FillRenderableTransforms(float alpha, Array<RenderableTransform>& out)` — lerps between `PreviousPosition` and `Position` for all entities with `TransformComponent`. The function is fully implemented.
- `MeshComponent::RenderInstanceId` — the intended hook for linking an ECS entity to a `RenderScene::MeshInstance`.
- `RenderScene::SetInstanceTransform(uint32_t id, Mat4f)` — the receiving API on the render side.

**What is missing:**
- No ECS system that reads `MeshComponent` + `TransformComponent` and calls `RenderScene::SetInstanceTransform`.
- `MeshComponent::RenderInstanceId` is always `UINT32_MAX`; nothing ever calls `RenderScene::AddMeshInstance` and writes the result back.
- `FillRenderableTransforms` output is never consumed.

Until the bridge is wired, mesh transforms in the scene are set manually by editor code via `RenderScene::SetInstanceTransform` when actors are dragged in the viewport.

---

## Asset Pipeline

```mermaid
flowchart TD
    src["Source file\n.glb / .fbx / .png / .hdr"]
    importer["GltfImporter / AssimpImporter\n(ThreadPool worker via ImportCoordinator)"]
    cooked["Cooked artifacts\n.zemesh · .zematerial · Assets/Textures/…"]
    ingest["AssetManager::IngestMesh\nAssetManager::IngestTextures\nAssetManager::IngestMaterial"]
    registry["AssetRegistry::SetState(Loaded)\n→ RRM::OnAssetReady → m_pending.push"]
    rrm["RRM::FlushPendingUploads\n(render thread, next BeginFrame)"]
    gpu["GPU global VB/IB\nTextureArray (bindless)"]
    pipeline["AppRenderPipeline::Tick\n(when InstancesDirty)\nSubMeshAllocation + DrawIndirect"]

    src --> importer --> cooked --> ingest --> registry --> rrm --> gpu --> pipeline
```

On scene reload: `EditorScene::ExtractAsync` calls `RRM::ResetGeometryBuffers()` before ingestion;
the render thread resets global buffer cursors to 0 so new geometry starts fresh.

---

## Virtual File System (VFS)

**Files:** `ZEngine/ZEngine/Core/VFS/`

```mermaid
graph TD
    ctx["VFSContext"]
    m1["Mount /ZodiacEngine\n→ VFSDiskBackend\n→ cwd/ZodiacEngine/\npriority = -1"]
    m2["Mount /\n→ VFSDiskBackend\n→ project root\npriority = 0"]
    path["VFSPath\nnormalized, immutable\nno-alloc value type"]
    scanner["VFSScanner\nasync directory walker\npopulates content browser cache"]
    watcher["VFSFileWatcher\nFSEvents / inotify / RDCW\ndebounced via VFSContext::Tick()"]
    meta["MetaFileIO\n.meta sidecars\nuuid · source path · artifact path"]
    reg["AssetRegistry\nuuid → SlotHandle + state\ntriggers RRM::OnAssetReady on Loaded"]

    ctx --> m1 & m2
    ctx --> scanner & watcher & meta & reg
    ctx --> path
```

**FileWatcher → hot-reload flow:**

```mermaid
sequenceDiagram
    participant FW as VFSFileWatcher
    participant CTX as VFSContext::Tick (main thread)
    participant AR as AssetRegistry
    participant IC as ImportCoordinator
    participant TP as ThreadPool

    FW->>CTX: debounced change event
    CTX->>AR: SetStale(uuid)
    CTX->>IC: Enqueue(path, Immediate)
    IC->>TP: dispatch reimport job (next Tick)
```

---

## Initialization Order

`Engine::Initialize()` — `ZEngine/ZEngine/Engine.cpp`

```mermaid
sequenceDiagram
    participant EP as EntryPoint
    participant Win as GameWindow
    participant Dev as VulkanDevice
    participant VFS as VFSContext
    participant AM as AssetManager
    participant ECS as ECS (Scene · ActorManager · WorldTick)
    participant IC as ImportCoordinator
    participant RRM as RenderResourceManager
    participant MTS as MainThreadScheduler

    EP->>Win: Initialize (GLFW + VkInstance + surface)
    Win->>Dev: Initialize (queues, VMA, bindless descriptors)
    Dev->>VFS: Initialize (mount table, disk backend)
    VFS->>AM: Initialize (UUID map, registry, 512 MB sub-arena)
    AM->>ECS: Initialize (Scene, ActorManager, WorldCommands, WorldTick)
    ECS->>IC: Initialize + register importers (Gltf, Assimp, EnvMap)
    IC->>RRM: Initialize (global VB/IB, texture timelines, upload pool)
    RRM->>AM: InitFallbackTexture (hot-pink 4×4 — requires RRM live)
    AM->>VFS: InitWatcher (FSEvents / inotify / RDCW)
    VFS->>MTS: Initialize (512 MPSC slots from MainArena)
```

Hard dependencies: Device before VFS (surface), RRM before fallback texture, watcher after working directory.

---

## Shutdown Order

```mermaid
flowchart TD
    T["s_request_terminate = true"]
    RT["Join render thread\nNO GPU work after this"]
    MTS["MainThreadScheduler::Shutdown\ndiscard pending tasks"]
    ECS["ECS::ActorManager::Shutdown\nECS::Scene::Shutdown"]
    RRM["RRM::Shutdown\nQueueWaitAll · destroy upload pools · free global buffers"]
    AM["AssetManager::Shutdown"]
    ARP["AppRenderPipeline::Shutdown\nRenderGraph::Dispose (pipelines, framebuffers)\nZUIRenderer::Deinitialize"]
    VFS["VFS::Shutdown"]
    DEV1["VulkanDevice::Deinitialize\nQueueWaitAll · first PendingFree drain\nSwapchainPtr→Dispose · CommandBufferMgr::Deinit\nsecond PendingFree drain"]
    WIN["Window::Deinitialize"]
    DEV2["VulkanDevice::Dispose\nfinal PendingFree drain\nGpuMem::Shutdown · vkDestroyDevice"]

    T --> RT --> MTS --> ECS --> RRM --> AM --> ARP --> VFS --> DEV1 --> WIN --> DEV2
```

See [Rendering Domain — Shutdown](rendering-domain.md#shutdown-and-teardown) for the Vulkan object destruction rules.
