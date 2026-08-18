# Render Graph Redesign — Production-Grade Barrier Insertion and Memory Aliasing

**Relates to:** `gpu-allocator-rearchitecture.md`, `per-frame-upload-heap.md`, `render-graph-integration.md`
**Replaces:** `RenderGraph.h/.cpp`, `IRenderGraphCallbackPass`, `RenderGraphNode`, `RenderGraphResourceBuilder`, `RenderGraphResourceInspector`
**Scope:** Full redesign of the render graph compile and execute paths to produce automatic pipeline barriers and alias transient render target memory.

---

## 1. What Is Wrong With the Current Design

Reading `RenderGraph.cpp` directly:

**Barrier insertion is manual and wrong.** `Execute()` emits one barrier per input texture
(always `COLOR_ATTACHMENT_OUTPUT -> SHADER_READ_ONLY`) and one barrier per output attachment
(always `TOP_OF_PIPE -> COLOR_ATTACHMENT / DEPTH_STENCIL`). The source stage is hardcoded
regardless of what the previous pass actually was. This produces over-synchronisation every
frame — every pass stalls the full pipeline before it starts, even when the previous pass was
a compute dispatch or a transfer, not a color attachment write.

**No transient resource lifetime tracking.** `Compile()` calls `Device->CreateTexture()` once
per attachment resource with no record of when that resource is first written or last read.
G-buffer normals at 4K (`R16G16B16A16_SFLOAT`) = 134 MB. That memory is allocated, held for
the entire frame, and freed at graph teardown — even though the lighting pass is the last
consumer and the resource is dead for the remaining 60%+ of the frame.

**`UnorderedHashMap` on the hot path.** `Execute()` calls `NodeMap[node_name]` and
`ResourceMap[input.Name]` inside the per-pass loop. Both are string-keyed hash map lookups.
At 25 passes with 3-5 resources each that is 100+ hash lookups per frame on the critical path.

**`Resize()` leaks.** Line 342: `Device->GlobalTextures.Create()` + `Device->GlobalTextures.Update()`
copies the old texture descriptor into a temp slot, then enqueues the temp for disposal. The
resize path allocates two texture slots per resource per resize event. Rapid viewport dragging
creates a leak storm. The deferred free queue also never receives the VkImage memory — only
the texture slot is enqueued.

**`Compile()` rebuilds framebuffers unconditionally** even if the render area has not changed.
`Resize()` also rebuilds all framebuffers for every node regardless of whether that node's
outputs changed size.

---

## 2. Design Goals

1. Automatic pipeline barriers — no pass implementation ever calls `vkCmdPipelineBarrier` directly.
2. Transient attachment aliasing — non-overlapping transient resources share physical memory via VMA alias pools.
3. `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT` + `VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED` for any
   attachment consumed only within a single render pass (tile memory on Mali/Adreno/Apple GPU).
4. Execute loop is array-indexed, not hash-keyed — zero string hashing on the hot path.
5. Barrier batching — collect all barriers needed before a pass into one `vkCmdPipelineBarrier` call.
6. Keep the existing `IRenderGraphCallbackPass` interface intact. Existing pass implementations
   (~~UploadPass~~ — removed; SkyboxPass and GridPass now use global VB/IB via RRM::RegisterBuiltinGeometry, `BasePass`, `DepthPrePass`, `SkyboxPass`, `GridPass`) require no changes.

---

## 3. Core Data Model

### 3.1 ResourceHandle — typed index, no string on hot path

```cpp
struct RGResourceHandle {
    uint32_t Index   = UINT32_MAX;
    uint32_t Version = 0;           // incremented on each write to detect read-after-write

    bool Valid() const { return Index != UINT32_MAX; }
};
```

### 3.2 RGResource — flat struct, all state in one place

```cpp
enum class RGResourceKind : uint8_t {
    Attachment,   // VkImage written as render target / depth target
    Texture,      // VkImage read-only (external or imported)
    Buffer,       // VkBuffer (external — per-frame heap, storage buffer set, etc.)
};

// Access describes how a pass uses a resource.
// The compiler uses this to derive src/dst stage + access masks for barriers.
enum class RGAccess : uint8_t {
    None,
    ColorWrite,           // vkCmdBeginRenderPass as color attachment
    DepthWrite,           // vkCmdBeginRenderPass as depth attachment
    DepthRead,            // input attachment, read-only depth
    ShaderRead,           // sampler2D / combined image sampler
    ShaderReadWrite,      // storage image read + write (compute)
    TransferRead,
    TransferWrite,
    Present,
};

struct RGResourceState {
    VkPipelineStageFlags Stage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags        Access = 0;
    VkImageLayout        Layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct RGResource {
    cstring              Name           = nullptr;
    RGResourceKind       Kind           = RGResourceKind::Attachment;
    bool                 External       = false;    // imported, not owned by the graph

    // Transient physical backing — null for external resources.
    // Set by the aliasing allocator during Compile().
    Textures::TextureHandle  TextureHandle  = {};
    RGResourceState          CurrentState   = {};

    // Lifetime — filled by the lifetime analysis pass in Compile().
    uint32_t             FirstPassIndex = UINT32_MAX;
    uint32_t             LastPassIndex  = 0;

    // For aliasing: physical memory can be reused after LastPassIndex.
    bool                 Transient      = true;

    // Spec used to (re-)allocate the physical backing on resize.
    Specifications::TextureSpecification Spec = {};
};
```

### 3.3 RGPassResource — a pass's declaration of one resource use

```cpp
struct RGPassResource {
    RGResourceHandle Handle = {};
    RGAccess         Access = RGAccess::None;
    cstring          BindingKey = nullptr;  // descriptor slot name, for textures
};
```

### 3.4 RGPass — compiled representation of one pass

```cpp
struct RGPass {
    cstring                               Name       = nullptr;
    bool                                  Enabled    = true;
    IRenderGraphCallbackPass*             Callback   = nullptr;
    RenderPasses::RenderPass*             Handle     = nullptr;
    Buffers::FramebufferVNext*            Framebuffer = nullptr;

    // Declared by the pass in Setup() via RGBuilder.
    Core::Containers::Array<RGPassResource> Reads;   // inputs
    Core::Containers::Array<RGPassResource> Writes;  // outputs

    // Filled by Compile() — the full barrier batch to emit before Execute().
    Core::Containers::Array<VkImageMemoryBarrier>  ImageBarriers;
    VkPipelineStageFlags                           BarrierSrcStage = 0;
    VkPipelineStageFlags                           BarrierDstStage = 0;
};
```

### 3.5 RenderGraph — the new top-level struct

```cpp
struct RenderGraph {
    Hardwares::VulkanDevicePtr   Device      = nullptr;
    Scenes::SceneDataPtr         SceneData   = nullptr;

    // Flat arrays — indexed by compile-time order.
    // No hash map on the Execute hot path.
    Core::Containers::Array<RGPass>     Passes;
    Core::Containers::Array<RGResource> Resources;

    // String -> index map used only during Setup/Compile, not Execute.
    Core::Containers::UnorderedHashMap<cstring, uint32_t> ResourceIndex;
    Core::Containers::UnorderedHashMap<cstring, uint32_t> PassIndex;

    // Builder and inspector exposed to pass Setup() implementations.
    // Same API surface as today so existing pass code compiles unchanged.
    RGBuilder*    Builder    = nullptr;
    RGInspector*  Inspector  = nullptr;

    // Transient resource pool used by the aliasing allocator.
    RGTransientPool TransientPool;

    void Initialize(Hardwares::VulkanDevicePtr device, Scenes::SceneDataPtr scene);
    void AddCallbackPass(cstring name, IRenderGraphCallbackPass* cb, bool enabled = true);
    void Setup();
    void Compile();
    void Execute(Hardwares::CommandBufferPtr cb);
    void Resize(uint32_t width, uint32_t height);
    void Dispose();

    RGResourceHandle ImportRenderTarget(cstring name, Textures::TextureHandle handle);
    RGResourceHandle CreateTransientAttachment(cstring name, const Specifications::TextureSpecification& spec);
};
```

---

## 4. Lifetime Analysis

This runs in `Compile()` after all passes have called `Setup()` and declared their reads and writes.

```
for i in 0..Passes.size():
    pass = Passes[i]
    for each write in pass.Writes:
        resource = Resources[write.Handle.Index]
        resource.FirstPassIndex = min(resource.FirstPassIndex, i)
    for each read in pass.Reads:
        resource = Resources[read.Handle.Index]
        resource.LastPassIndex  = max(resource.LastPassIndex,  i)
    // A pass that both reads and writes the same resource (read-modify-write)
    // contributes to both.
    for each write in pass.Writes where resource also in pass.Reads:
        resource.LastPassIndex  = max(resource.LastPassIndex,  i)
```

After this loop every transient resource knows exactly when it is born (first write) and
when it dies (last read).

---

## 5. Transient Aliasing Allocator

### 5.1 RGTransientPool

```cpp
struct RGTransientSlot {
    Textures::TextureHandle Handle         = {};
    Specifications::TextureSpecification Spec = {};
    uint32_t                FreeAfterPass  = 0;  // resource is dead after this pass index
};

struct RGTransientPool {
    // Arena-backed flat list of all allocated slots.
    Core::Containers::Array<RGTransientSlot> Slots;

    // Try to find an existing slot compatible with `spec` that is free
    // (FreeAfterPass < firstPassIndex). Returns invalid handle if none found.
    Textures::TextureHandle TryAlias(
        const Specifications::TextureSpecification& spec,
        uint32_t firstPassIndex);

    // Register a newly allocated slot into the pool.
    void Register(Textures::TextureHandle handle,
                  const Specifications::TextureSpecification& spec,
                  uint32_t lastPassIndex);

    // Update the free-after index when a slot is reused.
    void MarkInUse(Textures::TextureHandle handle, uint32_t lastPassIndex);

    void Clear();
};
```

### 5.2 Alias compatibility

Two texture specs are alias-compatible when:
- Same `VkFormat`
- Same width and height (or the slot's dimensions are >= the requested dimensions — exact match preferred)
- Same or superset of `VkImageUsageFlags` — the slot must support at least everything the new resource needs
- Same `layerCount` and `mipLevels`

This is a conservative check. Two resources with non-overlapping lifetimes and compatible specs
share one `VkImage` allocation. The Vulkan spec allows this as long as the aliased image is
re-initialized (transition from `VK_IMAGE_LAYOUT_UNDEFINED`) before use — which the barrier
system does automatically on every first write.

### 5.3 Allocation path in Compile()

```
for each transient resource r in dependency order (FirstPassIndex ascending):

    aliased = TransientPool.TryAlias(r.Spec, r.FirstPassIndex)

    if aliased.Valid():
        r.TextureHandle = aliased
        TransientPool.MarkInUse(aliased, r.LastPassIndex)
    else:
        // Determine usage flags from all accesses declared against this resource.
        usage = DeriveUsageFlags(r)  // see Section 5.4
        r.Spec.UsageFlags = usage
        r.TextureHandle = Device->CreateTexture(r.Spec)
        TransientPool.Register(r.TextureHandle, r.Spec, r.LastPassIndex)
```

### 5.4 Deriving `VkImageUsageFlags`

Walk all passes, collect every `RGAccess` declared against resource `r`:

```cpp
VkImageUsageFlags DeriveUsageFlags(const RGResource& r, const Array<RGPass>& passes)
{
    VkImageUsageFlags flags = 0;
    bool wroteAsColor = false, wroteAsDepth = false;
    bool readAsShader = false;

    for (auto& pass : passes) {
        for (auto& w : pass.Writes) {
            if (w.Handle.Index != r.Index) continue;
            if (w.Access == RGAccess::ColorWrite) { flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; wroteAsColor = true; }
            if (w.Access == RGAccess::DepthWrite) { flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; wroteAsDepth = true; }
        }
        for (auto& rd : pass.Reads) {
            if (rd.Handle.Index != r.Index) continue;
            if (rd.Access == RGAccess::ShaderRead)      { flags |= VK_IMAGE_USAGE_SAMPLED_BIT; readAsShader = true; }
            if (rd.Access == RGAccess::ShaderReadWrite)   flags |= VK_IMAGE_USAGE_STORAGE_BIT;
            if (rd.Access == RGAccess::TransferRead)      flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            if (rd.Access == RGAccess::TransferWrite)     flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
    }

    // Transient attachment: written and read only as an attachment within a single pass
    // (first and last pass are the same). Never sampled by a shader.
    bool singlePassLifetime = (r.FirstPassIndex == r.LastPassIndex);
    if (singlePassLifetime && !readAsShader && (wroteAsColor || wroteAsDepth)) {
        flags |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        // Caller sets VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED for this resource.
        // On tiled GPUs the image never touches DRAM.
    }

    return flags;
}
```

The `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT` path covers:
- SSAO blur intermediates (R8, half-res, written and blurred in a single compute pass)
- Bloom downsample/upsample intermediates at each mip level
- Shadow resolve buffer (depth-only, not sampled after the cascade pass resolves)
- G-buffer normals/specular on hardware where the lighting pass uses input attachments (TBDR path)

---

## 6. Automatic Barrier Insertion

### 6.1 State tracking

Each `RGResource` carries a `CurrentState` (stage, access mask, image layout). The graph
initialises all states to `{TOP_OF_PIPE, 0, UNDEFINED}` at the start of Compile. States are
then advanced pass-by-pass during the barrier-building loop.

### 6.2 Access -> stage + mask table

```cpp
struct RGAccessInfo {
    VkPipelineStageFlags Stage;
    VkAccessFlags        Access;
    VkImageLayout        Layout;
};

constexpr RGAccessInfo kAccessTable[] = {
    // None
    { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,                0,                                          VK_IMAGE_LAYOUT_UNDEFINED                     },
    // ColorWrite
    { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL      },
    // DepthWrite
    { VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
      | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
      | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL                                                                                             },
    // DepthRead
    { VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
    // ShaderRead
    { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,            VK_ACCESS_SHADER_READ_BIT,                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL      },
    // ShaderReadWrite (compute)
    { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,             VK_ACCESS_SHADER_READ_BIT
                                                        | VK_ACCESS_SHADER_WRITE_BIT,                VK_IMAGE_LAYOUT_GENERAL                       },
    // TransferRead
    { VK_PIPELINE_STAGE_TRANSFER_BIT,                   VK_ACCESS_TRANSFER_READ_BIT,                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL          },
    // TransferWrite
    { VK_PIPELINE_STAGE_TRANSFER_BIT,                   VK_ACCESS_TRANSFER_WRITE_BIT,                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL          },
    // Present
    { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,             0,                                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR               },
};
```

### 6.3 Barrier build loop (runs once in Compile())

```cpp
for (uint32_t i = 0; i < Passes.size(); ++i) {
    RGPass& pass = Passes[i];

    VkPipelineStageFlags srcStage = 0;
    VkPipelineStageFlags dstStage = 0;

    // Collect all image barriers needed before this pass executes.
    for (auto& rd : pass.Reads) {
        RGResource& res  = Resources[rd.Handle.Index];
        RGAccessInfo dst = kAccessTable[(int)rd.Access];

        // Only emit a barrier if layout or access changes.
        if (res.CurrentState.Layout == dst.Layout &&
            res.CurrentState.Access == dst.Access)
            continue;

        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = res.CurrentState.Layout;
        barrier.newLayout           = dst.Layout;
        barrier.srcAccessMask       = res.CurrentState.Access;
        barrier.dstAccessMask       = dst.Access;
        // Aliased resources always transition from UNDEFINED on first use,
        // discarding stale contents from the previous alias owner.
        if (res.FirstPassIndex == i)
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.image               = GetVkImage(res.TextureHandle);
        barrier.subresourceRange    = FullSubresourceRange(res);

        pass.ImageBarriers.push(barrier);
        srcStage |= res.CurrentState.Stage;
        dstStage |= dst.Stage;

        res.CurrentState = { dst.Stage, dst.Access, dst.Layout };
    }

    for (auto& wr : pass.Writes) {
        RGResource& res  = Resources[wr.Handle.Index];
        RGAccessInfo dst = kAccessTable[(int)wr.Access];

        if (res.CurrentState.Layout == dst.Layout &&
            res.CurrentState.Access == dst.Access)
            continue;

        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = res.CurrentState.Layout;
        barrier.newLayout           = dst.Layout;
        barrier.srcAccessMask       = res.CurrentState.Access;
        barrier.dstAccessMask       = dst.Access;
        if (res.FirstPassIndex == i)
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.image               = GetVkImage(res.TextureHandle);
        barrier.subresourceRange    = FullSubresourceRange(res);

        pass.ImageBarriers.push(barrier);
        srcStage |= res.CurrentState.Stage;
        dstStage |= dst.Stage;

        res.CurrentState = { dst.Stage, dst.Access, dst.Layout };
    }

    // Fallback: if no barriers needed, stage masks remain zero.
    // Guard against the degenerate case (TOP_OF_PIPE | 0).
    if (srcStage == 0) srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    if (dstStage == 0) dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    pass.BarrierSrcStage = srcStage;
    pass.BarrierDstStage = dstStage;
}
```

The barrier arrays are arena-allocated once per Compile() call. Execute() just reads them.

### 6.4 Execute loop — barrier emission

```cpp
void RenderGraph::Execute(Hardwares::CommandBufferPtr cb)
{
    for (uint32_t i = 0; i < Passes.size(); ++i) {
        RGPass& pass = Passes[i];

        if (!pass.Enabled) continue;

        // Emit the pre-computed barrier batch — one vkCmdPipelineBarrier call.
        if (!pass.ImageBarriers.empty()) {
            vkCmdPipelineBarrier(
                cb->Handle,
                pass.BarrierSrcStage,
                pass.BarrierDstStage,
                0,
                0, nullptr,       // memory barriers
                0, nullptr,       // buffer barriers
                pass.ImageBarriers.size(),
                pass.ImageBarriers.data());
        }

        pass.Callback->Execute(
            Device, Inspector, SceneData,
            pass.Handle, pass.Framebuffer, cb);
    }
}
```

No hash lookups. No per-pass barrier computation. One `vkCmdPipelineBarrier` per pass,
batching all image transitions for that pass. Total: O(N) where N = pass count.

---

## 7. RGBuilder API (replaces RenderGraphResourceBuilder)

The `RGBuilder` is the same object as today's `RenderGraphResourceBuilder` from the pass's
perspective, but returns `RGResourceHandle` instead of `RenderGraphResource&`. Pass
implementations call it identically from `Setup()`.

```cpp
struct RGBuilder {
    RenderGraph* Graph = nullptr;

    // Declare that the current pass writes a transient color attachment.
    // If the resource does not yet exist it is created.
    RGResourceHandle WriteColorAttachment(cstring name,
                                          const Specifications::TextureSpecification& spec);

    // Declare that the current pass writes a transient depth attachment.
    RGResourceHandle WriteDepthAttachment(cstring name,
                                          const Specifications::TextureSpecification& spec);

    // Declare that the current pass reads a resource as a shader texture.
    RGResourceHandle ReadTexture(cstring name, cstring binding_key);

    // Declare that the current pass reads a resource as a depth input attachment (read-only).
    RGResourceHandle ReadDepth(cstring name);

    // Import an externally-owned resource into the graph.
    RGResourceHandle ImportRenderTarget(cstring name, Textures::TextureHandle handle);
    RGResourceHandle ImportTexture(cstring name, Textures::TextureHandle handle);

    // Buffer resources (vertex, index, storage, indirect) — identical to today.
    RGResourceHandle AttachBuffer(cstring name, const Hardwares::StorageBufferSetHandle&);
    RGResourceHandle AttachBuffer(cstring name, const Hardwares::VertexBufferSetHandle&);
    RGResourceHandle AttachBuffer(cstring name, const Hardwares::IndexBufferSetHandle&);
    RGResourceHandle AttachBuffer(cstring name, const Hardwares::IndirectBufferSetHandle&);
    RGResourceHandle AttachBuffer(cstring name, const Hardwares::UniformBufferSetHandle&);
};
```

Each `Write*` / `Read*` call:
1. Creates or looks up the `RGResource` entry by name.
2. Records the access type on the current pass's `Reads` or `Writes` array.
3. Records `ProducerPassIndex` on the resource (for topology).
4. Returns the `RGResourceHandle` — the pass can store it for use in `Execute()`.

---

## 8. Topology — Replacing the Hand-Written DFS

The current topological sort walks `EdgeNodes` sets attached to each node. EdgeNodes are
populated during Compile() from the resource producer map.

The new design uses the same DFS but operates on indices, not strings:

```
// Build adjacency from resource producer/consumer declarations.
for each pass i:
    for each read r in pass.Reads:
        res = Resources[r.Handle.Index]
        if res.ProducerPassIndex is valid and != i:
            // pass[res.ProducerPassIndex] must execute before pass[i]
            Adjacency[res.ProducerPassIndex].insert(i)

// DFS post-order topological sort on integer pass indices.
// Cycle detection: same logic as current, but using index sets instead of string sets.
// Result: SortedPassIndices[] — the execution order.
```

The sorted index array is filled once in Compile() and read back by Execute(). No string
comparisons anywhere in the sort or the execution loop.

---

## 9. Resize Path

The current resize path (lines 339-354 of RenderGraph.cpp) allocates two new texture slots
per resource and leaks the VkImage. The new path:

```cpp
void RenderGraph::Resize(uint32_t width, uint32_t height)
{
    // Rebuild the transient pool with new dimensions.
    TransientPool.Clear();

    for (auto& res : Resources) {
        if (res.External || !res.Transient) continue;

        // Enqueue the old VkImage for deferred destruction.
        if (res.TextureHandle.Valid())
            Device->DeferFree({ .Kind = DeferredFreeEntry::Image,
                                .Image = res.TextureHandle });

        res.TextureHandle = {};
        res.Spec.Width    = width;
        res.Spec.Height   = height;
        res.CurrentState  = { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED };
    }

    // Re-run the aliasing allocator with the new dimensions.
    // This re-allocates only transient resources, in FirstPassIndex order.
    AllocateTransientResources();

    // Rebuild framebuffers only for passes whose output dimensions changed.
    for (auto& pass : Passes) {
        if (OutputsResized(pass))
            RebuildFramebuffer(pass, width, height);
    }

    // Notify the swapchain/bindless system about the new frame color RT.
    for (auto& res : Resources) {
        if (res.Name == RendererResourceName::FrameColorRenderTargetName)
            Device->TextureHandleToUpdates.Enqueue(res.TextureHandle);
    }
}
```

`Device->DeferFree` is the timeline-semaphore-gated path from `gpu-allocator-rearchitecture.md`.
`vmaDestroyImage` is called by `DeferredFreeQueue::Drain` in `TickMemory` — the resize path
never calls it directly.

---

## 10. Compute Pass Support

The redesigned graph supports compute passes with no special registration path. A compute pass
calls `AddCallbackPass` identically to a graphics pass. The barrier system handles it automatically
via `RGAccess::ShaderReadWrite` which maps to `VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT` in
`kAccessTable`. The only structural differences are in `Compile()` and in how `RenderPass`
creates its pipeline.

### 10.1 Framebuffer skip

`Compile()` creates a `FramebufferVNext` for every pass after building pipelines. Compute passes
have no render pass object and no framebuffer — the guard is:

```cpp
for (auto& pass : Passes) {
    if (!pass.Handle) continue;
    if (pass.Handle->Specification.Type == RenderPassType::COMPUTE) continue;

    Specifications::FrameBufferSpecificationVNext fb_spec = {
        .Width         = pass.Handle->RenderAreaWidth,
        .Height        = pass.Handle->RenderAreaHeight,
        .RenderTargets = pass.Handle->RenderTargets,
        .Attachment    = pass.Handle->Attachment,
    };
    pass.Framebuffer = ZPushStructCtorArgs(Device->Arena, Buffers::FramebufferVNext, Device, fb_spec);
}
```

`pass.Framebuffer` remains null for compute passes. The `Execute()` loop passes it as-is to
`Callback->Execute` — compute pass implementations receive a null framebuffer and must not
call `BeginRenderPass`.

### 10.2 Compute pipeline creation in RenderPass

`RenderPass::Initialize` is split on `Specification.Type`:

```cpp
void RenderPass::Initialize(VulkanDevice* device, const RenderPassSpecification& spec)
{
    m_device      = device;
    Specification = spec;

    if (spec.Type == RenderPassType::COMPUTE) {
        // No attachment, no VkRenderPass object.
        ComputePipeline = ZPushStructCtorArgs(device->Arena, Pipelines::ComputePipeline);
        ComputePipeline->Initialize(device, spec.PipelineSpecification.ShaderSpecificationValue.Name);
        return;
    }

    if (spec.Type != RenderPassType::GRAPHIC) return;

    // ... existing graphics path unchanged ...
}

void RenderPass::Bake()
{
    if (Specification.Type == RenderPassType::COMPUTE) {
        ComputePipeline->Bake();  // calls vkCreateComputePipelines
        return;
    }
    if (Specification.Type != RenderPassType::GRAPHIC) return;
    Pipeline->Bake();
}
```

`RenderPass` gains a `ComputePipeline* ComputePipeline = nullptr` field alongside
`GraphicPipeline* Pipeline`. Exactly one is non-null depending on pass type.

### 10.3 Execute path for compute passes

A compute pass `Execute()` implementation follows this pattern:

```cpp
void SSAOComputePass::Execute(
    VulkanDevicePtr device, RGInspector* inspector, SceneDataPtr,
    RenderPass* pass, FramebufferVNext* /*null*/, CommandBufferPtr cb)
{
    VkCommandBuffer cmd = cb->GetHandle();

    // Bind the compute pipeline.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                      pass->ComputePipeline->Handle);

    // Bind descriptor sets (device global bindless set + pass-local set).
    // vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ...)

    // Push dispatch size as a push constant if the shader needs it.
    // vkCmdPushConstants(...)

    uint32_t gx = (m_width  + 7) / 8;
    uint32_t gy = (m_height + 7) / 8;
    vkCmdDispatch(cmd, gx, gy, 1);
}
```

`CommandBuffer::Dispatch` is a new method added in `compute-pipeline.md`:
```cpp
void CommandBuffer::Dispatch(uint32_t x, uint32_t y, uint32_t z) {
    vkCmdDispatch(m_command_buffer, x, y, z);
}
```

Barriers before the dispatch are pre-built by the graph's barrier loop in `Compile()` and
emitted automatically in `Execute()` — the pass implementation never calls
`vkCmdPipelineBarrier` directly.

---

## 11. Migration from the Current Design

The current pass implementations (~~UploadPass~~ — removed; SkyboxPass and GridPass now use global VB/IB via RRM::RegisterBuiltinGeometry, `BasePass`, `DepthPrePass`, `SkyboxPass`,
`GridPass`) call `res_builder->CreateBufferSet`, `CreateRenderTarget`, `CreateRenderPassNode`,
and `res_inspector->GetVertexBufferSet` etc. These APIs are preserved with identical
signatures on `RGBuilder` and `RGInspector`. Migration is mechanical:

| Current call | New call | Notes |
|---|---|---|
| `res_builder->CreateRenderTarget(name, spec)` | `builder->WriteColorAttachment(name, spec)` | Returns `RGResourceHandle` instead of `RenderGraphResource&` |
| `res_builder->AttachRenderTarget(name, handle)` | `builder->ImportRenderTarget(name, handle)` | Same |
| `res_builder->AttachTexture(name, handle)` | `builder->ImportTexture(name, handle)` | Same |
| `res_builder->CreateBufferSet(name, type)` | `builder->AttachBuffer(name, ...)` | Typed overloads |
| `res_builder->CreateRenderPassNode(creation)` | Removed — each `Write*/Read*` call registers the node implicitly | |
| `res_inspector->GetRenderTarget(name)` | `inspector->GetTextureHandle(handle)` | Handle-indexed, no string |
| `res_inspector->GetVertexBufferSet(name)` | `inspector->GetVertexBufferSet(handle)` | Handle-indexed |
| `NodeMap[name].Enabled = false` | `graph->SetPassEnabled(name, false)` | String lookup only at setup time |

The `IRenderGraphCallbackPass` interface signatures are unchanged. Existing `Setup`, `Compile`,
and `Execute` implementations compile without modification after the builder/inspector API swap.

---

## 12. Memory Budget Impact at 4K

Based on the `GpuMemoryDomain::RenderTarget` pool from `gpu-allocator-rearchitecture.md`
(172 MB budget). With aliasing and transient bit:

| Resource | Format | Size (4K) | Aliasable with | Lazy alloc? |
|---|---|---|---|---|
| FrameDepthRT | D32_SFLOAT | 33 MB | — | No (persists frame-to-frame) |
| FrameColorRT (HDR) | R16G16B16A16_SFLOAT | 134 MB | — | No (read by ImGui) |
| G-buffer normals | R16G16B16A16_SFLOAT | 134 MB | Bloom ping (lifetime gap) | No |
| G-buffer albedo | R8G8B8A8_UNORM | 33 MB | Bloom pong | No |
| SSAO occlusion | R8_UNORM, half-res | 8 MB | Any half-res R8 | Yes |
| Bloom threshold | R16G16B16A16_SFLOAT | 134 MB | G-buffer normals (dead) | No |
| Bloom ping/pong × 5 levels | R16G16B16A16_SFLOAT | ~42 MB total | Each other (alternating) | No |
| Shadow depth (per cascade) | D32_SFLOAT, 2K | 16 MB | Other shadow maps | Yes |

Without aliasing: ~534 MB transient RT memory.
With aliasing (normals aliased with bloom threshold, ping aliases pong per-level, lazy SSAO/shadow): ~205 MB.
Net saving: ~329 MB — comfortably within the 172 MB budget when the two persistent RTs (depth + HDR color) are subtracted (167 MB combined), leaving 5 MB for small temporaries.

If the 172 MB budget proves tight, increase `GpuBudget::RenderTarget` or use `VK_EXT_memory_budget`
to query device headroom dynamically at runtime.

---

## 13. Deliverables Checklist

### Core graph

- [ ] `ZEngine/Rendering/Renderers/RenderGraph.h` — `RGResourceHandle`, `RGResource`, `RGResourceKind`, `RGAccess`, `RGAccessInfo`, `RGResourceState`, `RGPassResource`, `RGPass`, `RenderGraph`; remove `IRenderGraphCallbackPass*` raw pointer from `RenderGraphNode` (it moves to `RGPass.Callback`)
- [ ] `ZEngine/Rendering/Renderers/RenderGraph.cpp` — `Initialize`, `Setup`, `Compile` (lifetime analysis + aliasing allocator + barrier build), `Execute` (index loop + pre-built barriers), `Resize` (DeferFree + re-allocate + partial framebuffer rebuild), `Dispose`
- [ ] Keep `IRenderGraphCallbackPass` interface unchanged — `Setup`, `Compile`, `Execute` virtual methods; existing pass structs compile without changes

### Builder and inspector

- [ ] `ZEngine/Rendering/Renderers/RGBuilder.h/.cpp` — `WriteColorAttachment`, `WriteDepthAttachment`, `ReadTexture`, `ReadDepth`, `ImportRenderTarget`, `ImportTexture`, `AttachBuffer` (typed overloads); each call registers read/write on current pass and returns `RGResourceHandle`
- [ ] `ZEngine/Rendering/Renderers/RGInspector.h/.cpp` — all `Get*` methods indexed by `RGResourceHandle`; string-keyed overloads preserved for pass `Execute()` callsites that use names

### Aliasing allocator

- [ ] `ZEngine/Rendering/Renderers/RGTransientPool.h/.cpp` — `TryAlias`, `Register`, `MarkInUse`, `Clear`; alias compatibility check: format + dimensions + usage superset + layer/mip count
- [ ] `DeriveUsageFlags(RGResource, passes)` free function — derives `VkImageUsageFlags` and sets `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT` for single-pass-lifetime resources
- [ ] VMA allocation path: transient bit resources use `VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED`; all others use `GpuMemoryDomain::RenderTarget` segregated pool

### Barrier system

- [ ] `kAccessTable` constexpr array — stage + access + layout for every `RGAccess` value
- [ ] Barrier build loop in `Compile()` — one `VkImageMemoryBarrier` per resource state transition, stored in `RGPass.ImageBarriers`; transitions from `UNDEFINED` for aliased/first-use resources
- [ ] Execute loop emits `vkCmdPipelineBarrier` once per pass from pre-built arrays; no per-frame barrier construction

### Topology

- [ ] Index-based DFS topological sort; integer adjacency sets (no string sets in sort); cycle detection preserved; result stored in `SortedPassIndices: Array<uint32_t>`

### Compute pass

- [ ] `ZEngine/Rendering/Renderers/RenderPasses/RenderPass.h/.cpp` — add `ComputePipeline* ComputePipeline = nullptr` field; split `Initialize` and `Bake` on `RenderPassType::COMPUTE`; compute path creates `ComputePipeline`, skips `Attachment` and `VkRenderPass`
- [ ] `Compile()` framebuffer loop — guard: skip `FramebufferVNext` creation for `RenderPassType::COMPUTE` passes
- [ ] `Execute()` loop — pass null `Framebuffer` to compute callbacks; no `BeginRenderPass` call
- [ ] See `compute-pipeline.md` for `ComputePipeline`, `CommandBuffer::Dispatch`, `ShaderType::COMPUTE`, and `IComputeCallbackPass` deliverables

### Migration

- [ ] ~~UploadPass~~ — removed; SkyboxPass and GridPass now use global VB/IB via RRM::RegisterBuiltinGeometry; `BasePass`, `DepthPrePass`, `SkyboxPass`, `GridPass` updated to use new `RGBuilder`/`RGInspector` API — builder call sites only; `Execute()` bodies unchanged
- [ ] `GraphicRenderer::Initialize` updated — `RGBuilder::ImportRenderTarget` for `FrameColorRenderTarget` and `FrameDepthRenderTarget`; remove `ResourceBuilder->AttachRenderTarget` calls
- [ ] `GbufferPass::Setup` and `LightingPass::Setup` updated to use `WriteColorAttachment` and `ReadTexture` — these are currently commented out; enable them as part of this migration

### Tests

- [ ] `tests/Rendering/RenderGraphTest.cpp`:
  - Test 1 — linear chain A -> B -> C produces correct barrier sequence (A COLOR_WRITE -> B SHADER_READ has `COLOR_ATTACHMENT_OUTPUT -> FRAGMENT_SHADER` barrier)
  - Test 2 — diamond dependency A -> {B, C} -> D; B and C have no barrier between them; D waits on both
  - Test 3 — aliasing: resources X (lives pass 0-1) and Y (lives pass 3-4) with identical specs share one `VkImage`; confirmed via `TextureHandle` equality
  - Test 4 — transient bit: resource live only in pass 2 gets `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`
  - Test 5 — resize: old handles enqueued in `DeferFree`, new handles allocated, state reset to UNDEFINED
  - All tests pass under AddressSanitizer and UBSanitizer

### Manual smoke test

- [ ] Profile with RenderDoc: open a capture, confirm that all image transitions between passes match the expected layouts (no validation layer errors, no redundant full-pipeline stalls)
- [ ] Profile with Tracy: `ZENGINE_PROFILE_SCOPE("RenderGraph::Execute")` should show linear per-pass cost with no hash lookup spikes
- [ ] Measure RT memory with `vmaCalculateStatistics` before and after; confirm transient pool reduces peak allocation by at least 40% vs the baseline at 1440p
