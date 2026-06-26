# ZEngine — Roadmap

ZEngine is a professional-grade 3D engine built from scratch in C++20 targeting Vulkan,
developed by a team of contributors. The goal is a data-oriented, high-performance engine
capable of shipping singleplayer and multiplayer games on Windows, Linux, and macOS.

Design documents for all systems live in `ZEngine/docs/` and `ZEngine/docs/future-plan/`.
Implementation follows the phased plan in `ZEngine/docs/migration-plan.md`.


## Legend

| Symbol | Meaning |
|---|---|
| [x] | Complete and production-quality |
| [ ] | Designed — ready to implement |
| [-] | Planned — not yet designed |


## Foundation

| Status | Feature |
|---|---|
| [x] | Custom arena and pool allocators (`ArenaAllocator`, `PoolAllocator`) |
| [x] | Custom containers (`Array`, `String`, `UnorderedHashMap`, `HashSet`) |
| [x] | Custom math library (`Vec2/3/4`, `Mat2/3/4`, `Quaternion`, full math utils) |
| [x] | Arena-backed scratch memory (`ZGetScratch` / `ZReleaseScratch`) |
| [x] | Intrusive reference counting (`Ref<T>`, `WeakRef<T>`) |
| [x] | Thread pool (`ThreadPoolHelper`) |
| [x] | Logging system (`ZENGINE_CORE_INFO/WARN/ERROR/CRITICAL`) |
| [ ] | Memory budget system — per-subsystem watermarks and limits |
| [ ] | Profiling and instrumentation (`ZENGINE_PROFILE_SCOPE`, Tracy integration) |
| [ ] | Crash handler — minidump, stack trace, user dialog, optional telemetry upload |

---

## ECS and Object Model

| Status | Feature |
|---|---|
| [ ] | Entity-Component System — sparse-set + archetype mask hybrid |
| [ ] | Generational entity handles — stale handle detection |
| [ ] | `ComponentStorage<T>` — dense arrays, O(1) add/remove/get |
| [ ] | `ECS::Scene` — top-level context, ForEach template query |
| [ ] | `Query<Ts...>` — cached mask, zero virtual dispatch |
| [ ] | Actor layer — Tier 1 objects (player, camera, lights) over ECS |
| [ ] | `ActorManager` — lifetime management, `OnTick` dispatch |
| [ ] | `WorldCommands` — deferred structural mutations from systems |
| [ ] | ECS component reflection — type names, field metadata for editor |


## System Scheduler

| Status | Feature |
|---|---|
| [ ] | DAG-based parallel system scheduler |
| [ ] | Conflict detection via read/write component masks |
| [ ] | `OrderBefore(SystemID, SystemID)` — explicit ordering edges |
| [ ] | Wave-based parallel dispatch on `ThreadPoolHelper` |
| [ ] | Cycle detection — asserts on invalid dependency graphs |
| [ ] | Runtime system enable/disable |


## Game Loop

| Status | Feature |
|---|---|
| [x] | Main thread + render thread split |
| [x] | Delta time, vsync toggle |
| [ ] | Fixed timestep accumulator — physics-correct, deterministic simulation |
| [ ] | Frame rate cap — deterministic sleep + spin-wait |
| [ ] | Frame packet double-buffering (main thread to render thread) |
| [ ] | Transform interpolation — alpha blending between fixed steps |


## Rendering

| Status | Feature |
|---|---|
| [x] | Vulkan device abstraction (`VulkanDevice`) |
| [x] | Render graph with node/resource/pass compilation |
| [x] | Multi-threaded command buffer recording |
| [x] | Swapchain management and frame synchronisation |
| [x] | Vertex/index/transform/material storage buffers |
| [x] | Gizmo rendering (ImGuizmo) |
| [x] | ImGui integration |
| [ ] | Render resource manager — GPU lifetime, deferred deletion, hot-reload swaps |
| [ ] | Cascaded shadow maps — 4 cascades, directional light |
| [ ] | Spot and point light shadow maps |
| [ ] | Post-processing pipeline — bloom, ACES tone mapping, SSAO, FXAA, LUT color grading, vignette |
| [ ] | PBR material pipeline — roughness/metallic/normal/emissive |
| [ ] | Shader hot-reload — SPIR-V recompile without frame stall |
| [-] | LOD system — distance-based mesh switching |
| [-] | GPU frustum and occlusion culling |
| [-] | GPU-driven indirect rendering |
| [-] | Async compute |
| [-] | Deferred rendering path |
| [-] | KTX2 / BCn / ASTC texture compression |
| [-] | Motion blur |
| [-] | Ray-traced global illumination (long-term) |
| [-] | DirectX 12 / Metal RHI backends (long-term) |


## Lighting

| Status | Feature |
|---|---|
| [x] | Directional, point, and spot lights |
| [x] | GPU-packed light structs |
| [ ] | Shadow mapping for all three light types |
| [ ] | IBL / environment lighting — cubemap, irradiance, specular probes |
| [-] | Tile or cluster-based light culling |
| [-] | Lightmap baking |

---

## Animation

| Status | Feature |
|---|---|
| [ ] | Skeletal animation — `SkeletonComponent`, `AnimatorComponent`, `SkinningComponent` |
| [ ] | `AnimationSampleSystem` — uniform 30fps clip sampling, pose buffer |
| [ ] | `SkinningUploadSystem` — global pose to GPU bone matrix upload |
| [ ] | `AssimpImporter` skeleton and clip extraction |
| [-] | Animation blend trees — multi-clip blending with weights |
| [-] | Animation state machine — transition graph |
| [-] | Root motion extraction |
| [-] | Animation events — frame-triggered callbacks |

## Physics

| Status | Feature |
|---|---|
| [ ] | Jolt Physics integration (MIT, C++17) |
| [ ] | Rigid body dynamics — static, kinematic, dynamic |
| [ ] | Collider shapes — box, sphere, capsule, convex hull, triangle mesh |
| [ ] | Character controller (`JPH::CharacterVirtual`) |
| [ ] | Raycasting and shape casts |
| [ ] | Trigger volumes and contact events |
| [ ] | Collision layer filtering |


## Audio

| Status | Feature |
|---|---|
| [ ] | miniaudio integration (public domain, single-header) |
| [ ] | Spatial 3D audio — distance attenuation, listener positioning |
| [ ] | `AudioSourceComponent` / `AudioListenerComponent` |
| [ ] | Per-frame audio command buffer — no audio calls during `WorldTick` |
| [ ] | Music streaming — `PlayMusic`, `FadeMusic` |
| [ ] | Supported formats: OGG Vorbis, WAV, MP3 |

## Networking

| Status | Feature |
|---|---|
| [ ] | `INetTransport` — pluggable transport interface (GNS, ENet, raw UDP) |
| [ ] | `GNSTransport` — GameNetworkingSockets (Steam relay, NAT punch-through, encryption) |
| [ ] | `ENetTransport` — lightweight transport, no Steam dependency |
| [ ] | `UDPTransport` — raw UDP for studios that own the full stack |
| [ ] | `NetworkSession` — peer registry, broadcast, tick |
| [ ] | Network clock — NTP-style two-way sync, median drift filter |
| [ ] | `NetBitWriter` / `NetBitReader` — bit-packing, compressed float, smallest-3 quaternion |
| [ ] | `NetReplicationRegistry` — component descriptor table, serialize/deserialize dispatch |
| [ ] | `ReplicationSystem` — authority-side dirty-flag delta compression |
| [ ] | `ReplicationReceiveSystem` — client-side apply and interpolate |
| [ ] | `RollbackModule` — GGPO-style rollback (snapshot ring buffer, resimulate) |
| [ ] | `PredictionModule` — client prediction with server correction reconciliation |
| [ ] | `NetRPC` — flat dispatch table, reliable remote procedure calls |
| [ ] | `LagCompensator` — server-side history rewind for hit detection |
| [ ] | `NetRelevanceSystem` — spatial grid, per-peer relevance sets, newly in/out diff |
| [ ] | Lobby and matchmaking — Steam P2P via Steamworks |
| [-] | BVH-based relevance (v2) — replaces spatial grid for non-uniform entity distributions |
| [-] | Snapshot interpolation for remote entities (v2) — eliminates jitter at packet loss |
| [-] | Visual rollback — rollback cosmetics over server-authoritative game state (v2) |
| [-] | QUIC transport backend via msquic (v2) — encrypted, multiplexed, no Steam dependency |

## Virtual File System

| Status | Feature |
|---|---|
| [ ] | `VFSPath` — normalized, immutable, no-alloc path value type |
| [ ] | `IVFSFile`, `IVFSBackend`, `IVFSContext` — clean I/O interfaces |
| [ ] | `VFSDiskContext` — passthrough to `std::filesystem` |
| [ ] | Mount table — priority-ordered backend resolution |
| [ ] | `VFSDiskBackend` and `VFSZipBackend` |
| [ ] | Async directory scanner — replaces `directory_iterator` on render thread |
| [ ] | `VFSMemoryBackend` — scratch and test store |
| [ ] | File watcher — inotify / FSEvents / RDCW, debounced events |
| [ ] | `.meta` sidecar files — stable UUIDs, SHA256 reimport detection |
| [ ] | Asset registry — multi-index store, dependency graph, hot-reload cascade |

---

## Asset Pipeline

| Status | Feature |
|---|---|
| [x] | Assimp mesh, material, texture import |
| [x] | Asset manager — UUID-to-handle map, pending queue |
| [ ] | Priority-driven async import coordinator |
| [ ] | Shader asset pipeline — GLSL to SPIR-V at import, UUID-keyed cache |
| [ ] | Cook pipeline — SHA256-incremental, parallel, headless CI |
| [ ] | `VFSPakBackend` — runtime mounting of cooked `.pak` archives |
| [ ] | `AssimpImporter` skeletal animation extraction |
| [ ] | SDF font atlas generation (msdf-atlas-gen) |
| [-] | GLTF 2.0 import |
| [-] | Asset streaming — incremental load at runtime |


## Scene Serialization

| Status | Feature |
|---|---|
| [ ] | `YAMLSceneSerializer` — human-readable, VCS-diffable (editor builds) |
| [ ] | `BinarySceneSerializer` — zero-parse, memcpy-speed load (shipping) |
| [ ] | Stable UUID asset references — never file paths |
| [ ] | Component serializer registry — auto-discovers all component types |
| [ ] | Scene migration — versioned binary format with upgrade paths |
| [ ] | Atomic write — temp + rename, no partial-write corruption |

## UI System

| Status | Feature |
|---|---|
| [x] | Editor UI (ImGui + ImGuizmo) |
| [ ] | In-game UI — immediate-mode API over arena-allocated retained tree |
| [ ] | Widget types — Label, Button, Image, ProgressBar, Panel |
| [ ] | Layout — stack, anchor, split, inset helpers |
| [ ] | Screen stack — `UIScreenStack` for menu navigation |
| [ ] | UI render pass — orthographic, no depth test, alpha blend |
| [ ] | Input routing — UI consumes mouse before game |

## Text Rendering

| Status | Feature |
|---|---|
| [ ] | MSDF SDF font atlas (msdf-atlas-gen, offline) |
| [ ] | `FontManager` — atlas load, glyph metric pool |
| [ ] | UTF-8 text layout — word wrap, kerning, tab stops |
| [ ] | Localization — CSV string tables, hot-swap language |
| [-] | CJK / large atlas support |
| [-] | Right-to-left text |

---

## Scripting and Game Logic

| Status | Feature |
|---|---|
| [ ] | C++ DLL hot-reload — `ZGame_*` C ABI, Actor factory pattern |
| [ ] | Engine version check — rebuild detection at load time |
| [ ] | System re-registration on reload — `BeginRebuild` / `Commit` cycle |
| [-] | Lua 5.4 scripting — `LuaScriptComponent`, per-entity coroutine |
| [-] | Behavior tree — flat node array, no virtual dispatch |

## Save System

| Status | Feature |
|---|---|
| [ ] | Binary slot-based save (`ZSavFile`, FNV-32 checksum) |
| [ ] | `GameSaveData` — typed key-value store (int/float/bool/string/blob) |
| [ ] | Atomic write — temp + rename, corruption-safe |
| [ ] | Platform save directories (Windows AppData, XDG, macOS Library) |
| [ ] | Steam Cloud sync |
| [ ] | Settings save — separate file, not slot-based |

---

## Platform and Distribution

| Status | Feature |
|---|---|
| [x] | Windows (GLFW + Vulkan) |
| [x] | macOS (GLFW + MoltenVK) |
| [x] | Linux (GLFW + Vulkan) |
| [ ] | Steam SDK integration — init, overlay, achievements, cloud saves |
| [ ] | CPack packaging — NSIS/ZIP (Windows), AppImage (Linux), DMG (macOS) |
| [ ] | CI/CD pipeline — GitHub Actions, 3 OS x 3 build types |
| [ ] | Crash handler — minidump (Windows), signal + backtrace (Linux/macOS) |
| [-] | Console platforms (long-term) |

## Panzerfaust — Project Launcher

Panzerfaust is the .NET/Avalonia project manager. Users open it before the editor.
It scaffolds project structure, writes projectConfig.json, and launches Obelisk.

| Status | Feature |
|---|---|
| [x] | Project creation — scaffolds directory structure + projectConfig.json |
| [x] | Project list with open / delete |
| [x] | Launches Obelisk with --projectConfigFile + --launchEditor flags |
| [x] | Cross-platform (.NET 8, Avalonia, Windows / macOS / Linux) |
| [ ] | Project templates — Blank, 2D, 3D, Multiplayer starter |
| [ ] | Engine version selector — pick which ZEngine version to open project with |
| [ ] | Recent projects list with last-opened timestamp |
| [ ] | Project import — point at existing projectConfig.json |
| [ ] | Plugin marketplace browser — browse / install plugins before opening editor |

---

## Editor (Tetragrama)

| Status | Feature |
|---|---|
| [x] | Scene hierarchy view |
| [x] | Asset browser |
| [x] | Transform gizmos |
| [x] | Shader editor with hot-reload |
| [ ] | Asset thumbnail generation |
| [ ] | In-editor physics visualization |
| [ ] | Component inspector with reflection (`component-reflection.md`) |
| [ ] | Entity selection — hierarchy click + viewport raycast (`editor-entity-selection.md`) |
| [ ] | Play / Pause / Stop — scene snapshot, system registration switching (`editor-play-mode.md`) |
| [ ] | Undo / Redo — command pattern, 8 command types, merge window (`editor-undo-redo.md`) |
| [-] | Profiler overlay |
| [-] | Console / debug command panel |
