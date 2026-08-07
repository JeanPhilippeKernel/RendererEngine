# ZEngine — Render Graph Integration Guide

**Priority:** P0 — Post-processing, shadows, UI, text, particles, and animation skinning all depend on this
**Status:** Design — documents the existing RenderGraph and specifies integration patterns for all new passes
**Based on:** Existing `RenderGraph.h/.cpp` (live, production-quality)

---

## 1. What Already Exists

`RenderGraph` lives at `ZEngine/Rendering/Renderers/RenderGraph.h` and `RenderGraph.cpp`. It is
production code, not a stub. The following capabilities are fully implemented and in use today.

### 1.1 Topological Sort

`RenderGraph::Compile()` builds a directed acyclic graph (DAG) from resource producer/consumer
relationships and runs a DFS post-order topological sort to produce `SortedNodesMap` — an
`Array<cstring>` of pass names in dependency order. The sort detects cycles and logs an error
via `ZENGINE_CORE_ERROR` without crashing. `SortedNodesMap` is then traversed in-order for
Vulkan object creation (passes) and again in-order during `Execute()`.

The edge graph is built from resource declarations: if pass B lists resource `"hdr_color"` as
an input and the `ResourceMap` records `"hdr_color"` as produced by pass A, `Compile()` inserts
`B` into `A.EdgeNodes`. Pass A therefore precedes pass B in the sorted output.

### 1.2 Resource Declaration via ResourceBuilder

`RenderGraphResourceBuilder` is the write side. It populates `Graph->ResourceMap` and
`Graph->NodeMap` during the Setup phase. The following methods are implemented:

| Method | Type stored | External flag |
|---|---|---|
| `CreateRenderTarget(name, TextureSpec)` | `ATTACHMENT` | false — graph owns the texture |
| `CreateTexture(name, TextureSpec)` | `TEXTURE` | false — graph owns the texture |
| `CreateTexture(name, filename)` | `TEXTURE` | false — async loaded |
| `AttachRenderTarget(name, TextureHandle)` | `ATTACHMENT` | true — caller owns |
| `AttachTexture(name, TextureHandle)` | `TEXTURE` | true — caller owns |
| `AttachBuffer(name, StorageBufferSetHandle)` | `BUFFER_SET` | true — caller owns |
| `AttachBuffer(name, UniformBufferSetHandle)` | `BUFFER_SET` | true — caller owns |
| `CreateBufferSet(name, BufferSetCreationType)` | `BUFFER_SET` | false — graph owns |
| `CreateRenderPassNode(RenderGraphRenderPassCreation)` | writes `NodeMap[name].Creation` | — |

`CreateRenderPassNode` is the only method that writes into `NodeMap`; all others write
`ResourceMap`. A pass must call `CreateRenderPassNode` in Setup or the node will have no
`Creation` and `Compile()` will not build edges for it.

Resource entries with `External = true` are not allocated or freed by the graph. External
resources survive `Dispose()` intact.

### 1.3 Resource Query via ResourceInspector

`RenderGraphResourceInspector` is the read side. All query methods auto-create a placeholder
entry if the name is not yet in `ResourceMap`, so they are safe to call before a resource is
fully declared (though the returned handle will be invalid until Setup is complete for the
producing pass). The available queries:

```
GetRenderTarget(name)       -> TextureHandle
GetTexture(name)            -> TextureHandle
GetStorageBufferSet(name)   -> StorageBufferSetHandle
GetVertexBufferSet(name)    -> VertexBufferSetHandle
GetIndexBufferSet(name)     -> IndexBufferSetHandle
GetBufferUniformSet(name)   -> UniformBufferSetHandle
GetIndirectBufferSet(name)  -> IndirectBufferSetHandle
GetResource(name)           -> RenderGraphResource&
GetNode(name)               -> RenderGraphNode&   (asserts the node exists)
```

### 1.4 Three-Phase Pass Lifecycle

Every pass registered with `AddCallbackPass` goes through three phases driven by the graph:

```
RenderGraph::Setup()    -> calls pass->Setup()   for every registered node
RenderGraph::Compile()  -> builds edge graph, sorts, then calls pass->Compile() for each node
RenderGraph::Execute()  -> calls pass->Execute() for each enabled node in sorted order
```

The graph does not re-run Setup or Compile between frames unless `Resize()` is called.
`Execute()` is the only method called per-frame.

### 1.5 Automatic Barrier Insertion

`RenderGraph::Execute()` inserts `VkImageMemoryBarrier` commands automatically before
invoking each pass's `Execute()`:

- Output attachments (`ATTACHMENT` type): transitioned to
  `COLOR_ATTACHMENT_OPTIMAL` (color) or `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` (depth).
- Input textures (`TEXTURE` type in `Inputs`): transitioned from
  `COLOR_ATTACHMENT_OPTIMAL` (if the resource was produced as an attachment) or `UNDEFINED`
  to `SHADER_READ_ONLY_OPTIMAL`.

Passes must not insert redundant barriers for these resources. Passes that use resources in
ways the graph cannot infer (e.g., storage image writes in compute passes) must insert their
own barriers.

### 1.6 AddCallbackPass

```cpp
void RenderGraph::AddCallbackPass(cstring pass_name,
                                   IRenderGraphCallbackPass* const pass_callback,
                                   bool enabled = true);
```

Registers `pass_callback` under `pass_name` in `NodeMap`. The `Enabled` flag is stored in
`RenderGraphNode::Enabled` and checked each frame in `Execute()`. Passes may be registered
in any order; `Compile()` determines execution order from the resource graph.

---

## 2. The Three-Phase Lifecycle

### 2.1 Setup

Called once: `RenderGraph::Setup()` iterates `NodeMap` and calls each pass's `Setup()`.

A pass's `Setup()` must:
- Call `builder->CreateRenderPassNode(RenderGraphRenderPassCreation{...})` to register its
  name, inputs, and outputs with the graph. This is mandatory — without it, no edges are
  built and the pass may execute in arbitrary order.
- Declare every output resource it produces via `builder->CreateRenderTarget`,
  `builder->CreateTexture`, or `builder->CreateBufferSet`. Resources declared here will be
  allocated by the graph in `Compile()`.
- Attach external resources it consumes via `builder->AttachBuffer` or
  `builder->AttachRenderTarget` if those resources are owned outside the graph.

A pass's `Setup()` must not:
- Create any Vulkan objects (`VkPipeline`, `VkRenderPass`, `VkFramebuffer`, `VkDescriptorSet`,
  or any `vk*` handle). The graph has not yet sorted or allocated resources.
- Call `inspector->GetRenderTarget()` and use the returned handle as a valid texture — it
  will be invalid until `Compile()` runs.
- Allocate memory from the heap. Use `ZPushStruct` / `ZPushArray` on the device arena if
  pass-lifetime allocations are needed.

Arena allocation macros used in passes:
```
ZPushStruct(arena, Type)              — allocates sizeof(Type), default-constructs
ZPushStructCtor(arena, Type)          — allocates and zero-inits
ZPushStructCtorArgs(arena, Type, ...) — allocates and constructs with args
ZPushArray(arena, Type, count)        — allocates count × sizeof(Type)
```

All defined in `ZEngineDef.h`. Pass lifetime data goes on `Device->Arena`.
Per-frame data goes on a scratch arena obtained via `ZGetScratch(Device->Arena)`.

### 2.2 Compile

Called once (and again after `Resize()`): after sorting, `RenderGraph::Compile()` iterates
`SortedNodesMap` in order and calls each pass's `Compile()`.

By the time `Compile()` is called for a given pass, all passes that produce the pass's inputs
have already had their resources allocated (because they precede this pass in sorted order).
This makes `inspector->GetRenderTarget("hdr_lit")` safe and guaranteed to return a valid handle
for any input that was declared in a preceding pass's Setup.

A pass's `Compile()` must:
- Read input resource handles from `inspector` to wire framebuffer attachments, descriptor set
  bindings, and pipeline layout bindings.
- Create all Vulkan objects this pass owns: `VkRenderPass`, `VkFramebuffer`,
  `VkDescriptorSetLayout`, `VkPipelineLayout`, `VkPipeline`.
- Store those handles as private members (allocated from the device arena, not `new`).
- Write the compiled `RenderPass` pointer to `*output_pass`. The graph reads this pointer to
  build `node.Handle`, which it subsequently uses for framebuffer creation.

A pass's `Compile()` must not:
- Call `builder->CreateRenderTarget` or any other builder method. The declaration phase is over.
- Record Vulkan commands.
- Block on GPU completion (`vkDeviceWaitIdle`, `vkQueueWaitIdle`).

### 2.3 Execute

Called once per frame: `RenderGraph::Execute()` iterates `SortedNodesMap`, inserts barriers,
and calls each enabled pass's `Execute()`.

A pass's `Execute()` must:
- Record all Vulkan commands needed for this pass into `command_buffer`.
- Call `vkCmdBeginRenderPass` / `vkCmdEndRenderPass` (or the ZEngine command buffer wrapper
  equivalents) around draw calls for graphic passes.
- Use `inspector->GetRenderTarget()` or `inspector->GetStorageBufferSet()` to read the
  current frame's resource handles. Do not cache handles across frames if they could be
  invalidated by a resize.

A pass's `Execute()` must not:
- Create or destroy Vulkan objects. Resource creation belongs in Compile.
- Call any builder methods.
- Access `SortedNodesMap` or `NodeMap` directly.

---

## 3. The Canonical Frame Pass Order

The following table lists every pass that will be registered in `AppRenderPipeline::Initialize`,
in the order that satisfies dependencies. `Compile()` enforces this order via the DAG regardless
of `AddCallbackPass` call order, but the table is the canonical reference for what produces what.

```
Pass name string           Category            Produces                    Consumes
──────────────────────────────────────────────────────────────────────────────────────────────────
"DepthPrePass"             Geometry            hdr_depth                   scene geometry
"ShadowPassDir_0"          Shadow (CSM)        shadow_dir_0                scene geometry
"ShadowPassDir_1"          Shadow (CSM)        shadow_dir_1                scene geometry
"ShadowPassDir_2"          Shadow (CSM)        shadow_dir_2                scene geometry
"ShadowPassDir_3"          Shadow (CSM)        shadow_dir_3                scene geometry
"ShadowPassSpot_0"         Shadow (spot)       shadow_spot_0               scene geometry
"ShadowPassSpot_1"         Shadow (spot)       shadow_spot_1               scene geometry
"ShadowPassSpot_2"         Shadow (spot)       shadow_spot_2               scene geometry
"ShadowPassSpot_3"         Shadow (spot)       shadow_spot_3               scene geometry
"ShadowPassPoint_0"        Shadow (point)      shadow_point_0              scene geometry
"ShadowPassPoint_1"        Shadow (point)      shadow_point_1              scene geometry
"SkinningUploadPass"       Animation           bone_matrix_buffers         CPU animation data
"GeometryPass"             Scene geometry      hdr_color, hdr_normals      hdr_depth
"LightingPass"             Deferred lighting   hdr_lit                     hdr_color, hdr_normals,
                                                                           hdr_depth,
                                                                           shadow_dir_0..3,
                                                                           shadow_spot_0..3,
                                                                           shadow_point_0..1
"SSAOPass"                 Post-process        ssao                        hdr_depth, hdr_normals
"BloomThresholdPass"       Post-process        bloom_threshold             hdr_lit
"BloomDownsample_0"        Post-process        bloom_mip_0                 bloom_threshold
"BloomDownsample_1"        Post-process        bloom_mip_1                 bloom_mip_0
"BloomDownsample_2"        Post-process        bloom_mip_2                 bloom_mip_1
"BloomDownsample_3"        Post-process        bloom_mip_3                 bloom_mip_2
"BloomDownsample_4"        Post-process        bloom_mip_4                 bloom_mip_3
"BloomUpsample_4"          Post-process        bloom_upsample_4            bloom_mip_4
"BloomUpsample_3"          Post-process        bloom_upsample_3            bloom_upsample_4
"BloomUpsample_2"          Post-process        bloom_upsample_2            bloom_upsample_3
"BloomUpsample_1"          Post-process        bloom_upsample_1            bloom_upsample_2
"BloomUpsample_0"          Post-process        bloom_upsample_0            bloom_upsample_1
"ToneMappingPass"          Post-process        ldr_color                   hdr_lit,
                                                                           bloom_upsample_0,
                                                                           ssao
"FXAAPass"                 Post-process        ldr_fxaa                    ldr_color
"UIPass"                   UI                  ldr_final                   ldr_fxaa
"TextPass"                 Text                ldr_final (in-place)        ldr_final
"OverlayPass"              ImGui / editor      swapchain image             ldr_final
```

The depth pre-pass, shadow passes, and skinning upload pass have no data dependency on each
other and the graph may reorder them within their tier. The ordering constraint that matters is:
`GeometryPass` must follow `DepthPrePass`; `LightingPass` must follow all shadow passes and
`GeometryPass`; tone mapping must follow SSAO and all bloom upsample passes.

### Resource consumption map (all produced resources must have a consumer)

```
hdr_depth          → GeometryPass (depth test), SSAOPass (occlusion), LightingPass
hdr_color          → LightingPass (albedo input)
hdr_normals        → LightingPass (normal input), SSAOPass
hdr_lit            → BloomThresholdPass, ToneMappingPass
shadow_dir_{0..3}  → LightingPass (CSM shadow lookup)
shadow_spot_{0..3} → LightingPass (spot shadow lookup)
shadow_point_{0..1}→ LightingPass (point shadow lookup)
bone_matrix_buffers→ GeometryPass (skinned mesh vertex shader reads bone matrices)
ssao               → LightingPass (multiplied into ambient term)
bloom_threshold    → BloomDownsample_0
bloom_mip_{0..4}   → BloomDownsample_{n+1} or BloomUpsample_0
bloom_upsample_{0..4} → BloomUpsample_{n+1} or ToneMappingPass (bloom composite)
ldr_color          → FXAAPass
ldr_fxaa           → UIPass
ldr_final          → OverlayPass → swapchain present
```

---

## 4. Resource Naming Conventions

All passes must use exactly these names when declaring or consuming shared resources. The graph
is string-keyed; a typo creates a second disconnected resource entry rather than a compile error.

| Name | Format | Notes |
|---|---|---|
| `"hdr_color"` | `VK_FORMAT_R16G16B16A16_SFLOAT` | Main scene HDR color RT, full resolution |
| `"hdr_depth"` | `VK_FORMAT_D32_SFLOAT` | Main scene depth buffer, full resolution |
| `"hdr_normals"` | `VK_FORMAT_R16G16B16A16_SFLOAT` | View-space normals, full resolution |
| `"hdr_lit"` | `VK_FORMAT_R16G16B16A16_SFLOAT` | Post-lighting HDR composite, full resolution |
| `"ldr_color"` | `VK_FORMAT_R8G8B8A8_UNORM` | After tone mapping, full resolution |
| `"ldr_fxaa"` | `VK_FORMAT_R8G8B8A8_UNORM` | After FXAA, full resolution |
| `"ldr_final"` | `VK_FORMAT_R8G8B8A8_UNORM` | After UI and text, full resolution |
| `"shadow_dir_0"` .. `"shadow_dir_3"` | `VK_FORMAT_D32_SFLOAT` | CSM cascades 0-3, 2048x2048 each |
| `"shadow_spot_0"` .. `"shadow_spot_3"` | `VK_FORMAT_D32_SFLOAT` | Spot shadow maps, 1024x1024 each |
| `"shadow_point_0"` .. `"shadow_point_1"` | `VK_FORMAT_D32_SFLOAT` | Point light cube maps, 512x512 per face |
| `"ssao"` | `VK_FORMAT_R8_UNORM` | SSAO occlusion, full resolution |
| `"bloom_threshold"` | `VK_FORMAT_R16G16B16A16_SFLOAT` | Bloom extract, half resolution |
| `"bloom_mip_0"` .. `"bloom_mip_4"` | `VK_FORMAT_R16G16B16A16_SFLOAT` | Downsample chain |
| `"bloom_upsample_0"` .. `"bloom_upsample_4"` | `VK_FORMAT_R16G16B16A16_SFLOAT` | Upsample chain |
| `"bone_matrix_buffers"` | `BUFFER_SET / STORAGE` | Per-bone world matrices for skinning |

Shadow map texture specs must set `Width` and `Height` to their fixed sizes (2048, 1024, 512)
rather than 0. Only render targets that should track the window size use `Width = 0, Height = 0`.

### Resource name collision

Resource names are globally unique within a `RenderGraph` instance. All canonical
names (Section 4) are reserved. Custom passes must use unique names; a recommended
convention is: `"<subsystem>_<purpose>"` e.g. `"mymod_custom_bloom"`.

Duplicate resource names cause `ZENGINE_VALIDATE_ASSERT` in debug builds during
`RenderGraph::Compile()`. In release builds, the behavior is undefined (second
declaration silently overwrites the first).

---

## 5. How to Write a New Pass

The following is the complete, minimal skeleton for a new pass. Replace type and member names
with pass-specific names; do not copy-paste the comment strings.

```cpp
// MyPass.h
#pragma once
#include <Rendering/Renderers/RenderGraph.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>

namespace ZEngine::Rendering::Renderers
{
    struct MyCustomPass final : public IRenderGraphCallbackPass
    {
        // --- Phase 1: Setup --------------------------------------------------
        void Setup(
            Hardwares::VulkanDevicePtr const         device,
            cstring                                  name,
            RenderGraphResourceBuilderPtr const      builder,
            RenderGraphResourceInspectorPtr          inspector) override
        {
            // 1a. Declare outputs this pass produces.
            builder->CreateRenderTarget("ldr_color", Specifications::TextureSpecification{
                .Width  = 0,   // 0 = match swapchain size; tracked by Resize()
                .Height = 0,
                .Format = VK_FORMAT_R8G8B8A8_UNORM,
                .Usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            });

            // 1b. Register the render pass node (inputs + outputs).
            builder->CreateRenderPassNode(RenderGraphRenderPassCreation{
                .Name    = name,
                .Inputs  = {
                    { .Name = "hdr_lit",
                      .Type = RenderGraphResourceType::TEXTURE },
                },
                .Outputs = {
                    { .Name = "ldr_color",
                      .Type = RenderGraphResourceType::ATTACHMENT },
                },
            });
        }

        // --- Phase 2: Compile ------------------------------------------------
        void Compile(
            Hardwares::VulkanDevicePtr const         device,
            Scenes::SceneDataPtr const               scene,
            RenderPasses::RenderPassBuilder*         pass_builder,
            RenderGraphResourceInspectorPtr          inspector,
            RenderPasses::RenderPass**         const output_pass) override
        {
            // 2a. Read input handles — safe here because producer passes ran first.
            m_input_handle = inspector->GetRenderTarget("hdr_lit");

            // 2b. Wire the pass builder (already pre-wired by RenderGraph::Compile
            //     before this call — see note below).

            // 2c. Build the render pass, pipeline, descriptor sets.
            //     Store handles as arena-allocated members, never raw new/delete.
            //     Write the RenderPass pointer to output_pass when done.
            // *output_pass = <built RenderPass*>;
        }

        // --- Phase 3: Execute ------------------------------------------------
        void Execute(
            Hardwares::VulkanDevicePtr const         device,
            RenderGraphResourceInspectorPtr          inspector,
            Scenes::SceneDataPtr const               scene,
            RenderPasses::RenderPass*          const pass,
            Buffers::FramebufferVNext*         const framebuffer,
            Hardwares::CommandBufferPtr        const cmd) override
        {
            // 3a. Record commands.
            cmd->BeginRenderPass(pass, framebuffer);
            {
                cmd->SetViewport(pass->GetRenderAreaWidth(), pass->GetRenderAreaHeight());
                cmd->SetScissor(pass->GetRenderAreaWidth(), pass->GetRenderAreaHeight());
                cmd->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
                cmd->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);

                // Full-screen triangle: 3 vertices, no vertex buffer.
                cmd->Draw(3, 1, 0, 0);
            }
            cmd->EndRenderPass();
        }

    private:
        Textures::TextureHandle m_input_handle = {};
        // Pipeline, descriptor set layout, etc. stored as arena pointers.
    };
}
```

Note on the builder pre-wiring: `RenderGraph::Compile()` calls
`RenderPassBuilder->UseRenderTarget()` and `RenderPassBuilder->AddInputTexture()` for each
output and input declared in the node's `Creation` before calling `pass->Compile()`. The
`RenderPassBuilder` is therefore already populated when `Compile()` is entered. Passes should
read from it through the `pass_builder` parameter rather than querying the inspector redundantly
for attachment handles that are already wired.

---

## 6. The Main Lighting Pass

`LightingPass` is the central deferred lighting pass. It is not yet implemented but is
referenced by every post-process and shadow design doc. This section is the authoritative spec.

**Pass name string:** `"LightingPass"`

### 6.1 Inputs

| Input name | RenderGraphResourceType | Descriptor set | Binding |
|---|---|---|---|
| `"hdr_color"` | `TEXTURE` | set 3 | binding 0 |
| `"hdr_normals"` | `TEXTURE` | set 3 | binding 1 |
| `"hdr_depth"` | `TEXTURE` | set 3 | binding 2 |
| `"shadow_dir_0"` | `TEXTURE` | set 2 | binding 0 |
| `"shadow_dir_1"` | `TEXTURE` | set 2 | binding 1 |
| `"shadow_dir_2"` | `TEXTURE` | set 2 | binding 2 |
| `"shadow_dir_3"` | `TEXTURE` | set 2 | binding 3 |
| `"shadow_spot_0"` | `TEXTURE` | set 2 | binding 4 |
| `"shadow_spot_1"` | `TEXTURE` | set 2 | binding 5 |
| `"shadow_spot_2"` | `TEXTURE` | set 2 | binding 6 |
| `"shadow_spot_3"` | `TEXTURE` | set 2 | binding 7 |
| `"shadow_point_0"` | `TEXTURE` | set 2 | binding 8 |
| `"shadow_point_1"` | `TEXTURE` | set 2 | binding 9 |

### 6.2 Outputs

| Output name | RenderGraphResourceType | Format |
|---|---|---|
| `"hdr_lit"` | `ATTACHMENT` | `VK_FORMAT_R16G16B16A16_SFLOAT` |

### 6.3 Descriptor Set Layout

```
Set 0 — Scene UBO (updated once per frame)
  Binding 0: VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    struct SceneUBO {
        mat4 View;
        mat4 Proj;
        mat4 InvView;
        mat4 InvProj;
        vec4 CameraPositionWS;
        vec2 ScreenSize;
        float NearPlane;
        float FarPlane;
    };

Set 1 — Light array UBO (updated when scene lights change)
  Binding 0: VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    struct GpuDirectionLight { vec4 DirectionWS; vec4 Color; float Intensity; uint32_t CascadeCount; float _pad[2]; };
    struct GpuPointLight     { vec4 PositionWS;  vec4 Color; float Intensity; float Radius; float _pad[2]; };
    struct GpuSpotlight      { vec4 PositionWS; vec4 DirectionWS; vec4 Color;
                               float Intensity; float InnerCone; float OuterCone; float Range; };
    struct LightArrayUBO {
        GpuDirectionLight DirectionLights[4];
        GpuPointLight     PointLights[8];
        GpuSpotlight      SpotLights[8];
        uint32_t          DirectionLightCount;
        uint32_t          PointLightCount;
        uint32_t          SpotLightCount;
        uint32_t          _pad;
    };

Set 2 — Shadow UBO + shadow map samplers
  Binding 0: VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    struct CSMData       { mat4 LightSpaceMatrices[4]; float CascadeSplits[4]; };
    struct SpotShadowData{ mat4 LightSpaceMatrix; };
    struct PointShadowData{ float FarPlane; float _pad[3]; };
    struct ShadowUBO {
        CSMData        Directional;
        SpotShadowData Spot[4];
        PointShadowData Point[2];
    };
  Binding 1..4:   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER — shadow_dir_0..3
                  (VK_COMPARE_OP_LESS, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                   border color = (1,1,1,1))
  Binding 5..8:   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER — shadow_spot_0..3
  Binding 9..10:  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER — shadow_point_0..1
                  (cube sampler, VK_IMAGE_VIEW_TYPE_CUBE)

Set 3 — G-buffer textures (updated after GeometryPass)
  Binding 0: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER — hdr_color
  Binding 1: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER — hdr_normals
  Binding 2: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER — hdr_depth
             (aspect = VK_IMAGE_ASPECT_DEPTH_BIT)
```

### 6.4 Draw Call

Full-screen triangle, no vertex buffer. The vertex shader generates clip-space positions
and UVs from `gl_VertexIndex` using the identity:

```glsl
// vertex shader
void main() {
    vec2 uv  = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    v_uv = uv;
}
```

`vkCmdDraw(cmd, 3, 1, 0, 0)` — three vertices, one instance, no index buffer, no vertex
buffer binding.

---

## 6b. OverlayPass (Editor / ImGui)

Currently implemented as `AppRenderPipeline::RenderOverlay()` which bypasses the
RenderGraph entirely. Migration into the graph is a v2 deliverable.

When migrated:
- Name:    `"OverlayPass"`
- Inputs:  `"ldr_final"` (`TEXTURE`, read-only)
- Outputs: none (renders directly to swapchain image; declared as `REFERENCE` to swapchain)
- Descriptor set 0: ImGui font atlas (`sampler2D`)
- Draw calls: ImGui-generated vertex/index buffers, one draw per ImGui draw list
- Pipeline: alpha-blend enabled, no depth test, no depth write
- Only compiled and added to the graph in `ZENGINE_EDITOR` builds

Until migration is complete: `OverlayPass` is excluded from the canonical table
ordering. The graph executes all passes through `"ldr_final"`, then `RenderOverlay()`
runs as a manual step after graph `Execute()` in `AppRenderPipeline::EndFrame()`.

---

## 7. How AppRenderPipeline Wires Passes Together

`AppRenderPipeline::Initialize` constructs the `GraphicRenderer` which owns and initializes
the `RenderGraph`. All pass registrations happen inside `GraphicRenderer::Initialize` (or a
dedicated `RegisterPasses` helper called from there). The sequence is:

```cpp
void GraphicRenderer::RegisterPasses(RenderGraphPtr render_graph)
{
    // Depth and shadow passes — no dependency on each other.
    render_graph->AddCallbackPass("DepthPrePass",        ZNew(DepthPrePass));
    render_graph->AddCallbackPass("ShadowPassDir_0",     ZNew(ShadowPassDir, 0));
    render_graph->AddCallbackPass("ShadowPassDir_1",     ZNew(ShadowPassDir, 1));
    render_graph->AddCallbackPass("ShadowPassDir_2",     ZNew(ShadowPassDir, 2));
    render_graph->AddCallbackPass("ShadowPassDir_3",     ZNew(ShadowPassDir, 3));
    render_graph->AddCallbackPass("ShadowPassSpot_0",    ZNew(ShadowPassSpot, 0));
    render_graph->AddCallbackPass("ShadowPassSpot_1",    ZNew(ShadowPassSpot, 1));
    render_graph->AddCallbackPass("ShadowPassSpot_2",    ZNew(ShadowPassSpot, 2));
    render_graph->AddCallbackPass("ShadowPassSpot_3",    ZNew(ShadowPassSpot, 3));
    render_graph->AddCallbackPass("ShadowPassPoint_0",   ZNew(ShadowPassPoint, 0));
    render_graph->AddCallbackPass("ShadowPassPoint_1",   ZNew(ShadowPassPoint, 1));
    render_graph->AddCallbackPass("SkinningUploadPass",  ZNew(SkinningUploadPass));

    // Geometry and deferred lighting.
    render_graph->AddCallbackPass("GeometryPass",        ZNew(GeometryPass));
    render_graph->AddCallbackPass("LightingPass",        ZNew(LightingPass));

    // Post-process chain.
    // Passes are owned by PostProcessStack, not registered directly with the
    // RenderGraph via AddCallbackPass. PostProcessStack::Compile() calls
    // AddCallbackPass internally for each enabled pass in Order-sorted sequence.
    // GraphicRenderer holds a PostProcessStack member (or pointer) initialised
    // during engine startup.
    m_post_process_stack.AddPass(MakeSSAOPass        (arena, device, SSAOParams{}));
    m_post_process_stack.AddPass(MakeBloomPass        (arena, device, BloomParams{}));
    m_post_process_stack.AddPass(MakeToneMappingPass  (arena, device, ToneMappingParams{}));
    m_post_process_stack.AddPass(MakeFXAAPass         (arena, device, FXAAParams{}));
    // Optional passes — disabled at startup; editor can enable at runtime.
    m_post_process_stack.AddPass(MakeColorGradingPass        (arena, device, ColorGradingParams{}));
    m_post_process_stack.AddPass(MakeChromaticAberrationPass (arena, device, ChromaticAberrationParams{}));
    m_post_process_stack.AddPass(MakeVignettePass            (arena, device, VignetteParams{}));
    m_post_process_stack.Compile();

    // UI, text, editor overlay.
    render_graph->AddCallbackPass("UIPass",              ZNew(UIPass));
    render_graph->AddCallbackPass("TextPass",            ZNew(TextPass));
    render_graph->AddCallbackPass("OverlayPass",         ZNew(OverlayPass));  // ImGui

    render_graph->Setup();
    render_graph->Compile();
}
```

The call order to `AddCallbackPass` is irrelevant to execution order. `Compile()` determines
the actual execution order from the Inputs/Outputs declared by each pass in `Setup()`. The
listing above is alphabetically grouped by category only for readability.

`AppRenderPipeline::RenderScene` calls `SceneRenderer->DrawScene(...)` which internally calls
`RenderGraph::Execute(command_buffer)`. The ImGui overlay bypasses the graph entirely today
(see `AppRenderPipeline::RenderOverlay`); the `OverlayPass` entry in the table above
represents a future migration of that code into the graph.

---

## 8. Pass Enable/Disable at Runtime

Post-process passes are toggled through `PostProcessStack`, not directly through the
`RenderGraph`. The stack owns pass lifetime and communicates enable state to the graph via
the `PostProcessPassData::Enabled` flag, which `Compile()` propagates to the corresponding
`AddCallbackPass` node.

To disable a post-process pass at startup, set `Enabled = false` in the factory params or
call `SetEnabled` before `Compile()`:

```cpp
// Option A — pass disabled params to the factory.
auto entry = MakeSSAOPass(arena, device, SSAOParams{});
entry.Data.Enabled = false;
m_post_process_stack.AddPass(entry);

// Option B — disable after AddPass, before Compile().
m_post_process_stack.SetEnabled(StringHash("SSAOPass"), false);
```

To toggle a post-process pass after the graph is compiled (runtime):

```cpp
m_post_process_stack.SetEnabled(StringHash("FXAAPass"), false);
// Takes effect on the next PostProcessStack::Execute() call — no re-compile needed.
```

For non-post-process passes (geometry, shadow, UI) the RenderGraph node can be toggled
directly via the node map:

```cpp
render_graph->NodeMap["DepthPrePass"].Enabled = false;
```

Disabled passes are skipped in `Execute()`: their barriers are not emitted and their
`Execute()` callback is not called. Their resource producers still run normally. This means a
disabled pass's output resource exists and has been transitioned to the correct layout by its
preceding barriers; downstream passes that read from it will still work.

Do not disable a pass whose output is consumed by multiple downstream passes unless all
consumers are also disabled. The resource will exist but contain stale data from a previous
frame (or the initial clear value).

---

## 9. Resize Handling

`RenderGraph::Resize(uint32_t width, uint32_t height)` is the only entry point for window
resize events. `AppRenderPipeline::ResizeRenderTarget` calls it:

```cpp
void AppRenderPipeline::ResizeRenderTarget(uint32_t w, uint32_t h)
{
    if (SceneRenderer && SceneRenderer->RenderGraph)
        SceneRenderer->RenderGraph->Resize(w, h);
}
```

`Resize()` does the following for every node in `SortedNodesMap`:
1. Clears the node's input and output attachment lists on its `RenderPass::Specification`.
2. For each output with type `ATTACHMENT` (not `REFERENCE`): enqueues the old texture handle
   for deferred disposal, allocates a new texture at the new dimensions using the stored
   `TextureSpec`, and writes the new handle back into `ResourceMap`. Resources declared with
   `Width = 0, Height = 0` are re-created at the new `(width, height)`.
3. Re-binds all input attachments and input textures on the `RenderPass::Specification`.
4. Calls `node.Handle->UpdateRenderTargets()` and `node.Handle->UpdateInputBinding()`.
5. Recreates the `FramebufferVNext` in place (arena-allocated, so no free needed).

Shadow map resources use fixed `Width`/`Height` in their `TextureSpec` (e.g., 2048, 1024)
so they are recreated at their fixed sizes, not the window size. This is correct behaviour.

Passes that hold their own copies of `VkFramebuffer` or `VkImageView` outside the
`FramebufferVNext` managed by the graph must detect the resize and recreate those objects.
The recommended pattern is to compare the stored handle against
`inspector->GetRenderTarget(name)` at the start of `Execute()` and rebuild if it differs.
Alternatively, subscribe to a resize callback emitted by `AppRenderPipeline`.

---

## 10. Thread Safety

`RenderGraph` has no internal synchronization. All of the following must be called from the
render thread only:

- `AddCallbackPass`
- `Setup`
- `Compile`
- `Execute`
- `Resize`
- `Dispose`
- Any `ResourceInspector` or `ResourceBuilder` method

The main thread (game logic thread) communicates with the render thread exclusively through
the `RenderPayload` mailbox in `AppRenderPipeline`. The mailbox is a three-slot ring buffer
protected by `PaddedAtomic<int>` head/tail indices. The render thread reads the latest
committed payload at the start of each frame; the game thread writes into the next available
slot and advances the tail.

Implication for new systems: if an ECS system or animation system wants to enable/disable a
render graph pass (e.g., enable particles when a particle emitter is active), it must write
that intent into the `RenderPayload` struct, not call `GetNode(...).Enabled` directly from
the game thread.

---

## 11. File Layout

No new files are needed to use the render graph. The existing files are:

```
ZEngine/Rendering/Renderers/RenderGraph.h      — all public types and interfaces
ZEngine/Rendering/Renderers/RenderGraph.cpp    — full implementation
ZEngine/Applications/AppRenderPipeline.h       — pipeline, RenderPayload mailbox
ZEngine/Applications/AppRenderPipeline.cpp     — BeginFrame / RenderScene / EndFrame
ZEngine/Rendering/Renderers/GraphicRenderer.h  — owns RenderGraph, owns pass instances
```

New passes are added as new `.h`/`.cpp` files inside `ZEngine/Rendering/Renderers/` or a
subdirectory (e.g., `Renderers/PostProcess/`, `Renderers/Shadow/`). They implement
`IRenderGraphCallbackPass` and are registered in `GraphicRenderer::RegisterPasses`.

This document is the missing specification for the graph itself. Individual passes are
specified in their own design docs listed in the deliverables checklist below.

---

## 12. Deliverables Checklist

The following passes need to be designed and implemented. Each links to its own design doc
where it exists.

| Pass | Design doc | Status |
|---|---|---|
| `DepthPrePass` | (no separate doc — trivial depth-only draw) | Not started |
| `ShadowPassDir_0..3` (CSM) | `shadows.md` | Designed |
| `ShadowPassSpot_0..3` | `shadows.md` | Designed |
| `ShadowPassPoint_0..1` | `shadows.md` | Designed |
| `SkinningUploadPass` | `animation-system.md` | Designed |
| `GeometryPass` | (currently implemented as part of `GraphicRenderer`) | Exists, needs migration |
| `LightingPass` | this document (section 6) | Not started |
| `SSAOPass` | `post-processing.md` | Designed |
| `BloomThresholdPass` | `post-processing.md` | Designed |
| `BloomDownsample_0..4` | `post-processing.md` | Designed |
| `BloomUpsample_0..4` | `post-processing.md` | Designed |
| `ToneMappingPass` | `post-processing.md` | Designed |
| `FXAAPass` | `post-processing.md` | Designed |
| `UIPass` | `ui-system.md` | Designed |
| `TextPass` | `text-rendering.md` | Designed |
| `OverlayPass` (ImGui migration) | (migration of existing `ImGUIRenderer`) | Not started |

Implementation order recommendation: `LightingPass` first (unblocks all visual output),
then shadow passes (unblocks lighting quality), then SSAO and bloom (unblocks visual
polish), then `UIPass` and `TextPass` (unblocks in-engine UI), then `OverlayPass` migration.
