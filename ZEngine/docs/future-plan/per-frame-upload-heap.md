# Per-Frame Upload Heap — Replacing IBufferSet with a Linear Frame Heap

**Priority:** P1 — Required for scalable 4K scene rendering (hundreds of draw calls)  
**Status:** Design  
**Depends on:** `gpu-allocator-rearchitecture.md` (needs `GpuAllocator` and `GpuMemoryDomain::HostUniform`)  
**Blocks:** Nothing, but unlocks scalable per-draw-call UBO streaming and large bone matrix uploads

**Goal:** Replace the current `IBufferSet<T>` pattern (one `VkBuffer` per resource type per
frame) with a `PerFrameUploadHeap` — a single large persistently-mapped buffer per frame in
flight into which all per-frame CPU data is bump-allocated. Descriptor access switches from
per-buffer bindings to dynamic UBO offsets. Result: 3 `VkBuffer` objects total regardless of
scene complexity, zero `VmaAllocation` calls per frame in steady state, and one cache-local
write pass per frame for all per-draw data.

---

## 1. Current State Problems

The current `IBufferSet<T>` pattern:

```
VertexBufferSet[3]:     one VkBuffer per frame × scene geometry
StorageBufferSet[3]:    one VkBuffer per frame × storage type
IndexBufferSet[3]:      one VkBuffer per frame × index data
IndirectBufferSet[3]:   one VkBuffer per frame × draw call list
UniformBufferSet[3]:    one VkBuffer per frame × UBO type
```

For a scene with 50 draw calls, each with a per-object UBO (transform, material),
plus scene-global UBOs (camera, lights, skinning matrices):

- At minimum: `(50 per-object + 3 global) × 3 frames = 159 VkBuffer objects` just for uniforms
- 159 `VmaAllocation` objects in the `HostUniform` pool
- 159 separate descriptor set writes at bind time
- CPU writes are scattered across 159 non-contiguous mapped regions

This approach also makes the 40 MB bone matrix budget from memory-budget.md §3 expensive:
`10,000 entities × 256 bones × 16 bytes = 40 MB` as a single `StorageBuffer` works today,
but growing the scene requires tracking more buffer set handles and more descriptor updates.

### Comparison

| | Current (`IBufferSet`) | `PerFrameUploadHeap` |
|---|---|---|
| `VkBuffer` objects for per-frame data | O(resource_count × frames) | 3 (one per frame in flight) |
| `vmaCreateBuffer` calls per frame | 0 (pre-allocated) but one per new resource | 0 — bump pointer reset |
| CPU write locality | Scattered — one mapped ptr per buffer | Contiguous — one mapped region |
| Descriptor update per draw call | One full `vkUpdateDescriptorSets` per buffer | One `uint32_t` dynamic offset per draw |
| BAR window fragmentation | Each buffer occupies its own VMA slot | One allocation per frame covers everything |

---

## 2. `PerFrameUploadHeap`

One heap per frame in flight. The heap is a single `VkBuffer` in `GpuMemoryDomain::HostUniform`
(persistently mapped, `ALLOW_TRANSFER_INSTEAD` for BAR overflow). At the start of each frame
the bump pointer resets to zero — no `vmaDestroyBuffer`, no `vmaCreateBuffer`. The GPU has
finished reading the previous use of this frame's buffer (timeline semaphore ensures it).

```cpp
// ZEngine/Hardwares/PerFrameUploadHeap.h
struct PerFrameHeapAlloc {
    uint32_t Offset;  // byte offset into the frame's VkBuffer
    uint32_t Size;
};

struct PerFrameUploadHeap {
    static constexpr uint32_t kCapacity = GpuBudget::UniformBytes; // 64 MB per frame

    VkBuffer         Handle     = VK_NULL_HANDLE;
    VmaAllocation    Allocation = nullptr;
    void*            MappedPtr  = nullptr;
    uint32_t         WritePos   = 0;
    bool             Coherent   = false; // cached from VkMemoryPropertyFlags at init

    // Initialized once per frame slot in VulkanDevice::Initialize
    void Initialize(GpuAllocator* alloc, const char* debug_name);
    void Shutdown(GpuAllocator* alloc);

    // Reset at frame start — O(1), no GPU wait (timeline guarantees GPU is done)
    void Reset();

    // Push data into the heap. Returns the byte offset for use as a dynamic UBO offset.
    // Returns UINT32_MAX if the heap is full (caller must assert — should never happen
    // in a correctly budgeted scene).
    PerFrameHeapAlloc Push(const void* data, uint32_t size, uint32_t alignment);

    // Flush the written range if memory is not coherent. Called once per frame after
    // all Push() calls, before command buffer recording begins.
    void Flush(VmaAllocator allocator);
};
```

`Push` implementation:
```cpp
PerFrameHeapAlloc PerFrameUploadHeap::Push(const void* data, uint32_t size, uint32_t alignment) {
    uint32_t aligned_pos = (WritePos + alignment - 1) & ~(alignment - 1);
    ZENGINE_VALIDATE_ASSERT(aligned_pos + size <= kCapacity, "PerFrameUploadHeap full")
    memcpy((uint8_t*)MappedPtr + aligned_pos, data, size);
    WritePos = aligned_pos + size;
    return { aligned_pos, size };
}
```

No branches, no locks, no allocation — a pointer bump and a `memcpy`.

`Flush` is called once per frame after all `Push` calls complete, before the command buffer
is recorded. It issues a single `vmaFlushAllocation` covering `[0, WritePos]`:
```cpp
void PerFrameUploadHeap::Flush(VmaAllocator allocator) {
    if (!Coherent && WritePos > 0)
        vmaFlushAllocation(allocator, Allocation, 0, WritePos);
}
```

One flush per frame instead of one per buffer — reduces the number of cache-flush
syscalls from O(resource_count) to O(1).

---

## 3. Integration with `VulkanDevice`

`VulkanDevice` gains:

```cpp
// VulkanDevice.h
PerFrameUploadHeap FrameHeaps[3] = {}; // one per BufferredFrameCount
```

Initialized in `VulkanDevice::Initialize` after `GpuMem`:
```cpp
for (uint32_t i = 0; i < SwapchainPtr->BufferredFrameCount; ++i) {
    char name[64];
    snprintf(name, sizeof(name), "FrameHeap[%u]", i);
    FrameHeaps[i].Initialize(&GpuMem, name);
}
```

`TickMemory()` resets the current frame's heap:
```cpp
void VulkanDevice::TickMemory() {
    uint64_t completed = 0;
    vkGetSemaphoreCounterValue(LogicalDevice,
        SwapchainPtr->RenderTimeline->GetHandle(), &completed);
    PendingFree.Drain(&GpuMem, LogicalDevice, completed);
    GpuMem.Ring.Drain(completed);
    GpuMem.SampleBudgets();

    // Reset the heap for the frame we are about to record
    FrameHeaps[SwapchainPtr->CurrentFrame->Index].Reset();
}
```

The reset is a single `WritePos = 0` assignment — O(1).

---

## 4. New Per-Frame Data API

### Before: per-type buffer set

```cpp
// Current usage in GraphicRenderer::DrawScene
camera_buffer_set->At(frame_index).Write(frame_index, thread_index, &camera_data, sizeof(camera_data));
// → calls AsyncResLoader->UploadBuffer → may allocate staging buffer
// → VkDescriptorSet bound to one specific VkBuffer
```

### After: push into frame heap, bind with dynamic offset

```cpp
// Proposed usage
uint8_t fi = Device->SwapchainPtr->CurrentFrame->Index;
PerFrameUploadHeap& heap = Device->FrameHeaps[fi];

auto camera_alloc = heap.Push(&camera_data, sizeof(CameraData),
                              Device->MinUniformBufferOffsetAlignment());
// camera_alloc.Offset is the dynamic offset for this draw

auto lights_alloc = heap.Push(&lights_data, sizeof(LightsData),
                              Device->MinUniformBufferOffsetAlignment());

// One vkCmdBindDescriptorSets with dynamic offsets covers all per-frame data
uint32_t offsets[] = { camera_alloc.Offset, lights_alloc.Offset, ... };
vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipeline_layout, SET_PER_FRAME,
                        1, &per_frame_descriptor_set,
                        ARRAY_COUNT(offsets), offsets);
```

The per-frame descriptor set is allocated once per frame in flight at startup and is
never updated per-draw. It has a single binding per per-frame data type
(`DYNAMIC_UNIFORM_BUFFER`). Only the dynamic offset changes per draw call.

---

## 5. Descriptor Set Layout Change

Currently each buffer type has its own `VkDescriptorSetLayout` binding pointing to a
specific `VkBuffer`. After the heap migration:

```
Set 0 — per-pass (render pass inputs: textures, samplers) — unchanged
Set 1 — global bindless textures — unchanged (already in VulkanDevice)
Set 2 — per-frame dynamic UBOs (one binding per per-frame data type, DYNAMIC_UNIFORM_BUFFER)
Set 3 — per-draw push constants or storage buffer offsets
```

The per-frame set (set 2) is created once at `VulkanDevice::Initialize`. Its descriptor
writes point to `FrameHeaps[i].Handle` for frame `i`. Dynamic offsets at bind time select
the sub-region within the heap for each data type.

`minUniformBufferOffsetAlignment` (queried from `VkPhysicalDeviceLimits`) governs the
alignment passed to `PerFrameUploadHeap::Push`. Stored as:
```cpp
uint32_t VulkanDevice::MinUniformBufferOffsetAlignment() const {
    return (uint32_t)PhysicalDeviceProperties.properties.limits.minUniformBufferOffsetAlignment;
}
```

---

## 6. Migration Path for `IBufferSet<T>`

`IBufferSet<T>` and `IGraphicBuffer` are not deleted. They remain for vertex, index, and
indirect buffers — resources that are long-lived, uploaded once, and read many times.
These do not benefit from per-frame bump allocation.

What is replaced:

| Buffer type | Current approach | After migration |
|---|---|---|
| `UniformBuffer` (per-frame) | `UniformBufferSet[3]`, one `VkBuffer` per type per frame | `PerFrameUploadHeap::Push` per frame |
| `StorageBuffer` (per-frame write) | `StorageBufferSet[3]` | `PerFrameUploadHeap::Push` for write-per-frame storage |
| `StorageBuffer` (large, static) | `StorageBufferSet[3]` | Keep as `IGraphicBuffer` — `DeviceGeometry` pool |
| `VertexBuffer` | `VertexBufferSet[3]` | Unchanged — long-lived geometry |
| `IndexBuffer` | `IndexBufferSet[3]` | Unchanged — long-lived geometry |
| `IndirectBuffer` | `IndirectBufferSet[3]` | Kept but indirect draw data pushed into heap each frame |

The rule: if the data changes every frame and is consumed by the GPU in that same frame,
it belongs in the heap. If the data is uploaded once and read across many frames
(geometry, textures, static storage buffers), it stays in `IGraphicBuffer`.

---

## 7. Bone Matrix Storage

The 40 MB bone matrix buffer (memory-budget.md §3: `10,000 entities × 256 bones × 16 bytes`)
is a per-frame write — it is re-uploaded every frame for each animated entity. This is the
primary motivation for a 64 MB heap:

```cpp
// Per frame, after skinning system runs:
auto bone_alloc = heap.Push(skinning_system.BoneMatrices(),
                            skinning_system.TotalBytes(),
                            Device->MinStorageBufferOffsetAlignment());
// Bind as storage buffer with dynamic offset into the skinning pass
```

Without the heap, bone matrices require a 40 MB `StorageBufferSet[3]` (120 MB total) with
per-frame `vmaCopyMemoryToAllocation` calls. With the heap, the 40 MB sits inside the
64 MB `PerFrameUploadHeap` — no separate allocation, no staging, one `memcpy` per frame.

---

## 8. Indirect Draw Buffer

`IndirectBuffer` currently holds one `VkDrawIndirectCommand` per draw call, uploaded each
frame. After migration, the indirect command array is pushed into the heap each frame:

```cpp
auto indirect_alloc = heap.Push(draw_commands.data(),
                                draw_commands.size() * sizeof(VkDrawIndirectCommand),
                                sizeof(VkDrawIndirectCommand));
// vkCmdDrawIndirect uses heap.Handle at indirect_alloc.Offset
vkCmdDrawIndirect(cmd, heap.Handle, indirect_alloc.Offset,
                  (uint32_t)draw_commands.size(), sizeof(VkDrawIndirectCommand));
```

`IndirectBuffer` and `IndirectBufferSet` are then unused for per-frame indirect data and
can be removed or repurposed for static multi-draw-indirect batches.

---

## 9. Implementation Order

Depends on `gpu-allocator-rearchitecture.md` steps 1–5 being complete (GpuAllocator with
`GpuMemoryDomain::HostUniform` pool functional).

| Step | Files | Change | Risk |
|---|---|---|---|
| 1 | New `PerFrameUploadHeap.h` | Define struct; `Initialize`, `Shutdown`, `Reset`, `Push`, `Flush`. | Low — additive |
| 2 | `VulkanDevice.h` | Add `FrameHeaps[3]`, `MinUniformBufferOffsetAlignment()`. | Low |
| 3 | `VulkanDevice.cpp` — `Initialize` | Allocate and init 3 heaps. | Low |
| 4 | `VulkanDevice.cpp` — `TickMemory` | Add `FrameHeaps[fi].Reset()`. | Low |
| 5 | Shader / descriptor set layout | Add set 2 (`DYNAMIC_UNIFORM_BUFFER` per per-frame type) to shader reflection. Update `CreateDescriptorSets` in `Shader.cpp`. | Medium |
| 6 | `GraphicRenderer.cpp` — camera, lights | Migrate camera and light UBOs to `heap.Push`. Update bind calls to pass dynamic offsets. | Medium |
| 7 | `GraphicScene` — per-object transforms | Migrate per-draw transform UBOs to `heap.Push`. | Medium |
| 8 | Skinning system (future) | Migrate bone matrix upload to `heap.Push`. | Low — new system |
| 9 | `IndirectBuffer` | Push indirect commands into heap. Remove `IndirectBufferSet` per-frame path. | Low |
| 10 | `UniformBufferSet` cleanup | Remove `UniformBuffer::CreateBuffer` and `UniformBufferSet` from `VulkanDevice`. | Low — after step 7 |

---

## 10. Verification

1. Frame heap `WritePos` after all pushes must be less than `kCapacity`. Assert in Debug builds. Log peak `WritePos` via `MemoryProfiler` to validate budget sizing.
2. One `vkFlushMappedMemoryRanges` call per frame (not per buffer). Verify with `VK_LAYER_LUNARG_api_dump` — count `vkFlushMappedMemoryRanges` invocations before and after migration.
3. `vkCmdBindDescriptorSets` for per-frame set uses non-zero dynamic offsets and binds correctly. Verify with RenderDoc: capture a frame and inspect UBO bindings — data should match CPU-side values.
4. `VkBuffer` object count for per-frame data in RenderDoc: 3 (one per frame in flight) vs. O(N) before.
5. Scene stress test: 500 draw calls, 60 fps, 100 frames — no validation layer errors, no heap overflow asserts.
6. macOS / MoltenVK: `PerFrameUploadHeap` uses `GpuMemoryDomain::HostUniform` which is backed by unified memory. Verify writes are visible to GPU without staging (host-visible fast path in `UploadBuffer`).
