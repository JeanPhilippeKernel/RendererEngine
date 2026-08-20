# Memory Budget

Documents the full arena hierarchy, each slot's purpose, sizing rationale, and growth headroom.

**Root arena:** `ZGiga(8ULL)` = 8 GB (virtual reservation via `mmap` / `VirtualAlloc`; physical pages committed on first write — RSS is much lower than the virtual reservation).

---

## MemoryBudgetConfig slots

Defined in `ZEngine/ZEngine/Core/Memory/MemoryManager.h`. `Default()` is the game runtime profile; `Editor()` overrides UIContext and zeroes Audio/Network.

| Slot | Default | Editor | What lives here |
|---|---|---|---|
| `VulkanDevice` | 1 GB | 1 GB | VMA metadata, descriptor pool backing, command pools, swapchain objects |
| `ImportPipeline` | 1 GB | 1 GB | Engine importers: GltfImporter (64 MB) + AssimpImporter (350 MB) + EnvironmentMapImporter (32 MB) + ImportCoordinator. **Also** editor's duplicate GltfImporter (64 MB) + AssimpImporter (350 MB) in AssetImporterUIComponent — see [Editor import duplication](#editor-import-duplication) |
| `AssetManager` | 512 MB | 512 MB | Flat arrays: Meshes, NodeHierarchies, Materials, Textures, GPUMeshMaterials (all capped at 5000 entries). UUID hash maps (UUIDToTextureHandle, MeshToHierarchySlot). AssetRegistry. RenderResourceManager and ImportCoordinator structs placed in the budget parent arena |
| `ECSScene` | 512 MB | 512 MB | ComponentStorage dense arrays, EntityRegistry, ActorManager, WorldCommands staging buffers, WorldTick DAG |
| `Serializer` | 256 MB | 256 MB | EditorSceneSerializer scratch (150 MB sub-arena), scene file temporaries |
| `AnimationManager` | 256 MB | 256 MB | Skeleton data, clip arrays, pose pools (system not yet implemented) |
| `AudioEngine` | 128 MB | **0** | miniaudio state, decoded clip pool (disabled in editor) |
| `UIContext` | 64 MB | **128 MB** | ImguiLayer (64 MB) → all UI components (Dockspace 32 MB, AssetImporter 12 MB, ProjectView 4 MB, others) |
| `VirtualFS` | 64 MB | 64 MB | VFSContext mount table, VFSScanner cache, FileWatcher event queue, AssetRegistry scratch |
| `ShaderCache` | 64 MB | 64 MB | SPIR-V bytecode, shader reflection data |
| `Network` | 64 MB | **0** | Peer state, rollback ring buffers (disabled in editor) |
| `Logging` | 8 MB | 8 MB | Logger ring buffer (spdlog), category filter arrays |
| `Swapchain` | 8 MB | 8 MB | Swapchain-specific metadata |
| `Input` | 4 MB | 4 MB | InputManager state, key/axis binding tables |
| **Total committed** | ~3.95 GB | ~3.82 GB | **Headroom: ~4 GB** reserved for future systems |

---

## Arenas NOT in the budget config

These are carved directly from `MainArena` and are intentionally outside `MemoryBudgetConfig`.

| Who | Size | Why outside budget |
|---|---|---|
| `EditorScene::LocalArena` | 200 MB | Scene-level system, not a UI component. Covers AssetFiles list, scene graph data, seqlock instance buffers, material/texture path strings. |
| `AppRenderPipeline::LocalArena` | 30 MB | Render-pipeline-level scratch, carved from VulkanDevice arena |

---

## ImportPipeline breakdown

```
ImportPipeline (1 GB)
├── ImportCoordinator struct + queue    ~1 MB
├── Engine importers
│   ├── s_gltf_arena   (GltfImporter)   64 MB
│   ├── s_assimp_arena (AssimpImporter) 350 MB
│   └── s_envmap_arena (EnvMapImporter)  32 MB
└── Editor importers (AssetImporterUIComponent)
    ├── GltfImporterArena               64 MB
    └── AssimpImporterArena            350 MB
                                ─────────────
                    Total used:        861 MB
                    Headroom:          163 MB
```

### Editor import duplication

`AssetImporterUIComponent` maintains its own `GltfImporter` + `AssimpImporter` instances because:
1. The editor import UI requires granular callbacks (per-file output, per-log-message, progress percentage) that `ImportCoordinator`'s `ImportCallback` (`void(*)(void*, bool success)`) does not support.
2. Imports triggered from the editor panel run concurrently with background ImportCoordinator jobs — sharing importer instances would require an additional mutex and could stall the background pipeline.

**Design debt:** when `ImportCoordinator` is extended to support full progress/log/output callbacks (tracked in `import-pipeline.md`), the editor importers should be removed and `AssetImporterUIComponent::StartImport` should call `ImportCoordinator::Enqueue` instead.

---

## UIContext component chain (Editor)

```
UIContext (128 MB, Editor)
└── ImguiLayer::LocalArena (64 MB)
    ├── DockspaceUIComponent::LocalArena     32 MB
    │   └── (mesh deserialization scratch — large meshes need up to ~20 MB)
    ├── AssetImporterUIComponent::LocalArena  8 MB
    ├── AssetImporterUIComponent::LocalStringArena  4 MB
    ├── ProjectViewUIComponent::m_local_arena 4 MB
    └── remaining for other components       ~16 MB
```

---

## Headroom for future systems

The ~4 GB headroom in the 8 GB root is reserved for systems not yet built:

| System | Planned budget | Notes |
|---|---|---|
| `StreamingManager` | 2 GB | Open world chunk geometry + texture page streaming; arena cleared per-region-transition |
| `PhysicsEngine` | 512 MB | Rigid bodies, terrain collision, broad-phase structures |
| `NavigationEngine` | 256 MB | NavMesh, pathfinding graph, agent state |

These will be added as new `SubArenaConfig` fields in `MemoryBudgetConfig` when the systems are implemented.

---

## How to update this document

1. When a budget slot changes size, update the table above.
2. When a new slot is added, add it to the table and to `TotalCommitted()`.
3. When `EditorScene::LocalArena` or other unbudgeted arenas change, update the "Arenas NOT in the budget config" table.
4. Recalculate the total and verify headroom is positive with at least 1 GB margin.
