# GPU Allocator Rearchitecture — VMA Pools, Staging Ring, Timeline Drain

**Priority:** P0 — Required before 4K resolution target; fixes confirmed correctness bugs  
**Status:** Partially implemented — VMA segregated pool foundation introduced; full rearchitecture pending  
**Depends on:** Nothing — self-contained hardware layer change  
**Blocks:** `per-frame-upload-heap.md` (needs clean allocator API first)

**Goal:** Replace the current flat VMA usage in `VulkanDevice` with a `GpuAllocator` struct
that owns segregated memory pools, a persistent staging ring, and a timeline-gated deferred
free queue. Eliminates 8 confirmed gaps from the VMA analysis, fixes 2 correctness bugs,
and provides the foundation for 4K resource budgets and cross-platform unified memory support.

---

## 1. Current State Problems

| ID | Location | Problem |
|---|---|---|
| C1 | `VulkanDevice.cpp:1179` | `DEDICATED_MEMORY_BIT` hardcoded on every image — exhausts `VkDeviceMemory` object limit on large scenes |
| C2 | `VulkanDevice.cpp:2478` | `WriteTextureData` staging uses `HOST_ACCESS_RANDOM` — wrong memory type for sequential writes |
| C3 | `VulkanDevice.cpp:477` | `VmaAllocatorCreateInfo` has no flags — no BDA support, no driver budget signals |
| C4 | Entire codebase | No `vmaGetHeapBudgets` call — engine is blind to VRAM pressure at 4K |
| H1 | `VulkanDevice.cpp:1042` | All DEVICE_LOCAL allocations share one default pool — geometry holes fragment texture blocks |
| H2 | `AsyncResourceLoader.cpp:237,273` | Per-upload `vmaCreateBuffer` / `vmaDestroyBuffer` — `vkAllocateMemory` storm on bulk scene loads |
| H3 | `VulkanDevice.cpp:2183` | `UniformBuffer` missing `ALLOW_TRANSFER_INSTEAD` — overflows BAR window to slow system RAM |
| H4 | `VulkanDevice.cpp:1337` | `DirtyCollector` polls idle frames — queue accumulates unboundedly during continuous rendering |
| H5 | `AsyncResourceLoader.cpp:261` | `ClearBuffer` direct-map clear path (line 268) skips `vmaFlushAllocation` after `secure_memset` — GPU reads stale data on non-coherent memory |
| H6 | `VulkanDevice.cpp:1384` | `DirtyResources::BUFFER`/`BUFFERMEMORY` call raw `vkDestroyBuffer`/`vkFreeMemory` — VMA allocation leaks if wrong path is taken |

---

## 2. New Types

### `GpuMemoryDomain`

```cpp
// ZEngine/Hardwares/GpuAllocator.h
enum class GpuMemoryDomain : uint8_t {
    DeviceGeometry,  // DEVICE_LOCAL — vertex, index, storage, indirect
    DeviceTexture,   // DEVICE_LOCAL — sampled images, suballocated
    RenderTarget,    // DEVICE_LOCAL — RT, depth, shadow maps; driver-advised dedicated
    HostUniform,     // BAR window — uniforms, frequently-written storage
    HostStaging,     // HOST_VISIBLE | HOST_COHERENT — staging ring source
};
```

Callers declare what they need. `GpuAllocator` selects the pool, the VMA flags, and whether to dedicate. No VMA flags leak through the interface.

### `GpuBudget` (compile-time, 4K target — from memory-budget.md §3)

```cpp
namespace ZEngine::Core::Memory {
    constexpr uint64_t GeometryBytes     = 512ULL << 20;  // vertex + index + storage
    constexpr uint64_t TextureBytes      = 512ULL << 20;  // BC7/BC5 atlas
    constexpr uint64_t RenderTargetBytes = 172ULL << 20;  // shadow maps + post-process + thumbnails
    constexpr uint64_t UniformBytes      =  64ULL << 20;  // per-frame UBOs + bone matrices
    constexpr uint64_t StagingBytes      =  64ULL << 20;  // staging ring capacity
    constexpr float    WarnPressure      = 0.90f;
}
```

Derivation of `RenderTargetBytes` from memory-budget.md §3:
- CSM shadow maps (4 × 2048×2048 × D32): 64 MB
- Spot shadow maps (4 × 1024×1024 × D32): 16 MB
- Point shadow maps (2 × cubemap × 512×512 × D32): 12 MB
- Post-process RTs (HDR + bloom mips + SSAO + LUT): 48 MB
- Thumbnail cache (512 × 128×128 × RGBA8): 32 MB
- Total: 172 MB

### `StagingRing`

One 64 MB allocation, persistently mapped, reused for all uploads. Replaces per-upload
`vmaCreateBuffer` / `vmaDestroyBuffer` in `AsyncResourceLoader`.

```cpp
struct StagingRing {
    static constexpr uint64_t kCapacity  = StagingBytes;
    static constexpr uint32_t kMaxChunks = 256;

    struct Chunk {
        uint32_t Offset;
        uint32_t Size;
        uint64_t TimelineValue; // free when timeline semaphore completed >= this
    };

    VmaAllocation Allocation = nullptr;
    VkBuffer      Handle     = VK_NULL_HANDLE;
    void*         MappedPtr  = nullptr;
    uint32_t      WritePos   = 0;
    uint32_t      ReadPos    = 0;
    Chunk         Chunks[kMaxChunks] = {};
    uint32_t      ChunkHead  = 0;
    uint32_t      ChunkTail  = 0;

    void  Initialize(VmaAllocator allocator);
    void  Shutdown(VmaAllocator allocator);

    // Returns mapped pointer + VkBuffer byte offset. Returns nullptr when the ring is
    // full — caller falls back to a one-shot staging buffer for oversized transfers.
    void* Allocate(uint32_t size, uint32_t alignment, uint32_t* out_vk_offset);

    // Record a submitted chunk so Drain() can release it.
    void  Submit(uint32_t vk_offset, uint32_t size, uint64_t timeline_value);

    // Advance ReadPos past all chunks whose TimelineValue <= completed. O(drained_count).
    void  Drain(uint64_t completed_value);
};
```

`Allocate` is a compare-and-advance on `WritePos` — no heap allocation, no lock.
`Drain` linearly scans from `ChunkHead` and advances `ReadPos`. In steady state
(no bulk load) the ring oscillates around a small write window; `ReadPos` catches up
within one frame.

### `DeferredFreeQueue`

Replaces `HandleManager<DirtyResource>`, `HandleManager<BufferView>`,
`HandleManager<BufferImage>`, `RunningDirtyCollector`, and the entire `DirtyCollector()`
thread.

```cpp
// ZEngine/Hardwares/DeferredFreeQueue.h
struct DeferredFreeEntry {
    enum class Kind : uint8_t { Buffer, Image, VkHandle };
    Kind     EntryKind;
    uint64_t TimelineValue; // drain when render_timeline completed >= this value
    union {
        BufferView  Buffer;
        BufferImage Image;
        struct {
            void*                         Handle;
            Rendering::DeviceResourceType Type;
            void*                         Extra; // used for DESCRIPTORSET pool pointer
        } Vk;
    };
};

struct DeferredFreeQueue {
    static constexpr uint32_t kCapacity = 2048;

    DeferredFreeEntry Entries[kCapacity] = {};
    uint32_t          Head = 0;
    uint32_t          Tail = 0;

    void Enqueue(DeferredFreeEntry entry);
    void Drain(GpuAllocator* alloc, VkDevice device, uint64_t completed_timeline_value);
};
```

`Drain` walks from `Head` while `Entries[Head].TimelineValue <= completed_timeline_value`,
calls `GpuAllocator::FreeBuffer` / `FreeImage` / `vkDestroy*`, advances `Head`. O(k) where
k is the number of resources freed this frame — bounded by `MaxFramesInFlight * MaxResourcesDestroyedPerFrame` in steady state.

### `GpuAllocator`

Single owner of `VmaAllocator` and all `VmaPool` handles. Lives inside `VulkanDevice`
as a value member.

```cpp
// ZEngine/Hardwares/GpuAllocator.h
struct GpuAllocator {
    VmaAllocator Allocator  = nullptr;
    VmaPool      Pools[5]   = {};   // indexed by GpuMemoryDomain cast to uint8_t
    StagingRing  Ring       = {};
    VmaBudget    HeapBudgets[VK_MAX_MEMORY_HEAPS] = {};
    uint32_t     HeapCount  = 0;
    bool         HasBudgetExt = false;

    void        Initialize(VkPhysicalDevice physical_device, VkDevice device,
                           VkInstance instance, bool has_memory_budget_ext,
                           bool has_buffer_device_address);
    void        Shutdown();

    BufferView  AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                               GpuMemoryDomain domain, const char* debug_name = nullptr);
    BufferImage AllocateImage(const VkImageCreateInfo& info, GpuMemoryDomain domain,
                              VkDevice device, VkImageAspectFlagBits aspect,
                              VkImageViewType view_type, uint32_t layer_count);
    void        FreeBuffer(BufferView& view);
    void        FreeImage(BufferImage& image, VkDevice device);

    // Call once per frame. O(VK_MAX_MEMORY_HEAPS).
    void        SampleBudgets();
    float       HeapPressure(uint32_t heap_index) const;
};
```

Internal flag mapping (inside `AllocateBuffer` — not exposed to callers):

| Domain | VMA usage | VMA flags |
|---|---|---|
| `DeviceGeometry` | `AUTO_PREFER_DEVICE` | none (DEVICE_LOCAL only; CPU writes via staging ring) |
| `DeviceTexture` | `AUTO_PREFER_DEVICE` | none (driver-queried dedicated when advised) |
| `RenderTarget` | `AUTO_PREFER_DEVICE` | driver-queried dedicated only |
| `HostUniform` | `AUTO_PREFER_DEVICE` | `SEQUENTIAL_WRITE \| ALLOW_TRANSFER_INSTEAD \| MAPPED` |
| `HostStaging` | `AUTO_PREFER_DEVICE` | `SEQUENTIAL_WRITE \| MAPPED` |

`AllocateImage` calls `vmaCreateImage`, which internally queries `VkMemoryDedicatedRequirementsKHR`
on the created `VkImage` handle and sets `DEDICATED_MEMORY_BIT` only when
`prefersDedicatedAllocation || requiresDedicatedAllocation`. This requires the allocator to have
been created with `VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT` — already guaranteed on
Vulkan 1.1+ by VMA 3.x. This replaces the hardcoded flag at `VulkanDevice.cpp:1156`.

Pool `maxBlockCount` values derived from `GpuBudget`:
```
DeviceGeometry : GeometryBytes  / 256 MB = 2 blocks
DeviceTexture  : TextureBytes   / 256 MB = 2 blocks
RenderTarget   : 0 (unlimited, driver-dedicated)
HostUniform    : UniformBytes   / 64 MB  = 1 block
HostStaging    : 1 (the ring's single block)
```

When a pool's `maxBlockCount` is reached, `vmaCreateBuffer` returns
`VK_ERROR_OUT_OF_DEVICE_MEMORY`. `AllocateBuffer` propagates this as a null `BufferView`
so callers can trigger LRU eviction from `GlobalTextures` before retrying.

---

## 3. Changes to Existing Files

### `VulkanDevice.h`

Remove:
- `VmaAllocator VmaAllocatorValue` (line 617)
- `HandleManager<DirtyResource> DirtyResources` (line 641)
- `HandleManager<BufferView> DirtyBuffers` (line 642)
- `HandleManager<BufferImage> DirtyBufferImages` (line 643)
- `std::atomic_bool RunningDirtyCollector` (line 644)
- `void DirtyCollector()` declaration (line 676)
- `void EnqueueBufferForDeletion(BufferView&)`, `void EnqueueBufferImageForDeletion(BufferImage&)`, `void EnqueueForDeletion(...)` (all overloads)

Add:
```cpp
GpuAllocator      GpuMem      = {};
DeferredFreeQueue PendingFree = {};
void              TickMemory();   // call in AcquireNextImage
void              DeferFree(DeferredFreeEntry entry);
```

Change `CreateBuffer` signature:
```cpp
// Before:
BufferView CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                        VmaAllocationCreateFlags vma_create_flags = 0);
// After:
BufferView CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                        GpuMemoryDomain domain, const char* debug_name = nullptr);
```

Add `Domain` field to `BufferView` so `DeferredFreeQueue` knows which pool to return to:
```cpp
struct BufferView {
    uint8_t         FrameIndex  = std::numeric_limits<uint8_t>::max();
    BufferType      Type        = BufferType::UNKNOWN;
    GpuMemoryDomain Domain      = GpuMemoryDomain::DeviceGeometry;
    VkBuffer        Handle      = VK_NULL_HANDLE;
    VmaAllocation   Allocation  = nullptr;
    operator bool() const { return Handle != VK_NULL_HANDLE; }
};
```

### `VulkanDevice.cpp`

`Initialize()` — allocator setup (currently line 477):
```cpp
bool has_budget = /* VK_EXT_memory_budget in enabled device extensions */;
bool has_bda    = /* VkPhysicalDeviceBufferDeviceAddressFeatures.bufferDeviceAddress */;
GpuMem.Initialize(PhysicalDevice, LogicalDevice, Instance, has_budget, has_bda);
```

`GpuAllocator::Initialize` sets:
```cpp
VmaAllocatorCreateFlags flags = 0;
if (has_budget) flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
if (has_bda)    flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
```

`Deinitialize()` — replace three `__cleanup*` calls and `RunningDirtyCollector.store(false)`:
```cpp
// QueueWaitAll() already called above — GPU is idle
PendingFree.Drain(&GpuMem, LogicalDevice, UINT64_MAX);
GpuMem.Ring.Drain(UINT64_MAX);
```

`Dispose()` — replace `vmaDestroyAllocator(VmaAllocatorValue)` with `GpuMem.Shutdown()`.

`CreateBuffer` — routes directly to `GpuMem.AllocateBuffer`. The entire current 40-line body collapses to one call.

`CreateImage` — routes to `GpuMem.AllocateImage`. The hardcoded `DEDICATED_MEMORY_BIT` at line 1179 is removed.

`WriteTextureData` (line 2478) — fix C2:
```cpp
// Before: HOST_ACCESS_RANDOM (wrong)
BufferView staging = CreateBuffer(..., VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);

// After: use staging ring for sequential write
uint32_t ring_offset = 0;
void* ring_ptr = GpuMem.Ring.Allocate(resource->BufferSize, 4, &ring_offset);
```

`DeferFree` — replaces all three enqueue methods:
```cpp
void VulkanDevice::DeferFree(DeferredFreeEntry entry) {
    entry.TimelineValue = SwapchainPtr->RenderTimelineNextValue;
    PendingFree.Enqueue(entry);
}
```

All 12 callsites (`Image2DBuffer::Dispose`, `IGraphicBuffer::CleanUpMemory`,
`DeviceSwapchain::Clear`, `RendererPipeline::Deinitialize`, `Attachment::Dispose`,
`Framebuffer::Dispose`, `Fence::~Fence`, `Semaphore::~Semaphore`, `Shader::Dispose`,
`VulkanDevice::Deinitialize` × 4) switch to `DeferFree({...})`.

`TickMemory()`:
```cpp
void VulkanDevice::TickMemory() {
    uint64_t completed = 0;
    vkGetSemaphoreCounterValue(LogicalDevice,
        SwapchainPtr->RenderTimeline->GetHandle(), &completed);
    PendingFree.Drain(&GpuMem, LogicalDevice, completed);
    GpuMem.Ring.Drain(completed);
    GpuMem.SampleBudgets(); // O(VK_MAX_MEMORY_HEAPS)
}
```

Remove `DirtyCollector()` entirely: delete method body, delete `ThreadPoolHelper::Submit`
call at line 663, delete `RunningDirtyCollector.store(false)` at line 670.

Typed buffer `CreateBuffer()` callsites (lines 2162–2183) — new domain arguments:
```cpp
BufferView VertexBuffer::CreateBuffer() {
    return m_device->CreateBuffer(m_total_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        GpuMemoryDomain::DeviceGeometry, "VertexBuffer");
}
BufferView UniformBuffer::CreateBuffer() {
    return m_device->CreateBuffer(m_total_size,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        GpuMemoryDomain::HostUniform, "UniformBuffer");
}
// StorageBuffer, IndexBuffer, IndirectBuffer: DeviceGeometry
```

### `AsyncResourceLoader.cpp`

`UploadFromStagingBuffer` (line 216) — use staging ring:
```cpp
uint32_t ring_offset = 0;
void* ring_ptr = Device->GpuMem.Ring.Allocate((uint32_t)byte_size, 4, &ring_offset);
if (!ring_ptr) {
    // Ring full (large burst load): fall back to one-shot staging buffer
    ring_ptr = /* one-shot path, same as today */;
} else {
    memcpy(ring_ptr, data, byte_size);
    // CopyBuffer uses Device->GpuMem.Ring.Handle as source at ring_offset
    Device->GpuMem.Ring.Submit(ring_offset, (uint32_t)byte_size, signal_value);
    // No RetireStagingBuffers slot — ring owns lifetime
}
```

`RetireStagingBuffers` and `TransferRetireStagingBuffers` arrays become unused and are
removed. The staging destroy loops in `ResetCommandBuffers` (lines 574–609) and
`Shutdown` (lines 796–813) are removed.

`ClearBuffer` host-visible path (line 261) — fix H5, add missing flush:
```cpp
// After secure_memset on pMappedData:
if (!(mem_prop_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
    vmaFlushAllocation(Device->GpuMem.Allocator,
                       buffer_view->Allocation, offset, byte_size);
}
```

All 14 `Device->VmaAllocatorValue` references renamed to `Device->GpuMem.Allocator`.

### `DeviceSwapchain.cpp`

`AcquireNextImage` — add `TickMemory` call immediately after `vkAcquireNextImageKHR`:
```cpp
Device->TickMemory();
```

This is the single per-frame drain point. It fires after the GPU has advanced its
timeline (previous frame completed) and before any new command buffers are recorded.

---

## 4. New Files

### `ZEngine/Hardwares/GpuAllocator.h`

Types: `GpuMemoryDomain`, budget constants, `StagingRing`, `GpuAllocator`.

`VulkanDevice.h` replaces `#include <vk_mem_alloc.h>` with `#include <ZEngine/Hardwares/GpuAllocator.h>`.

### `ZEngine/Hardwares/GpuAllocator.cpp`

`VMA_IMPLEMENTATION` and `VMA_VULKAN_VERSION` move here from `VulkanDevice.cpp` — they must live in exactly one translation unit. `VulkanDevice.cpp` removes its definitions.

All `GpuAllocator` and `StagingRing` method bodies.

### `ZEngine/Hardwares/DeferredFreeQueue.h`

`DeferredFreeEntry` and `DeferredFreeQueue` — header-only, all methods inline.

---

## 5. Cross-Platform Notes

### Unified memory (Apple Silicon via MoltenVK, Intel iGPU)

`GpuAllocator::Initialize` probes whether `DeviceGeometry` and `HostStaging` resolve to
the same memory type index via `vmaFindMemoryTypeIndexForBufferInfo`. When they match:

```cpp
if (geometry_mem_type == staging_mem_type) {
    Pools[(uint8_t)GpuMemoryDomain::HostStaging] =
        Pools[(uint8_t)GpuMemoryDomain::DeviceGeometry];
    m_staging_shares_geometry_pool = true;
}
```

On unified memory `UploadBuffer` takes the direct `vmaCopyMemoryToAllocation` path
(geometry buffer is already host-visible) — the staging ring is bypassed entirely.

### MoltenVK / macOS

`VK_EXT_memory_budget` supported on MoltenVK 1.2+. `has_budget` is probed from the
extension list at device creation; `VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT` is set
only when present. `SampleBudgets` calls `vmaCalculateStatistics` as fallback when the
extension is absent.

### Mobile Vulkan (Adreno, Mali)

BAR (`HostUniform` pool) may resolve to a plain `HOST_VISIBLE | HOST_COHERENT` type on
Mali/PowerVR where the dedicated BAR window is absent. VMA handles this silently via
`AUTO_PREFER_DEVICE`. `ALLOW_TRANSFER_INSTEAD` on uniform buffers ensures that when the
small BAR fills up, data is staged to DEVICE_LOCAL rather than falling back to slow
system RAM.

---

## 6. Budget Enforcement

`SampleBudgets` in debug/profiling builds:
```cpp
void GpuAllocator::SampleBudgets() {
    if (HasBudgetExt)
        vmaGetHeapBudgets(Allocator, HeapBudgets);
    else
        /* vmaCalculateStatistics fallback */;

    for (uint32_t i = 0; i < HeapCount; ++i) {
        float p = (float)HeapBudgets[i].usage / (float)HeapBudgets[i].budget;
        if (p > GpuBudget::WarnPressure)
            ZENGINE_CORE_WARN("[GPU] Heap %u at %.0f%% (%zu / %zu MB)",
                              i, p * 100.f,
                              HeapBudgets[i].usage >> 20,
                              HeapBudgets[i].budget >> 20);
    }
}
```

---

## 7. Implementation Order

Each step compiles and passes existing tests before the next begins.

| Step | Files | Change | Risk |
|---|---|---|---|
| 1 | New `GpuAllocator.h/.cpp` | Define types, Initialize/Shutdown/Allocate/Free. Move `VMA_IMPLEMENTATION` here. | Low — additive |
| 2 | New `DeferredFreeQueue.h` | Define `DeferredFreeEntry`, `DeferredFreeQueue`. | Low — additive |
| 3 | `VulkanDevice.h` | Add `GpuMem`, `PendingFree`, `TickMemory`, `DeferFree`. Remove `VmaAllocatorValue`. | Low |
| 4 | `VulkanDevice.cpp` — `Initialize` | Wire `GpuMem.Initialize`. Keep old dirty queues alive. | Low |
| 5 | `VulkanDevice.cpp` — `CreateBuffer` / `CreateImage` | Route through `GpuMem`. Update typed buffer `CreateBuffer()` to pass `GpuMemoryDomain`. | Medium |
| 6 | `AsyncResourceLoader.cpp` — flush fix | Add `vmaFlushAllocation` to `ClearBuffer` host-visible path. | None |
| 7 | `AsyncResourceLoader.cpp` — staging ring | Replace per-upload `CreateBuffer(TRANSFER_SRC)` with `GpuMem.Ring.Allocate`. Remove `RetireStagingBuffers`. | High — core upload path |
| 8 | `VulkanDevice.cpp` — `DeferFree` | Implement `DeferFree`, migrate all 12 enqueue callsites. | Medium |
| 9 | `DeviceSwapchain.cpp` — `TickMemory` | Add `Device->TickMemory()` in `AcquireNextImage`. | Low |
| 10 | `VulkanDevice.cpp` — remove `DirtyCollector` | Delete thread, remove dirty HandleManagers, remove three `__cleanup*` methods. | Low — depends on step 8 |
| 11 | `VulkanDevice.h` — cleanup | Remove old declarations and alias. | Low |

---

## 8. Verification

1. Debug build with `VMA_DEBUG_DETECT_CORRUPTION=1`, `VMA_DEBUG_MARGIN=16` (already in CMakeLists). Run scene load + 100-frame render + unload. Zero corruption reports.
2. Assert inside `GpuAllocator::FreeBuffer`: verify `vkGetSemaphoreCounterValue(RenderTimeline) >= entry.TimelineValue` at destruction time. Validates the timeline gate.
3. After 1000 upload calls confirm `GpuMem.Ring.ReadPos == GpuMem.Ring.WritePos` (ring fully drained). Trace with `VK_LAYER_LUNARG_api_dump` — zero `vkAllocateMemory` calls during streaming after startup.
4. RenderDoc before/after step 5: `VkDeviceMemory` object count drops from one-per-texture to one-per-256MB-block for suballocated textures.
5. Write a test: clear a non-coherent buffer, GPU readback, assert zeroed. Validates the `ClearBuffer` flush fix.
6. Build on macOS (MoltenVK): confirm `has_budget` is false when the extension is absent; no crash; `SampleBudgets` uses statistics fallback.
