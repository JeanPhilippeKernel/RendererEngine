# TLSF Allocator Integration

**Status:** Design — not yet implemented
**Scope:** Texture upload pipeline, per-worker decode slabs, future ECS component storage
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

The gap is exactly the texture upload path: buffers that range from 256 KB (small 2D texture) to 48 MB (4K equirectangular HDR cubemap), each with an independent lifetime tied to a GPU upload, running concurrently across multiple thread pool workers.

---

## 2. What TLSF Is

TLSF (Two-Level Segregated Fit, mattconte/tlsf) is a general-purpose dynamic allocator with two guarantees that matter here:

**O(1) alloc, realloc, and free.** The two-level bitmap locates the best-fit free list in a constant number of bit operations — no linear scan, no tree traversal.

**Bounded fragmentation.** Internal fragmentation is bounded to `< 2×` the requested size. External fragmentation cannot accumulate unboundedly because adjacent free blocks are always merged (`tlsf_free` coalesces neighbors in O(1)).

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
       │         TLSFSlab backing buffer (e.g. 32 MB)     │
       │                                                  │
       │  [tlsf header] [free block] [alloc A] [free] ... │
       │                                 ▲          ▲     │
       │                           tlsf_malloc  tlsf_free │
       └──────────────────────────────────────────────────┘
```

The arena is NOT touched for any individual texture allocation after init. The slab is a fixed reservation; TLSF operates entirely within it.

### 3.1 API

```cpp
// ZEngine/ZEngine/Core/Memory/TLSFSlab.h

struct TLSFSlab {
    void*  Backing = nullptr;
    tlsf_t Pool    = nullptr;

    void  Init(Core::Memory::ArenaAllocator* arena, size_t size);
    void* Alloc(size_t n);
    void* Realloc(void* ptr, size_t n);
    void  Free(void* ptr);
    void  Shutdown();

    size_t Overhead() const;  // tlsf internal metadata bytes
};
```

`Init` performs the single arena carve and calls `tlsf_create_with_pool`. `Shutdown` calls `tlsf_destroy` — the backing memory remains in the arena (arenas do not free individual allocations).

---

## 4. Thread Safety Design

The thread pool uses a fixed-size worker array (`ThreadPool::MAX_WORKERS = 16`). Each worker runs `WorkerRun(size_t idx)` for its lifetime. Tasks are dispatched round-robin starting at a shared cursor, so **the submitting thread does not know which worker will execute a given task**.

The clean solution is thread-local storage. Each worker sets a thread-local slab pointer at the start of `WorkerRun`:

```
 ThreadPool::WorkerRun(idx)
       │
       ├── t_worker_slab = &m_slabs[idx]   ← set thread_local at worker start
       │
       └── task loop:
             ├── task A → lambda calls GetWorkerSlab() → &m_slabs[idx]
             ├── task B → lambda calls GetWorkerSlab() → &m_slabs[idx]
             └── ...
```

No lock is ever needed — each slab is owned exclusively by one OS thread.

```
 Workers and their slabs:

 worker[0] ──owns──► slab[0]  32 MB  ─┐
 worker[1] ──owns──► slab[1]  32 MB   │  all backed from Device->Arena
 worker[2] ──owns──► slab[2]  32 MB   │  (4 × 32 MB = 128 MB reservation)
 worker[3] ──owns──► slab[3]  32 MB  ─┘
```

Worker slabs are sized to hold the maximum single-texture decode (a 4K equirectangular HDR = ~48 MB after cubemap conversion). 32 MB covers typical 2K textures; 64 MB covers the HDR worst case.

### 4.1 Fallback: inline execution

`ThreadPool::Submit` has a fallback path where a task runs inline on the submitting thread (all worker queues full). That thread has no `t_worker_slab` set. The fallback must call `malloc`/`free` directly or assert — inline GPU upload is already a degraded path.

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
     ├── float* raw = slab[N].Alloc(w*h*4*4)     ← TLSF, no lock
     ├── Bitmap out{w, h, 4, FLOAT, raw_allocator}
     │
     ├── Bitmap vertical_cross → slab[N] intermediate buffers
     ├── Bitmap cubemap        → slab[N] intermediate buffers
     │
     ├── uint8_t* pixels = slab[N].Alloc(bytes)  ← TLSF
     ├── memmove(pixels, cubemap.Buffer, bytes)
     │
     └── TextureDeferral {
             Pixels  = pixels               ← raw pointer
             ByteSize = bytes
             Slab    = &slab[N]             ← back-reference for free
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
             └── d.Slab->Free(d.Pixels)     ← O(1) TLSF free, block merges back
```

The intermediate bitmaps (`vertical_cross`, `cubemap`) are freed before the lambda exits — their TLSF blocks are reclaimed immediately, so by the time `pixels` is the only surviving allocation, the slab has plenty of headroom.

### 5.3 TextureDeferral struct change

```cpp
// Before
struct TextureDeferral {
    std::variant<unsigned char*, std::vector<uint8_t>> Buffer;
    bool IsLarge = false;
    ...
};

// After
struct TextureDeferral {
    unsigned char*              Pixels   = nullptr;
    size_t                      ByteSize = 0;
    bool                        IsLarge  = false;  // true = Slab owns Pixels, false = borrowed
    Core::Memory::TLSFSlab*     Slab     = nullptr; // null when IsLarge = false
    ...
};
```

---

## 6. Memory Layout Diagram

```
 Device->Arena
 ┌──────────────────────────────────────────────────────────────┐
 │  engine objects  │  slab[0] 32MB  │  slab[1] 32MB  │  ...   │
 └──────────────────┴────────────────┴────────────────┴────────┘
                           │
                    slab[0] internals:
                    ┌─────────────────────────────────────────┐
                    │ [tlsf hdr 3KB] │ [free 32MB - overhead] │
                    └─────────────────────────────────────────┘
                    after alloc(16MB) for texture A:
                    ┌─────────────────────────────────────────┐
                    │ [tlsf hdr] │ [tex A  16MB] │ [free 16MB]│
                    └─────────────────────────────────────────┘
                    after free(tex A) and alloc(256KB) for texture B:
                    ┌──────────────────────────────────────────────────┐
                    │ [tlsf hdr] │ [tex B 256KB] │ [free ~16MB merged] │
                    └──────────────────────────────────────────────────┘
```

The freed 16 MB block merges back with the trailing free region — the slab is effectively full-size again for the next texture.

---

## 7. Sizing

| Variable | Formula | Typical value |
|---|---|---|
| Max texture bytes | 4K × 4K × 4ch × 4B (float cubemap) | ~256 MB (equirect before conversion) → ~48 MB (cubemap faces) |
| Per-worker slab | `max_cubemap_bytes × 2` (one live + one intermediate) | 64 MB |
| Total reservation | `per_worker_slab × MAX_WORKERS` | 64 MB × 16 = 1 GB (maximum hardware concurrency) |
| Practical target | `per_worker_slab × typical_worker_count` | 64 MB × 4–8 = 256–512 MB |

In practice `hardware_concurrency() - 1` is the worker count (capped at 16). On a developer machine with 12 cores that is 11 workers. Slabs should only be created for actively-running workers, not pre-allocated for `MAX_WORKERS`.

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

 TLSF (single 64 MB slab per worker):
   - alloc/free O(1) regardless of size
   - freed block merges with neighbors in O(1)
   - fragmentation bounded < 2×
   - 64 MB per worker covers the full size range
   - zero system heap involvement after slab init   ← win
```

---

## 9. Future Scope

### 9.1 AssetManager long-lived containers

`AssetManager::NodeHierarchies`, `Meshes`, `Materials`, `Textures`, `UUIDToTextureHandle` are all `Array<T>` or `UnorderedHashMap<K,V>` backed by `AssetManager::Arena`. Every `grow()` abandons the old block in the arena — the dead blocks accumulate for the entire session.

A `TLSFSlab` behind a thin `ZResize`-compatible shim would give those containers real `realloc()` behavior: when the slab's `Realloc` grows a block, TLSF checks whether the physically adjacent block is free and extends in-place before copying. No arena waste on grow.

This requires the container types (`Array<T>`, `UnorderedHashMap<K,V>`) to accept a typed allocator parameter rather than a raw `ArenaAllocator*`. That is a separate refactor — tracked as a future task, not in scope here.

### 9.2 ECS component storage

When heterogeneous component types land (issue #642 and follow-on work), each component archetype has a different size. One `PoolAllocator` per component type would work but requires pre-sizing each pool at registration time. A `TLSFSlab` per archetype table avoids the fixed-size constraint and handles tables with mixed-size extension components cleanly.

### 9.3 Lambda captures in ThreadPoolHelper

`ThreadPoolHelper::Submit(T&& f)` currently does `new Fn(...)` and `delete fn` (one system heap alloc per submitted lambda). Routing those captures through the worker's slab would eliminate the last system-heap touch on the upload hot path.

---

## 10. What Does Not Change

- `ArenaAllocator` and `PoolAllocator` — no modifications. TLSF fills the gap, it does not replace the existing allocators for the cases they already handle well.
- `RenderPass::SetDynamicUniform` `std::vector<VkWriteDescriptorSet>` — `frame_count` is 2–3 elements. Stack arrays are the right fix there; TLSF adds unnecessary overhead.
- `Bitmap::Buffer` intermediate chain (`vertical_cross`, `cubemap`) — these are scoped to the lambda and freed before the lambda exits. They are candidates for follow-up but are not blocking.
- GPU allocator (`GpuAllocator.h` / VMA) — manages `VkDeviceMemory`; entirely separate from CPU-side TLSF.

---

## 11. Files Affected

| File | Change |
|---|---|
| `ZEngine/ZEngine/Core/Memory/TLSFSlab.h` | New — `TLSFSlab` wrapper |
| `ZEngine/ZEngine/Core/Memory/TLSFSlab.cpp` | New — Init, Alloc, Realloc, Free, Shutdown |
| `ZEngine/ZEngine/Helpers/ThreadPool.h` | Add `thread_local TLSFSlab*` and `GetWorkerSlab()`; set it in `WorkerRun` |
| `ZEngine/ZEngine/Rendering/RenderResourceManager.h` | Replace `TextureDeferral::Buffer` variant with `Pixels + Slab`; add `m_upload_slabs[]` |
| `ZEngine/ZEngine/Rendering/RenderResourceManager.cpp` | Init slabs; replace `std::vector<uint8_t>` decode buffers with slab allocs; `CompleteDeferrals` calls `Slab->Free` |
| `ZEngine/ZEngine/Rendering/Buffers/Bitmap.h` | Optional follow-up: expose allocator param so intermediate bitmaps use the worker slab |
