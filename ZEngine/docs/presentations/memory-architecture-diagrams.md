---
marp: true
theme: default
paginate: true
style: |
  section {
    font-family: 'Segoe UI', 'Helvetica Neue', Arial, sans-serif;
    font-size: 20px;
    padding: 32px 48px;
    display: flex;
    flex-direction: column;
    justify-content: flex-start;
  }
  h1 {
    font-size: 1.3em;
    color: #1a1a2e;
    border-bottom: 3px solid #e94560;
    padding-bottom: 8px;
    margin-bottom: 20px;
    flex-shrink: 0;
  }
---

# Engine Memory Architecture — 8 GB Root Arena

```mermaid
graph TD
    OS["OS — mmap / VirtualAlloc"]
    ROOT["Root Arena  8 GB virtual reservation"]

    OS --> ROOT

    ROOT --> VD["VulkanDevice  1 GB\nVMA · command pools · descriptor pools"]
    ROOT --> IP["ImportPipeline  1 GB\nGltf · Assimp · EnvMap importers"]
    ROOT --> AM["AssetManager  512 MB\nMeshes · Materials · UUID maps"]
    ROOT --> ECS["ECSScene  512 MB\nComponentStorage · EntityRegistry"]
    ROOT --> SZ["Serializer  256 MB\nScene file scratch"]
    ROOT --> AN["AnimationMgr  256 MB\nAnimation clips · blend trees · state machines"]
    ROOT --> UI["UIContext  128 MB\nImGui · editor components"]
    ROOT --> VFS["VirtualFS  64 MB\nMount table · scanner · filewatcher"]
    ROOT --> SC["ShaderCache  64 MB\nSPIR-V · reflection data"]
    ROOT --> HR["Headroom  ~4.2 GB\nStreaming · Physics · Navigation"]

    style ROOT fill:#1a1a2e,color:#fff
    style OS   fill:#0f3460,color:#fff
    style HR   fill:#555,color:#ccc
```

---

# Memory Budget — Committed Distribution

```mermaid
pie title Committed Budget (~3.8 GB of 8 GB virtual)
    "VulkanDevice"    : 1024
    "ImportPipeline"  : 1024
    "AssetManager"    : 512
    "ECSScene"        : 512
    "Serializer"      : 256
    "AnimationMgr"    : 256
    "UIContext"       : 128
    "VirtualFS"       : 64
    "ShaderCache"     : 64
    "Other (logging, input, swapchain)" : 20
```

---

# Memory Model — Lifetime Hierarchy

```mermaid
graph LR
    subgraph Engine["Engine Lifetime — shutdown only"]
        direction TB
        VD2["VulkanDevice Arena\nShader cache, VMA, descriptor pools"]
        ECS2["ECSScene Arena\nArchetype tables, entity registry"]
        AM2["AssetManager Arena\nMesh / material / texture arrays"]
    end

    subgraph Scene["Scene Lifetime — scene load/unload"]
        direction TB
        ES["EditorScene::LocalArena  200 MB\nInstance arrays, scene graph, strings"]
    end

    subgraph Task["Per-Task — cleared after task"]
        direction TB
        IP["ImportPipeline  1 GB\nGltf + Assimp decode scratch\nCleared after each import session"]
    end

    subgraph Frame["Per-Frame — reset end of frame"]
        direction TB
        AT["ArenaTemp (ZGetScratch)\nBarrier batch, draw list\nCamera UBO staging"]
    end

    subgraph Object["Per-Object — individual free"]
        direction TB
        PA["PoolAllocator\nEntity slots, command buffer handles\nMesh instance slots"]
    end

    Engine --> Scene --> Task --> Frame
    Engine --> Object

    style Engine fill:#1a1a2e,color:#fff
    style Scene  fill:#16213e,color:#fff
    style Task   fill:#0f3460,color:#fff
    style Frame  fill:#2ecc71,color:#000
    style Object fill:#3498db,color:#fff
```

---

# Memory Model — Allocation Flow

```mermaid
graph TD
    ROOT["Root Arena  8 GB"]

    ROOT --> VD["VulkanDevice  1 GB\n(ArenaAllocator)"]
    ROOT --> AM["AssetManager  512 MB\n(ArenaAllocator)"]
    ROOT --> ECS["ECSScene  512 MB\n(ArenaAllocator)"]

    VD  --> RP["AppRenderPipeline\n30 MB sub-arena"]
    VD  --> TLS["TLSFSlab × N workers\n64–128 MB each\n(Phase 1 target — not yet in codebase)"]
    RP  --> SC["ZGetScratch\nArenaTemp — reset each frame"]

    AM  --> PM["Array&lt;AssetMesh&gt; · Array&lt;AssetMaterial&gt;\nbacked by ArenaAllocator (grows → dead blocks)"]
    AM  --> PH["UnorderedHashMap&lt;uuid,Handle&gt;\nbacked by ArenaAllocator (grows → dead blocks)"]

    ECS --> PE["PoolAllocator\nEntitySlots fixed-cap"]
    ECS --> PC["PoolAllocator\nComponentSlots fixed-cap"]

    style ROOT fill:#1a1a2e,color:#fff
    style SC   fill:#2ecc71,color:#000
    style TLS  fill:#e94560,color:#fff
```

---

# Allocation Decision Tree

```mermaid
flowchart TD
    START(["New allocation needed"])

    Q1{"Group lifetime?\ne.g. reset each frame\nor after import"}
    Q1b{"Stable after init?\nno grows once setup is done"}
    Q2{"Fixed size?\nsame N bytes\nevery time"}
    Q3{"Variable size +\nindividual lifetime?"}
    WARN(["Re-examine the lifetime.\nDo NOT use std::vector / new."])

    A1["ArenaAllocator or ArenaTemp\nPick arena whose lifetime matches.\nAlloc: ~3 cycles. No free."]
    A1g["TLSFSlab\nGroup lifetime but grows unpredictably.\nPre-size OR use slab for realloc.\nAlloc/free: ~30 cycles. O(1)."]
    A2["PoolAllocator\nCarve from parent arena at init.\nAlloc: ~5 cyc + memset(chunk). Free: ~5 cyc. O(1)."]
    A3["TLSFSlab\nOne slab carved from arena at init.\nAlloc/free: ~30 cycles. O(1). Variable size."]

    START --> Q1
    Q1 -- YES --> Q1b
    Q1b -- YES --> A1
    Q1b -- "NO, grows" --> A1g
    Q1 -- NO  --> Q2
    Q2 -- YES --> A2
    Q2 -- NO  --> Q3
    Q3 -- YES --> A3
    Q3 -- NO  --> WARN

    style A1   fill:#2ecc71,color:#000
    style A1g  fill:#e94560,color:#fff
    style A2   fill:#3498db,color:#fff
    style A3   fill:#e94560,color:#fff
    style WARN fill:#e74c3c,color:#fff
```

---

# Thread Safety — Per-Worker TLSFSlabs

```mermaid
graph TD
    SUB["Submitter Thread\nThreadPoolHelper::Submit(lambda)"]

    SUB -- "round-robin\ncursor % WorkerCount" --> W0
    SUB -- "round-robin" --> W1
    SUB -- "round-robin" --> W2
    SUB -- "round-robin" --> W3

    subgraph TP["ThreadPool (hardware_concurrency - 1, max 16)"]
        W0["Worker 0\nWorkerRun(0)\nt_worker_slab = &slab[0]"]
        W1["Worker 1\nWorkerRun(1)\nt_worker_slab = &slab[1]"]
        W2["Worker 2\nWorkerRun(2)\nt_worker_slab = &slab[2]"]
        W3["Worker 3\nWorkerRun(3)\nt_worker_slab = &slab[3]"]
    end

    W0 -- "exclusive\nno lock" --> S0["TLSFSlab[0]\n64 MB"]
    W1 -- "exclusive\nno lock" --> S1["TLSFSlab[1]\n64 MB"]
    W2 -- "exclusive\nno lock" --> S2["TLSFSlab[2]\n64 MB"]
    W3 -- "exclusive\nno lock" --> S3["TLSFSlab[3]\n64 MB"]

    S0 & S1 & S2 & S3 -- "one arena carve at init" --> VA["VulkanDevice Arena\n1 GB"]

    style VA fill:#1a1a2e,color:#fff
    style S0 fill:#e94560,color:#fff
    style S1 fill:#e94560,color:#fff
    style S2 fill:#e94560,color:#fff
    style S3 fill:#e94560,color:#fff
```

---

# Upload Pipeline — Sequence View

```mermaid
sequenceDiagram
    participant S  as Submitter
    participant TP as ThreadPool Worker N
    participant SL as TLSFSlab[N]
    participant Q  as DeferralQueue
    participant RT as Render Thread
    participant GPU as GPU Upload

    S  ->> TP  : Submit(decode task)
    TP ->> SL  : Alloc(w×h×4×4)        ── decode buffer
    Note over TP: stbi_load writes here only if STBI_MALLOC overridden
    TP ->> TP  : stbi_load / DeserializeEnvMap → raw pixels
    TP ->> SL  : Alloc(cubemap_bytes)   ── final pixel buffer
    TP ->> SL  : Free(decode buffer)    ── intermediates freed immediately
    TP ->> Q   : Enqueue TextureDeferral { Pixels*, ByteSize, Slab* }

    RT ->> Q   : Pop TextureDeferral
    RT ->> GPU : UploadTextureBuffer(Pixels)
    Note over RT,SL: Cross-thread free — requires lock or deferred-free queue (open problem)
    RT ->> SL  : Free(Pixels)           ── O(1), block merges back
```

---

# Roadmap — TLSF Integration

```mermaid
gantt
    title TLSF Integration Roadmap
    dateFormat  YYYY-MM-DD
    axisFormat  %b %Y

    section Phase 1 — Upload Pipeline (v0.5.0)
    TLSFSlab wrapper (h + cpp)       :p1a, 2026-09-01, 3d
    ThreadPool TLS slab pointer      :p1b, after p1a, 2d
    Cross-thread free resolution     :p1c, after p1b, 3d
    RRM slab init + sizing           :p1d, after p1c, 2d
    TextureDeferral struct change    :p1e, after p1d, 2d
    Replace upload std::vector       :p1f, after p1e, 3d
    STBI_MALLOC override             :p1g, after p1f, 2d
    Profile + validate               :p1h, after p1g, 3d

    section Phase 2 — AssetManager (v0.6.0)
    Array/HashMap typed allocator    :p2a, 2026-10-01, 7d
    AssetManagerSlab creation        :p2b, after p2a, 3d
    Container migration              :p2c, after p2b, 5d
    Dead-block waste measurement     :p2d, after p2c, 2d

    section Phase 3 — ECS Storage (v1.0.0)
    Per-archetype TLSFSlab           :p3a, 2026-12-01, 5d
    Variable-payload component types :p3b, after p3a, 7d
```
