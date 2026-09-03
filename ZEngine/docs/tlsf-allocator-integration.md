# TLSF Allocator Integration

**Status:** Phase 1 and Phase 2 complete (merged to develop). Phase 3 pending — blocked on ECS heterogeneous components.
**Scope:** Texture upload pipeline, per-worker decode slabs, long-lived asset container allocations
**Relates to:** `memory-allocator-audit.md`, `memory-budget.md`, `asset-manager.md`

---

## 1. The Allocator Landscape Today

The engine has two custom allocators, both backed by OS-reserved virtual memory or a parent arena.

### 1.1 ArenaAllocator — bump-pointer

```
 m_memory
   │
   ▼
 ┌──────────────────────────────────────────────────────┐
 │  A  │  B  │  C  │  D  │       uncommitted            │
 └──────────────────────────────────────────────────────┘
         ▲ m_current_offset
```

Allocation is a single pointer bump — O(1). There is no `Free`. When a container like `Array<T>` outgrows its block, `ZResize` bumps a new (larger) block forward and the old block becomes permanently dead weight in the arena.

Suited for: scratch arenas reset each frame, import pipelines cleared after use, any lifetime that matches "allocate a bunch, discard all at once."

### 1.2 PoolAllocator — fixed-size free list

```
 pool backing (carved from arena)
 ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
 │  A  │  B  │  C  │  D  │  E  │  F  │  G  │  H  │
 └──┬──┴──┬──┴──┬──┴─────┴─────┴──┬──┴──┬──┴──┬──┘
    └─────┘     └──────────────────┘     │free │free
         free list links                 └─────┘
```

All chunks are identical size. Alloc/free are O(1) free-list push/pop, zero fragmentation.

Suited for: many objects of the same type — entity slots, command buffer handles, `MeshInstance` entries.

### 1.3 The Gap

```
                   Short lifetime          Long lifetime
                 ┌─────────────────┬──────────────────────────┐
 Fixed size      │  ArenaTemp ✓    │  PoolAllocator ✓         │
                 ├─────────────────┼──────────────────────────┤
 Variable size   │  ArenaTemp ✓    │  ArenaAllocator           │
                 │                 │  leaks dead blocks on     │
                 │                 │  every grow()  ← GAP      │
                 └─────────────────┴──────────────────────────┘
```

The gap covers two concrete use cases:

- **Texture upload path:** decode buffers that range from 256 KB (small 2D texture) to 48 MB (4K HDR cubemap), each with an independent lifetime tied to a GPU upload, running concurrently across multiple thread pool workers.
- **Long-lived asset containers:** `Array<T>` and `UnorderedHashMap<K,V>` in AssetManager that grow over the session — each `grow()` abandons the old arena block permanently.

---

## 2. What TLSF Is

TLSF (Two-Level Segregated Fit, mattconte/tlsf) is a general-purpose dynamic allocator with two guarantees that matter here:

**O(1) alloc, realloc, and free.** The two-level bitmap locates the best-fit free list in a constant number of bit operations — no linear scan, no tree traversal.

**Bounded fragmentation.** Internal fragmentation is bounded to `< 2x` the requested size. External fragmentation cannot accumulate unboundedly because adjacent free blocks are always merged (`tlsf_free` coalesces neighbors in O(1)).

Internally, TLSF maintains a two-level segregated free list:

```
 First-level index (log2 of block size)
   │
   ▼
 ┌────┬────┬────┬────┬────┬────┐
 │ L0 │ L1 │ L2 │ L3 │ L4 │...│  first-level bitmap
 └──┬─┴──┬─┴────┴────┴────┴───┘
    │     │
    │     └─► second-level bitmap (subdivides each L into 16–32 sub-classes)
    │               │
    │               ▼
    │         ┌────┬────┬────┬─────┐
    └────────►│SL0 │SL1 │SL2 │ ... │  free block lists
              └────┴────┴────┴─────┘
```

`tlsf_malloc(n)` computes the first- and second-level indices for size `n` in two bit operations, pops from that free list in O(1), and splits the remainder back in O(1). `tlsf_free` merges left and right neighbors using physical block headers (boundary tags), then re-inserts into the correct list.

---

## 3. TLSFSlab — the Engine Wrapper

A `TLSFSlab` carves its backing memory from an existing `ArenaAllocator` at init time, then manages all subsequent alloc/free/realloc calls independently inside that fixed block.

```
 Device->Arena  (or any ArenaAllocator)
   │
   └── ZPushSize(arena, slab_size, 64)   ← one arena bump at engine init
             │
             ▼
       ┌──────────────────────────────────────────────────┐
       │         TLSFSlab backing buffer (e.g. 128 MB)    │
       │                                                  │
       │  [tlsf header] [free block] [alloc A] [free] ... │
       │                                 ▲          ▲     │
       │                           tlsf_malloc  tlsf_free │
       └──────────────────────────────────────────────────┘
```

The arena is NOT touched for any individual allocation after init. The slab is a fixed reservation; TLSF operates entirely within it.

### 3.1 API

```cpp
// ZEngine/ZEngine/Core/Memory/TLSFSlab.h

struct TLSFSlab
{
    void*  Backing = nullptr; // raw backing buffer carved from the parent arena
    tlsf_t Pool    = nullptr; // TLSF pool handle (opaque)

    // Carve `bytes` from `arena` and initialise the TLSF pool over it.
    void   Init(ArenaAllocator* arena, size_t bytes);

    // Allocate `n` bytes. Asserts on exhaustion. Never returns null.
    void*  Alloc(size_t n);

    // Reallocate `ptr` to `n` bytes. O(1) in-place if adjacent block is free.
    void*  Realloc(void* ptr, size_t n);

    // Return `ptr` to the slab. No-op on nullptr. O(1), coalesces neighbours.
    void   Free(void* ptr);

    // Destroy the TLSF pool. Does NOT free the backing memory — the parent arena owns it.
    void   Shutdown();

    // Bytes consumed by TLSF metadata (~3 KB per slab). Diagnostic use only.
    size_t Overhead() const;

private:
    // Atomic spinlock — protects concurrent Alloc/Free across threads.
    // Typical contention pattern: one worker calls Alloc, render thread calls
    // Free after GPU upload completes. Uncontended lock overhead is ~5 ns.
    mutable std::atomic_flag m_lock = ATOMIC_FLAG_INIT;
};
```

`Init` performs the single arena carve and calls `tlsf_create_with_pool`. `Shutdown` calls `tlsf_destroy` — the backing memory remains in the arena (arenas do not free individual allocations).

---

## 4. Thread Safety Design

The thread pool uses a fixed-size worker array (`ThreadPool::MAX_WORKERS = 16`). Each worker runs `WorkerRun(size_t idx)` for its lifetime. Tasks are dispatched round-robin; the submitting thread does not know which worker will execute a given task.

Each worker sets a thread-local slab pointer before starting its task loop via a `RegisterWorkerInit` callback. This callback is registered by `RenderResourceManager::InitUploadSlabs` at startup:

```
 RRM::InitUploadSlabs(worker_count)
   │
   ├── for i in [0, worker_count):
   │       m_upload_slabs[i].Init(Device->Arena, UPLOAD_SLAB_BYTES)
   │
   └── ThreadPool::RegisterWorkerInit(
           [](void* ctx, size_t idx) { SetWorkerSlab(&slabs[idx]); },
           m_upload_slabs)

 ThreadPool::WorkerRun(idx)
   │
   ├── init_fn(ctx, idx)          ← calls SetWorkerSlab(&slabs[idx])
   │                                 t_worker_slab = &slabs[idx]
   │
   └── task loop:
         ├── task A → GetWorkerSlab() → &slabs[idx]
         ├── task B → GetWorkerSlab() → &slabs[idx]
         └── ...
```

No lock is needed for per-worker allocation — each slab is owned exclusively by one OS thread. The spinlock in `TLSFSlab` exists only for cross-thread `Free`: the render thread calls `Slab->Free(Pixels)` after GPU upload completes, racing with a worker's next `Alloc`.

```
 Workers and their slabs:

 worker[0] ──owns──► slab[0]  128 MB  ─┐
 worker[1] ──owns──► slab[1]  128 MB   │  all backed from Device->Arena
 worker[2] ──owns──► slab[2]  128 MB   │  (N × 128 MB reservation)
 worker[3] ──owns──► slab[3]  128 MB  ─┘
 ...up to MAX_WORKERS (16)
```

### 4.1 Fallback: inline execution

`ThreadPool::Submit` has a fallback path where a task runs inline on the submitting thread (all worker queues full). That thread has no `t_worker_slab` set (`GetWorkerSlab()` returns nullptr). The `STBI_MALLOC` override falls back to `std::malloc`/`std::free` on that path — inline GPU upload is already a degraded path.

---

## 5. Upload Pipeline Before and After

### 5.1 Before

```
 Thread pool worker N
     │
     ├── std::vector<uint8_t> buffer        ← malloc(bytes) via system heap
     ├── std::vector<float>   output_buf    ← malloc(w*h*4*4) via system heap
     │
     ├── Bitmap vertical_cross              ← std::vector inside → malloc
     ├── Bitmap cubemap                     ← std::vector inside → malloc
     │
     └── TextureDeferral {
             Buffer = std::vector<uint8_t>  ← ownership moved into queue
             IsLarge = true
         }
             │
             ▼
     ThreadSafeQueue<TextureDeferral>
             │
             ▼
     Render thread: CompleteDeferrals()
             │
             ├── UploadTextureBuffer(...)
             └── ~TextureDeferral()         ← std::vector destructor → free()
```

Each upload touches the system heap 4–5 times. Multiple concurrent uploads contend on the global allocator lock.

### 5.2 After

```
 Thread pool worker N  (t_worker_slab = &slab[N])
     │
     ├── STBI_MALLOC → GetWorkerSlab()->Alloc(...)  ← TLSF, no lock (same thread)
     │
     ├── uint8_t* pixels = slab[N].Alloc(bytes)     ← TLSF
     ├── memmove(pixels, decoded_data, bytes)
     │
     └── TextureDeferral {
             Pixels   = pixels
             ByteSize = bytes
             Slab     = &slab[N]    ← non-null = slab owns Pixels
             TexHandle = handle
         }
             │
             ▼
     ThreadSafeQueue<TextureDeferral>
             │
             ▼
     Render thread: CompleteDeferrals()
             │
             ├── UploadTextureBuffer(...)
             └── if (d.Slab) d.Slab->Free(d.Pixels)  ← O(1) TLSF free, block merges back
                                                         spinlock protects cross-thread free
```

### 5.3 TextureDeferral struct

```cpp
// ZEngine/ZEngine/Rendering/RenderResourceManager.h

struct TextureDeferral
{
    uint8_t*                           Pixels    = nullptr; // pixel data (slab-owned when Slab != nullptr)
    size_t                             ByteSize  = 0;       // size of Pixels in bytes
    Core::Memory::TLSFSlab*            Slab      = nullptr; // owning slab; nullptr = borrowed pointer
    Rendering::Textures::TextureHandle TexHandle = {};
    uint8_t                            FrameIdx  = 0;
    uint8_t                            ThreadIdx = 0;
};
```

Ownership rule: if `Slab != nullptr`, `CompleteDeferrals` calls `Slab->Free(Pixels)` after the GPU upload. If `Slab == nullptr`, the pointer is borrowed and must not be freed here.

---

## 6. Memory Layout Diagram

```
 Device->Arena
 ┌──────────────────────────────────────────────────────────────┐
 │  engine objects  │  slab[0] 128MB  │  slab[1] 128MB  │  ... │
 └──────────────────┴─────────────────┴─────────────────┴──────┘
                           │
                    slab[0] internals:
                    ┌─────────────────────────────────────────┐
                    │ [tlsf hdr ~3KB] │ [free 128MB - overhead]│
                    └─────────────────────────────────────────┘
                    after alloc(16MB) for texture A:
                    ┌─────────────────────────────────────────┐
                    │ [tlsf hdr] │ [tex A  16MB] │ [free ~112MB]│
                    └─────────────────────────────────────────┘
                    after free(tex A) and alloc(256KB) for texture B:
                    ┌──────────────────────────────────────────────────┐
                    │ [tlsf hdr] │ [tex B 256KB] │ [free ~112MB merged] │
                    └──────────────────────────────────────────────────┘
```

The freed 16 MB block merges back with the trailing free region — the slab is effectively full-size again for the next texture.

---

## 7. Sizing

| Variable | Formula | Implemented value |
|---|---|---|
| Max texture bytes | 4K x 4K x 4ch x 4B (float cubemap) | ~48 MB (cubemap faces after equirect conversion) |
| Per-worker slab | `max_cubemap_bytes x 2.5` (headroom for intermediate bitmaps) | 128 MB (`UPLOAD_SLAB_BYTES`) |
| Total reservation | `per_worker_slab x worker_count` | 128 MB x N (N = `hardware_concurrency - 1`, capped at 16) |
| AssetManager container slab | sum of NodeHierarchies + Meshes + Materials + maps | 256 MB (`CONTAINER_SLAB_BYTES`) |

Slabs are only created for actively-running workers at `InitUploadSlabs` time — not pre-allocated for the full `MAX_WORKERS` static array.

---

## 8. Comparison: Arena vs Pool vs TLSF

```
 Texture upload size range:

  256 KB ──────────────────────────────────────────── 48 MB
    │        │           │             │               │
   icon   2K diffuse  2K normal    4K albedo       4K cubemap

 ArenaAllocator:
   - each upload = one forward bump
   - no free: upload A's memory sits dead until arena reset
   - arena reset = engine shutdown or scene reload
   - dead memory grows with every import session   ← leak

 PoolAllocator (chunk = 48 MB):
   - 8 in-flight slots = 384 MB reserved
   - a 256 KB icon wastes 47.75 MB per slot        ← waste

 PoolAllocator (multiple size classes):
   - 4 classes (256KB / 4MB / 16MB / 64MB)
   - correct class must be chosen at alloc time
   - blocks cannot merge across classes             ← fragmentation cliff

 TLSF (single 128 MB slab per worker):
   - alloc/free O(1) regardless of size
   - freed block merges with neighbors in O(1)
   - fragmentation bounded < 2x
   - 128 MB per worker covers the full size range with headroom
   - zero system heap involvement after slab init   ← win
```

---

## 9. Phase 2 — Typed Allocator for Containers (done)

`Array<T>` and `UnorderedHashMap<K,V>` both accept an optional `TLSFSlab*` at init time:

```cpp
// Array<T> — ZEngine/ZEngine/Core/Containers/Array.h
void init(Memory::TLSFSlab* slab, size_type initial_capacity);
void init(Memory::TLSFSlab* slab, size_type initial_capacity, size_type initial_size);
// reserve(): if (m_slab) m_data = (pointer) m_slab->Realloc(m_data, new_alloc_size);
// ~Array():  if (m_slab && m_data) m_slab->Free(m_data);

// UnorderedHashMap<K,V> — ZEngine/ZEngine/Core/Containers/UnorderedHashMap.h
void init(Memory::TLSFSlab* slab, size_type slot_capacity = 16);
// rehash(): if (m_slab) m_entries.init(m_slab, new_cap, new_cap);
```

When a slab is provided, `reserve()` calls `Realloc` which extends in-place when the physically adjacent TLSF block is free — zero arena waste on grow.

`AssetManager` creates a 256 MB `ContainerSlab` at `Initialize` and uses it for five long-lived containers:

```cpp
// ZEngine/ZEngine/Managers/AssetManager.h
static constexpr size_t CONTAINER_SLAB_BYTES = 256 * 1024 * 1024;
Core::Memory::TLSFSlab  ContainerSlab        = {};

// Containers migrated (ZEngine/ZEngine/Managers/AssetManager.cpp):
NodeHierarchies.init(&ContainerSlab, 5000);
Meshes.init(&ContainerSlab, 5000);
Materials.init(&ContainerSlab, 5000);
UUIDToTextureHandle.init(&ContainerSlab, 5000);
UUIDToMaterialSlot.init(&ContainerSlab, 5000);
```

---

## 10. Future Scope

### 10.1 ECS component storage (Phase 3 — blocked)

When heterogeneous component types land (physics, animation, scripting), each component archetype has a different size. One `PoolAllocator` per component type would work but requires pre-sizing each pool at registration time. A `TLSFSlab` per archetype table avoids the fixed-size constraint and handles tables with mixed-size extension components cleanly.

Blocked on: physics/animation/scripting systems not yet built.

### 10.2 Lambda captures in ThreadPoolHelper (done)

`ThreadPoolHelper::Submit(T&& f)` now carves `[TLSFSlab* header | Fn closure]` from a dedicated 512 KB closure slab (`ThreadPool::InitClosureSlab`, called from `RenderResourceManager::Initialize`) instead of `new`/`delete`. Falls back to `::operator new`/`delete` when the closure slab hasn't been initialized yet (early startup, tests). Eliminates the last system-heap touch on the upload hot path.

### 10.3 Bitmap intermediate buffers (done)

`Bitmap` now carries an optional `TLSFSlab*` on every constructor and on both static conversion methods (`EquirectangularMapToVerticalCross`, `VerticalCrossToCubemap`) — later reshaped into free functions under `BitmapConvert::EquirectToCross`/`CrossToCubemap` (see below). All intermediate bitmaps in the HDR cubemap pipeline are now slab-backed end to end, not just the initial STBI decode.

**Follow-up (PR #730):** `Bitmap`'s API was reshaped for clarity independent of the TLSF work — `BitmapType`/`BitmapFormat` became `enum class`, the ambiguous constructor overloads were replaced with named factory functions (`Bitmap::Create`, `Bitmap::FromData`), and the implementation moved out of the header into `Bitmap.cpp`. The TLSFSlab plumbing described above carried over unchanged through that reshape.

---

## 11. What Does Not Change

- `ArenaAllocator` and `PoolAllocator` — no modifications. TLSF fills the gap; it does not replace the existing allocators for the cases they already handle well.
- `RenderPass::SetDynamicUniform` `VkWriteDescriptorSet` arrays — 2–3 elements. Stack arrays are the right fix; TLSF adds unnecessary overhead.
- GPU allocator (`GpuAllocator.h` / VMA) — manages `VkDeviceMemory`; entirely separate from CPU-side TLSF.

---

## 12. Files Affected

| File | Change | Status |
|---|---|---|
| `ZEngine/ZEngine/Core/Memory/TLSFSlab.h` | New — `TLSFSlab` wrapper with spinlock | Done |
| `ZEngine/ZEngine/Core/Memory/TLSFSlab.cpp` | New — Init, Alloc, Realloc, Free, Shutdown | Done |
| `ZEngine/ZEngine/Helpers/ThreadPool.h` | `thread_local TLSFSlab*`, `SetWorkerSlab`, `GetWorkerSlab`, `RegisterWorkerInit` | Done |
| `ZEngine/ZEngine/Rendering/RenderResourceManager.h` | `TextureDeferral` flat struct; `m_upload_slabs[MAX_WORKERS]`; `InitUploadSlabs` | Done |
| `ZEngine/ZEngine/Rendering/RenderResourceManager.cpp` | `STBI_MALLOC/REALLOC/FREE` override; slab init; `CompleteDeferrals` calls `Slab->Free` | Done |
| `ZEngine/ZEngine/Core/Containers/Array.h` | `init(TLSFSlab*, capacity)` overloads; slab-aware `reserve` and destructor | Done |
| `ZEngine/ZEngine/Core/Containers/UnorderedHashMap.h` | `init(TLSFSlab*, capacity)` overload; slab-aware `rehash` | Done |
| `ZEngine/ZEngine/Managers/AssetManager.h` | `ContainerSlab` field + `CONTAINER_SLAB_BYTES` | Done |
| `ZEngine/ZEngine/Managers/AssetManager.cpp` | `ContainerSlab.Init`; 5 container migrations; `ContainerSlab.Shutdown` | Done |
| `ZEngine/ZEngine/Rendering/Buffers/Bitmap.h` / `.cpp` | Slab-aware `Create`/`FromData`; `BitmapConvert::EquirectToCross`/`CrossToCubemap` forward the slab | Done |
| `ZEngine/ZEngine/Helpers/ThreadPool.h` | `m_closure_slab`; `InitClosureSlab`/`GetClosureSlab`; `Submit<T>` carves `[TLSFSlab*\|Fn]` from it | Done |
