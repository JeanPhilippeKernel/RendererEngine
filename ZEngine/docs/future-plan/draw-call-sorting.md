# Draw Call Sorting — Packed 64-Bit Sort Key and Per-Frame DrawCommandBuffer

**Priority:** P1 — Required for correct transparency ordering and GPU-driver-friendly pipeline batching  
**Status:** Design  
**Relates to:** `rendering-flow.md`, `render-graph-redesign.md`, `per-frame-upload-heap.md`  
**Depends on:** Nothing in flight; can be implemented against the current `RenderScene` and `SceneData` structs  
**Blocks:** Correct alpha-blended rendering; multi-pipeline batching; future GPU-driven culling

**Goal:** Replace the unsorted, unordered-map-iteration-order indirect draw submission with a
deterministic, camera-aware sort that groups opaque draws front-to-back (minimizing GPU overdraw
and early-Z waste), sorts transparent draws back-to-front (producing correct alpha compositing),
and batches draws by pipeline and material to reduce PSO and descriptor set rebinds. All CPU sort
work runs on the render thread inside `AppRenderPipeline::RenderScene`, uses arena-allocated
scratch, and writes a single sorted `VkDrawIndirectCommand` array to the existing
`IndirectBufferHandle` before the passes execute.

---

## 1. Current State

`AppRenderPipeline::RenderScene` iterates `scene->NodeSubMeshesAllocations` — an
`UnorderedHashMap<uint32_t, SubMeshAllocation>` — and fills `DrawIndirectCommands` in whatever
order the hash map returns entries. The resulting `VkDrawIndirectCommand` array contains:

```cpp
DrawIndirectCommands.push({
    .vertexCount   = SubMeshAllocations[i].IndexCount,
    .instanceCount = SubMeshAllocations[i].InstanceCount,
    .firstVertex   = 0,
    .firstInstance = i,   // used by the vertex shader as the DrawData index
});
```

`firstInstance = i` encodes the draw's index into the GPU-side `RenderDataBufferHandle` storage
buffer. The vertex shader reads `gl_InstanceIndex` to retrieve the matching `DrawData` record.
Because `i` follows unordered-map iteration order, the draw-to-data mapping is correct but the
draw submission order is arbitrary. Three concrete consequences:

- Transparent objects are submitted in arbitrary order. The depth buffer cannot resolve the
  compositing order, so overdraw produces visually incorrect results for any alpha-blended mesh.
- Opaque draws are submitted with no depth ordering. The GPU rasterizes far fragments first as
  often as it rasterizes near fragments first, performing redundant shading work that an early-Z
  depth pre-pass is meant to avoid.
- Draws with the same pipeline shader are interleaved with draws using different shaders.
  Each pipeline switch is a full GPU pipeline state object (PSO) rebind — one of the most
  expensive per-draw operations on all desktop Vulkan drivers.

`DepthPrePass::Execute` and `GbufferPass::Execute` both call:

```cpp
command_buffer->DrawIndirect(*indirect_buffer->At(device->SwapchainPtr->CurrentFrame->Index));
```

The `CommandBuffer::DrawIndirect` implementation forwards to `vkCmdDrawIndirect` with `offset = 0`
and `buffer.CommandCount` covering the entire buffer. There is no way to issue a partial range or
two separate ranges from the same buffer without a new API surface on `CommandBuffer`.

The disabled `#if 0` block in `GraphicScene.h` contains `struct DrawData`, which is the correct
per-draw record shape. It is not yet used in the live path. This design activates a version of it.

---

## 2. Sort Key Structure

Every draw is assigned a 64-bit integer sort key before sorting. All key bits are packed such
that a single unsigned ascending comparison produces the correct submission order for a given
bucket. The bit layout from most significant to least significant:

```
 63        62 61     59 58        49 48          37 36           13 12          0
 +-----------+----------+-----------+-------------+----------------+-------------+
 | Translucency | Pass  | Pipeline  |  Material   |     Depth      |   Scratch   |
 |   bucket   | layer  |    ID     |     ID      |   (24 bits)    |  (13 bits)  |
 |  (2 bits)  | (3 bits)| (10 bits)| (12 bits)   |                |             |
 +-----------+----------+-----------+-------------+----------------+-------------+
```

Field definitions:

- **Translucency bucket** (bits 63-62, 2 bits): `0` = fully opaque, `1` = alpha-tested,
  `2` = alpha-blended transparent. Opaque draws come first in ascending key order; transparent
  draws come last. Alpha-tested sits between the two because it writes to depth normally but
  may discard fragments; it must precede transparent draws.

- **Pass layer** (bits 61-59, 3 bits): `0` = depth pre-pass, `1` = geometry (G-buffer),
  `2` = forward, `3` = post. Only one indirect buffer is emitted per pass; this field is used
  when a single sort buffer is shared across multiple passes. In the current two-pass model
  (DepthPrePass + GbufferPass) both reads come from the same sorted buffer, so this field is
  set to `0` for all draws and is reserved for the multi-pass path described in
  `render-graph-redesign.md`.

- **Pipeline/shader ID** (bits 58-49, 10 bits): a stable integer identifying the PSO variant.
  Grouping by pipeline ID before material ID ensures that all draws using the same shader are
  contiguous, eliminating PSO rebinds within a bucket. 1024 distinct pipelines is sufficient
  for the foreseeable pass set; `render-graph-redesign.md` §4 assigns integer pipeline handles
  from the same integer pool.

- **Material ID** (bits 48-37, 12 bits): `SubMeshAllocation::MaterialId` truncated to 12 bits.
  Within the same pipeline, draws sharing a material are contiguous, reducing descriptor set
  updates. 4096 material slots matches the current `MaterialBufferHandle` capacity.

- **Depth** (bits 36-13, 24 bits): a linearised, quantised view-space depth. Encoding depends
  on translucency bucket; see section 6.

- **Sub-sort scratch** (bits 12-0, 13 bits): reserved for secondary sorting criteria added
  later (shadow caster flag, LOD level, instance group). Set to `0` by `Build`.

The named constants and packing function:

```cpp
// ZEngine/Rendering/Scenes/DrawCommandBuffer.h

namespace ZEngine::Rendering::Scenes
{
    namespace SortKey
    {
        static constexpr uint64_t kTranslucencyShift = 62u;
        static constexpr uint64_t kPassLayerShift    = 59u;
        static constexpr uint64_t kPipelineIdShift   = 49u;
        static constexpr uint64_t kMaterialIdShift   = 37u;
        static constexpr uint64_t kDepthShift        = 13u;

        static constexpr uint64_t kTranslucencyMask  = 0x3ULL   << kTranslucencyShift;
        static constexpr uint64_t kPassLayerMask     = 0x7ULL   << kPassLayerShift;
        static constexpr uint64_t kPipelineIdMask    = 0x3FFULL << kPipelineIdShift;
        static constexpr uint64_t kMaterialIdMask    = 0xFFFULL << kMaterialIdShift;
        static constexpr uint64_t kDepthMask         = 0xFFFFFFULL << kDepthShift;
        static constexpr uint64_t kScratchMask       = 0x1FFFULL;

        inline uint64_t Pack(
            uint32_t translucency,   // 0–2
            uint32_t pass_layer,     // 0–7
            uint32_t pipeline_id,    // 0–1023
            uint32_t material_id,    // 0–4095
            uint32_t depth_key,      // 0–0xFFFFFF
            uint32_t scratch = 0)    // 0–8191
        {
            return (static_cast<uint64_t>(translucency & 0x3)    << kTranslucencyShift)
                 | (static_cast<uint64_t>(pass_layer   & 0x7)    << kPassLayerShift)
                 | (static_cast<uint64_t>(pipeline_id  & 0x3FF)  << kPipelineIdShift)
                 | (static_cast<uint64_t>(material_id  & 0xFFF)  << kMaterialIdShift)
                 | (static_cast<uint64_t>(depth_key    & 0xFFFFFF) << kDepthShift)
                 | (static_cast<uint64_t>(scratch      & 0x1FFF));
        }
    } // namespace SortKey
} // namespace ZEngine::Rendering::Scenes
```

---

## 3. DrawCommand Struct

`DrawCommand` is the CPU-side record that travels through build, sort, and flush. It is never
written to the GPU directly; `Flush` extracts the two GPU-facing sub-structs from it.

```cpp
// ZEngine/Rendering/Scenes/DrawCommandBuffer.h

#include <vulkan/vulkan_core.h>
#include <ZEngine/Rendering/Scenes/GraphicScene.h>  // DrawData (from the #if 0 block, now live)

namespace ZEngine::Rendering::Scenes
{
    struct DrawCommand
    {
        uint64_t            SortKey;    // packed 64-bit key from SortKey::Pack
        VkDrawIndirectCommand GpuCmd;   // written verbatim to IndirectBufferHandle
        DrawData            Data;       // written verbatim to RenderDataBufferHandle
    };
} // namespace ZEngine::Rendering::Scenes
```

`DrawData` is lifted out of the `#if 0` block in `GraphicScene.h` verbatim:

```cpp
struct DrawData
{
    uint32_t TransformIndex;  // index into GlobalTransforms[] / TransformBufferHandle
    uint32_t MaterialIndex;   // index into Materials[]       / MaterialBufferHandle
    uint32_t VertexOffset;    // byte-offset start into VertexBufferHandle
    uint32_t IndexOffset;     // element-offset start into IndexBufferHandle
    uint32_t VertexCount;     // not consumed by indirect draw; available for compute
    uint32_t IndexCount;      // = VkDrawIndirectCommand::vertexCount (index draw emulation)
};
```

`GpuCmd` mirrors the fields the GPU consumes:

```cpp
GpuCmd.vertexCount   = alloc.IndexCount;     // index draw encoded as vertex count
GpuCmd.instanceCount = alloc.InstanceCount;  // always 1 for static meshes
GpuCmd.firstVertex   = 0;
GpuCmd.firstInstance = <slot in sorted output array>; // set during Flush, not Build
```

`firstInstance` is the index of this draw's `DrawData` record in the sorted
`RenderDataBufferHandle` array. It cannot be set during `Build` because the sorted position is
unknown until after `Sort`. `Flush` assigns it as it writes out the sorted records.

---

## 4. Sorting Algorithm

`std::sort` applied to `Array<DrawCommand>` would move the full `DrawCommand` struct (48 bytes
with typical alignment) during each comparison swap. At 10 000 draw calls that is up to
10 000 × log2(10 000) ≈ 130 000 struct moves, each touching three cache lines. The sort key
is 8 bytes; the comparison is on 8 bytes; but the move cost is 48 bytes. The comparison-to-move
ratio is 1:6, making `std::sort` cache-hostile for large draw counts.

The correct algorithm is a stable, in-place LSD (least-significant-digit) radix sort on the
64-bit key. The sort operates on indices into the `DrawCommand` array rather than on the structs
themselves, then applies the index permutation in a single pass at the end.

The 64-bit key is processed as two 32-bit halves in LSD order — lower word (bits 0-31) first,
upper word (bits 32-63) second. Each 32-bit half is sorted using four rounds of 8-bit counting
sort (256 histogram buckets per round, four rounds × 8 bits = 32 bits per logical pass). The
total work is eight counting rounds, one pass through the index array per round. Memory cost is
one 256-entry `uint32_t` histogram (1 KB) and one output-index array of `N × 4` bytes, both
allocated from the per-frame scratch arena and released after `Flush` completes.

The choice of 8-bit digits (256 buckets) keeps the histogram within a single L1 cache line
group on all current desktop hardware. 16-bit digits (65 536 buckets) fit in L2 but exceed L1
on most configurations and produce slower sorts below approximately 100 000 elements.

```cpp
// ZEngine/Rendering/Scenes/DrawCommandBuffer.cpp (internal, not part of the public interface)

// Sorts indices[0..count) by the 64-bit keys[i] in ascending order.
// output_indices is a scratch buffer of count uint32_t elements.
static void RadixSort64(
    Core::Memory::ArenaAllocator* scratch_arena,
    const uint64_t*               keys,
    uint32_t*                     indices,
    uint32_t                      count)
{
    if (count == 0) { return; }

    uint32_t* temp = static_cast<uint32_t*>(
        scratch_arena->Allocate(count * sizeof(uint32_t), alignof(uint32_t)));

    // Initialise identity permutation.
    for (uint32_t i = 0; i < count; ++i) { indices[i] = i; }

    uint32_t hist[256] = {};

    // Eight passes: bytes 0-3 (lower word), then bytes 4-7 (upper word).
    for (uint32_t pass = 0; pass < 8; ++pass)
    {
        const uint32_t shift = pass * 8u;

        // Build histogram.
        for (uint32_t i = 0; i < count; ++i)
        {
            uint8_t digit = static_cast<uint8_t>((keys[indices[i]] >> shift) & 0xFF);
            ++hist[digit];
        }

        // Prefix sum.
        uint32_t running = 0;
        for (uint32_t b = 0; b < 256; ++b)
        {
            uint32_t cnt = hist[b];
            hist[b]      = running;
            running     += cnt;
        }

        // Scatter into temp.
        for (uint32_t i = 0; i < count; ++i)
        {
            uint8_t  digit    = static_cast<uint8_t>((keys[indices[i]] >> shift) & 0xFF);
            temp[hist[digit]] = indices[i];
            ++hist[digit];
        }

        // Swap indices and temp for the next pass.
        for (uint32_t i = 0; i < count; ++i) { indices[i] = temp[i]; }

        for (uint32_t b = 0; b < 256; ++b) { hist[b] = 0; }
    }
}
```

`Sort()` on `DrawCommandBuffer` calls `RadixSort64` twice — once for the opaque bucket, once
for the transparent bucket — using scratch from `LocalArena` via `ZGetScratch`. The sort runs
entirely on the render thread after `Build()` and before `Flush()`. No GPU work depends on
`indices` or `keys`; those are freed when `ZReleaseScratch` is called after `Flush`.

---

## 5. Opaque vs Transparent Split

`Build` partitions draw commands into two flat arrays before sorting:

- `Array<DrawCommand> Opaque` — draws where `MeshMaterial::Factors.x` (transparency) equals
  `1.0f` and `MeshMaterial::Factors.z` (AlphaTest) equals `0.0f`. These are submitted with
  depth write enabled and depth test `LESS`. They must appear before transparent draws so the
  depth buffer is fully populated by the time transparent draws execute.

- `Array<DrawCommand> Transparent` — draws where `MeshMaterial::Factors.x < 1.0f` OR
  `MeshMaterial::Factors.z > 0.0f`. Alpha-tested draws (`Factors.z > 0`) go here with
  translucency bucket value `1`; fully blended draws go here with translucency bucket value `2`.

After sorting, `Flush` writes the opaque bucket first into the GPU indirect buffer, immediately
followed by the transparent bucket. The byte boundary between the two is:

```
OpaqueByteOffset      = 0
TransparentByteOffset = OpaqueCount * sizeof(VkDrawIndirectCommand)
```

Each pass issues two `vkCmdDrawIndirect` calls against the same `VkBuffer`, specifying explicit
offsets and counts. `CommandBuffer` needs one additional entry point to expose this:

```cpp
// ZEngine/ZEngine/Hardwares/VulkanDevice.h — new overload on CommandBuffer
void DrawIndirectRange(
    const Hardwares::IndirectBuffer& buffer,
    uint32_t                         first_command,
    uint32_t                         command_count);
```

Implementation:

```cpp
// VulkanDevice.cpp
void CommandBuffer::DrawIndirectRange(
    const Hardwares::IndirectBuffer& buffer,
    uint32_t                         first_command,
    uint32_t                         command_count)
{
    if (command_count == 0) { return; }
    vkCmdDrawIndirect(
        m_command_buffer,
        reinterpret_cast<VkBuffer>(buffer.GetNativeBufferHandle()),
        static_cast<VkDeviceSize>(first_command) * sizeof(VkDrawIndirectCommand),
        command_count,
        sizeof(VkDrawIndirectCommand));
}
```

`DepthPrePass::Execute` issues only the opaque range. `GbufferPass::Execute` issues both
ranges in order. See section 9 for the updated `Execute` bodies.

---

## 6. Depth Value Encoding

View-space depth is computed by transforming the mesh's world-space origin through the camera
view matrix. The world-space origin is the translation column of `GlobalTransforms[TransformId]`.

```cpp
// Inside DrawCommandBuffer::Build, per SubMeshAllocation entry

const Core::Maths::Mat4f& world = scene->GlobalTransforms[alloc.TransformId];
// Column 3 of the world transform is the world-space translation.
// Mat4f is column-major; index [3] is the 4th column.
Core::Maths::Vec4f world_origin = Core::Maths::Vec4f(world[3][0], world[3][1], world[3][2], 1.0f);

const Core::Maths::Mat4f& view = camera->GetView();
Core::Maths::Vec4f view_pos    = view * world_origin;

// Vulkan right-hand view space: camera looks along -Z.
// Objects in front of the camera have negative view_pos.z.
// Negate to produce a positive linear depth in [0, far_plane].
float linear_depth = -view_pos.z;
float far_plane    = camera->Settings.FarPlane;  // default 10000.0f in CameraSetting

// Clamp to [0, 1] before quantising.
float norm_depth   = linear_depth / far_plane;
norm_depth         = norm_depth < 0.0f ? 0.0f : (norm_depth > 1.0f ? 1.0f : norm_depth);
uint32_t depth_q   = static_cast<uint32_t>(norm_depth * 0xFFFFFFu);
```

For **opaque** draws (`translucency = 0` or `1`):

```cpp
uint32_t depth_key = depth_q;
// Small depth_key = close to camera = drawn first in ascending key sort.
// Opaque geometry is submitted front-to-back: near fragments write depth early,
// occluded fragments are culled by early-Z before the fragment shader runs.
```

For **transparent** draws (`translucency = 2`):

```cpp
uint32_t depth_key = (~depth_q) & 0xFFFFFFu;
// Bitwise NOT: far objects (large depth_q) become small depth_key.
// Ascending key sort submits far transparent draws first, near draws last.
// This is the correct painter's algorithm order for back-to-front blending.
```

Both buckets are sorted by ascending 64-bit key. The NOT encoding means the identical sort
path handles both buckets without a comparator change.

---

## 7. DrawCommandBuffer Struct

`DrawCommandBuffer` is a per-frame-in-flight struct. Three instances live on `AppRenderPipeline`,
one per swapchain frame slot (matching `SwapchainPtr->BufferredFrameCount = 3`). Each is
initialised once from `AppRenderPipeline::Initialize` and cleared at the start of each dirty
rebuild; they are never destroyed until the pipeline shuts down.

```cpp
// ZEngine/Rendering/Scenes/DrawCommandBuffer.h

#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/ArenaAllocator.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <ZEngine/Rendering/Scenes/GraphicScene.h>

namespace ZEngine::Rendering::Scenes
{
    struct DrawCommandBuffer
    {
        Core::Containers::Array<DrawCommand> Opaque;
        Core::Containers::Array<DrawCommand> Transparent;
        uint32_t                             OpaqueCount      = 0;
        uint32_t                             TransparentCount = 0;

        // Persistent arena owns the Opaque and Transparent arrays.
        // Must be sub-arenaed from AppRenderPipeline::LocalArena at init time.
        Core::Memory::ArenaAllocator*        Arena = nullptr;

        // Called once from AppRenderPipeline::Initialize.
        void Init(Core::Memory::ArenaAllocator* parent_arena, uint32_t capacity_hint);

        // Clears both buckets (does not free arena memory), then walks scene->NodeSubMeshesAllocations
        // to fill Opaque and Transparent with one DrawCommand per SubMeshAllocation.
        // Computes view-space depth from scene->GlobalTransforms and camera->GetView().
        // Sets SortKey and Data; leaves GpuCmd.firstInstance = 0 (assigned by Flush).
        // Only callable when DrawCommandsDirty[fi] is true; see section 8.
        void Build(
            const Rendering::Scenes::RenderScenePtr& scene,
            const Rendering::Cameras::CameraPtr&     camera,
            uint32_t                                 pipeline_id);

        // Runs RadixSort64 on both buckets using scratch from Arena.
        void Sort(Core::Memory::ArenaAllocator* scratch_arena);

        // Writes sorted VkDrawIndirectCommand and DrawData arrays to the GPU buffers.
        // Assigns GpuCmd.firstInstance = position in sorted output order.
        // Opaque records written first, transparent records immediately after.
        void Flush(
            uint8_t                             frame_index,
            uint8_t                             thread_index,
            Hardwares::IndirectBuffer*          indirect_buf,
            Hardwares::StorageBuffer*           render_data_buf);
    };
} // namespace ZEngine::Rendering::Scenes
```

`Build` implementation sketch:

```cpp
void DrawCommandBuffer::Build(
    const Rendering::Scenes::RenderScenePtr& scene,
    const Rendering::Cameras::CameraPtr&     camera,
    uint32_t                                 pipeline_id)
{
    OpaqueCount      = 0;
    TransparentCount = 0;
    Opaque.clear();
    Transparent.clear();

    const Core::Maths::Mat4f& view      = camera->GetView();
    const float               far_plane = camera->Settings.FarPlane;

    for (const auto& [node_id, alloc] : scene->NodeSubMeshesAllocations)
    {
        // Retrieve the material to determine translucency bucket.
        // MaterialId indexes into the asset manager's GPUMeshMaterials array.
        // For the sort key we only need the bucket; material_id is used for batching.
        uint32_t material_id  = alloc.MaterialId & 0xFFF;
        uint32_t transform_id = alloc.TransformId;

        // Compute view-space depth from world-space origin.
        const Core::Maths::Mat4f& world = scene->GlobalTransforms[transform_id];
        Core::Maths::Vec4f world_origin(world[3][0], world[3][1], world[3][2], 1.0f);
        Core::Maths::Vec4f view_pos = view * world_origin;

        float norm = (-view_pos.z) / far_plane;
        norm       = norm < 0.0f ? 0.0f : (norm > 1.0f ? 1.0f : norm);
        uint32_t depth_q = static_cast<uint32_t>(norm * 0xFFFFFFu);

        // Determine translucency. Without a live material lookup here, the bucket
        // is deduced from SubMeshAllocation flags. A future pass-through from the
        // asset manager's GPUMeshMaterials should feed Factors.x and Factors.z.
        // For now, treat all draws as opaque (translucency = 0).
        // The full implementation requires reading MeshMaterial::Factors per draw.
        uint32_t translucency = 0; // replaced with material lookup in full impl
        uint32_t depth_key    = (translucency == 2)
                                    ? ((~depth_q) & 0xFFFFFFu)
                                    : depth_q;

        DrawCommand cmd = {};
        cmd.SortKey = SortKey::Pack(translucency, 0, pipeline_id, material_id, depth_key);

        cmd.Data.TransformIndex = transform_id;
        cmd.Data.MaterialIndex  = alloc.MaterialId;
        cmd.Data.VertexOffset   = alloc.VertexOffset;
        cmd.Data.IndexOffset    = alloc.IndexOffset;
        cmd.Data.VertexCount    = alloc.VertexCount;
        cmd.Data.IndexCount     = alloc.IndexCount;

        cmd.GpuCmd.vertexCount   = alloc.IndexCount;
        cmd.GpuCmd.instanceCount = alloc.InstanceCount;
        cmd.GpuCmd.firstVertex   = 0;
        cmd.GpuCmd.firstInstance = 0; // assigned by Flush

        if (translucency == 0 || translucency == 1)
        {
            Opaque.push(cmd);
            ++OpaqueCount;
        }
        else
        {
            Transparent.push(cmd);
            ++TransparentCount;
        }
    }
}
```

`Flush` implementation sketch:

```cpp
void DrawCommandBuffer::Flush(
    uint8_t                    frame_index,
    uint8_t                    thread_index,
    Hardwares::IndirectBuffer* indirect_buf,
    Hardwares::StorageBuffer*  render_data_buf)
{
    auto scratch    = ZGetScratch(Arena);

    uint32_t total  = OpaqueCount + TransparentCount;
    auto* gpu_cmds  = static_cast<VkDrawIndirectCommand*>(
        scratch.Arena->Allocate(total * sizeof(VkDrawIndirectCommand),
                                alignof(VkDrawIndirectCommand)));
    auto* draw_data = static_cast<DrawData*>(
        scratch.Arena->Allocate(total * sizeof(DrawData), alignof(DrawData)));

    uint32_t slot = 0;

    // Opaque draws occupy slots [0, OpaqueCount).
    for (uint32_t i = 0; i < OpaqueCount; ++i, ++slot)
    {
        DrawCommand& cmd          = Opaque[i];
        cmd.GpuCmd.firstInstance  = slot;
        gpu_cmds[slot]            = cmd.GpuCmd;
        draw_data[slot]           = cmd.Data;
    }

    // Transparent draws occupy slots [OpaqueCount, OpaqueCount + TransparentCount).
    for (uint32_t i = 0; i < TransparentCount; ++i, ++slot)
    {
        DrawCommand& cmd          = Transparent[i];
        cmd.GpuCmd.firstInstance  = slot;
        gpu_cmds[slot]            = cmd.GpuCmd;
        draw_data[slot]           = cmd.Data;
    }

    indirect_buf->Write(
        frame_index, thread_index,
        Core::Containers::ArrayView<VkDrawIndirectCommand>{gpu_cmds, total});

    // see RRM::UpdateBuffer for the actual implementation
    render_data_buf->Write(frame_index, thread_index, draw_data, total * sizeof(DrawData));

    ZReleaseScratch(scratch);
}
```

---

## 8. Dirty Flag Integration

The existing dirty flags in `RenderScene` are:

```cpp
std::atomic_bool MeshAllocationDirty[3]  = {false, false, false};
std::atomic_bool TransformBufferDirty[3] = {false, false, false};
```

Both trigger geometry and transform uploads. Neither captures the case where the camera has
moved but the scene geometry is static. Camera movement changes view-space depth for every
draw, invalidating all sort keys without changing any mesh or transform data.

Add a third flag to `RenderScene`:

```cpp
// GraphicScene.h — inside struct RenderScene
std::atomic_bool DrawCommandsDirty[3] = {false, false, false};
```

`DrawCommandsDirty[fi]` is set to `true` by any caller that changes data affecting sort order:

1. **Mesh allocation change**: wherever `MeshAllocationDirty[fi]` is set to `true`, also set
   `DrawCommandsDirty[fi]` to `true`. The two flags track different work (geometry upload vs
   sort rebuild) and may diverge in future; they are set independently, not aliased.

2. **Transform change**: wherever `TransformBufferDirty[fi]` is set to `true`, also set
   `DrawCommandsDirty[fi]` to `true`. Object movement changes view-space depth.

3. **Camera movement**: `AppRenderPipeline` stores one `Mat4f PreviousView[3]` array. At the
   start of `RenderScene`, before the dirty check:

```cpp
// AppRenderPipeline.cpp — inside RenderScene, before the existing if-block
const Core::Maths::Mat4f& current_view = camera->GetView();
if (current_view != PreviousView[frame_index])
{
    scene->DrawCommandsDirty[frame_index].store(true, std::memory_order_release);
    PreviousView[frame_index] = current_view;
}
```

`DrawCommandBuffer::Build` is called only when `DrawCommandsDirty[fi]` is true. When neither
the scene nor the camera has changed for a given frame slot, the sorted buffers from the
previous cycle for that slot are still valid and can be submitted without a rebuild. This
matches the existing `MeshAllocationDirty` / `TransformBufferDirty` pattern in
`AppRenderPipeline::RenderScene` and described in `rendering-flow.md` §2 step [4].

The rebuild sequence in `AppRenderPipeline::RenderScene` after the existing dirty uploads:

```cpp
if (scene->DrawCommandsDirty[frame_index].exchange(false, std::memory_order_acq_rel))
{
    auto& dcb = DrawCmdBuffers[frame_index];

    // pipeline_id 0 is a placeholder until pipeline handle integers are assigned
    // per render-graph-redesign.md §4.
    dcb.Build(scene, camera, /*pipeline_id=*/0);
    dcb.Sort(/*scratch_arena=*/&LocalArena);
    dcb.Flush(
        frame_index,
        RenderMainThreadIndex,
        indirect_buffer,
        rd_buffer);
}
```

`DrawCmdBuffers` is a member of `AppRenderPipeline`:

```cpp
// AppRenderPipeline.h
Rendering::Scenes::DrawCommandBuffer DrawCmdBuffers[3] = {};
```

Each `DrawCommandBuffer` is initialised in `AppRenderPipeline::Initialize`:

```cpp
for (int i = 0; i < Device->SwapchainPtr->BufferredFrameCount; ++i)
{
    DrawCmdBuffers[i].Init(&LocalArena, /*capacity_hint=*/1024);
}
```

---

## 9. Integration with DepthPrePass and GbufferPass

Both passes need the per-frame `DrawCommandBuffer` to know the opaque and transparent counts
at `Execute` time. The counts are stored as `OpaqueCount` and `TransparentCount` on the struct,
which is accessible through `AppRenderPipeline`. Since `Execute` receives `SceneDataPtr`, the
cleanest path is to store the per-frame counts in `SceneData` alongside the buffer handles.

Add two arrays to `SceneData`:

```cpp
// GraphicScene.h — inside struct SceneData
uint32_t OpaqueDrawCount[3]      = {0, 0, 0};
uint32_t TransparentDrawCount[3] = {0, 0, 0};
```

`Flush` writes these after it writes the GPU buffers:

```cpp
scene_data->OpaqueDrawCount[frame_index]      = OpaqueCount;
scene_data->TransparentDrawCount[frame_index] = TransparentCount;
```

Updated `DepthPrePass::Execute`:

```cpp
void DepthPrePass::Execute(
    Hardwares::VulkanDevicePtr const         device,
    RenderGraphResourceInspectorPtr          res_inspector,
    Rendering::Scenes::SceneDataPtr const    scene,
    RenderPasses::RenderPass* const          pass,
    Buffers::FramebufferVNext* const         framebuffer,
    Hardwares::CommandBufferPtr const        command_buffer)
{
    if (!scene || !scene->IndirectBufferHandle) { return; }

    const uint32_t fi             = device->SwapchainPtr->CurrentFrame->Index;
    const uint32_t opaque_count   = scene->OpaqueDrawCount[fi];
    if (opaque_count == 0) { return; }

    auto indirect_buffer = device->IndirectBufferSetManager.Access(scene->IndirectBufferHandle);
    auto* buf            = indirect_buffer->At(fi);

    command_buffer->BeginRenderPass(pass, framebuffer->Handle, false);
    {
        uint32_t w = pass->GetRenderAreaWidth();
        uint32_t h = pass->GetRenderAreaHeight();
        command_buffer->SetViewport(w, h);
        command_buffer->SetScissor(w, h);
    }
    command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
    command_buffer->BindDescriptorSets(fi);

    // Opaque range only. DepthPrePass does not submit transparent draws.
    command_buffer->DrawIndirectRange(*buf, /*first_command=*/0, opaque_count);

    command_buffer->EndRenderPass();
}
```

Updated `GbufferPass::Execute`:

```cpp
void GbufferPass::Execute(
    Hardwares::VulkanDevicePtr const         device,
    RenderGraphResourceInspectorPtr          res_inspector,
    Rendering::Scenes::SceneDataPtr const    scene,
    RenderPasses::RenderPass* const          pass,
    Buffers::FramebufferVNext* const         framebuffer,
    Hardwares::CommandBufferPtr const        command_buffer)
{
    CHECK_AND_ESCAPE_NULL(scene)
    CHECK_AND_ESCAPE_NULL(scene->IndirectBufferHandle)

    const uint32_t fi               = device->SwapchainPtr->CurrentFrame->Index;
    const uint32_t opaque_count     = scene->OpaqueDrawCount[fi];
    const uint32_t transparent_count = scene->TransparentDrawCount[fi];
    if (opaque_count == 0 && transparent_count == 0) { return; }

    auto indirect_buffer = device->IndirectBufferSetManager.Access(scene->IndirectBufferHandle);
    auto* buf            = indirect_buffer->At(fi);

    command_buffer->BeginRenderPass(pass, framebuffer->Handle, false);
    {
        uint32_t w = pass->GetRenderAreaWidth();
        uint32_t h = pass->GetRenderAreaHeight();
        command_buffer->SetViewport(w, h);
        command_buffer->SetScissor(w, h);
    }
    command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
    command_buffer->BindDescriptorSets(fi);

    // Opaque range: slots [0, opaque_count). Sorted front-to-back.
    if (opaque_count > 0)
    {
        command_buffer->DrawIndirectRange(*buf, 0, opaque_count);
    }

    // Transparent range: slots [opaque_count, opaque_count + transparent_count).
    // Sorted back-to-front. The GBuffer pipeline must have depth write disabled and
    // blending enabled for this range; a pipeline variant switch precedes this call
    // once per-draw pipeline state is supported (render-graph-redesign.md §7).
    if (transparent_count > 0)
    {
        command_buffer->DrawIndirectRange(*buf, opaque_count, transparent_count);
    }

    command_buffer->EndRenderPass();
}
```

---

## 10. GPU-Side DrawData Layout

`RenderDataBufferHandle` holds a flat array of `DrawData` records in sorted submission order.
The array is indexed by `gl_InstanceIndex`, which Vulkan sets equal to `VkDrawIndirectCommand::firstInstance`
for each draw. After `Flush`, slot `i` in the GPU array corresponds to the `i`-th sorted draw
command.

GLSL storage buffer definition (shared between `depth_prepass_scene` and `g_buffer` shaders):

```glsl
// shaders/include/draw_data.glsl

struct DrawData {
    uint TransformIndex;
    uint MaterialIndex;
    uint VertexOffset;
    uint IndexOffset;
    uint VertexCount;
    uint IndexCount;
};

layout(set = 0, binding = 2, std430) readonly buffer DrawDataSB {
    DrawData draws[];
} draw_data_buf;
```

The binding number matches the existing `DrawDataSB` name used in `DepthPrePass::Compile` and
`GbufferPass::Compile` via `(*output_pass)->SetInput("DrawDataSB", scene->RenderDataBufferHandle)`.
The struct layout must be `std430` with no padding between fields; six consecutive `uint` fields
with no vec3/vec4 alignment issues satisfy this constraint.

Vertex shader usage:

```glsl
// shaders/depth_prepass_scene.vert (illustrative excerpt)

layout(set = 0, binding = 0) uniform UBCamera {
    mat4 View;
    mat4 Projection;
    vec3 CameraPosition;
} ub_camera;

layout(set = 0, binding = 1, std430) readonly buffer VertexSB {
    float data[];
} vertex_buf;

layout(set = 0, binding = 2, std430) readonly buffer DrawDataSB {
    DrawData draws[];
} draw_data_buf;

layout(set = 0, binding = 3, std430) readonly buffer TransformSB {
    mat4 transforms[];
} transform_buf;

void main()
{
    DrawData draw      = draw_data_buf.draws[gl_InstanceIndex];
    mat4     model     = transform_buf.transforms[draw.TransformIndex];

    // Vertices are stored as interleaved floats; stride depends on vertex format.
    // VertexOffset is a float-element offset into vertex_buf.data[].
    uint   base        = draw.VertexOffset + gl_VertexIndex * VERTEX_STRIDE_FLOATS;
    vec3   position    = vec3(vertex_buf.data[base],
                              vertex_buf.data[base + 1],
                              vertex_buf.data[base + 2]);

    gl_Position = ub_camera.Projection * ub_camera.View * model * vec4(position, 1.0);
}
```

The G-buffer vertex shader additionally reads `draw.MaterialIndex` to fetch the per-draw
material from `MatSB` (bound as `scene->MaterialBufferHandle`):

```glsl
// shaders/g_buffer.vert (additional line after DrawData fetch)
uint material_idx = draw_data_buf.draws[gl_InstanceIndex].MaterialIndex;
// passed as flat varying to fragment shader:
// flat out uint v_MaterialIndex;
// v_MaterialIndex = material_idx;
```

---

## 11. File Layout and Deliverables

### New files

- [ ] `ZEngine/ZEngine/Rendering/Scenes/DrawCommandBuffer.h` — `DrawData` struct (moved from
  `GraphicScene.h` `#if 0`), `SortKey` namespace, `DrawCommand` struct, `DrawCommandBuffer`
  struct declaration
- [ ] `ZEngine/ZEngine/Rendering/Scenes/DrawCommandBuffer.cpp` — `RadixSort64` implementation,
  `DrawCommandBuffer::Init`, `Build`, `Sort`, `Flush` implementations
- [ ] `ZEngine/shaders/include/draw_data.glsl` — shared GLSL `DrawData` struct and
  `DrawDataSB` binding definition, included by both `depth_prepass_scene.vert` and
  `g_buffer.vert`

### Modified files

- [ ] `ZEngine/ZEngine/Rendering/Scenes/GraphicScene.h` — remove `DrawData` from the `#if 0`
  block; add `DrawCommandsDirty[3]` to `RenderScene`; add `OpaqueDrawCount[3]` and
  `TransparentDrawCount[3]` to `SceneData`; include `DrawCommandBuffer.h`
- [ ] `ZEngine/ZEngine/Hardwares/VulkanDevice.h` — add `DrawIndirectRange` declaration to
  `CommandBuffer`
- [ ] `ZEngine/ZEngine/Hardwares/VulkanDevice.cpp` — implement `CommandBuffer::DrawIndirectRange`
- [ ] `ZEngine/ZEngine/Applications/AppRenderPipeline.h` — add `DrawCommandBuffer DrawCmdBuffers[3]`
  and `Core::Maths::Mat4f PreviousView[3]` members
- [ ] `ZEngine/ZEngine/Applications/AppRenderPipeline.cpp` — `Initialize`: call
  `DrawCmdBuffers[i].Init`; `RenderScene`: add camera-movement dirty check, add
  `DrawCommandsDirty` branch that calls `Build` / `Sort` / `Flush`; propagate dirty flag
  set-sites for mesh and transform changes
- [ ] `ZEngine/ZEngine/Rendering/Renderers/RendererPasses.cpp` — replace single
  `DrawIndirect` calls in `DepthPrePass::Execute` and `GbufferPass::Execute` with the
  two-call `DrawIndirectRange` pattern from section 9
- [ ] `ZEngine/shaders/depth_prepass_scene.vert` — include `draw_data.glsl`; switch from
  current per-draw data source to `draw_data_buf.draws[gl_InstanceIndex]`
- [ ] `ZEngine/shaders/g_buffer.vert` — same; add `flat out uint v_MaterialIndex` output

### Tests

- [ ] **Opaque sort order**: build a `DrawCommandBuffer` from a synthetic `RenderScene`
  containing three opaque draws at view-space depths 10, 5, and 20. After `Build` and `Sort`,
  assert `Opaque[0].Data` corresponds to depth 5, `Opaque[1]` to depth 10, `Opaque[2]` to
  depth 20 (ascending depth key = front-to-back).

- [ ] **Transparent sort order**: build a `DrawCommandBuffer` from a synthetic scene containing
  three transparent draws at view-space depths 10, 5, and 20. After `Build` and `Sort`, assert
  `Transparent[0]` corresponds to depth 20, `Transparent[1]` to depth 10, `Transparent[2]` to
  depth 5 (NOT-encoded depth key ascending = back-to-front).

- [ ] **Build idempotency when not dirty**: call `Build` once on a scene, record
  `OpaqueCount` and the first sort key. Set `DrawCommandsDirty[fi] = false`. Confirm that
  the `DrawCommandsDirty` guard in `AppRenderPipeline::RenderScene` skips the `Build` call
  on the next frame tick; `OpaqueCount` and first key are unchanged.

- [ ] **Flush byte layout**: after `Flush`, read back the raw bytes written to the
  `IndirectBuffer` mock. Assert that bytes `[0, OpaqueCount * sizeof(VkDrawIndirectCommand))`
  match the opaque `GpuCmd` array in sorted order, and that `GpuCmd.firstInstance` for the
  i-th record equals `i`. Assert that bytes `[OpaqueCount * sizeof(VkDrawIndirectCommand), ...)`
  match the transparent commands with `firstInstance` values starting at `OpaqueCount`.

- [ ] **Pipeline batching**: build a scene with six draws — two pairs sharing the same pipeline
  ID and two singletons — all opaque at the same depth. Assert that after `Sort`, the two draws
  with matching pipeline ID are adjacent in `Opaque`.

- [ ] **Camera-movement dirty**: construct a `DrawCommandBuffer`, call `Build`, confirm
  `DrawCommandsDirty[fi]` is `false`. Move the camera (change `PreviousView[fi]` to differ from
  `camera->GetView()`). Confirm that the dirty check in `AppRenderPipeline::RenderScene` sets
  `DrawCommandsDirty[fi]` to `true` and triggers a `Build` on the next call.
