# Asset Manager

Single authority over all CPU-side cooked asset data at runtime. Owns every mesh, material,
texture, and node hierarchy ingested during a session, and owns the `GPUMeshMaterials` mirror
that drives the `MatSB` storage buffer read by the G-buffer fragment shader every frame.

**Status:** Current runtime ownership reference. The manager has 1,024 MiB in the configured
CPU profile and a 256 MiB TLSF container slab; its CPU assets are session-lifetime data. GPU
geometry residency/eviction is a separate `RenderResourceManager` responsibility.

See also: [Engine Architecture](engine-architecture.md) · [Rendering Domain](rendering-domain.md) · [Memory Management](memory-management.md)

---

## Table of Contents

- [Position in the Pipeline](#position-in-the-pipeline)
- [Memory Layout](#memory-layout)
- [Data Structures](#data-structures)
- [Ingest Pipeline](#ingest-pipeline)
  - [Deduplication](#deduplication)
  - [IngestMesh](#ingestmesh)
  - [IngestTexture](#ingesttexture)
  - [IngestMaterial](#ingestmaterial)
- [GPU Material Binding — End to End](#gpu-material-binding-end-to-end)
- [Lookup API](#lookup-api)
- [Thread Safety](#thread-safety)
- [Initialization Order Contract](#initialization-order-contract)
- [Known Gaps](#known-gaps)

---

## Position in the Pipeline

```mermaid
flowchart LR
    disk[(Disk\n.zemesh / .zematerial\n.png textures)]
    importers["Importers\nGltfImporter\nAssimpImporter"]
    codec["AssetCodec\nSerialize* / Deserialize*"]
    AM["AssetManager\nIngestMesh\nIngestTexture\nIngestMaterial"]
    RRM["RenderResourceManager\nSubmitTextureFile\nUpdateBuffer"]
    GPU[("GPU\nTextureArray\nMatSB · VertexSB")]
    renderer["GraphicRenderer\nDrawScene"]

    disk -->|cook-time| importers
    disk -->|load-time| codec
    importers --> codec
    codec --> AM
    AM -->|TextureHandle| RRM
    RRM -->|async GPU upload| GPU
    AM -->|GPUMeshMaterials| renderer
    renderer -->|UpdateBuffer every frame| GPU
```

**AssetManager does not** stream assets in or out. Everything ingested lives for the lifetime
of the session in a fixed arena. A future `StreamingManager` will sit above it and manage
residency.

---

## Memory Layout

The manager receives the 1,024 MiB `AssetManager` arena configured by `MemoryBudgetConfig`.
The meshes, node hierarchies, materials, texture-handle map, and material-slot map use a 256 MiB
TLSF slab so their variable-size growth can reclaim old blocks. `GPUMeshMaterials`, textures,
and some maps remain arena-backed. This is explicit engine-owned allocation, not a claim that
third-party import/decode code never uses another allocator.

```mermaid
graph TD
    root["Configured CPU profile limit · 8 GiB"]
    asset["AssetManager arena · 1,024 MiB\n256 MiB TLSF container slab"]

    meshes["Meshes\nArray of AssetMesh · cap 5000"]
    hier["NodeHierarchies\nArray of AssetNodeHierarchy · cap 5000"]
    mats["Materials\nArray of AssetMaterial · cap 5000"]
    tex["Textures\nArray of AssetTexture · cap 5000"]
    gpu["GPUMeshMaterials\nArray of MeshMaterial · cap 5000\nmirrors Materials 1:1"]

    uuid_map["UUIDToTextureHandle\nHash map: uuid → TextureHandle"]
    hier_map["MeshToHierarchySlot\nHash map: MeshUUID → slot_index"]
    registry["AssetRegistry\nuuid → SlotHandle + AssetState"]

    root --> asset
    asset --> meshes
    asset --> hier
    asset --> mats
    asset --> tex
    asset --> gpu
    asset --> uuid_map
    asset --> hier_map
    asset --> registry
```

---

## Data Structures

### Flat arrays and slot indexing

Access to any asset is always by **slot index** extracted from an `AssetHandle`. The UUID is
first resolved through `AssetRegistry` to an `AssetHandle`; the handle encodes the type and
slot in a single `uint32_t`:

```
31      28 27                              0
┌──────────┬─────────────────────────────────┐
│  type(4) │         slot index (28)          │
└──────────┴─────────────────────────────────┘
```

`CreateHandle(slot, type)` and `ReadAssetHandleIndex(handle)` are the only encode/decode
points — no caller should bit-shift manually.

### `MeshMaterial` — the GPU struct

`MeshMaterial` is uploaded verbatim to `MatSB` (set 0, binding 5) every frame via
`RRM::UpdateBuffer`. It mirrors `AssetMaterial` but replaces UUID texture references with
**bindless `TextureArray` indices**.

```
struct MeshMaterial {             // std140 · 96 bytes
    gpuvec4  AmbientColor;
    gpuvec4  EmissiveColor;
    gpuvec4  AlbedoColor;
    gpuvec4  SpecularColor;
    gpuvec4  RoughnessColor;
    gpuvec4  Factors;
    uint64_t EmissiveMap;         // TextureArray index  OR  0xFFFFFFFF (INVALID_MAP_HANDLE)
    uint64_t AlbedoMap;
    uint64_t SpecularMap;
    uint64_t NormalMap;
    uint64_t OpacityMap;
    uint64_t _padding;
};
```

Fragment shader gate: `if (material.AlbedoMap < INVALID_MAP_HANDLE)` — valid textures have
small indices; unset slots hold `0xFFFFFFFF` and are skipped, falling back to the material
color fields.

---

## Ingest Pipeline

### Deduplication

Every `Ingest*` method calls `IsRegistered(uuid)` first. If the UUID is already in
`AssetRegistry`, the call is a **no-op** — the asset is already resident.

### IngestMesh

```mermaid
flowchart TD
    A["IngestMesh(mesh, hierarchy)"]
    B{"IsRegistered\nmesh.MeshUUID?"}
    C["return — already loaded"]
    D["Copy mesh vertices + indices + submeshes\ninto Arena · Meshes[slot]"]
    E["RegisterAsset MESH"]
    F["Copy hierarchy transforms + names\ninto Arena · NodeHierarchies[hier_slot]"]
    G["RegisterAsset MESH_HIERARCHY"]
    H["MeshToHierarchySlot[MeshUUID] = hier_slot\nO(1) lookup — no linear scan"]
    I["Registry.SetState → Loaded\n(fires hot-reload callbacks)"]

    A --> B
    B -->|yes| C
    B -->|no| D --> E --> F --> G --> H --> I
```

### IngestTexture

`IngestTexture(uuid, path)` is the **single canonical texture upload point**. Both
`IngestTextures` (batch) and `IngestMaterial` (per-slot fallback) call it.

```mermaid
flowchart TD
    A["IngestTexture(uuid, path)"]
    B{"IsRegistered\nuuid?"}
    C["return existing handle\nfrom UUIDToTextureHandle"]
    D["Textures.push_use()\nreserve slot"]
    E{"path empty?"}
    F["snprintf WorkingSpacePath + path\n→ abs_path\nRRM.SubmitTextureFile(abs_path)"]
    G{"Handle\nvalid?"}
    H["ASSERT FallbackHandle.Valid()\nlog 'not found'\nHandle = FallbackHandle"]
    I["ASSERT FallbackHandle.Valid()\nlog 'no path — extraction failed'\nHandle = FallbackHandle"]
    J["RegisterAsset TEXTURE\nUUIDToTextureHandle[uuid] = Handle\nreturn Handle"]

    A --> B
    B -->|yes| C
    B -->|no| D --> E
    E -->|no| F --> G
    G -->|valid| J
    G -->|invalid| H --> J
    E -->|yes| I --> J
```

The `ASSERT FallbackHandle.Valid()` enforces the startup invariant: `InitFallbackTexture()`
must run before any ingest. A crash here means the initialization sequence is broken — not
a recoverable error.

### IngestMaterial

```mermaid
flowchart TD
    A["IngestMaterial(mat)"]
    B{"IsRegistered\nmat.MaterialUUID?"}
    C["return — already loaded"]
    D["Materials.push(mat)\nGPUMeshMaterials.push_use()\nRegisterAsset MATERIAL"]
    E["Copy color fields\n(Albedo, Emissive, Roughness, Specular, Ambient, Factors)"]
    F["tex_handle(AlbedoTexUUID, AlbedoTexPath)"]
    G{"UUID in\nUUIDToTextureHandle?"}
    H["return Handle.Index"]
    I{"AlbedoTexPath\nempty?"}
    J["IngestTexture(uuid, path)\nreturn new Handle.Index\n— triggers GPU upload now"]
    K["return INVALID_MAP_HANDLE\n(shader skips this slot)"]
    L["gpu_mat.AlbedoMap = result\n↩ repeat for Emissive · Normal · Opacity · Specular"]

    A --> B
    B -->|yes| C
    B -->|no| D --> E --> F --> G
    G -->|yes| H --> L
    G -->|no| I
    I -->|no| J --> L
    I -->|yes| K --> L
```

The path-fallback branch (`IngestTexture` called from inside `IngestMaterial`) is what makes
**scene reload** and **dragged-.zmesh** work without a prior `IngestTextures` call: the material
carries its own texture paths from the `.zematerial` JSON and can trigger GPU upload on demand.

---

## GPU Material Binding — End to End

```mermaid
sequenceDiagram
    participant BG as Importer (background thread)
    participant AM as AssetManager
    participant RRM as RenderResourceManager
    participant GPU as GPU
    participant RT as GraphicRenderer (main thread)
    participant FS as g_buffer.frag

    BG->>AM: IngestTextures([tex_0 .. tex_N])
    AM->>RRM: SubmitTextureFile(abs_path) per texture
    RRM-->>AM: TextureHandle { Index = K }
    AM->>AM: UUIDToTextureHandle[uuid] = { Index = K }

    BG->>AM: IngestMaterial(mat)
    AM->>AM: tex_handle(AlbedoTexUUID) → K
    AM->>AM: GPUMeshMaterials[slot].AlbedoMap = K

    BG->>AM: IngestMesh(mesh, hier)
    AM->>AM: Meshes[slot] = mesh

    Note over RT: Next frame — InstancesDirty = true

    RT->>AM: GetMeshAsset(MeshUUID) → &Meshes[slot]
    RT->>AM: GetAsset for AssetMaterial (sub.MaterialUUID) → &Materials[mat_slot]
    RT->>RT: alloc.MaterialId = mat_slot

    RT->>RRM: UpdateBuffer(MaterialBuffer, GPUMeshMaterials)
    RRM->>GPU: vmaMemcpy → MatSB[mat_slot].AlbedoMap = K

    FS->>GPU: mat = FetchMaterial(MaterialIdx)
    FS->>GPU: texture(TextureArray[mat.AlbedoMap], uv)
    Note over FS: only if mat.AlbedoMap < INVALID_MAP_HANDLE
```

---

## Lookup API

`GetAsset<T>(key)` resolves a UUID or handle to a pointer into the flat arrays.
Two key types are supported for each of the four asset types (8 specializations total):

```mermaid
graph LR
    uuid["uuid key"]
    handle["AssetHandle key"]
    reg["AssetRegistry\nFindByUUID(uuid)"]
    sh["rec→SlotHandle"]
    idx["ReadAssetHandleIndex(h)"]
    arr["flat array\ne.g. Meshes[index]"]
    ptr["T* (or nullptr)"]

    uuid --> reg --> sh --> handle
    handle --> idx --> arr --> ptr
```

If the UUID is not registered or the slot index is out of range, all paths return `nullptr`.

---

## Thread Safety

| Method | Thread-safe | Notes |
|---|---|---|
| `IngestMesh` | Yes | Acquires `IngestMutex` (recursive) |
| `IngestTexture` | Yes | Acquires `IngestMutex` |
| `IngestTextures` | Yes | Calls `IngestTexture` per element |
| `IngestMaterial` | Yes | Acquires `IngestMutex`; may call `IngestTexture` (recursive lock) |
| `IsRegistered` | No standalone synchronization | Caller must not race registry mutation. |
| `GetAsset<T>(uuid/handle)` | No standalone synchronization | Safe after ingestion is quiescent or under the caller's synchronization. |
| `GetMeshNodeHierarchy` | No standalone synchronization | O(1) lookup; does not acquire `IngestMutex`. |
| `TryGetMeshBounds` | Yes | Acquires `IngestMutex` and copies the bounds. |
| `InitFallbackTexture` | Main thread only | Called once during engine init |

`IngestMutex` is `std::recursive_mutex` — `IngestMaterial` can call `IngestTexture` while
holding the lock without deadlocking.

---

## Initialization Order Contract

```mermaid
sequenceDiagram
    participant EP as EntryPoint
    participant MM as MemoryManager
    participant Log as Logger
    participant Eng as Engine::Initialize
    participant AM as AssetManager
    participant RRM as RenderResourceManager

    EP->>MM: Initialize(ZGiga(8ULL), Editor())
    Note over MM: ZGiga uses uint64_t — no overflow
    EP->>Log: Logger::Initialize(LoggingArena)
    EP->>Eng: app→Initialize(&manager)
    Eng->>AM: AssetManager::Initialize(arena, device, ws_path)
    Note over AM: FallbackTextureHandle is invalid here
    Eng->>RRM: RenderResourceManager::Initialize()
    Eng->>AM: AssetManager::InitFallbackTexture()
    Note over AM: FallbackTextureHandle = RRM.GetOrCreateFallbackTexture()
    Note over AM: Safe to call Ingest* from this point
```

Any `IngestTexture` call that reaches the fallback path before `InitFallbackTexture` fires:
```
ZENGINE_VALIDATE_ASSERT(FallbackTextureHandle.Valid(),
    "FallbackTextureHandle not initialized — InitFallbackTexture must be called before ingesting assets")
```

---

## Known Gaps

| Gap | Tracking |
|---|---|
| CPU-side cooked asset eviction — manager data remains session-lifetime | Future streaming/asset residency policy |
| GPU geometry residency/eviction policy | Implemented separately in `RenderResourceManager`; do not infer CPU AssetManager eviction from it. |
| Raster and environment-map import paths remain duplicated | [#750](https://github.com/JeanPhilippeKernel/RendererEngine/issues/750) |
| Manual reimport from the content browser needs current ZUI-panel integration | [#602](https://github.com/JeanPhilippeKernel/RendererEngine/issues/602) |
| Importer and ZUI regression-test scaffolding is absent | [#735](https://github.com/JeanPhilippeKernel/RendererEngine/issues/735) |
| Initial scanner results do not yet enqueue an import batch | `VFSScanner` currently registers assets; watcher modifications enqueue immediate reimport. |
