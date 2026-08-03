# Thumbnail Generation — Editor Asset Previews

**Priority:** P4 — Implement after render-resource-manager, VFS scanner, and import pipeline are live  
**Status:** Design — no blocking correctness issues; editor-only feature  
**Depends on:** `render-resource-manager.md`, `vfs-ticket3-scanner-memory-backend.md`, `import-pipeline.md`  
**Blocks:** Nothing critical — editor UX only

**Goal**: Provide 128×128 RGBA8 thumbnail images for every importable asset (textures,
meshes, materials) inside the editor's `ProjectViewUIComponent`, with async off-thread
generation, GPU-resident cache, LRU eviction, and persistent `.thumb` files — all
without RTTI, virtual dispatch in the hot path, exceptions, or `new`/`delete` in hot
paths.

---

## 1. `ThumbnailHandle`

A generational handle that identifies a single 128×128 RGBA8 GPU image slot, or one
of the well-known placeholder constants. All editor UI code holds `ThumbnailHandle`
values; nothing holds raw GPU image pointers.

```cpp
// ZEngine/Editor/Thumbnails/ThumbnailHandle.h
#pragma once
#include <cstdint>

namespace ZEngine::Editor {

    struct ThumbnailHandle {
        uint32_t Index      = 0;
        uint32_t Generation = 0;

        bool IsValid() const { return Generation != 0; }
        bool operator==(const ThumbnailHandle&) const = default;
    };

    constexpr ThumbnailHandle INVALID_THUMBNAIL              = {0, 0};
    constexpr ThumbnailHandle PLACEHOLDER_THUMBNAIL_MESH     = {1, 1};
    constexpr ThumbnailHandle PLACEHOLDER_THUMBNAIL_TEXTURE  = {2, 1};
    constexpr ThumbnailHandle PLACEHOLDER_THUMBNAIL_MATERIAL = {3, 1};

}  // namespace ZEngine::Editor
```

**Design notes**:

- `Index` is a stable slot index into `ThumbnailCache`'s internal dense GPU image table.
  Slot 0 is reserved as the invalid sentinel; slots 1–3 are reserved for the three
  placeholder images, which are loaded once at editor startup and never evicted.
- `Generation` starts at `1` for the first occupant of a recyclable slot and increments
  each time the slot is evicted and reused. Generation `0` is the sentinel for
  `INVALID_THUMBNAIL`. `IsValid()` is a single compare-to-zero.
- Placeholder constants have `Generation = 1` and indices in the reserved range `[1, 3]`.
  They survive every `Evict()` call because the eviction loop starts from index `4`.
- `operator==` is defaulted (C++20). Two handles are equal only when both `Index` and
  `Generation` match, so a stale handle that survived an evict cycle is not mistaken for
  the new occupant of the same slot.
- `ThumbnailHandle` is 8 bytes and fits in a register. It is safe to copy by value
  everywhere.

---

## 2. `ThumbnailCache`

Maps asset UUIDs to `ThumbnailHandle` values in memory, persists small PNG snapshots
(`.thumb` files) beside `.meta` files on disk, loads them at editor startup, and
enforces an LRU eviction cap against GPU memory.

```cpp
// ZEngine/Editor/Thumbnails/ThumbnailCache.h
#pragma once
#include <Editor/Thumbnails/ThumbnailHandle.h>
#include <uuid.h>
#include <VFS/IVFSContext.h>
#include <Core/Containers/UnorderedHashMap.h>

namespace ZEngine::Editor {

    class ThumbnailCache {
    public:
        // Returns the handle for uuid, or INVALID_THUMBNAIL if not present.
        ThumbnailHandle Lookup(const uuids::uuid& uuid) const;

        // Associates uuid with handle and updates its LRU timestamp.
        void            Store(const uuids::uuid& uuid, ThumbnailHandle handle);

        // Removes uuid from the in-memory map; does NOT delete the .thumb file.
        void            Invalidate(const uuids::uuid& uuid);

        // Walks the asset tree and loads every *.thumb file found beside a *.meta.
        // Called once on editor startup before the first frame.
        void            LoadFromDisk(VFS::IVFSContext& ctx);

        // Encodes rgba8_128x128 (128*128*4 bytes) as a small PNG and writes it
        // to <asset_path>.thumb via ctx. Also calls Store() for the handle.
        void            SaveToDisk(VFS::IVFSContext& ctx,
                                   const uuids::uuid& uuid,
                                   const uint8_t* rgba8_128x128);

        // Evicts the (count - max_count) least-recently-used GPU thumbnails when
        // the live count exceeds max_count. Placeholder slots are never evicted.
        // Evicted entries are removed from m_map; their .thumb files remain on disk.
        void            Evict(uint32_t max_count);

    private:
        struct Entry {
            ThumbnailHandle Handle;
            uint64_t        LastAccessFrame = 0;  // frame counter at last Lookup/Store
        };

        // UUID → Entry (handle + LRU timestamp)
        Core::Containers::UnorderedHashMap<uuids::uuid, Entry> m_map;

        // Monotonically increasing frame counter; incremented by ThumbnailGenerator::Tick().
        uint64_t m_frame = 0;

        // Slot generation table: m_generations[index] is the current generation for
        // that GPU image slot. Incremented each time the slot is recycled.
        Core::Containers::Array<uint32_t> m_generations;

        // Free list of evicted slot indices (>= 4, since 0-3 are reserved).
        Core::Containers::Array<uint32_t> m_free_slots;
    };

}  // namespace ZEngine::Editor
```

**`Lookup(uuid)`**:
1. Find `uuid` in `m_map`. On miss, return `INVALID_THUMBNAIL`.
2. On hit, update `entry.LastAccessFrame = m_frame`. Return `entry.Handle`.

**`Store(uuid, handle)`**:
1. Insert or overwrite `m_map[uuid] = {handle, m_frame}`.

**`Invalidate(uuid)`**:
1. Erase `uuid` from `m_map`. The `.thumb` file is left on disk; it will be
   regenerated by a subsequent `SaveToDisk` call after the asset is re-imported.

**`LoadFromDisk(ctx)`**:
1. Enumerate all files matching `*.thumb` via `VFSDirectoryCache`.
2. For each `.thumb` file: decode the PNG (stb_image or engine's image loader),
   upload the decoded 128×128 RGBA8 to a new GPU slot, call `Store(uuid, handle)`.
3. Placeholder images (mesh icon, texture icon, material icon) are uploaded first
   and assigned slots 1–3 before any disk thumbnails are processed.

**`SaveToDisk(ctx, uuid, rgba8_128x128)`**:
1. Encode 128×128 RGBA8 to PNG in memory (stb_image_write or equivalent).
   Target size ≤ 8 KB; use PNG compression level 6.
2. Derive the `.thumb` path from the asset's `.meta` path stored in `AssetRegistry`
   (same directory, same stem, extension `.thumb`).
3. Write the PNG bytes through `ctx` (VFS write).
4. Allocate a GPU slot: pop from `m_free_slots` if non-empty, else append a new slot
   and grow `m_generations`.
5. Upload the RGBA8 data to the GPU slot via `RenderResourceManager`.
6. Build a `ThumbnailHandle{slot_index, m_generations[slot_index]}` and call
   `Store(uuid, handle)`.

**`Evict(max_count)`**:
1. If `m_map.Size() <= max_count`, return immediately — nothing to evict.
2. Collect all entries with `Handle.Index >= 4` (skip reserved placeholders).
3. Sort by `LastAccessFrame` ascending (oldest first).
4. Evict `m_map.Size() - max_count` entries: release the GPU image slot via
   `RenderResourceManager`, push the slot index back onto `m_free_slots`,
   increment `m_generations[slot_index]`, erase from `m_map`.
5. `.thumb` files are not deleted; they become the fallback for the next `Lookup`
   + `LoadFromDisk` cycle.

**`.thumb` file convention**:

| Asset path              | Meta path                 | Thumb path                  |
|-------------------------|---------------------------|-----------------------------|
| `Assets/Wood.png`       | `Assets/Wood.png.meta`    | `Assets/Wood.png.thumb`     |
| `Assets/Cube.fbx`       | `Assets/Cube.fbx.meta`    | `Assets/Cube.fbx.thumb`     |
| `Assets/Rock.mat`       | `Assets/Rock.mat.meta`    | `Assets/Rock.mat.thumb`     |

The UUID embedded in the `.meta` file is used as the cache key. `LoadFromDisk`
reads the sibling `.meta` to extract the UUID before associating the thumb.

---

## 3. `ThumbnailRequest`

A lightweight value type that describes a single async thumbnail generation request.
No virtual dispatch; the callback is a `std::function` invoked exactly once on the
main thread by `ThumbnailGenerator::Tick()`.

```cpp
// ZEngine/Editor/Thumbnails/ThumbnailRequest.h
#pragma once
#include <Editor/Thumbnails/ThumbnailHandle.h>
#include <uuid.h>
#include <functional>

namespace ZEngine::Editor {

    enum class ThumbnailPriority : uint8_t {
        Prefetch = 0,  // low-priority background generation (not yet visible)
        Visible  = 1,  // high-priority: asset is visible in the project view now
    };

    using ThumbnailCallback = std::function<void(const uuids::uuid&, ThumbnailHandle)>;

    struct ThumbnailRequest {
        uuids::uuid        AssetUUID;
        ThumbnailPriority  Priority = ThumbnailPriority::Visible;
        ThumbnailCallback  Callback;   // fired on main thread; never nullptr
    };

}  // namespace ZEngine::Editor
```

**Design notes**:

- `Priority` controls queue ordering. `Visible` requests are drained first in
  `ThumbnailGenerator::Tick()`. `Prefetch` requests fill idle worker slots.
- `Callback` is always non-null at the time it is enqueued. `ThumbnailGenerator`
  asserts this. The callback receives `INVALID_THUMBNAIL` only on hard failure
  (asset file missing, decode error); it never receives a placeholder — callers
  consult `ThumbnailCache::Lookup()` for the placeholder path.
- `ThumbnailRequest` is moved into the internal queue (no copy of the `std::function`
  in the hot enqueue path). The callback's captured variables must be
  copy-safe (no raw pointers into stack frames).

---

## 4. `ThumbnailGenerator`

Orchestrates the full pipeline: deduplicates in-flight requests, dispatches work to
`ThreadPoolHelper`, drains completed results on the main thread via `Tick()`, uploads
GPU images, persists `.thumb` files, and fires callbacks.

```cpp
// ZEngine/Editor/Thumbnails/ThumbnailGenerator.h
#pragma once
#include <Editor/Thumbnails/ThumbnailRequest.h>
#include <Editor/Thumbnails/ThumbnailCache.h>
#include <Managers/AssetManager.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <atomic>
#include <mutex>

namespace ZEngine::Editor {

    class ThumbnailGenerator {
    public:
        explicit ThumbnailGenerator(ThumbnailCache* cache,
                                    AssetManager*   asset_manager);

        // Enqueues a request. If uuid already has a valid cache entry, fires the
        // callback immediately (synchronously) and returns without enqueuing.
        // If a request for uuid is already in-flight, the new callback is appended
        // to the existing in-flight entry's callback list (deduplication).
        void RequestThumbnail(ThumbnailRequest request);

        // Called every frame on the main thread.
        // - Promotes completed async results from m_result_queue to the GPU.
        // - Calls SaveToDisk for each completed result.
        // - Fires all pending callbacks for that UUID.
        // - Increments the internal frame counter used by LRU.
        // - Triggers Evict(512) if the cache exceeded the cap.
        void Tick();

    private:
        // Dispatched to ThreadPoolHelper. Returns raw 128×128 RGBA8 pixels.
        // The result is placed into m_result_queue (lock-protected).
        void GenerateTextureThumbnail (const uuids::uuid& uuid, AssetHandle handle);
        void GenerateMeshThumbnail    (const uuids::uuid& uuid, AssetHandle handle);
        void GenerateMaterialThumbnail(const uuids::uuid& uuid, AssetHandle handle);

        struct PendingEntry {
            // All callbacks waiting on this UUID (deduplicated in-flight requests)
            Core::Containers::Array<ThumbnailCallback> Callbacks;
            ThumbnailPriority                          Priority;
        };

        struct CompletedResult {
            uuids::uuid                          UUID;
            Core::Containers::Array<uint8_t>     RGBA8_128x128; // 128*128*4 bytes
            bool                                 Success = false;
        };

        ThumbnailCache* m_cache;
        AssetManager*   m_asset_manager;

        // UUIDs currently in-flight (main-thread only; no mutex needed here)
        Core::Containers::UnorderedHashMap<uuids::uuid, PendingEntry> m_pending;

        // Thread-safe result queue: worker threads push, Tick() drains.
        std::mutex                               m_result_mutex;
        Core::Containers::Array<CompletedResult> m_result_queue;

        // Sorted request queues (Visible first, then Prefetch).
        // Mutated only on main thread.
        Core::Containers::Array<uuids::uuid> m_visible_queue;
        Core::Containers::Array<uuids::uuid> m_prefetch_queue;

        // Count of currently active worker tasks.
        std::atomic<uint32_t> m_active_jobs{0};

        // Maximum concurrent thumbnail jobs.
        static constexpr uint32_t kMaxConcurrentJobs = 4;
    };

}  // namespace ZEngine::Editor
```

**`RequestThumbnail(request)`** (main thread):
1. Call `m_cache->Lookup(request.AssetUUID)`. If `IsValid()`, invoke `request.Callback`
   immediately and return — cache hit requires zero async work.
2. If `request.AssetUUID` is already in `m_pending`, append `request.Callback` to
   `m_pending[uuid].Callbacks` (deduplicate) and update `Priority` to the higher value.
   Return.
3. Otherwise, insert `{request.Callbacks = [request.Callback], request.Priority}` into
   `m_pending`. Push `uuid` onto `m_visible_queue` or `m_prefetch_queue` according to
   priority.

**`Tick()`** (main thread, called once per frame):
1. Increment the internal frame counter; forward it to `m_cache` so LRU timestamps
   are current.
2. Drain `m_result_queue` under `m_result_mutex`. For each `CompletedResult`:
   a. If `result.Success`: call `m_cache->SaveToDisk(ctx, uuid, rgba8_data)` which
      uploads to GPU and calls `Store`. Fire all callbacks in `m_pending[uuid].Callbacks`
      with the resulting handle.
   b. If `!result.Success`: fire all callbacks with `INVALID_THUMBNAIL`.
   c. Erase `uuid` from `m_pending`. Decrement `m_active_jobs`.
3. While `m_active_jobs < kMaxConcurrentJobs`:
   a. Dequeue from `m_visible_queue` first, then `m_prefetch_queue`.
   b. Look up the asset type in `AssetRegistry` for the UUID.
   c. Dispatch the appropriate `Generate*Thumbnail` method to `ThreadPoolHelper`.
   d. Increment `m_active_jobs`.
4. Call `m_cache->Evict(512)` if the GPU slot count exceeds the cap.

**`GenerateTextureThumbnail(uuid, handle)`** (worker thread):
1. Load the source image bytes through `IVFSContext` (path from `AssetRegistry`).
2. Decode with stb_image to whatever native dimensions.
3. Downsample to 128×128 using a two-pass box filter (separable, no allocations in
   the inner loop — write into a pre-allocated 128×128×4 byte buffer).
4. Push `CompletedResult{uuid, pixels, true}` into `m_result_queue`.

**`GenerateMeshThumbnail(uuid, handle)`** (worker thread → main-thread GPU work):
1. Load mesh data (vertex/index buffers) through `AssetManager`.
2. Submit an offscreen render command to `OffscreenRenderer` (Section 5).
   The offscreen render executes on the main thread's Vulkan queue during the next
   `Tick()` call. The worker thread enqueues the mesh data and waits on a
   `std::promise`; `Tick()` resolves it after completing the GPU read-back.
3. Push the read-back pixels as `CompletedResult`.

**`GenerateMaterialThumbnail`** follows the same pattern as mesh: enqueue a sphere
render command with the material's shader/texture bindings applied.

---

## 5. `OffscreenRenderer`

A minimal Vulkan offscreen renderer dedicated to thumbnail generation. It reuses the
engine's existing `VkDevice`, `VkQueue`, descriptor pool, and resources managed by
`RenderResourceManager`. No second Vulkan device is created.

### Resources (created once, never recreated)

| Resource                     | Specification                                              |
|------------------------------|------------------------------------------------------------|
| `VkImage` color attachment   | 128×128, `VK_FORMAT_R8G8B8A8_UNORM`, 1 mip, 1 layer       |
| `VkImage` depth attachment   | 128×128, `VK_FORMAT_D32_SFLOAT`, device-local              |
| `VkFramebuffer`              | Wraps both images; 128×128                                 |
| `VkRenderPass`               | Single subpass, color + depth, `LOAD_OP_CLEAR`             |
| Staging `VkBuffer`           | 128×128×4 bytes, `HOST_VISIBLE | HOST_COHERENT`            |
| `VkCommandBuffer`            | Pre-allocated from engine's transfer/graphics pool         |
| `VkFence`                    | Signaled after each render; re-used (not recreated)        |

All of the above are held inside `OffscreenRenderer` by value (handles, not pointers).
`RenderResourceManager::AllocateImage` and `AllocateBuffer` are used for allocation;
`new` is never called.

### Fixed directional light

Mesh and material renders use a single hardcoded directional light:

```cpp
struct ThumbnailLight {
    glm::vec3 Direction = glm::normalize(glm::vec3(0.5f, -1.0f, -0.5f));
    glm::vec3 Color     = {1.0f, 1.0f, 1.0f};
    float     Intensity = 1.2f;
};
```

There is no UI for adjusting this light; it is a compile-time constant.

### Camera

A fixed orbit camera is placed at a fixed elevation angle (30°) and azimuth (45°),
at a distance that fits the mesh's bounding sphere within the 128×128 viewport with
a 10% margin. `OffscreenRenderer` computes the view/projection matrices from the
asset's `AABB` (axis-aligned bounding box) provided by `AssetManager`.

For materials, the camera views a unit sphere centered at the origin with the same
fixed offset.

### Render flow (called from `ThumbnailGenerator::Tick()` on the main thread)

```
BeginRenderPass(framebuffer, clear_color={0.15, 0.15, 0.15, 1})
  BindPipeline(thumbnail_pipeline)
  BindDescriptorSets(mesh_data, material_data, light_ubo, transform_push_constant)
  DrawIndexed(mesh)
EndRenderPass()
CopyImageToBuffer(color_attachment → staging_buffer)
WaitFence()
memcpy(staging_buffer → result_pixels)
ResetFence()
```

`thumbnail_pipeline` is a separate `VkPipeline` compiled from simple PBR-lite shaders
(no deferred shading, no shadow maps). It is compiled once at `OffscreenRenderer`
initialization using `RenderResourceManager::CreateGraphicsPipeline`.

### Thread safety

`OffscreenRenderer::Render()` is called exclusively from the main thread inside
`ThumbnailGenerator::Tick()`. Worker threads enqueue work via `m_result_queue` and
never call Vulkan APIs directly.

---

## 6. Placeholder Strategy

While an async generation request is in-flight, `ProjectViewUIComponent` needs
something to display immediately. Placeholders are:

- `PLACEHOLDER_THUMBNAIL_MESH` — a small mesh-icon image (loaded from engine assets).
- `PLACEHOLDER_THUMBNAIL_TEXTURE` — a small texture-icon image.
- `PLACEHOLDER_THUMBNAIL_MATERIAL` — a small material-icon image.

These three GPU slots (indices 1–3) are populated during `ThumbnailCache::LoadFromDisk`
before any per-asset thumbnails are loaded, and they are never evicted.

**Lookup logic in `ProjectViewUIComponent`**:

```cpp
ThumbnailHandle handle = m_thumbnail_cache->Lookup(asset_uuid);
if (!handle.IsValid()) {
    // Not yet generated or evicted. Fire a request and show placeholder.
    AssetType type = m_asset_registry->GetType(asset_uuid);
    handle = PlaceholderFor(type);   // returns one of the three constants
    m_generator->RequestThumbnail({asset_uuid, ThumbnailPriority::Visible,
        [this, asset_uuid](const uuids::uuid& uuid, ThumbnailHandle h) {
            m_cached_handles[uuid] = h;   // update per-UUID handle map
        }
    });
}
DrawThumbnail(handle);
```

`PlaceholderFor` is a free function:

```cpp
inline ThumbnailHandle PlaceholderFor(AssetType type) {
    switch (type) {
        case AssetType::Mesh:     return PLACEHOLDER_THUMBNAIL_MESH;
        case AssetType::Texture:  return PLACEHOLDER_THUMBNAIL_TEXTURE;
        case AssetType::Material: return PLACEHOLDER_THUMBNAIL_MATERIAL;
        default:                  return PLACEHOLDER_THUMBNAIL_TEXTURE;
    }
}
```

The placeholder is shown for at most a few frames on a typical machine. Once the
callback fires, `m_cached_handles[uuid]` is updated and the next `DrawThumbnail` call
uses the real image.

---

## 7. `ProjectViewUIComponent` Integration

`ProjectViewUIComponent` is the ImGui-based panel that lists project assets in a grid.
Before this feature it displayed only file names and type icons. After this feature each
asset cell shows its 128×128 thumbnail image.

### Data additions to `ProjectViewUIComponent`

```cpp
// Added members:
ThumbnailGenerator*  m_generator;           // injected at construction
ThumbnailCache*      m_cache;               // injected at construction
AssetRegistry*       m_asset_registry;      // already present

// Per-asset handle cache (UUID → last-known ThumbnailHandle).
// Avoids a Lookup() call per visible cell per frame by caching the handle
// at callback time and only re-querying after invalidation.
Core::Containers::UnorderedHashMap<uuids::uuid, ThumbnailHandle> m_cached_handles;
```

### Per-cell render logic

```cpp
void ProjectViewUIComponent::DrawAssetCell(const AssetEntry& entry)
{
    ThumbnailHandle handle = INVALID_THUMBNAIL;

    // 1. Check local per-frame cache first (avoids hash lookup on every frame
    //    for already-resolved thumbnails).
    auto it = m_cached_handles.Find(entry.UUID);
    if (it != m_cached_handles.End()) {
        handle = it->second;
    }

    // 2. If not cached locally, ask ThumbnailCache.
    if (!handle.IsValid()) {
        handle = m_cache->Lookup(entry.UUID);
        if (handle.IsValid()) {
            m_cached_handles[entry.UUID] = handle;
        }
    }

    // 3. If still invalid, placeholder + enqueue async request.
    if (!handle.IsValid()) {
        handle = PlaceholderFor(m_asset_registry->GetType(entry.UUID));
        m_generator->RequestThumbnail({
            entry.UUID,
            ThumbnailPriority::Visible,
            [this, uuid = entry.UUID](const uuids::uuid&, ThumbnailHandle h) {
                m_cached_handles[uuid] = h.IsValid() ? h : PlaceholderFor(
                    m_asset_registry->GetType(uuid));
            }
        });
    }

    // 4. Draw. RenderResourceManager provides an ImTextureID for a given handle.
    ImTextureID tex_id = m_render_resource_manager->GetImTextureID(handle);
    ImGui::Image(tex_id, ImVec2(128.f, 128.f));
    ImGui::TextUnformatted(entry.Name.c_str());
}
```

### Staleness detection

`MetaFileIO` already tracks asset staleness via its `Stale` flag. When an asset is
re-imported, the asset pipeline calls:

```cpp
m_thumbnail_cache->Invalidate(uuid);
m_cached_handles.Erase(uuid);    // ProjectViewUIComponent clears its local entry
```

On the next `DrawAssetCell` call for that UUID, step 2 returns `INVALID_THUMBNAIL`,
step 3 shows the placeholder, and a new `RequestThumbnail` is queued — no special
invalidation path needed in the UI code.

---

## 8. Eviction Policy

### Cap

512 GPU thumbnails in memory at any time. This corresponds to:

```
512 × 128 × 128 × 4 bytes = 33,554,432 bytes ≈ 32 MB GPU memory
```

This is well within the budget allocated to the editor on integrated and discrete GPUs.

### LRU mechanism

Each `ThumbnailCache::Entry` stores `LastAccessFrame` (a `uint64_t` frame counter
incremented in `ThumbnailGenerator::Tick()`). Every `Lookup()` and `Store()` updates
`LastAccessFrame` to the current frame. `Evict(max_count)` is called at the end of
each `Tick()` when the live entry count exceeds `max_count`.

Eviction procedure:
1. Collect all `(uuid, entry)` pairs where `entry.Handle.Index >= 4`.
2. Partial-sort (or nth-element) by `LastAccessFrame` ascending to identify the
   `excess_count` oldest entries. Full sort is not required; `std::nth_element` is O(N).
3. For each evicted entry:
   - Call `RenderResourceManager::ReleaseImage(handle)` to free the GPU allocation.
   - Push `handle.Index` onto `m_free_slots`.
   - Increment `m_generations[handle.Index]` (stales any outstanding handles to this slot).
   - Erase `uuid` from `m_map`.

### Fallback after eviction

After eviction, `ThumbnailCache::Lookup(uuid)` returns `INVALID_THUMBNAIL` for the
evicted UUID. `ProjectViewUIComponent` detects this on the next frame (its local
`m_cached_handles` entry still exists but its handle's generation no longer matches the
slot's current generation — the handle is stale). The component clears the stale local
entry and re-enqueues a `RequestThumbnail`.

`ThumbnailGenerator::RequestThumbnail` checks the cache first. On a miss it then
checks whether a `.thumb` file exists (via `IVFSContext`) before launching a full
render job:

```
Lookup() → miss
  → try LoadFromDisk for this single UUID
      → .thumb found: decode + upload, no render job needed
      → .thumb not found: dispatch full Generate* job
```

This means re-display of recently evicted thumbnails is nearly instantaneous (just a
PNG decode + GPU upload), whereas newly encountered assets require full generation.

### Placeholder entries

Slots 0–3 are never inserted into the LRU logic. `Evict()` skips any entry whose
`Handle.Index < 4`. Placeholder images are permanent residents.

---

## 9. Unit Tests

File: `ZEngine/tests/Editor/Thumbnails/ThumbnailTest.cpp`

### Test 1 — Cache hit: UUID with stored handle returns it immediately, no async request

```cpp
TEST(ThumbnailCache, CacheHitReturnsImmediately)
{
    ThumbnailCache cache;
    uuids::uuid uuid = uuids::uuid_system_generator{}();

    ThumbnailHandle expected{4, 1};
    cache.Store(uuid, expected);

    bool callback_fired = false;
    MockThumbnailGenerator generator(&cache, nullptr);
    generator.RequestThumbnail({
        uuid, ThumbnailPriority::Visible,
        [&](const uuids::uuid&, ThumbnailHandle h) {
            callback_fired = true;
            EXPECT_EQ(h, expected);
        }
    });

    // Callback must fire synchronously (cache hit path) — no Tick() needed.
    EXPECT_TRUE(callback_fired);
    // No async job was dispatched.
    EXPECT_EQ(generator.ActiveJobCount(), 0u);
}
```

### Test 2 — Cache miss: RequestThumbnail fires async job; Tick completes it; callback fires with valid handle

```cpp
TEST(ThumbnailGenerator, CacheMissFiresAsyncJobAndCallback)
{
    ThumbnailCache cache;
    MockAssetManager asset_manager;
    ThumbnailGenerator generator(&cache, &asset_manager);

    uuids::uuid uuid = uuids::uuid_system_generator{}();
    asset_manager.RegisterFakeTexture(uuid, /* 256×256 checkerboard */ MakeCheckerboard());

    ThumbnailHandle received = INVALID_THUMBNAIL;
    generator.RequestThumbnail({
        uuid, ThumbnailPriority::Visible,
        [&](const uuids::uuid&, ThumbnailHandle h) { received = h; }
    });

    // Pump until callback fires (at most 100 ticks to avoid infinite loop).
    for (int i = 0; i < 100 && !received.IsValid(); ++i)
        generator.Tick();

    EXPECT_TRUE(received.IsValid());
    EXPECT_NE(received, INVALID_THUMBNAIL);
    // Cache now holds the result.
    EXPECT_EQ(cache.Lookup(uuid), received);
}
```

### Test 3 — Placeholder returned while request is pending

```cpp
TEST(ThumbnailGenerator, PlaceholderReturnedWhilePending)
{
    ThumbnailCache cache;
    MockAssetManager asset_manager;
    ThumbnailGenerator generator(&cache, &asset_manager);

    uuids::uuid uuid = uuids::uuid_system_generator{}();
    asset_manager.RegisterFakeMesh(uuid, MakeCubeMesh());

    // Before any request, Lookup returns invalid.
    EXPECT_FALSE(cache.Lookup(uuid).IsValid());

    // Enqueue but do not Tick().
    generator.RequestThumbnail({uuid, ThumbnailPriority::Visible, [](auto, auto){}});

    // UI code: Lookup still invalid (no Tick yet), so it shows placeholder.
    ThumbnailHandle h = cache.Lookup(uuid);
    if (!h.IsValid())
        h = PlaceholderFor(AssetType::Mesh);

    EXPECT_EQ(h, PLACEHOLDER_THUMBNAIL_MESH);
    EXPECT_TRUE(h.IsValid());
}
```

### Test 4 — Completion callback fires exactly once

```cpp
TEST(ThumbnailGenerator, CallbackFiresExactlyOnce)
{
    ThumbnailCache cache;
    MockAssetManager asset_manager;
    ThumbnailGenerator generator(&cache, &asset_manager);

    uuids::uuid uuid = uuids::uuid_system_generator{}();
    asset_manager.RegisterFakeTexture(uuid, MakeCheckerboard());

    int fire_count = 0;
    generator.RequestThumbnail({
        uuid, ThumbnailPriority::Visible,
        [&](const uuids::uuid&, ThumbnailHandle) { ++fire_count; }
    });

    // Tick many more times than needed.
    for (int i = 0; i < 200; ++i)
        generator.Tick();

    EXPECT_EQ(fire_count, 1);
}
```

### Test 5 — Stale invalidation: after MetaFileIO reports Stale, Lookup returns invalid, new request generated

```cpp
TEST(ThumbnailCache, StaleInvalidationTriggersNewRequest)
{
    ThumbnailCache cache;
    MockAssetManager asset_manager;
    ThumbnailGenerator generator(&cache, &asset_manager);

    uuids::uuid uuid = uuids::uuid_system_generator{}();
    asset_manager.RegisterFakeTexture(uuid, MakeCheckerboard());

    // Generate initial thumbnail.
    ThumbnailHandle first = INVALID_THUMBNAIL;
    generator.RequestThumbnail({uuid, ThumbnailPriority::Visible,
        [&](const uuids::uuid&, ThumbnailHandle h){ first = h; }});
    for (int i = 0; i < 100 && !first.IsValid(); ++i) generator.Tick();
    ASSERT_TRUE(first.IsValid());

    // Simulate MetaFileIO marking the asset stale → pipeline calls Invalidate.
    cache.Invalidate(uuid);
    EXPECT_FALSE(cache.Lookup(uuid).IsValid());

    // New request should be accepted (not deduplicated against old).
    ThumbnailHandle second = INVALID_THUMBNAIL;
    generator.RequestThumbnail({uuid, ThumbnailPriority::Visible,
        [&](const uuids::uuid&, ThumbnailHandle h){ second = h; }});
    for (int i = 0; i < 100 && !second.IsValid(); ++i) generator.Tick();

    EXPECT_TRUE(second.IsValid());
    // The new handle may reuse the same GPU slot (same Index) but must be current.
    EXPECT_TRUE(cache.Lookup(uuid).IsValid());
}
```

### Test 6 — LRU eviction: after exceeding 512, oldest entry evicted; re-request falls back to .thumb file

```cpp
TEST(ThumbnailCache, LRUEvictionFallsBackToThumbFile)
{
    MockVFSContext vfs;
    ThumbnailCache cache;

    // Fill cache with 512 entries (slots 4..515).
    Core::Containers::Array<uuids::uuid> uuids_list;
    for (uint32_t i = 0; i < 512; ++i) {
        uuids::uuid u = uuids::uuid_system_generator{}();
        uuids_list.PushBack(u);
        // Fake RGBA8 data (all zeros for simplicity).
        uint8_t pixels[128 * 128 * 4] = {};
        cache.SaveToDisk(vfs, u, pixels);   // stores + writes .thumb
    }

    // The oldest UUID (uuids_list[0]) should be evicted when we add entry 513.
    uuids::uuid overflow_uuid = uuids::uuid_system_generator{}();
    uint8_t pixels[128 * 128 * 4] = {};
    cache.SaveToDisk(vfs, overflow_uuid, pixels);
    cache.Evict(512);

    // Oldest entry is gone from GPU cache.
    EXPECT_FALSE(cache.Lookup(uuids_list[0]).IsValid());

    // Re-requesting it should load from .thumb (no full render job).
    MockThumbnailGenerator generator(&cache, nullptr);
    ThumbnailHandle reloaded = INVALID_THUMBNAIL;
    generator.RequestThumbnail({
        uuids_list[0], ThumbnailPriority::Visible,
        [&](const uuids::uuid&, ThumbnailHandle h){ reloaded = h; }
    });
    for (int i = 0; i < 100 && !reloaded.IsValid(); ++i) generator.Tick();

    EXPECT_TRUE(reloaded.IsValid());
    // Verify .thumb path was read, not a render job (checked via MockVFSContext).
    EXPECT_TRUE(vfs.WasThumbFileRead(uuids_list[0]));
    EXPECT_EQ(generator.RenderJobCount(), 0u);
}
```

---

## 10. Deliverables Checklist

- [ ] `ZEngine/Editor/Thumbnails/ThumbnailHandle.h` — generational handle struct,
      `INVALID_THUMBNAIL`, `PLACEHOLDER_THUMBNAIL_MESH/TEXTURE/MATERIAL` constants,
      `IsValid()`, `operator==`
- [ ] `ZEngine/Editor/Thumbnails/ThumbnailCache.h` + `.cpp` — `Lookup`, `Store`,
      `Invalidate`, `LoadFromDisk`, `SaveToDisk`, `Evict`; LRU entry struct with frame
      timestamp; slot generation table; free-list recycling; placeholder slots 1–3 never
      evicted
- [ ] `ZEngine/Editor/Thumbnails/ThumbnailRequest.h` — `ThumbnailPriority` enum,
      `ThumbnailCallback` alias, `ThumbnailRequest` struct
- [ ] `ZEngine/Editor/Thumbnails/ThumbnailGenerator.h` + `.cpp` — `RequestThumbnail`
      with cache-hit fast path and in-flight deduplication; `Tick()` draining result
      queue, uploading GPU images, firing callbacks; separate visible/prefetch queues;
      `kMaxConcurrentJobs = 4`; `GenerateTextureThumbnail`, `GenerateMeshThumbnail`,
      `GenerateMaterialThumbnail` dispatched via `ThreadPoolHelper`
- [ ] `ZEngine/Editor/Thumbnails/OffscreenRenderer.h` + `.cpp` — single 128×128
      `VkFramebuffer`; reuses engine `VkDevice`; fixed directional light constant;
      bounding-sphere-fitted camera; PNG read-back via staging buffer; `VkFence` reuse;
      no `new`/`delete`; `RenderResourceManager` for all Vulkan resource allocation
- [ ] `ZEngine/Editor/Thumbnails/PlaceholderFor.h` — `PlaceholderFor(AssetType)`
      free function returning the appropriate placeholder constant
- [ ] `ProjectViewUIComponent` updated — `DrawAssetCell` uses `ThumbnailHandle`; local
      `m_cached_handles` map; calls `RequestThumbnail(Visible)` on first miss; updates
      `m_cached_handles` in callback; calls `cache.Invalidate` + local map erase on
      MetaFileIO stale notification
- [ ] `ThumbnailCache::LoadFromDisk` called during editor startup (before first frame)
      in `EditorLayer::OnAttach` or equivalent initialization path
- [ ] Placeholder GPU images (mesh/texture/material icons) uploaded to slots 1–3 during
      `LoadFromDisk`; never inserted into LRU sort; never evicted
- [ ] `.thumb` file convention documented and followed: `<asset_path>.thumb` adjacent to
      `<asset_path>.meta`; PNG format; target size ≤ 8 KB; UUID read from sibling `.meta`
- [ ] Eviction cap set to `512` GPU thumbnails (`≈ 32 MB`); `Evict(512)` called at end
      of every `Tick()`; evicted entries fall back to `.thumb` file on next request
- [ ] `ThumbnailGenerator` never calls `new`/`delete` in hot paths; all buffers
      allocated via `Core::Containers::Array` or `RenderResourceManager`
- [ ] `tests/Editor/Thumbnails/ThumbnailTest.cpp` — all 6 tests pass under
      AddressSanitizer and UBSanitizer
- [ ] Manual smoke test: open a project with 600 assets; scroll through the entire
      `ProjectViewUIComponent`; verify all thumbnails load within 5 seconds; GPU memory
      usage stays below 40 MB for thumbnails; no ASAN errors; `.thumb` files appear on
      disk beside each `.meta` file
