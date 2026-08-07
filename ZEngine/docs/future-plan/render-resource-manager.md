# Render Resource Manager — GPU Lifetime & Hot-Reload

**Priority:** P3 — Implement alongside import-pipeline.md  
**Status:** Design  
**Depends on:** `import-pipeline.md`, `vfs-ticket6-asset-registry.md`, `gpu-allocator-rearchitecture.md`  
**Blocks:** `animation-system.md` (SkinningUploadSystem), `shader-asset-pipeline.md`

**Goal**: Implement `ZEngine::Rendering::RenderResourceManager` (RRM), a single authority
over every GPU resource (buffers, images, samplers, pipelines) allocated through VMA.
The RRM accepts data from the asset pipeline, uploads it to the GPU via a staging path,
supports zero-downtime hot-reload via deferred handle swaps, and frees GPU memory safely
after in-flight frames have drained — all without exceptions, without `new`/`delete` in
the hot path, and without the asset layer knowing anything about Vulkan.

---

## 1. Responsibility Boundary

Two subsystems share ownership of asset state, and neither reaches into the other's domain.

### AssetRegistry

The `AssetRegistry` owns the **import state** of every asset: the raw bytes on disk, the
deserialized CPU-side representation (`MeshAsset`, `TextureAsset`, etc.), the UUID-to-path
mapping, and the import timestamp. It fires callbacks when an asset transitions:

- **`OnAssetReady(uuid, AssetHandle)`** — the CPU-side data is valid and ready to consume.
- **`OnAssetStale(uuid, AssetHandle)`** — a newer version has been imported; the old
  CPU-side data is about to be replaced.

The `AssetRegistry` does **not** allocate `VkBuffer`, `VkImage`, or `VmaAllocation`. It
has no reference to a `VkDevice` and no knowledge of frame indices or deletion queues.

### RenderResourceManager

The RRM owns the **GPU lifetime** of every uploaded resource: the `VkBuffer`/`VkImage`
handles, the `VmaAllocation` backing them, and the deferred-deletion schedule. It reacts
to `AssetRegistry` callbacks by uploading new GPU resources or scheduling swaps for
hot-reload.

The RRM does **not** touch raw asset bytes after the upload is complete. It does not hold
a reference to the `AssetRegistry`'s internal import tables. It does not know whether an
`AssetHandle` came from disk, a procedural generator, or a network stream.

### Boundary diagram

```
┌─────────────────────────────────┐        ┌──────────────────────────────────────┐
│         AssetRegistry           │        │       RenderResourceManager           │
│                                 │        │                                       │
│  owns:  UUID → AssetHandle      │        │  owns: BufferHandle → GPUBuffer      │
│         raw bytes / CPU data    │        │         ImageHandle  → GPUImage      │
│         import timestamps       │        │         staging scratch memory        │
│                                 │        │         deferred deletion queues      │
│  fires: OnAssetReady  ──────────┼───────►│  Upload*(AssetHandle)                │
│         OnAssetStale  ──────────┼───────►│  ScheduleSwap(old_handle, new_asset) │
└─────────────────────────────────┘        └──────────────────────────────────────┘
```

The coupling flows in one direction: the RRM depends on `AssetHandle` (an opaque
index into the registry) and on the callback contract. The registry knows nothing about
GPU handles.

---

## 2. GPU Handle Types

Handles follow the same generational-index pattern used by `ECS::EntityID`. The tag
parameter makes handles from different resource pools incompatible at compile time:
a `BufferHandle` cannot be accidentally passed where an `ImageHandle` is expected.

```cpp
// ZEngine/Rendering/RenderHandle.h
#pragma once
#include <cstdint>

namespace ZEngine::Rendering {

    template<typename Tag>
    struct RenderHandle {
        uint32_t Index      = 0;
        uint32_t Generation = 0;

        bool IsValid() const { return Generation != 0; }
        bool operator==(const RenderHandle&) const = default;
    };

    struct BufferTag   {};
    struct ImageTag    {};
    struct SamplerTag  {};
    struct PipelineTag {};

    using BufferHandle   = RenderHandle<BufferTag>;
    using ImageHandle    = RenderHandle<ImageTag>;
    using SamplerHandle  = RenderHandle<SamplerTag>;
    using PipelineHandle = RenderHandle<PipelineTag>;

}  // namespace ZEngine::Rendering
```

**Design notes**:

- `Index` is a stable slot index into the relevant resource pool (e.g.
  `m_buffer_pool`). Slots are reused across resource lifetimes.
- `Generation` starts at `1` for the first occupant of each slot. Generation `0` is
  the sentinel for an invalid handle. `IsValid()` is therefore a single compare-to-zero.
- `operator==` is defaulted (C++20). Two handles are equal only when both `Index`
  and `Generation` match — a recycled slot produces a new generation, making the old
  handle stale automatically.
- The `Tag` parameter is empty and never instantiated; it exists only to produce
  distinct template specializations. No runtime cost.
- All four handle types are trivially copyable and fit in 8 bytes, so they are passed
  by value everywhere.

---

## 3. GPUBuffer and GPUImage

```cpp
// ZEngine/Rendering/GPUResource.h
#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace ZEngine::Rendering {

    struct GPUBuffer {
        VkBuffer           Buffer     = VK_NULL_HANDLE;
        VmaAllocation      Allocation = VK_NULL_HANDLE;
        uint64_t           Size       = 0;
        VkBufferUsageFlags Usage      = 0;
    };

    struct GPUImage {
        VkImage       Image      = VK_NULL_HANDLE;
        VkImageView   View       = VK_NULL_HANDLE;
        VmaAllocation Allocation = VK_NULL_HANDLE;
        VkFormat      Format     = VK_FORMAT_UNDEFINED;
        VkExtent3D    Extent     = {};
        uint32_t      MipLevels  = 1;
    };

}  // namespace ZEngine::Rendering
```

**Design notes**:

- Both structs are plain aggregates — no constructor, no destructor, no virtual
  functions. The RRM explicitly calls `vmaDestroyBuffer` / `vmaDestroyImage` /
  `vkDestroyImageView` when it decides a resource is no longer needed. No RAII
  wrapper in the hot path.
- `GPUBuffer::Usage` is stored so the RRM can assert compatible usage at bind time
  (e.g. a staging buffer must not be used as a vertex buffer).
- `GPUImage` carries both the `VkImage` and its default `VkImageView` (covering all
  mip levels, all array layers, identity component mapping). Specialized views (e.g.
  single-mip for render-target write) are created externally on demand and are not
  owned by the RRM.
- `MipLevels` is stored so the barrier code can issue a single `VkImageMemoryBarrier`
  covering all mips in one call.
- Both structs are zero-initialised to `VK_NULL_HANDLE` / `VK_FORMAT_UNDEFINED` by
  aggregate initialisation, which gives safe sentinel values for free.

---

## 4. RenderResourceManager

```cpp
// ZEngine/Rendering/RenderResourceManager.h
#pragma once
#include <Rendering/GPUResource.h>
#include <Rendering/RenderHandle.h>
#include <Managers/AssetManager.h>
#include <VFS/VFSResult.h>

namespace ZEngine::Rendering {

    // THREAD SAFETY:
    // - FlushPendingUploads(): render thread only (called from BeginFrame)
    // - OnAssetReady() / ScheduleSwap(): may be called from asset loading thread
    //   protected by m_pending_mutex
    // - EnqueueDeletion(): render thread only (called after GPU work completes)
    // - GetBuffer() / GetImage() / GetPipeline(): render thread read-only;
    //   asset thread writes via ScheduleSwap() under m_pending_mutex
    // - Singleton Get(): valid from any thread after Initialize(); no lock needed
    //   (pointer is set once at startup, read-only thereafter)
    class RenderResourceManager {
    public:
        static RenderResourceManager& Get();

        // Upload mesh / texture from AssetHandle → GPU; returns handle.
        VFS::VFSResult<BufferHandle> UploadMesh(AssetHandle asset_handle);
        VFS::VFSResult<ImageHandle>  UploadTexture(AssetHandle asset_handle);

        // Hot-reload: upload new version, swap after in-flight frames drain.
        void ScheduleSwap(BufferHandle old_handle, AssetHandle new_asset);
        void ScheduleSwap(ImageHandle  old_handle, AssetHandle new_asset);

        // Release (deferred by FRAMES_IN_FLIGHT frames).
        void Release(BufferHandle handle);
        void Release(ImageHandle  handle);

        // Called once per frame from the render thread.
        void BeginFrame(uint32_t frame_index);
        void EndFrame(uint32_t frame_index);

        // Returns a read-only pointer to the GPU buffer at this handle.
        // CONTRACT: This pointer is valid only for the current frame.
        //           Do NOT store this pointer across BeginFrame() calls.
        //           The pool slot may be recycled or swapped in a future frame.
        //           Call GetBuffer() again each frame to obtain a fresh pointer.
        [[nodiscard]] const GPUBuffer* GetBuffer(BufferHandle handle) const;
        // Mutable version: render thread only, for internal use by upload/swap paths.
        [[nodiscard]] GPUBuffer*       GetBufferMutable(BufferHandle handle);
        // Returns a read-only view. Do NOT store across BeginFrame() calls.
        // To replace or resize: use ScheduleSwap or UploadTexture.
        [[nodiscard]] const GPUImage* GetImage(ImageHandle handle) const;
        // Mutable version: render thread only, for internal upload/swap paths.
        [[nodiscard]] GPUImage*       GetImageMutable(ImageHandle handle);

        // Enqueue a Vulkan object for safe deferred deletion.
        // The object is destroyed after N frames (default 3 = max frames in flight)
        // to ensure it is not in use by any in-flight GPU command buffer.
        // Called from: shader hot-reload (VkShaderModule), pipeline rebuild (VkPipeline),
        // buffer resize (VkBuffer/VmaAllocation), texture eviction (VkImage).
        void EnqueueDeletion(VkShaderModule module);
        void EnqueueDeletion(VkPipeline pipeline);
        void EnqueueDeletion(VkBuffer buffer, VmaAllocation allocation);
        void EnqueueDeletion(VkImage image, VkImageView view, VmaAllocation allocation);

    private:
        // IMPORTANT: FRAMES_IN_FLIGHT = 3 (triple-buffered swapchain, matching
        // SwapchainPtr->BufferredFrameCount in VulkanDevice).
        //
        // Slot recycling delay must be FRAMES_IN_FLIGHT frames, not FRAMES_IN_FLIGHT - 1.
        //
        // Reasoning:
        // - Frame N submits GPU work referencing resource R; signal timeline value N.
        // - Frames N+1 and N+2 also run concurrently (pipeline depth = 3).
        // - Frame N+3 begins. GPU work for frame N is guaranteed retired because
        //   TickMemory queries vkGetSemaphoreCounterValue and only drains entries
        //   whose TimelineValue <= completed. The DeferredFreeQueue in GpuAllocator
        //   enforces the same timeline gate, so the RRM delegates safe-frame
        //   computation to the timeline value stored at enqueue time.
        //
        // The m_deletion_queues ring therefore needs FRAMES_IN_FLIGHT slots.
        static constexpr uint32_t FRAMES_IN_FLIGHT = 3;

        // ... HandleManager-backed pools for GPUBuffer, GPUImage ...
    };

}  // namespace ZEngine::Rendering
```

**Design notes**:

- `VFS::VFSResult<T>` (same result type used throughout the VFS layer) carries either
  a valid `T` or a `VFSError` code. No exceptions. Callers check `result.IsOk()`
  before using the handle.
- `Get()` returns the process-singleton instance. The RRM is initialised once from the
  render thread after the `VkDevice` and `VmaAllocator` are ready, and torn down before
  the device is destroyed. It must not be called from asset-loading threads without the
  explicit upload queue wiring described in Section 5.
- `BeginFrame` / `EndFrame` bracket each frame on the render thread. `BeginFrame`
  drains the deletion queue for frames that are now safe to free (see Section 7).
  `EndFrame` advances the frame counter and flushes any pending swap entries (see
  Section 6).
- `GetBuffer` / `GetImage` return raw pointers into pool storage. Callers must not
  store these pointers across `BeginFrame` calls — a deferred deletion may invalidate
  the slot. Record the handle and call `Get*` again each frame.

---

## 5. Upload Path

The upload path follows the standard Vulkan transfer idiom: allocate a CPU-visible
staging buffer, map it, copy asset bytes in, record a `vkCmdCopyBuffer*` command, issue
a pipeline barrier to transfer ownership and layout, submit on the transfer queue, and
signal a fence. The final GPU resource is `DEVICE_LOCAL` (not CPU-visible).

### Step-by-step

**1. Acquire staging memory from the StagingRing**

After `gpu-allocator-rearchitecture.md` lands, the RRM does not call `vmaCreateBuffer`
for staging. All uploads go through `GpuAllocator::Ring` (the 64 MB persistent ring
inside `VulkanDevice::GpuMem`). For assets larger than the ring can serve in one chunk,
a one-shot `GpuMemoryDomain::HostStaging` allocation is used as fallback.

```cpp
uint32_t ring_offset = 0;
void* ring_ptr = Device->GpuMem.Ring.Allocate(
    (uint32_t)asset_byte_size, 4, &ring_offset);

if (!ring_ptr) {
    // Oversized asset (> ring capacity): fall back to a one-shot HostStaging buffer.
    BufferView one_shot = Device->GpuMem.AllocateBuffer(
        asset_byte_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        GpuMemoryDomain::HostStaging,
        "RRM::StagingFallback");
    // ... use one_shot.Handle as transfer source, then DeferFree after submit ...
}
```

**2. Copy into staging memory**

```cpp
std::memcpy(ring_ptr, asset_bytes, asset_byte_size);
// Flush only if the HostStaging pool is not HOST_COHERENT.
// GpuAllocator caches this flag at init time.
if (!Device->GpuMem.StagingIsCoherent()) {
    vmaFlushAllocation(Device->GpuMem.Allocator,
                       Device->GpuMem.Ring.Allocation,
                       ring_offset, asset_byte_size);
}
```

**3. Allocate device-local destination buffer**

```cpp
// Route through GpuAllocator — no direct vmaCreateBuffer call.
BufferView dst = Device->GpuMem.AllocateBuffer(
    asset_byte_size,
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
        | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    GpuMemoryDomain::DeviceGeometry,
    debug_name);

GPUBuffer gpu_buf{};
gpu_buf.Buffer     = dst.Handle;
gpu_buf.Allocation = dst.Allocation;
gpu_buf.Size       = asset_byte_size;
gpu_buf.Usage      = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
```

**4. Record copy command**

```cpp
VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
vkBeginCommandBuffer(m_transfer_cmd, &begin);

VkBufferCopy region{};
region.srcOffset = 0;
region.dstOffset = 0;
region.size      = asset_byte_size;
vkCmdCopyBuffer(m_transfer_cmd, staging.Buffer, dst.Buffer, 1, &region);
```

**5. Pipeline barrier — release from transfer queue**

When the transfer and graphics queues belong to different families, ownership must be
explicitly transferred. First, the transfer queue *releases* the resource:

```cpp
VkBufferMemoryBarrier release_barrier{};
release_barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
release_barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
release_barrier.dstAccessMask       = 0;   // ignored in release
release_barrier.srcQueueFamilyIndex = m_transfer_family;
release_barrier.dstQueueFamilyIndex = m_graphics_family;
release_barrier.buffer              = dst.Buffer;
release_barrier.offset              = 0;
release_barrier.size                = VK_WHOLE_SIZE;

vkCmdPipelineBarrier(m_transfer_cmd,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    0, 0, nullptr, 1, &release_barrier, 0, nullptr);

vkEndCommandBuffer(m_transfer_cmd);

VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
submit.commandBufferCount = 1;
submit.pCommandBuffers    = &m_transfer_cmd;
vkQueueSubmit(m_transfer_queue, 1, &submit, m_upload_fence);
```

**6. Pipeline barrier — acquire on graphics queue**

After the fence signals (polled at `BeginFrame` or via a semaphore chain), the graphics
queue *acquires* ownership:

```cpp
VkBufferMemoryBarrier acquire_barrier{};
acquire_barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
acquire_barrier.srcAccessMask       = 0;   // ignored in acquire
acquire_barrier.dstAccessMask       = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
                                    | VK_ACCESS_INDEX_READ_BIT;
acquire_barrier.srcQueueFamilyIndex = m_transfer_family;
acquire_barrier.dstQueueFamilyIndex = m_graphics_family;
acquire_barrier.buffer              = dst.Buffer;
acquire_barrier.offset              = 0;
acquire_barrier.size                = VK_WHOLE_SIZE;

vkCmdPipelineBarrier(m_graphics_cmd,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
    0, 0, nullptr, 1, &acquire_barrier, 0, nullptr);
```

**7. Texture-specific path — vkCmdCopyBufferToImage + layout transitions**

For `GPUImage` uploads, the staging copy uses `vkCmdCopyBufferToImage`, and an image
memory barrier is required to transition the layout from `VK_IMAGE_LAYOUT_UNDEFINED`
to `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL` before the copy, and from
`VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL` to `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`
after. The `subresourceRange` must cover all `MipLevels`:

```cpp
VkImageMemoryBarrier pre_copy_barrier{};
pre_copy_barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
pre_copy_barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
pre_copy_barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
pre_copy_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
pre_copy_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
pre_copy_barrier.image               = dst_image.Image;
pre_copy_barrier.subresourceRange    = {
    VK_IMAGE_ASPECT_COLOR_BIT, 0, dst_image.MipLevels, 0, 1
};
pre_copy_barrier.srcAccessMask = 0;
pre_copy_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

vkCmdPipelineBarrier(m_transfer_cmd,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    0, 0, nullptr, 0, nullptr, 1, &pre_copy_barrier);

// ... vkCmdCopyBufferToImage for each mip level ...

VkImageMemoryBarrier post_copy_barrier = pre_copy_barrier;
post_copy_barrier.oldLayout    = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
post_copy_barrier.newLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
post_copy_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
post_copy_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

vkCmdPipelineBarrier(m_transfer_cmd,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    0, 0, nullptr, 0, nullptr, 1, &post_copy_barrier);
```

**8. Staging memory retirement**

The ring chunk is retired via the timeline semaphore — no deletion queue entry needed:

```cpp
// signal_value is the timeline value that will be signalled when the
// vkQueueSubmit for this transfer completes.
uint64_t signal_value = Device->SwapchainPtr->RenderTimelineNextValue;
Device->GpuMem.Ring.Submit(ring_offset, (uint32_t)asset_byte_size, signal_value);
// GpuAllocator::Ring.Drain(completed) inside TickMemory reclaims the chunk
// once the GPU signals that timeline value.
```

For one-shot fallback buffers, enqueue via `Device->DeferFree`:

```cpp
Device->DeferFree({
    DeferredFreeEntry::Kind::Buffer,
    .Buffer = one_shot  // the BufferView returned by AllocateBuffer
    // TimelineValue is set by DeferFree from RenderTimelineNextValue
});
```

---

## 6. Hot-Reload Swap

Hot-reload keeps the old GPU resource bound and visible to the renderer while a new
version is being uploaded. Only when the upload fence has signalled and all in-flight
frames that could reference the old resource have retired is the pointer swapped and
the old resource queued for deletion.

### SwapEntry

```cpp
// ZEngine/Rendering/RenderResourceManager.h (private)
enum class ResourceKind : uint8_t { Buffer, Image };

struct SwapEntry {
    ResourceKind Kind;

    // Old handle currently bound by the renderer.
    union {
        BufferHandle OldBuffer;
        ImageHandle  OldImage;
    };

    // New GPU resource produced by the upload path (not yet visible to renderers).
    union {
        GPUBuffer NewBuffer;
        GPUImage  NewImage;
    };

    // The frame index at which the swap becomes safe:
    // swap_safe_frame = frame_at_upload_complete + FRAMES_IN_FLIGHT
    uint32_t SwapSafeFrame = 0;
};
```

### Swap lifecycle

**Phase 1 — Trigger** (`ScheduleSwap` called, e.g. from `OnAssetStale` callback):

1. Upload the new asset version to a fresh GPU resource (Section 5). Do not assign it
   to any handle yet.
2. Create a `SwapEntry` recording the old handle, the new resource, and
   `SwapSafeFrame = m_current_frame + FRAMES_IN_FLIGHT`.
3. Append to `m_pending_swaps`.

During this phase the old handle remains fully valid. `GetBuffer(old_handle)` returns
the old `GPUBuffer*` unchanged. The renderer sees no disruption.

**Phase 2 — Swap** (`EndFrame` on the frame where `frame_index >= SwapSafeFrame`):

```cpp
void RenderResourceManager::EndFrame(uint32_t frame_index) {
    // EndFrame is render-thread-only. ScheduleSwap writes to m_pending_swaps from asset thread.
    // We must drain the swap list under the pending mutex to avoid racing with ScheduleSwap.
    {
        std::lock_guard lock(m_pending_mutex);
        for (auto it = m_pending_swaps.begin(); it != m_pending_swaps.end(); ) {
            if (frame_index < it->SwapSafeFrame) { ++it; continue; }
            // Safe to swap: all in-flight frames that referenced the old resource have retired.
            if (it->Kind == ResourceKind::Buffer) {
                GPUBuffer* slot = GetBufferMutable(it->OldBuffer);
                // Enqueue the old GPU resource for timeline-gated destruction via
                // VulkanDevice::DeferFree. Do NOT call vmaDestroyBuffer here —
                // the GPU may still be reading from this buffer in in-flight frames.
                Device->DeferFree({
                    DeferredFreeEntry::Kind::Buffer,
                    /* TimelineValue set by DeferFree from RenderTimelineNextValue */
                    .Buffer = BufferView{.Handle = slot->Buffer, .Allocation = slot->Allocation}
                });
                // Swap: the handle now points to the new GPU resource.
                *slot = it->NewBuffer;
            }
            it = m_pending_swaps.erase(it);
        }
    }
    ++m_current_frame;
}
```

After the swap, `GetBuffer(old_handle)` returns the **new** `GPUBuffer*` — the handle
value is identical; only the underlying resource has changed. No render system code needs
to be updated.

**Phase 3 — Deletion** (handled by the frame deletion queue, Section 7):

The old `GPUBuffer`/`GPUImage` is freed `FRAMES_IN_FLIGHT` frames after the swap,
guaranteeing no frame still in flight references it.

---

## 7. Frame Deletion Queue

Each frame has its own ring slot containing a list of resources to free. On `BeginFrame`,
the slot from `FRAMES_IN_FLIGHT` frames ago is drained — by that point, all frames that
could have referenced those resources have retired.

### DeferredRelease

```cpp
// ZEngine/Rendering/RenderResourceManager.h (private)
struct DeferredRelease {
    ResourceKind Kind;
    union {
        GPUBuffer Buffer;
        GPUImage  Image;
    };
};
```

### Ring structure

```cpp
// Inside RenderResourceManager private members:
Core::Containers::Array<DeferredRelease>
    m_deletion_queues[FRAMES_IN_FLIGHT];  // indexed by frame_index % FRAMES_IN_FLIGHT
```

### BeginFrame drain

The RRM does not call `vmaDestroyBuffer` / `vmaDestroyImage` directly. After
`gpu-allocator-rearchitecture.md` lands, all GPU memory is owned by `GpuAllocator` inside
`VulkanDevice`. The RRM enqueues resources with `Device->DeferFree(entry)` and the
`DeferredFreeQueue::Drain` call inside `TickMemory` performs the actual destruction once
the timeline semaphore confirms the GPU has retired those command buffers.

The RRM's `BeginFrame` therefore only needs to advance the ring slot and flush pending
uploads — it does not call any VMA or Vulkan destroy functions itself.

```cpp
void RenderResourceManager::BeginFrame(uint32_t frame_index) {
    // Flush uploads queued from the asset thread.
    FlushPendingUploads();

    // Release handle pool slots for resources whose timeline gate has passed.
    // Actual GPU memory is freed by DeferredFreeQueue::Drain inside TickMemory,
    // which has already run in AcquireNextImage before this call.
    uint32_t drain_slot = frame_index % FRAMES_IN_FLIGHT;
    for (auto& entry : m_deletion_queues[drain_slot]) {
        // Invalidate the handle pool slot so GetBuffer/GetImage return nullptr.
        if (entry.Kind == ResourceKind::Buffer)
            m_buffer_pool.Free(entry.Buffer.PoolIndex, entry.Buffer.Generation);
        else
            m_image_pool.Free(entry.Image.PoolIndex, entry.Image.Generation);
    }
    m_deletion_queues[drain_slot].Clear();
}
```

**Design notes**:

- The ring has exactly `FRAMES_IN_FLIGHT` (3) slots. A resource enqueued in frame `F`
  has its handle pool slot invalidated in `BeginFrame(F + FRAMES_IN_FLIGHT)`. The actual
  GPU memory was already destroyed by `DeferredFreeQueue::Drain` inside `TickMemory`
  once the timeline semaphore confirmed the GPU had retired all work for that frame.
- `Clear()` resets the `Array` size to zero without releasing the heap allocation.
  The underlying memory is reused each period, so steady-state operation produces no
  allocator traffic.
- `vmaDestroyBuffer` / `vmaDestroyImage` are called exclusively from
  `DeferredFreeQueue::Drain` inside `VulkanDevice::TickMemory`, not from the RRM.
  The RRM's deletion queue only tracks which handle pool slots to invalidate.
- Handle pool slots are **not** recycled in `BeginFrame`. Slot recycling is a separate
  concern managed by the `HandleManager`; it recycles the slot only after the generation
  counter has advanced, preventing ABA collisions on reused slots.

---

## 8. Integration with AssetRegistry

The RRM registers two callbacks with the `AssetRegistry` at startup. These callbacks
are invoked from the asset-loading thread and must be thread-safe; the RRM uses a
mutex-protected pending-upload queue to marshal work onto the render thread.

### Callback wiring

```cpp
// ZEngine/Rendering/RenderResourceManager.cpp

void RenderResourceManager::Init(AssetRegistry& registry) {
    // OnAssetReady: called when a new or first-time asset finishes importing.
    registry.OnAssetReady.Connect([this](UUID uuid, AssetHandle handle) {
        // Determine asset type from registry metadata.
        AssetType type = AssetRegistry::Get().GetType(uuid);
        {
            std::lock_guard lock(m_pending_mutex);
            m_pending_uploads.push_back({uuid, handle, type});
        }
    });

    // OnAssetStale: called when a hot-reload replaces an existing asset.
    registry.OnAssetStale.Connect([this](UUID uuid, AssetHandle handle) {
        // Look up the currently-live GPU handle for this UUID.
        std::lock_guard lock(m_uuid_map_mutex);
        if (auto it = m_uuid_to_buffer.find(uuid); it != m_uuid_to_buffer.end()) {
            ScheduleSwap(it->second, handle);
        } else if (auto it = m_uuid_to_image.find(uuid); it != m_uuid_to_image.end()) {
            ScheduleSwap(it->second, handle);
        }
    });
}
```

### Pending-upload flush (called from BeginFrame on render thread)

```cpp
void RenderResourceManager::FlushPendingUploads() {
    Core::Containers::Array<PendingUpload> local;
    {
        std::lock_guard lock(m_pending_mutex);
        local.Swap(m_pending_uploads);
    }
    for (auto& pending : local) {
        if (pending.Type == AssetType::Mesh) {
            auto result = UploadMesh(pending.Handle);
            if (result.IsOk()) {
                std::lock_guard lock(m_uuid_map_mutex);
                m_uuid_to_buffer[pending.UUID] = result.Value();
            }
        } else if (pending.Type == AssetType::Texture) {
            auto result = UploadTexture(pending.Handle);
            if (result.IsOk()) {
                std::lock_guard lock(m_uuid_map_mutex);
                m_uuid_to_image[pending.UUID] = result.Value();
            }
        }
    }
}
```

**Design notes**:

- The `OnAssetReady` / `OnAssetStale` lambdas only write to a thread-safe pending queue.
  They do not call Vulkan APIs, which are not safe from the asset thread.
- `FlushPendingUploads` is called at the start of `BeginFrame`, before any draw calls
  are recorded. New GPU resources are therefore ready by the time the frame's command
  buffers are built.
- `m_uuid_to_buffer` and `m_uuid_to_image` are the RRM's only reference to asset UUIDs.
  They are written once (on first upload or hot-reload swap) and read on `OnAssetStale`.
  This is the minimum coupling required to resolve UUID → GPU handle for the swap path.
- If `UploadMesh` or `UploadTexture` fails (e.g. out of device memory), the error is
  logged and the UUID is left unmapped. The renderer continues using any previously
  uploaded version (if a swap was in progress) or renders nothing for that asset.

---

## 9. Unit Tests

File: `ZEngine/tests/Rendering/RenderResourceManagerTest.cpp`

### Test 1 — UploadMesh returns valid BufferHandle

```cpp
TEST(RRM, UploadMeshReturnsValidBufferHandle)
{
    MockVulkanContext ctx;
    RenderResourceManager rrm;
    rrm.InitForTest(ctx.Device(), ctx.Allocator());

    AssetHandle mesh = TestAssets::CreateMesh(/* vertex count */ 64);
    auto result = rrm.UploadMesh(mesh);

    ASSERT_TRUE(result.IsOk());
    EXPECT_TRUE(result.Value().IsValid());
}
```

### Test 2 — UploadTexture returns valid ImageHandle

```cpp
TEST(RRM, UploadTextureReturnsValidImageHandle)
{
    MockVulkanContext ctx;
    RenderResourceManager rrm;
    rrm.InitForTest(ctx.Device(), ctx.Allocator());

    AssetHandle tex = TestAssets::CreateTexture(/* width */ 64, /* height */ 64);
    auto result = rrm.UploadTexture(tex);

    ASSERT_TRUE(result.IsOk());
    EXPECT_TRUE(result.Value().IsValid());
}
```

### Test 3 — Release marks handle invalid

```cpp
TEST(RRM, ReleaseMarksHandleInvalid)
{
    MockVulkanContext ctx;
    RenderResourceManager rrm;
    rrm.InitForTest(ctx.Device(), ctx.Allocator());

    AssetHandle mesh = TestAssets::CreateMesh(32);
    BufferHandle handle = rrm.UploadMesh(mesh).Value();

    rrm.Release(handle);

    // Drain deletion queues.
    for (uint32_t i = 0; i < RenderResourceManager::FRAMES_IN_FLIGHT + 1; ++i) {
        rrm.BeginFrame(i);
        rrm.EndFrame(i);
    }

    EXPECT_FALSE(handle.IsValid());
    EXPECT_EQ(rrm.GetBuffer(handle), nullptr);
}
```

### Test 4 — Hot-reload swap: old handle still valid during FRAMES_IN_FLIGHT, invalid after

```cpp
TEST(RRM, HotReloadSwapOldHandleValidDuringInflight)
{
    MockVulkanContext ctx;
    RenderResourceManager rrm;
    rrm.InitForTest(ctx.Device(), ctx.Allocator());

    AssetHandle v1 = TestAssets::CreateMesh(32);
    BufferHandle handle = rrm.UploadMesh(v1).Value();

    AssetHandle v2 = TestAssets::CreateMesh(64);  // updated mesh
    rrm.BeginFrame(0);
    rrm.ScheduleSwap(handle, v2);
    rrm.EndFrame(0);

    // During the in-flight window, old handle must still resolve.
    for (uint32_t i = 1; i < RenderResourceManager::FRAMES_IN_FLIGHT; ++i) {
        rrm.BeginFrame(i);
        EXPECT_NE(rrm.GetBuffer(handle), nullptr)
            << "Handle must remain valid during frame " << i;
        rrm.EndFrame(i);
    }

    // After FRAMES_IN_FLIGHT frames the new resource is in the slot.
    uint32_t safe_frame = RenderResourceManager::FRAMES_IN_FLIGHT;
    rrm.BeginFrame(safe_frame);
    GPUBuffer* buf = rrm.GetBuffer(handle);
    ASSERT_NE(buf, nullptr);
    // New buffer has the larger size.
    EXPECT_EQ(buf->Size, TestAssets::MeshByteSize(64));
    rrm.EndFrame(safe_frame);
}
```

### Test 5 — Double-free guard: second Release on same handle returns error

```cpp
TEST(RRM, DoubleFreeGuardReturnsError)
{
    MockVulkanContext ctx;
    RenderResourceManager rrm;
    rrm.InitForTest(ctx.Device(), ctx.Allocator());

    AssetHandle mesh = TestAssets::CreateMesh(16);
    BufferHandle handle = rrm.UploadMesh(mesh).Value();

    rrm.Release(handle);

    // Drain.
    for (uint32_t i = 0; i < RenderResourceManager::FRAMES_IN_FLIGHT + 1; ++i) {
        rrm.BeginFrame(i);
        rrm.EndFrame(i);
    }

    // Second release on the same (now-stale) handle must not crash and must
    // surface an error rather than silently double-freeing.
    auto result = rrm.ReleaseChecked(handle);
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.Error(), VFS::VFSError::InvalidHandle);
}
```

### Test 6 — Handle invalidation: GetBuffer on released handle returns nullptr

```cpp
TEST(RRM, GetBufferOnReleasedHandleReturnsNullptr)
{
    MockVulkanContext ctx;
    RenderResourceManager rrm;
    rrm.InitForTest(ctx.Device(), ctx.Allocator());

    AssetHandle mesh = TestAssets::CreateMesh(8);
    BufferHandle handle = rrm.UploadMesh(mesh).Value();

    rrm.Release(handle);

    // Drain all in-flight frames.
    for (uint32_t i = 0; i < RenderResourceManager::FRAMES_IN_FLIGHT + 1; ++i) {
        rrm.BeginFrame(i);
        rrm.EndFrame(i);
    }

    EXPECT_EQ(rrm.GetBuffer(handle), nullptr);
}
```

**Test infrastructure notes**:

- `MockVulkanContext` provides a real `VkDevice` + `VmaAllocator` created against a
  Vulkan validation layer-enabled headless instance. Tests run on the host GPU if
  available; otherwise they are skipped with `GTEST_SKIP()` when no Vulkan device
  is found.
- `TestAssets::CreateMesh(vertex_count)` registers a synthetic mesh in a local
  `AssetRegistry` instance and returns its `AssetHandle`. No disk I/O.
- All tests run under AddressSanitizer and UBSanitizer.

---

## 10. Deliverables Checklist

- [ ] `ZEngine/Rendering/RenderHandle.h` — `RenderHandle<Tag>` generational handle template + four `using` aliases (`BufferHandle`, `ImageHandle`, `SamplerHandle`, `PipelineHandle`)
- [ ] `ZEngine/Rendering/GPUResource.h` — `GPUBuffer` and `GPUImage` plain aggregates; zero-initialised sentinel values; no RAII
- [ ] `ZEngine/Rendering/RenderResourceManager.h` — public API (`UploadMesh`, `UploadTexture`, `ScheduleSwap`, `Release`, `BeginFrame`, `EndFrame`, `GetBuffer`, `GetImage` (const), `GetImageMutable`); `FRAMES_IN_FLIGHT = 3` constant
- [ ] `ZEngine/Rendering/RenderResourceManager.cpp` — `Init`, `FlushPendingUploads`, upload path with staging buffer, barrier structs, queue-family ownership transfer for transfer-to-graphics handoff
- [ ] Staging buffer path: `GpuAllocator::Ring.Allocate()` → `memcpy` → `Ring.Submit(signal_val)` → `vkCmdCopyBuffer` → release barrier on transfer queue → acquire barrier on graphics queue
- [ ] Texture path: `VK_IMAGE_LAYOUT_UNDEFINED` → `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL` → `vkCmdCopyBufferToImage` → `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`; barrier covers all mip levels
- [ ] `SwapEntry` struct: old handle union + new resource union + `SwapSafeFrame`; `EndFrame` drains entries where `frame_index >= SwapSafeFrame`, swaps slot in-place, enqueues old resource for deferred deletion
- [ ] `DeferredRelease` ring: `m_deletion_queues[FRAMES_IN_FLIGHT]`; `BeginFrame` invalidates the handle pool slot for `frame_index % FRAMES_IN_FLIGHT`; actual GPU memory freed by `DeferredFreeQueue::Drain` in `VulkanDevice::TickMemory` once the timeline semaphore confirms the GPU is done
- [ ] `AssetRegistry::OnAssetReady` → `m_pending_uploads` (mutex-protected); flushed in `BeginFrame` on render thread via `FlushPendingUploads`
- [ ] `AssetRegistry::OnAssetStale` → `ScheduleSwap` using `m_uuid_to_buffer` / `m_uuid_to_image` maps
- [ ] `ReleaseChecked(handle)` returns `VFS::VFSResult<void>` with `VFSError::InvalidHandle` on double-free; `Release(handle)` asserts in debug, no-ops in release
- [ ] `GetBuffer` / `GetImage` return `nullptr` for any handle whose generation does not match the pool slot's current generation (covers released, stale, and default-constructed handles)
- [ ] `HandleManager`-backed pool used for both `GPUBuffer` and `GPUImage` slots; slot recycled only after `FRAMES_IN_FLIGHT` additional frames to prevent ABA on the same generation value
- [ ] `tests/Rendering/RenderResourceManagerTest.cpp` — all 6 tests pass under AddressSanitizer and UBSanitizer
- [ ] Manual smoke test: load a 1 M-triangle mesh and a 4K texture; hot-reload both assets while the scene is rendering; confirm no validation layer errors, no ASAN errors, and no visible frame tear during the swap window
