# ZEngine — Compute Pipeline Support

**Priority:** P2 — Required for SSAO, bloom threshold, GPU-driven culling, and any effect that
benefits from general-purpose GPU compute.
**Status:** Design
**Depends on:** `render-graph-redesign.md`, `shader-asset-pipeline.md`
**Blocks:** `post-processing.md` (SSAO, bloom), `next-year-plans/culling-system.md` (DrawCull)

**Goal:** Add end-to-end compute pipeline support to ZEngine. This covers shader-stage
recognition, pipeline creation, command recording, the render pass compute path, a thin
builder and callback helper, and three concrete compute pass examples that exercise the
full stack. Every change is grounded in the current state of the codebase; nothing is
designed in the abstract.

---

## 1. ShaderType::COMPUTE — ShaderEnums.h

`ShaderEnums.h` currently defines four shader types. `COMPUTE` is absent and
`CompilationStage::GetEShLanguage` falls through to `EShLangGeometry` for any type it does
not explicitly recognise, which means feeding a compute source file through the current
pipeline silently produces broken SPIR-V.

**Before:**

```cpp
// ZEngine/Rendering/Shaders/ShaderEnums.h
enum class ShaderType
{
    VERTEX   = 0,
    FRAGMENT = 1,
    GEOMETRY = 2,
    UNKNOWN  = 3
};
```

```cpp
// ZEngine/Rendering/Shaders/Compilers/CompilationStage.cpp
EShLanguage CompilationStage::GetEShLanguage(const ShaderType type)
{
    if (type == ShaderType::VERTEX)
        return EShLangVertex;
    if (type == ShaderType::FRAGMENT)
        return EShLangFragment;
    return EShLangGeometry;   // COMPUTE falls here — incorrect
}
```

**After:**

```cpp
// ZEngine/Rendering/Shaders/ShaderEnums.h
enum class ShaderType
{
    VERTEX   = 0,
    FRAGMENT = 1,
    GEOMETRY = 2,
    UNKNOWN  = 3,
    COMPUTE  = 4
};
```

```cpp
// ZEngine/Rendering/Shaders/Compilers/CompilationStage.cpp
EShLanguage CompilationStage::GetEShLanguage(const ShaderType type)
{
    if (type == ShaderType::VERTEX)
        return EShLangVertex;
    if (type == ShaderType::FRAGMENT)
        return EShLangFragment;
    if (type == ShaderType::COMPUTE)
        return EShLangCompute;
    return EShLangGeometry;
}
```

No other changes are required in `CompilationStage`. The existing `SetShaderRules` call
already passes `GetEShLanguage(information_list.Type)` to `shader.setEnvInput`, so compute
shaders automatically get the correct GLSL environment once the function returns
`EShLangCompute`. The Vulkan 1.3 / SPIR-V 1.6 target set in `SetShaderRules` is valid for
compute shaders.

The `shader-asset-pipeline.md` design defines a `ShaderStage::Compute` value and maps the
`.comp` extension to it. The `COMPUTE = 4` value added here must align with that mapping when
the asset pipeline is implemented: the import code should translate `ShaderStage::Compute` to
`ShaderType::COMPUTE` before invoking `CompilationStage`.

---

## 2. ComputePipeline Struct — RendererPipeline.h

`GraphicPipeline` lives in
`ZEngine/Rendering/Renderers/Pipelines/RendererPipeline.h`. A parallel `ComputePipeline`
struct is added to the same file and namespace.

**Declaration (RendererPipeline.h):**

```cpp
struct ComputePipeline
{
public:
    ComputePipeline() {}
    ~ComputePipeline() {}

    Shaders::Shader*         Shader = nullptr;
    Hardwares::VulkanDevice* Device = nullptr;
    VkPipeline               Handle = VK_NULL_HANDLE;
    VkPipelineLayout         Layout = VK_NULL_HANDLE;

    void Initialize(Hardwares::VulkanDevice* device, const char* shader_name);
    void Bake();
    void Dispose();
};
```

**Implementation (RendererPipeline.cpp):**

`Initialize` resolves the shader from the device shader cache, the same way
`GraphicPipeline::Initialize` does. The layout is built from the shader's descriptor set
layout — identical to the graphics path.

```cpp
void ComputePipeline::Initialize(Hardwares::VulkanDevice* device, const char* shader_name)
{
    Device = device;
    Shader = device->ShaderCaches.Find(shader_name);
    ZENGINE_VALIDATE_ASSERT(Shader != nullptr, "ComputePipeline: shader not found in cache");

    // Build the pipeline layout from the shader's descriptor set layout.
    // Same path as GraphicPipeline::Initialize — one descriptor set, optional push constants.
    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount             = 1;
    layout_info.pSetLayouts                = &Shader->DescriptorSetLayout;
    layout_info.pushConstantRangeCount     = Shader->PushConstantRanges.size();
    layout_info.pPushConstantRanges        = Shader->PushConstantRanges.data();

    ZENGINE_VALIDATE_ASSERT(
        vkCreatePipelineLayout(Device->LogicalDevice, &layout_info, nullptr, &Layout) == VK_SUCCESS,
        "ComputePipeline: failed to create pipeline layout");
}
```

`Bake` calls `vkCreateComputePipelines`. The shader stage is always
`VK_SHADER_STAGE_COMPUTE_BIT`. No render pass handle, no vertex input state, no
blend state — compute pipelines require none of those.

```cpp
void ComputePipeline::Bake()
{
    ZENGINE_VALIDATE_ASSERT(Shader   != nullptr,       "ComputePipeline::Bake: shader is null");
    ZENGINE_VALIDATE_ASSERT(Layout   != VK_NULL_HANDLE, "ComputePipeline::Bake: layout is null");
    ZENGINE_VALIDATE_ASSERT(Handle   == VK_NULL_HANDLE, "ComputePipeline::Bake: already baked");

    VkPipelineShaderStageCreateInfo stage = {};
    stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = Shader->GetShaderModule();
    stage.pName  = "main";

    VkComputePipelineCreateInfo create_info = {};
    create_info.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    create_info.stage  = stage;
    create_info.layout = Layout;

    ZENGINE_VALIDATE_ASSERT(
        vkCreateComputePipelines(
            Device->LogicalDevice,
            VK_NULL_HANDLE,   // no pipeline cache in this path; add later if profiling shows benefit
            1, &create_info,
            nullptr,
            &Handle) == VK_SUCCESS,
        "ComputePipeline::Bake: vkCreateComputePipelines failed");
}
```

`Dispose` mirrors `GraphicPipeline::Dispose`:

```cpp
void ComputePipeline::Dispose()
{
    if (Handle != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Device->LogicalDevice, Handle, nullptr);
        Handle = VK_NULL_HANDLE;
    }
    if (Layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(Device->LogicalDevice, Layout, nullptr);
        Layout = VK_NULL_HANDLE;
    }
}
```

---

## 3. CommandBuffer::Dispatch — VulkanDevice.h

`CommandBuffer` in `VulkanDevice.h` exposes `DrawIndirect`, `BeginRenderPass`,
`EndRenderPass`, `BindPipeline`, `BindDescriptorSets`, `PushConstants`, and
`TransitionImageLayout`, but no dispatch command. Compute passes cannot call `vkCmdDispatch`
directly on a raw `VkCommandBuffer` handle — doing so bypasses the state tracking used by the
rest of the command buffer API.

Add the declaration to the `CommandBuffer` struct in `VulkanDevice.h`:

```cpp
void Dispatch(uint32_t x, uint32_t y, uint32_t z);
```

The implementation in `VulkanDevice.cpp`:

```cpp
void CommandBuffer::Dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    vkCmdDispatch(m_command_buffer, x, y, z);
}
```

No state validation is needed beyond what the Vulkan validation layer catches. The caller is
responsible for having bound a compute pipeline before calling `Dispatch`. Parallel to how
`DrawIndirect` requires a bound graphics pipeline, this contract is enforced at the validation
layer level, not in the wrapper.

---

## 4. RenderPass Compute Path — RenderPass.h / RenderPass.cpp

`RenderPass` currently has a single `Pipelines::GraphicPipeline* Pipeline` field.
`Initialize` and `Bake` always construct a `GraphicPipeline`, and a `COMPUTE` pass type
would crash in `vkCreateRenderPass` because a compute pipeline has no attachment or
framebuffer.

**RenderPass.h change:** Add a `ComputePipeline` field alongside the existing `Pipeline`
field. Exactly one is non-null at runtime, depending on pass type.

```cpp
struct RenderPass
{
    // ... existing fields unchanged ...
    Pipelines::GraphicPipeline*  Pipeline        = {nullptr};
    Pipelines::ComputePipeline*  ComputePipeline = {nullptr};  // new

    void Initialize(Hardwares::VulkanDevice* device,
                    const Specifications::RenderPassSpecification& specification);
    void Bake();
    // ... rest unchanged ...
};
```

**RenderPass::Initialize change:**

```cpp
void RenderPass::Initialize(Hardwares::VulkanDevice* device,
                             const Specifications::RenderPassSpecification& spec)
{
    m_device      = device;
    Specification = spec;

    if (spec.Type == RenderPassType::COMPUTE)
    {
        // No VkRenderPass object. No attachment. No framebuffer.
        ComputePipeline = ZPushStructCtorArgs(device->Arena, Pipelines::ComputePipeline);
        ComputePipeline->Initialize(
            device,
            spec.PipelineSpecification.ShaderSpecificationValue.Name);
        return;
    }

    if (spec.Type != RenderPassType::GRAPHIC)
        return;

    // Existing graphics path — unchanged below this line.
    Pipeline = ZPushStructCtorArgs(device->Arena, Pipelines::GraphicPipeline);
    Pipeline->Initialize(device, Specifications::GraphicRendererPipelineSpecification{spec.PipelineSpecification});
    // ... attachment creation, VkRenderPass creation, etc. ...
}
```

**RenderPass::Bake change:**

```cpp
void RenderPass::Bake()
{
    if (Specification.Type == RenderPassType::COMPUTE)
    {
        ComputePipeline->Bake();
        return;
    }
    if (Specification.Type != RenderPassType::GRAPHIC)
        return;

    Pipeline->Bake();
}
```

**Framebuffer contract:** `ComputePipeline` passes leave `Framebuffer` null in the owning
`RGPass`. Pass `Execute` implementations that receive a null framebuffer must not call
`BeginRenderPass`. This is enforced by the pattern shown in section 6 below.

---

## 5. ComputePassBuilder

`RenderPassBuilder` in `RenderPass.h` provides a fluent API for configuring graphics passes.
Its members include vertex input bindings, attribute descriptions, blend state, depth test
flags, and attachment lists — none of which are applicable to compute. A separate builder
eliminates the possibility of a compute pass accidentally configuring vertex input state or
enabling depth write.

`ComputePassBuilder` is added to `RenderPass.h` in the same namespace as
`RenderPassBuilder`:

```cpp
struct ComputePassBuilder
{
    Core::Memory::ArenaAllocator* Arena = nullptr;

    void Initialize(Core::Memory::ArenaAllocator* arena);

    ComputePassBuilder& UseShader(const char* name);
    ComputePassBuilder& SetPushConstantRange(
        uint32_t size,
        VkShaderStageFlags stage = VK_SHADER_STAGE_COMPUTE_BIT);

    // Returns the completed spec with Type = RenderPassType::COMPUTE.
    // No pipeline state flags, no attachment list, no render area defaults.
    Specifications::RenderPassSpecification Detach();

private:
    Specifications::RenderPassSpecification m_spec{};
};
```

Implementation:

```cpp
void ComputePassBuilder::Initialize(Core::Memory::ArenaAllocator* arena)
{
    Arena  = arena;
    m_spec = {};
    m_spec.Type = Specifications::RenderPassType::COMPUTE;
}

ComputePassBuilder& ComputePassBuilder::UseShader(const char* name)
{
    m_spec.PipelineSpecification.ShaderSpecificationValue.Name = name;
    return *this;
}

ComputePassBuilder& ComputePassBuilder::SetPushConstantRange(
    uint32_t size, VkShaderStageFlags stage)
{
    m_spec.PipelineSpecification.PushConstantSize  = size;
    m_spec.PipelineSpecification.PushConstantStage = stage;
    return *this;
}

Specifications::RenderPassSpecification ComputePassBuilder::Detach()
{
    return std::move(m_spec);
}
```

A pass's `Compile` call is reduced to:

```cpp
ComputePassBuilder builder;
builder.Initialize(arena);
auto spec = builder
    .UseShader("ssao_compute")
    .SetPushConstantRange(sizeof(SSAOPushConstants))
    .Detach();
```

---

## 6. IComputeCallbackPass

`IRenderGraphCallbackPass` has three methods: `Setup`, `Compile`, and `Execute`. A concrete
pass must implement all three. For compute passes the `Compile` step is always the same:
construct a `COMPUTE` `RenderPassSpecification` via `ComputePassBuilder`, create a
`RenderPass`, and write it to the output pointer. The `Execute` step always starts with a
`vkCmdBindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE)` before the pass-specific dispatch.
Repeating this boilerplate in every compute pass implementation introduces a class of bug
where a pass forgets to bind the pipeline or binds it with the wrong bind point.

`IComputeCallbackPass` is a convenience base class that handles `Compile` and the pipeline
bind inside `Execute`, leaving only `SetupCompute` and `ExecuteCompute` for subclasses.

```cpp
// ZEngine/Rendering/Renderers/RenderPasses/IComputeCallbackPass.h
#pragma once
#include <ZEngine/Rendering/Renderers/RenderPasses/RenderPass.h>
#include <ZEngine/Rendering/RenderGraph/IRenderGraphCallbackPass.h>

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct IComputeCallbackPass : public IRenderGraphCallbackPass
    {
        // Setup() delegates to SetupCompute() so the subclass sees only
        // RGBuilder — it never needs to touch RGInspector during setup.
        void Setup(
            Hardwares::VulkanDevicePtr device,
            cstring                    name,
            RGBuilder*                 builder,
            RGInspector*               inspector) final;

        // Compile() builds the COMPUTE RenderPass. Subclasses do not override this.
        void Compile(
            Hardwares::VulkanDevicePtr  device,
            Scenes::SceneDataPtr        scene,
            RenderPassBuilder*          /*unused*/,
            RGInspector*                inspector,
            RenderPass**                output) final;

        // Execute() binds the compute pipeline then delegates to ExecuteCompute().
        void Execute(
            Hardwares::VulkanDevicePtr  device,
            RGInspector*                inspector,
            Scenes::SceneDataPtr        scene,
            RenderPass*                 pass,
            Buffers::FramebufferVNext*  framebuffer,   // always null for compute
            Hardwares::CommandBufferPtr cb) final;

        // Subclass interface.
        virtual void SetupCompute(Hardwares::VulkanDevicePtr, RGBuilder*) = 0;
        virtual void ExecuteCompute(
            Hardwares::VulkanDevicePtr  device,
            RGInspector*                inspector,
            Scenes::SceneDataPtr        scene,
            VkPipeline                  pipeline,
            VkPipelineLayout            layout,
            Hardwares::CommandBufferPtr cb) = 0;

        // Subclass provides the shader name; Compile() uses it.
        virtual const char* GetShaderName() const = 0;

        // Optional: subclass overrides to supply push constant size.
        // Default: 0 (no push constants).
        virtual uint32_t GetPushConstantSize() const { return 0; }

    private:
        Core::Memory::ArenaAllocator* m_arena = nullptr;
    };
}
```

**IComputeCallbackPass::Compile implementation:**

```cpp
void IComputeCallbackPass::Compile(
    Hardwares::VulkanDevicePtr  device,
    Scenes::SceneDataPtr        /*scene*/,
    RenderPassBuilder*          /*unused*/,
    RGInspector*                /*inspector*/,
    RenderPass**                output)
{
    ComputePassBuilder builder;
    builder.Initialize(device->Arena);
    builder.UseShader(GetShaderName());
    if (GetPushConstantSize() > 0)
        builder.SetPushConstantRange(GetPushConstantSize());

    auto spec = builder.Detach();
    auto* pass = ZPushStructCtorArgs(device->Arena, RenderPass);
    pass->Initialize(device, spec);
    pass->Bake();
    *output = pass;
}
```

**IComputeCallbackPass::Execute implementation:**

```cpp
void IComputeCallbackPass::Execute(
    Hardwares::VulkanDevicePtr  device,
    RGInspector*                inspector,
    Scenes::SceneDataPtr        scene,
    RenderPass*                 pass,
    Buffers::FramebufferVNext*  /*framebuffer — always null*/,
    Hardwares::CommandBufferPtr cb)
{
    VkCommandBuffer cmd = cb->GetHandle();

    vkCmdBindPipeline(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        pass->ComputePipeline->Handle);

    ExecuteCompute(device, inspector, scene,
                   pass->ComputePipeline->Handle,
                   pass->ComputePipeline->Layout,
                   cb);
}
```

The subclass receives `VkPipeline` and `VkPipelineLayout` directly so it can bind descriptor
sets and push constants without digging into `RenderPass` internals.

---

## 7. Concrete Compute Pass Examples

### 7a. SSAOComputePass

Reads the depth render target and a world-space normals render target as shader inputs.
Writes to a half-resolution R8 occlusion render target. The output is consumed by the
lighting pass.

Resource declarations use `RGAccess::ShaderRead` for inputs and `RGAccess::ShaderReadWrite`
for the output, which maps to `VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT` and
`VK_IMAGE_LAYOUT_GENERAL` in the access table defined in `render-graph-redesign.md`,
section 6.2.

```cpp
struct SSAOComputePass final : public IComputeCallbackPass
{
    RGResourceHandle m_depth_handle     = {};
    RGResourceHandle m_normals_handle   = {};
    RGResourceHandle m_occlusion_handle = {};
    uint32_t         m_width            = 0;
    uint32_t         m_height           = 0;

    const char* GetShaderName()      const override { return "ssao_compute"; }
    uint32_t    GetPushConstantSize() const override { return sizeof(SSAOPushConstants); }

    void SetupCompute(Hardwares::VulkanDevicePtr device, RGBuilder* builder) override
    {
        // Read depth and normals produced by earlier passes.
        m_depth_handle   = builder->ReadTexture("FrameDepthRT",   "depth_input");
        m_normals_handle = builder->ReadTexture("GBufferNormals", "normals_input");

        // Half-resolution R8 occlusion map — transient, aliasable after last consumer.
        Specifications::TextureSpecification occl_spec = {};
        occl_spec.Format = Specifications::ImageFormat::R8_UNORM;
        occl_spec.Width  = device->Swapchain->Width  / 2;
        occl_spec.Height = device->Swapchain->Height / 2;
        m_width          = occl_spec.Width;
        m_height         = occl_spec.Height;

        m_occlusion_handle = builder->WriteColorAttachment("SSAOOcclusion", occl_spec);
    }

    void ExecuteCompute(
        Hardwares::VulkanDevicePtr  device,
        RGInspector*                inspector,
        Scenes::SceneDataPtr        /*scene*/,
        VkPipeline                  /*pipeline — already bound by IComputeCallbackPass*/,
        VkPipelineLayout            layout,
        Hardwares::CommandBufferPtr cb) override
    {
        VkCommandBuffer cmd = cb->GetHandle();

        SSAOPushConstants push = {};
        push.DepthTextureIndex   = inspector->GetTextureBindlessIndex(m_depth_handle);
        push.NormalsTextureIndex = inspector->GetTextureBindlessIndex(m_normals_handle);
        push.OutputImageIndex    = inspector->GetTextureBindlessIndex(m_occlusion_handle);
        push.Width               = m_width;
        push.Height              = m_height;

        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), &push);

        uint32_t gx = (m_width  + 7) / 8;
        uint32_t gy = (m_height + 7) / 8;
        cb->Dispatch(gx, gy, 1);
    }
};
```

GLSL compute shader (`ssao_compute.comp`):

```glsl
#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(push_constant) uniform PushConstants {
    uint DepthTextureIndex;
    uint NormalsTextureIndex;
    uint OutputImageIndex;
    uint Width;
    uint Height;
} pc;

layout(set = 0, binding = 0) uniform sampler2D Textures[];
layout(set = 0, binding = 1, r8) uniform writeonly image2D StorageImages[];

void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= int(pc.Width) || coord.y >= int(pc.Height))
        return;

    vec2 uv = (vec2(coord) + 0.5) / vec2(pc.Width, pc.Height);

    float depth   = texture(Textures[pc.DepthTextureIndex],   uv).r;
    vec3  normal  = texture(Textures[pc.NormalsTextureIndex], uv).xyz * 2.0 - 1.0;

    // SSAO kernel sampling omitted for brevity; result clamped to [0, 1].
    float occlusion = 1.0;

    imageStore(StorageImages[nonuniformEXT(pc.OutputImageIndex)],
               coord, vec4(occlusion, 0.0, 0.0, 0.0));
}
```

### 7b. BloomThresholdComputePass

Reads the full-resolution HDR color render target. Writes to a bloom threshold render target
of the same dimensions. Only pixels whose luminance exceeds the threshold value are written
as non-zero. This pass is the first stage of the bloom chain described in
`post-processing.md`.

```cpp
struct BloomThresholdComputePass final : public IComputeCallbackPass
{
    RGResourceHandle m_hdr_handle       = {};
    RGResourceHandle m_threshold_handle = {};
    uint32_t         m_width            = 0;
    uint32_t         m_height           = 0;

    const char* GetShaderName()      const override { return "bloom_threshold_compute"; }
    uint32_t    GetPushConstantSize() const override { return sizeof(BloomThresholdPushConstants); }

    void SetupCompute(Hardwares::VulkanDevicePtr device, RGBuilder* builder) override
    {
        m_hdr_handle = builder->ReadTexture("FrameColorRT", "hdr_input");

        Specifications::TextureSpecification spec = {};
        spec.Format = Specifications::ImageFormat::R16G16B16A16_SFLOAT;
        spec.Width  = device->Swapchain->Width;
        spec.Height = device->Swapchain->Height;
        m_width     = spec.Width;
        m_height    = spec.Height;

        m_threshold_handle = builder->WriteColorAttachment("BloomThreshold", spec);
    }

    void ExecuteCompute(
        Hardwares::VulkanDevicePtr  /*device*/,
        RGInspector*                inspector,
        Scenes::SceneDataPtr        /*scene*/,
        VkPipeline                  /*pipeline*/,
        VkPipelineLayout            layout,
        Hardwares::CommandBufferPtr cb) override
    {
        VkCommandBuffer cmd = cb->GetHandle();

        BloomThresholdPushConstants push = {};
        push.HdrTextureIndex    = inspector->GetTextureBindlessIndex(m_hdr_handle);
        push.OutputImageIndex   = inspector->GetTextureBindlessIndex(m_threshold_handle);
        push.Threshold          = 1.0f;   // luminance threshold; could be a UBO field
        push.Width              = m_width;
        push.Height             = m_height;

        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), &push);

        uint32_t gx = (m_width  + 7) / 8;
        uint32_t gy = (m_height + 7) / 8;
        cb->Dispatch(gx, gy, 1);
    }
};
```

GLSL compute shader (`bloom_threshold_compute.comp`):

```glsl
#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(push_constant) uniform PushConstants {
    uint  HdrTextureIndex;
    uint  OutputImageIndex;
    float Threshold;
    uint  Width;
    uint  Height;
} pc;

layout(set = 0, binding = 0) uniform sampler2D Textures[];
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D StorageImages[];

void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= int(pc.Width) || coord.y >= int(pc.Height))
        return;

    vec2 uv    = (vec2(coord) + 0.5) / vec2(pc.Width, pc.Height);
    vec3 color = texture(Textures[pc.HdrTextureIndex], uv).rgb;
    float lum  = dot(color, vec3(0.2126, 0.7152, 0.0722));
    vec3  out_color = lum > pc.Threshold ? color : vec3(0.0);

    imageStore(StorageImages[nonuniformEXT(pc.OutputImageIndex)],
               coord, vec4(out_color, 1.0));
}
```

### 7c. DrawCullPass

GPU-driven visibility culling. Reads a per-draw-call scene bounds buffer (a storage buffer
containing `AABB` structs for each draw call). Writes a filtered `VkDrawIndirectCommand`
buffer that the base pass consumes via `DrawIndirect`. Only draw calls whose bounding boxes
pass the frustum test are written with a non-zero instance count.

This pass is the compute component of the GPU-driven rendering path described in
`next-year-plans/culling-system.md`.

```cpp
struct DrawCullPass final : public IComputeCallbackPass
{
    RGResourceHandle m_scene_bounds_handle   = {};
    RGResourceHandle m_indirect_buffer_handle = {};
    uint32_t         m_draw_count            = 0;

    const char* GetShaderName()      const override { return "draw_cull_compute"; }
    uint32_t    GetPushConstantSize() const override { return sizeof(DrawCullPushConstants); }

    void SetupCompute(Hardwares::VulkanDevicePtr /*device*/, RGBuilder* builder) override
    {
        // Read-only: per-object AABB data uploaded once per frame by UploadPass.
        m_scene_bounds_handle = builder->AttachBuffer(
            "SceneBoundsBuffer",
            inspector->GetStorageBufferSetHandle("SceneBoundsBuffer"));

        // Read-write: indirect draw buffer. The cull shader writes draw commands;
        // the base pass reads them via vkCmdDrawIndirect.
        // RGAccess::ShaderReadWrite maps to VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        // in the kAccessTable in render-graph-redesign.md section 6.2.
        m_indirect_buffer_handle = builder->AttachBuffer(
            "IndirectDrawBuffer",
            inspector->GetIndirectBufferSetHandle("IndirectDrawBuffer"));
    }

    void ExecuteCompute(
        Hardwares::VulkanDevicePtr  /*device*/,
        RGInspector*                inspector,
        Scenes::SceneDataPtr        scene,
        VkPipeline                  /*pipeline*/,
        VkPipelineLayout            layout,
        Hardwares::CommandBufferPtr cb) override
    {
        VkCommandBuffer cmd = cb->GetHandle();

        m_draw_count = scene->DrawList.size();

        DrawCullPushConstants push = {};
        push.DrawCount            = m_draw_count;
        push.FrustumPlanes[0]     = scene->Camera.FrustumPlane(0);
        push.FrustumPlanes[1]     = scene->Camera.FrustumPlane(1);
        push.FrustumPlanes[2]     = scene->Camera.FrustumPlane(2);
        push.FrustumPlanes[3]     = scene->Camera.FrustumPlane(3);
        push.FrustumPlanes[4]     = scene->Camera.FrustumPlane(4);
        push.FrustumPlanes[5]     = scene->Camera.FrustumPlane(5);

        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), &push);

        uint32_t gx = (m_draw_count + 63) / 64;
        cb->Dispatch(gx, 1, 1);
    }
};
```

GLSL compute shader (`draw_cull_compute.comp`):

```glsl
#version 460

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct AABB {
    vec3 Min;
    float _pad0;
    vec3 Max;
    float _pad1;
};

struct VkDrawIndirectCommand {
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};

layout(push_constant) uniform PushConstants {
    uint  DrawCount;
    vec4  FrustumPlanes[6];
} pc;

layout(set = 0, binding = 2) readonly buffer SceneBoundsBuffer {
    AABB Bounds[];
};

layout(set = 0, binding = 3) buffer IndirectDrawBuffer {
    VkDrawIndirectCommand Commands[];
};

bool FrustumTest(AABB box)
{
    for (int i = 0; i < 6; ++i)
    {
        vec3 n = pc.FrustumPlanes[i].xyz;
        float d = pc.FrustumPlanes[i].w;
        vec3 p = mix(box.Min, box.Max, greaterThanEqual(n, vec3(0.0)));
        if (dot(n, p) + d < 0.0)
            return false;
    }
    return true;
}

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= pc.DrawCount)
        return;

    bool visible = FrustumTest(Bounds[idx]);
    Commands[idx].instanceCount = visible ? 1u : 0u;
}
```

---

## 8. Render Graph Integration

The barrier system in `render-graph-redesign.md` already handles compute barriers. The
`kAccessTable` entry for `RGAccess::ShaderReadWrite` maps to
`VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT` with `VK_ACCESS_SHADER_READ_BIT |
VK_ACCESS_SHADER_WRITE_BIT` and `VK_IMAGE_LAYOUT_GENERAL`. A compute pass that writes a
storage image automatically receives the correct pre-pass barrier, and a graphics pass that
subsequently reads that image as a texture receives the correct
`COMPUTE_SHADER -> FRAGMENT_SHADER` barrier with the layout transition from
`VK_IMAGE_LAYOUT_GENERAL` to `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`.

No special registration path is needed. Compute passes call `AddCallbackPass` identically to
graphics passes:

```cpp
// GraphicRenderer::RegisterPasses — excerpt showing SSAO compute pass insertion
void GraphicRenderer::RegisterPasses()
{
    RenderGraph->AddCallbackPass("Upload Pass",      upload_pass);
    RenderGraph->AddCallbackPass("Depth Pre-Pass",   scene_depth_prepass);
    RenderGraph->AddCallbackPass("Base Pass",        base_pass);
    RenderGraph->AddCallbackPass("Skybox Pass",      skybox_pass);
    RenderGraph->AddCallbackPass("Grid Pass",        grid_pass);

    // Compute passes register identically. The graph detects the pass type
    // from RenderPassSpecification::Type = COMPUTE after Compile() runs.
    RenderGraph->AddCallbackPass("SSAO Compute",           ssao_compute_pass);
    RenderGraph->AddCallbackPass("Bloom Threshold Compute", bloom_threshold_pass);
    RenderGraph->AddCallbackPass("Draw Cull Compute",       draw_cull_pass);
}
```

The topology sort in `render-graph-redesign.md` section 8 operates on resource
producer/consumer declarations regardless of pass type. `DrawCullPass` writes
`IndirectDrawBuffer`; `BasePass` reads it. The sort places `DrawCullPass` before `BasePass`
automatically.

---

## 9. Framebuffer Skip in RenderGraph::Compile

`render-graph-redesign.md` section 10.1 describes the framebuffer skip guard. It is
reproduced here for completeness and to clarify the implementation obligation for
`RenderGraph::Compile`.

After pipeline creation and before the framebuffer creation loop, insert:

```cpp
for (auto& pass : Passes)
{
    if (!pass.Handle)
        continue;

    // Compute passes have no VkRenderPass object and no framebuffer.
    // pass.Framebuffer remains null. Execute() passes it as-is to Callback->Execute.
    if (pass.Handle->Specification.Type == RenderPassType::COMPUTE)
        continue;

    Specifications::FrameBufferSpecificationVNext fb_spec = {
        .Width         = pass.Handle->RenderAreaWidth,
        .Height        = pass.Handle->RenderAreaHeight,
        .RenderTargets = pass.Handle->RenderTargets,
        .Attachment    = pass.Handle->Attachment,
    };
    pass.Framebuffer = ZPushStructCtorArgs(
        Device->Arena, Buffers::FramebufferVNext, Device, fb_spec);
}
```

`pass.Framebuffer` is null for every `COMPUTE` pass after `Compile()`. The `Execute` loop
in section 6.4 of `render-graph-redesign.md` passes it directly to `Callback->Execute`; the
`IComputeCallbackPass::Execute` override receives a null framebuffer and is defined not to
call `BeginRenderPass`. Any compute pass that attempts to call `BeginRenderPass` with a null
framebuffer will produce a validation error and an assertion failure in `BeginRenderPass`.

The `Resize` path also skips framebuffer rebuild for compute passes. The same guard applies
in `RenderGraph::Resize` where it iterates `OutputsResized(pass)`:

```cpp
for (auto& pass : Passes)
{
    if (pass.Handle && pass.Handle->Specification.Type == RenderPassType::COMPUTE)
        continue;
    if (OutputsResized(pass))
        RebuildFramebuffer(pass, width, height);
}
```

---

## 10. File Layout and Deliverables Checklist

Every item below is a discrete deliverable. Items marked as files to create are new; all
others are modifications to existing files.

### Source changes

- [ ] `ZEngine/Rendering/Shaders/ShaderEnums.h` — add `COMPUTE = 4` to `ShaderType`
- [ ] `ZEngine/Rendering/Shaders/Compilers/CompilationStage.cpp` — add `EShLangCompute` branch to `GetEShLanguage`
- [ ] `ZEngine/Rendering/Renderers/Pipelines/RendererPipeline.h` — add `ComputePipeline` struct declaration
- [ ] `ZEngine/Rendering/Renderers/Pipelines/RendererPipeline.cpp` — add `ComputePipeline::Initialize`, `ComputePipeline::Bake`, `ComputePipeline::Dispose` implementations
- [ ] `ZEngine/Hardwares/VulkanDevice.h` — add `void Dispatch(uint32_t x, uint32_t y, uint32_t z)` declaration to `CommandBuffer`
- [ ] `ZEngine/Hardwares/VulkanDevice.cpp` — add `CommandBuffer::Dispatch` implementation calling `vkCmdDispatch`
- [ ] `ZEngine/Rendering/Renderers/RenderPasses/RenderPass.h` — add `ComputePipeline* ComputePipeline = nullptr` field; add `ComputePassBuilder` struct
- [ ] `ZEngine/Rendering/Renderers/RenderPasses/RenderPass.cpp` — update `RenderPass::Initialize` and `RenderPass::Bake` with COMPUTE branch; add `ComputePassBuilder` method implementations
- [ ] `ZEngine/Rendering/Renderers/RenderPasses/IComputeCallbackPass.h` — create new file; declare `IComputeCallbackPass`
- [ ] `ZEngine/Rendering/Renderers/RenderPasses/IComputeCallbackPass.cpp` — create new file; implement `Compile` and `Execute` overrides
- [ ] `ZEngine/Rendering/RenderGraph/RenderGraph.cpp` — add COMPUTE skip guard in the framebuffer creation loop and the resize loop
- [ ] `ZEngine/Rendering/Renderers/GraphicRenderer.cpp` — add SSAO, bloom threshold, and draw cull pass instances to `RegisterPasses`

### Compute pass implementations

- [ ] `ZEngine/Rendering/Renderers/PostProcessing/SSAOComputePass.h` — create; declare `SSAOComputePass`
- [ ] `ZEngine/Rendering/Renderers/PostProcessing/SSAOComputePass.cpp` — create; implement `SetupCompute` and `ExecuteCompute`
- [ ] `ZEngine/Rendering/Renderers/PostProcessing/BloomThresholdComputePass.h` — create; declare `BloomThresholdComputePass`
- [ ] `ZEngine/Rendering/Renderers/PostProcessing/BloomThresholdComputePass.cpp` — create; implement `SetupCompute` and `ExecuteCompute`
- [ ] `ZEngine/Rendering/Renderers/Culling/DrawCullPass.h` — create; declare `DrawCullPass`
- [ ] `ZEngine/Rendering/Renderers/Culling/DrawCullPass.cpp` — create; implement `SetupCompute` and `ExecuteCompute`

### Shader assets

- [ ] `Assets/Shaders/ssao_compute.comp` — create; GLSL source from section 7a
- [ ] `Assets/Shaders/bloom_threshold_compute.comp` — create; GLSL source from section 7b
- [ ] `Assets/Shaders/draw_cull_compute.comp` — create; GLSL source from section 7c

### Tests

- [ ] `Tests/Rendering/Shaders/ComputeShaderCompilationTest.cpp` — verify that a `.comp` source with `ShaderType::COMPUTE` produces non-empty `BinarySource` and passes `ValidationStage` without error
- [ ] `Tests/Rendering/Pipelines/ComputePipelineBakeTest.cpp` — call `ComputePipeline::Initialize` + `ComputePipeline::Bake` against a real `VulkanDevice` instance; assert `Handle != VK_NULL_HANDLE` and no validation layer errors
- [ ] `Tests/Rendering/CommandBuffer/DispatchTest.cpp` — record `CommandBuffer::Dispatch(4, 4, 1)` in a one-time command buffer; verify via mock or validation layer that `vkCmdDispatch(4, 4, 1)` was emitted
- [ ] `Tests/Rendering/RenderGraph/SSAOComputePassIntegrationTest.cpp` — run one full frame with `SSAOComputePass` registered; assert zero Vulkan validation layer errors; assert `SSAOOcclusion` texture transitions from `UNDEFINED` to `VK_IMAGE_LAYOUT_GENERAL` before dispatch and from `GENERAL` to `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` before the first consumer pass reads it
