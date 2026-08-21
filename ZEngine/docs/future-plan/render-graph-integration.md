# ZEngine — Render Graph Integration Guide

**Priority:** P0 — post-processing, shadows, UI, text, and particles all depend on this
**Status:** Partially implemented — render graph core complete; passes DepthPrePass, GbufferPass, SkyboxPass, GridPass implemented; LightingPass and post-process chain not started
**Files:**
```
ZEngine/ZEngine/Rendering/Renderers/RenderGraph.h
ZEngine/ZEngine/Rendering/Renderers/RenderGraph.cpp
ZEngine/ZEngine/Rendering/Renderers/GraphicRenderer.h
ZEngine/ZEngine/Rendering/Renderers/GraphicRenderer.cpp
ZEngine/ZEngine/Rendering/Renderers/RendererPasses.h
ZEngine/ZEngine/Rendering/Renderers/RendererPasses.cpp
```

---

## 1. What Exists

`RenderGraph` is production code. The following capabilities are fully implemented.

### 1.1 Topological Sort

`RenderGraph::Compile()` builds a DAG from resource producer/consumer relationships and runs a
DFS post-order topological sort to produce an execution order. The sort detects cycles and logs
an error via `ZENGINE_CORE_ERROR` without crashing. Passes are executed in sorted order during
`Execute()`.

Edges are built from resource declarations: if pass B declares a read on `"hdr_color"` and pass
A declared it as a write, `Compile()` places A before B in execution order.

### 1.2 Resource Declaration via RenderGraphResourceBuilder (Setup phase only)

`RenderGraphResourceBuilderPtr` is the write side. All declarations must happen inside
`IRenderGraphCallbackPass::Setup()`.

| Method | Description |
|---|---|
| `WriteColorAttachment(name, TextureSpecification)` → `RGResourceHandle` | Declares a transient color attachment written by this pass. Graph owns the texture. |
| `WriteDepthAttachment(name, TextureSpecification)` → `RGResourceHandle` | Declares a transient depth attachment written by this pass. Graph owns the texture. |
| `ReadTexture(name, binding_key = nullptr)` → `RGResourceHandle` | Declares a sampled texture read by this pass. |
| `ReadDepth(name)` → `RGResourceHandle` | Declares a depth resource read (depth test, no write). |
| `ImportRenderTarget(name, TextureHandle)` → `RGResourceHandle` | Registers an externally-owned render target. Graph does not own or free it. |
| `AttachRenderTarget(name, TextureHandle)` → `RGResourceHandle` | Attaches an already-imported RT by name. |

`RGResourceHandle` is a typed index (`uint32_t Index`, `uint32_t Version`). Call `.Valid()` to
check before use.

External resources (imported or attached) survive `Dispose()` intact.

### 1.3 Resource Query via RenderGraphResourceInspector (Compile + Execute)

`RenderGraphResourceInspectorPtr` is the read side.

| Method | Cost | Notes |
|---|---|---|
| `GetTextureHandle(RGResourceHandle)` → `TextureHandle` | O(1) | Preferred in Execute — no string lookup |
| `GetRenderTarget(cstring)` → `TextureHandle` | string lookup | Use in Compile for handles not stored from Setup |
| `GetTexture(cstring)` → `TextureHandle` | string lookup | |

### 1.4 Three-Phase Pass Lifecycle

```
RenderGraph::Setup()    calls pass->Setup()   for every registered pass
RenderGraph::Compile()  builds edge graph, sorts, then calls pass->Compile() per pass
RenderGraph::Execute()  calls pass->Execute() per enabled pass in sorted order
```

`Execute()` is the only method called per frame. `Resize()` triggers a full re-Compile.

### 1.5 Automatic Barrier Insertion

`Execute()` inserts `VkImageMemoryBarrier` commands before each pass based on `RGAccess`
entries in `kAccessTable`. `RuntimeState` tracks actual per-frame image layout starting from
UNDEFINED on frame 0; `CurrentState` is compile-time simulation only.

- Color write attachments: transitioned to `COLOR_ATTACHMENT_OPTIMAL`.
- Depth write attachments: transitioned to `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`.
- `DepthRead`: stays in `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL`
  (`depthWriteEnable=false` in pipeline). This is MoltenVK-compatible.
- Shader-read textures: transitioned to `SHADER_READ_ONLY_OPTIMAL`.

Passes must not insert redundant barriers for resources they declared in Setup. Passes that use
resources in ways the graph cannot infer (e.g., storage image writes in compute) must insert
their own barriers.

### 1.6 RenderGraph Public API

```cpp
void Initialize(VulkanDevicePtr device, SceneDataPtr data = nullptr);
void AddCallbackPass(cstring name, IRenderGraphCallbackPass* cb, bool enabled = true);
RGPass* GetPass(cstring name);       // O(1) name lookup; use for config/enable, not Execute
void SetPassEnabled(cstring name, bool enabled);
RGResourceHandle ImportRenderTarget(cstring name, TextureHandle handle);
void Setup();
void Compile();
void Execute(CommandBufferPtr cb);
void Resize(uint32_t w, uint32_t h);
void Dispose();
```

### 1.7 Key Types

```cpp
struct RGResourceHandle { uint32_t Index = UINT32_MAX; uint32_t Version = 0; bool Valid() const; };

enum class RGAccess : uint8_t {
    None, ColorWrite, DepthWrite, DepthRead,
    ShaderRead, ShaderReadWrite, TransferRead, TransferWrite, Present
};

struct RGPass {
    cstring Name;
    bool Enabled;
    IRenderGraphCallbackPass* Callback;
    RenderPass* Handle;
    FramebufferVNext* Framebuffer;
    Array<RGPassResource> Reads;
    Array<RGPassResource> Writes;
};

struct RGResource {
    cstring Name;
    RGResourceKind Kind;
    bool External;
    TextureHandle TextureHandle;
    RGResourceState CurrentState;   // compile-time simulation
    RGResourceState RuntimeState;   // actual per-frame layout
    TextureSpecification Spec;
};
```

---

## 2. The Three-Phase Lifecycle

### 2.1 Setup

Called once: `RenderGraph::Setup()` calls each pass's `Setup()`.

A pass's `Setup()` must:
- Declare every resource it writes via `WriteColorAttachment` or `WriteDepthAttachment`.
- Declare every resource it reads via `ReadTexture` or `ReadDepth`.
- Attach external resources via `ImportRenderTarget` or `AttachRenderTarget`.
- Store returned `RGResourceHandle` values as members for use in Compile and Execute.

A pass's `Setup()` must not:
- Create any Vulkan objects (`VkPipeline`, `VkRenderPass`, `VkFramebuffer`,
  `VkDescriptorSet`, or any `vk*` handle).
- Use returned handles as valid textures — allocation happens in `Compile()`.

Arena allocation macros for pass-lifetime data:
```
ZPushStruct(arena, Type)
ZPushStructCtor(arena, Type)
ZPushStructCtorArgs(arena, Type, ...)
ZPushArray(arena, Type, count)
```
All defined in `ZEngineDef.h`. Pass-lifetime allocations go on `Device->Arena`;
per-frame scratch allocations use `ZGetScratch(Device->Arena)`.

### 2.2 Compile

Called once (and again after `Resize()`): after sorting, the graph calls each pass's
`Compile()`.

Before calling `pass->Compile()`, the graph pre-populates `RenderPassBuilder`:
- Each declared Write → `pass_builder->UseRenderTarget(handle)`
- Each declared Read → `pass_builder->AddInputAttachment(handle)`

The `pass_builder` parameter is already populated when `Compile()` is entered. Passes call
`pass_builder->SetPipelineName(...)`, `pass_builder->UseShader(...)`, etc. on it.

A pass's `Compile()` must:
- Read input handles from `res_inspector` when they were not stored in Setup.
- Create all Vulkan objects this pass owns: render pass, framebuffer, descriptor sets,
  pipeline layout, pipeline. Store them as arena-allocated members.
- Write the compiled `RenderPass` pointer to `*output_pass`.

A pass's `Compile()` must not:
- Call any builder methods. The declaration phase is over.
- Record Vulkan commands.
- Block on GPU completion.

### 2.3 Execute

Called once per frame: the graph inserts barriers and calls each enabled pass's `Execute()`.

A pass's `Execute()` must:
- Record all Vulkan commands into `command_buffer`.
- Call begin/end render pass around draw calls for graphic passes.
- Use `res_inspector->GetTextureHandle(handle)` (preferred) or
  `res_inspector->GetRenderTarget(name)` to get the current frame's texture handles.

A pass's `Execute()` must not:
- Create or destroy Vulkan objects.
- Call any builder methods.

---

## 3. Canonical Frame Pass Order

The table below lists every pass in dependency order. Columns note current implementation
status. Passes with no data dependency on each other (e.g., shadow passes) may be reordered
by the graph within their tier.

```
Pass name string           Category            Produces                    Consumes                    Status
"Depth Pre-Pass"           Geometry            FrameDepth                  scene geometry              Implemented
"G-Buffer Pass"            Scene geometry      FrameColor, gbuffer_normals FrameDepth                  Implemented
"Skybox Pass"              Sky                 FrameColor (in-place)       FrameDepth                  Implemented (disabled at startup)
"Grid Pass"                Editor              FrameColor (in-place)       FrameDepth                  Implemented
"ShadowPassDir_0"          Shadow (CSM)        shadow_dir_0                scene geometry              Not started
"ShadowPassDir_1"          Shadow (CSM)        shadow_dir_1                scene geometry              Not started
"ShadowPassDir_2"          Shadow (CSM)        shadow_dir_2                scene geometry              Not started
"ShadowPassDir_3"          Shadow (CSM)        shadow_dir_3                scene geometry              Not started
"ShadowPassSpot_0"         Shadow (spot)       shadow_spot_0               scene geometry              Not started
"ShadowPassSpot_1"         Shadow (spot)       shadow_spot_1               scene geometry              Not started
"ShadowPassSpot_2"         Shadow (spot)       shadow_spot_2               scene geometry              Not started
"ShadowPassSpot_3"         Shadow (spot)       shadow_spot_3               scene geometry              Not started
"ShadowPassPoint_0"        Shadow (point)      shadow_point_0              scene geometry              Not started
"ShadowPassPoint_1"        Shadow (point)      shadow_point_1              scene geometry              Not started
"SkinningUploadPass"       Animation           bone_matrix_buffers         CPU animation data          Not started
"LightingPass"             Deferred lighting   hdr_lit                     hdr_color, hdr_normals,     Not started
                                                                           hdr_depth,
                                                                           shadow_dir_0..3,
                                                                           shadow_spot_0..3,
                                                                           shadow_point_0..1
"SSAOPass"                 Post-process        ssao                        hdr_depth, hdr_normals      Not started
"BloomThresholdPass"       Post-process        bloom_threshold             hdr_lit                     Not started
"BloomDownsample_0..4"     Post-process        bloom_mip_0..4              bloom_threshold/prev        Not started
"BloomUpsample_0..4"       Post-process        bloom_upsample_0..4         bloom_mip/prev              Not started
"ToneMappingPass"          Post-process        ldr_color                   hdr_lit, bloom_upsample_0,  Not started
                                                                           ssao
"FXAAPass"                 Post-process        ldr_fxaa                    ldr_color                   Not started
"UIPass"                   UI                  ldr_final                   ldr_fxaa                    Not started
"TextPass"                 Text                ldr_final (in-place)        ldr_final                   Not started
"OverlayPass"              ImGui / editor      swapchain image             ldr_final                   Not started
```

Ordering constraints: `G-Buffer Pass` must follow `Depth Pre-Pass`; `LightingPass` must follow
all shadow passes and `G-Buffer Pass`; tone mapping must follow SSAO and all bloom upsample
passes.

---

## 4. Resource Naming Conventions

All passes must use exactly these names when declaring or consuming shared resources. The graph
is string-keyed; a typo creates a disconnected resource entry rather than a compile error.

| Name | Format | Notes |
|---|---|---|
| `"hdr_color"` | `VK_FORMAT_R16G16B16A16_SFLOAT` | Main scene HDR color RT, full resolution |
| `"hdr_depth"` | `VK_FORMAT_D32_SFLOAT` | Main scene depth buffer, full resolution |
| `"hdr_normals"` | `VK_FORMAT_R16G16B16A16_SFLOAT` | View-space normals, full resolution |
| `"hdr_lit"` | `VK_FORMAT_R16G16B16A16_SFLOAT` | Post-lighting HDR composite, full resolution |
| `"ldr_color"` | `VK_FORMAT_R8G8B8A8_UNORM` | After tone mapping, full resolution |
| `"ldr_fxaa"` | `VK_FORMAT_R8G8B8A8_UNORM` | After FXAA, full resolution |
| `"ldr_final"` | `VK_FORMAT_R8G8B8A8_UNORM` | After UI and text, full resolution |
| `"shadow_dir_0"` .. `"shadow_dir_3"` | `VK_FORMAT_D32_SFLOAT` | CSM cascades, 2048x2048 each |
| `"shadow_spot_0"` .. `"shadow_spot_3"` | `VK_FORMAT_D32_SFLOAT` | Spot shadow maps, 1024x1024 each |
| `"shadow_point_0"` .. `"shadow_point_1"` | `VK_FORMAT_D32_SFLOAT` | Point light cube maps, 512x512 per face |
| `"ssao"` | `VK_FORMAT_R8_UNORM` | SSAO occlusion, full resolution |
| `"bloom_threshold"` | `VK_FORMAT_R16G16B16A16_SFLOAT` | Bloom extract, half resolution |
| `"bloom_mip_0"` .. `"bloom_mip_4"` | `VK_FORMAT_R16G16B16A16_SFLOAT` | Downsample chain |
| `"bloom_upsample_0"` .. `"bloom_upsample_4"` | `VK_FORMAT_R16G16B16A16_SFLOAT` | Upsample chain |
| `"bone_matrix_buffers"` | `BUFFER_SET / STORAGE` | Per-bone world matrices for skinning |

Shadow map specs must set `Width` and `Height` to their fixed sizes (2048, 1024, 512) rather
than 0. Only render targets that track the window size use `Width = 0, Height = 0`.

Resource names are globally unique within a `RenderGraph` instance. Custom passes must use
unique names; recommended convention: `"<subsystem>_<purpose>"`, e.g. `"mymod_custom_bloom"`.

---

## 5. How to Write a New Pass

Minimal skeleton for a new pass. `m_color_handle` and `m_input_handle` are stored from Setup
and reused in Compile and Execute.

```cpp
// MyPass.h
#pragma once
#include <Rendering/Renderers/RenderGraph.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>

namespace ZEngine::Rendering::Renderers
{
    struct MyCustomPass final : public IRenderGraphCallbackPass
    {
        void Setup(
            Hardwares::VulkanDevicePtr const         device,
            cstring                                  name,
            RenderGraphResourceBuilderPtr const      res_builder,
            RenderGraphResourceInspectorPtr          res_inspector) override
        {
            m_color_handle = res_builder->WriteColorAttachment("ldr_color",
                Specifications::TextureSpecification{
                    .Width  = 0,
                    .Height = 0,
                    .Format = VK_FORMAT_R8G8B8A8_UNORM,
                    .Usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                            | VK_IMAGE_USAGE_SAMPLED_BIT,
                });
            m_input_handle = res_builder->ReadTexture("hdr_lit");
        }

        void Compile(
            Hardwares::VulkanDevicePtr const         device,
            Rendering::Scenes::SceneDataPtr const    scene,
            RenderPasses::RenderPassBuilder*         pass_builder,
            RenderGraphResourceInspectorPtr          res_inspector,
            RenderPasses::RenderPass**         const output_pass) override
        {
            // pass_builder is pre-populated: UseRenderTarget called for each Write,
            // AddInputAttachment called for each Read. Configure the rest here.
            pass_builder->SetPipelineName("my_pass_pipeline");
            pass_builder->UseShader("my_pass.vert", "my_pass.frag");

            // Build render pass, pipeline, descriptor sets.
            // Store handles as arena-allocated members.
            // *output_pass = <compiled RenderPass*>;
        }

        void Execute(
            Hardwares::VulkanDevicePtr const         device,
            RenderGraphResourceInspectorPtr          res_inspector,
            Rendering::Scenes::SceneDataPtr const    scene,
            RenderPasses::RenderPass*          const pass,
            Buffers::FramebufferVNext*         const framebuffer,
            Hardwares::CommandBufferPtr        const command_buffer) override
        {
            // O(1) handle lookup — prefer GetTextureHandle over GetTexture in Execute.
            Textures::TextureHandle input = res_inspector->GetTextureHandle(m_input_handle);

            command_buffer->BeginRenderPass(pass, framebuffer);
            {
                command_buffer->SetViewport(pass->GetRenderAreaWidth(), pass->GetRenderAreaHeight());
                command_buffer->SetScissor(pass->GetRenderAreaWidth(), pass->GetRenderAreaHeight());
                command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
                command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
                command_buffer->Draw(3, 1, 0, 0);  // full-screen triangle
            }
            command_buffer->EndRenderPass();
        }

        void Deinitialize(Hardwares::VulkanDevicePtr const device) override {}

    private:
        RGResourceHandle m_color_handle = {};
        RGResourceHandle m_input_handle = {};
    };
}
```

---

## 6. The Main Lighting Pass

`LightingPass` is not yet implemented. This section is the authoritative spec.

**Pass name string:** `"LightingPass"`

### 6.1 Inputs

| Input name | Descriptor set | Binding |
|---|---|---|
| `"hdr_color"` | set 3 | binding 0 |
| `"hdr_normals"` | set 3 | binding 1 |
| `"hdr_depth"` | set 3 | binding 2 |
| `"shadow_dir_0"` | set 2 | binding 0 |
| `"shadow_dir_1"` | set 2 | binding 1 |
| `"shadow_dir_2"` | set 2 | binding 2 |
| `"shadow_dir_3"` | set 2 | binding 3 |
| `"shadow_spot_0"` | set 2 | binding 4 |
| `"shadow_spot_1"` | set 2 | binding 5 |
| `"shadow_spot_2"` | set 2 | binding 6 |
| `"shadow_spot_3"` | set 2 | binding 7 |
| `"shadow_point_0"` | set 2 | binding 8 |
| `"shadow_point_1"` | set 2 | binding 9 |

All inputs are declared via `ReadTexture(name)` in Setup. Depth resources are declared via
`ReadDepth(name)` and stay in `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` at runtime.

### 6.2 Output

| Output name | Format |
|---|---|
| `"hdr_lit"` | `VK_FORMAT_R16G16B16A16_SFLOAT` |

Declared via `WriteColorAttachment("hdr_lit", spec)` in Setup.

### 6.3 Descriptor Set Layout

```
Set 0 — Scene UBO (once per frame)
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

Set 1 — Light array UBO (when scene lights change)
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
    struct CSMData        { mat4 LightSpaceMatrices[4]; float CascadeSplits[4]; };
    struct SpotShadowData { mat4 LightSpaceMatrix; };
    struct PointShadowData{ float FarPlane; float _pad[3]; };
    struct ShadowUBO {
        CSMData         Directional;
        SpotShadowData  Spot[4];
        PointShadowData Point[2];
    };
  Binding 1..4:  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER — shadow_dir_0..3
                 (VK_COMPARE_OP_LESS, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                  border color = (1,1,1,1))
  Binding 5..8:  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER — shadow_spot_0..3
  Binding 9..10: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER — shadow_point_0..1
                 (cube sampler, VK_IMAGE_VIEW_TYPE_CUBE)

Set 3 — G-buffer textures (after G-Buffer Pass)
  Binding 0: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER — hdr_color
  Binding 1: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER — hdr_normals
  Binding 2: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER — hdr_depth
             (aspect = VK_IMAGE_ASPECT_DEPTH_BIT)
```

### 6.4 Draw Call

Full-screen triangle, no vertex buffer. The vertex shader generates clip-space positions and
UVs from `gl_VertexIndex`:

```glsl
void main() {
    vec2 uv  = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    v_uv = uv;
}
```

`vkCmdDraw(cmd, 3, 1, 0, 0)` — three vertices, one instance, no index buffer, no vertex buffer
binding.

---

## 7. How GraphicRenderer Registers Passes

`GraphicRenderer::Initialize` is the orchestrator. It attaches external render targets and
registers passes before calling `Setup()` and `Compile()`:

```cpp
RenderGraph->ResourceBuilder->AttachRenderTarget("FrameDepth", FrameDepthRenderTarget);
RenderGraph->ResourceBuilder->AttachRenderTarget("FrameColor", FrameColorRenderTarget);
RenderGraph->AddCallbackPass("Depth Pre-Pass", scene_depth_prepass);
RenderGraph->AddCallbackPass("G-Buffer Pass",  gbuffer_pass);
RenderGraph->AddCallbackPass("Skybox Pass",    skybox_pass, false);  // disabled until sky config
RenderGraph->AddCallbackPass("Grid Pass",      grid_pass);
RenderGraph->Setup();
RenderGraph->Compile();
```

Registration order in `AddCallbackPass` is irrelevant to execution order; `Compile()` determines
order from the resource graph.

---

## 8. Pass Enable/Disable at Runtime

Use `SetPassEnabled` and `GetPass` to toggle or configure passes after the graph is compiled:

```cpp
RenderGraph->SetPassEnabled("Skybox Pass", true);
auto* pass = RenderGraph->GetPass("Skybox Pass");
if (pass) {
    static_cast<SkyboxPass*>(pass->Callback)->EnvMapPath = path;
}
```

`GetPass` is O(1) by name and is intended for configuration, not for calling Execute.

Disabled passes are skipped in `Execute()`: their barriers are not emitted and their `Execute()`
callback is not called. Their declared output resources still exist in the graph. Do not disable
a pass whose output is consumed by downstream passes unless those consumers are also disabled.

---

## 9. Resize Handling

`RenderGraph::Resize(uint32_t width, uint32_t height)` is the only entry point for window resize
events. For every registered pass:

1. Resources declared with `Width = 0, Height = 0` in their `TextureSpecification` are
   re-created at the new `(width, height)`.
2. Resources declared with fixed dimensions (e.g., shadow maps at 2048) are recreated at their
   fixed sizes.
3. The graph updates `FramebufferVNext` in place.
4. A full re-Compile runs after all resources are reallocated.

Passes that cache `VkFramebuffer` or `VkImageView` handles outside the graph-managed
`FramebufferVNext` must detect the resize. Recommended pattern: compare the stored handle
against `res_inspector->GetTextureHandle(m_handle)` at the start of `Execute()` and rebuild if
it differs. Alternatively, subscribe to the resize callback from `AppRenderPipeline`.

---

## 10. Thread Safety

`RenderGraph` has no internal synchronization. All of the following must be called from the
render thread only:

- `AddCallbackPass`, `Setup`, `Compile`, `Execute`, `Resize`, `Dispose`
- Any `RenderGraphResourceInspector` or `RenderGraphResourceBuilder` method

The main thread communicates with the render thread exclusively through the `RenderPayload`
mailbox in `AppRenderPipeline`. The mailbox is a three-slot ring buffer protected by
`PaddedAtomic<int>` head/tail indices.

If an ECS system or animation system wants to enable or disable a render graph pass, it must
write that intent into `RenderPayload`, not call `SetPassEnabled` from the game thread.

---

## 11. Deliverables Checklist

| Pass | Design doc | Status |
|---|---|---|
| `Depth Pre-Pass` | — | Implemented |
| `G-Buffer Pass` | — | Implemented |
| `Skybox Pass` | — | Implemented (disabled at startup) |
| `Grid Pass` | — | Implemented |
| `ShadowPassDir_0..3` (CSM) | `shadows.md` | Not started |
| `ShadowPassSpot_0..3` | `shadows.md` | Not started |
| `ShadowPassPoint_0..1` | `shadows.md` | Not started |
| `SkinningUploadPass` | `animation-system.md` | Not started |
| `LightingPass` | this document (section 6) | Not started |
| `SSAOPass` | `post-processing.md` | Not started |
| `BloomThresholdPass` | `post-processing.md` | Not started |
| `BloomDownsample_0..4` | `post-processing.md` | Not started |
| `BloomUpsample_0..4` | `post-processing.md` | Not started |
| `ToneMappingPass` | `post-processing.md` | Not started |
| `FXAAPass` | `post-processing.md` | Not started |
| `UIPass` | `ui-system.md` | Not started |
| `TextPass` | `text-rendering.md` | Not started |
| `OverlayPass` (ImGui migration) | — | Not started |

Implementation order recommendation: `LightingPass` first (unblocks all visual output), then
shadow passes (unblocks lighting quality), then SSAO and bloom (unblocks visual polish), then
`UIPass` and `TextPass`, then `OverlayPass` migration.
