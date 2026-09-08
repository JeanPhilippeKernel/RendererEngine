#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Rendering/Buffers/Framebuffer.h>
#include <ZEngine/Rendering/Renderers/Pipelines/RendererPipeline.h>
#include <ZEngine/Rendering/Specifications/RenderPassSpecification.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <vulkan/vulkan.h>
#include <set>
#include <unordered_set>

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct RenderPass
    {
        RenderPass() {}
        ~RenderPass();

        uint32_t                                RenderAreaWidth  = 0;
        uint32_t                                RenderAreaHeight = 0;

        Specifications::RenderPassSpecification Specification    = {};
        std::set<std::string>                   Inputs           = {};
        Core::Containers::Array<uint32_t>       RenderTargets    = {};
        Renderers::RenderPasses::Attachment*    Attachment       = {nullptr};
        Pipelines::GraphicPipeline*             Pipeline         = {nullptr};

        void                                    Initialize(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification);
        void                                    Dispose();
        void                                    Bake();
        bool                                    Verify();

        // Bind a storage buffer (STORAGE_BUFFER) to all frame descriptor sets by name.
        void                                    SetStorageBuffer(std::string_view name, const Core::Memory::BufferView* buffer);

        // Bind a per-frame dynamic uniform (UNIFORM_BUFFER_DYNAMIC) from the FrameHeap.
        void                                    SetDynamicUniform(std::string_view name, VkDeviceSize range);

        // Bind a single texture as SAMPLED_IMAGE (or the type declared in the shader).
        void                                    SetTexture(std::string_view name, const Textures::TextureHandle& texture);

        // Bind a sampler at compile time (SAMPLER). Use for LinearWrapSampler etc.
        void                                    SetSampler(cstring name, const VkDescriptorImageInfo& sampler_info);

        // Connects this pass to the engine's global bindless TextureArray
        // (set=1, binding=0, 600 slots). Registers descriptor sets for per-frame
        // texture slot updates via DeviceSwapchain::Present().
        // Asserts that the named binding is VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE.
        void                                    UseTextureArray(std::string_view name);

        void                                    UpdateInputBinding();
        ZRawPtr(Renderers::RenderPasses::Attachment) GetAttachment() const;
        void     UpdateRenderTargets();
        uint32_t GetRenderAreaWidth() const;
        uint32_t GetRenderAreaHeight() const;

    private:
        std::pair<bool, Specifications::LayoutBindingSpecification> ValidateInput(std::string_view key);

    private:
        Hardwares::VulkanDevice* m_device;
    };
    ZDEFINE_PTR(RenderPass);

    struct RenderPassBuilder
    {
        Core::Memory::ArenaAllocator*           Arena = nullptr;

        void                                    Initialize(Core::Memory::ArenaAllocator* arena);

        RenderPassBuilder&                      SetName(std::string_view name);
        RenderPassBuilder&                      SetPipelineName(std::string_view name);
        RenderPassBuilder&                      EnablePipelineBlending(bool value);
        RenderPassBuilder&                      EnablePipelineDepthTest(bool value);
        RenderPassBuilder&                      EnablePipelineDepthWrite(bool value);
        RenderPassBuilder&                      PipelineDepthCompareOp(uint32_t value);
        RenderPassBuilder&                      SetShaderOverloadMaxSet(uint32_t count);
        RenderPassBuilder&                      SetOverloadPoolSize(uint32_t count);
        RenderPassBuilder&                      SetCullMode(uint32_t);

        RenderPassBuilder&                      SetInputBindingCount(uint32_t count);
        RenderPassBuilder&                      SetStride(uint32_t input_binding_index, uint32_t value);
        RenderPassBuilder&                      SetRate(uint32_t input_binding_index, uint32_t value);

        RenderPassBuilder&                      SetInputAttributeCount(uint32_t count);
        RenderPassBuilder&                      SetLocation(uint32_t input_attribute_index, uint32_t value);
        RenderPassBuilder&                      SetBinding(uint32_t input_attribute_index, uint32_t input_binding_index);
        RenderPassBuilder&                      SetFormat(uint32_t input_attribute_index, Specifications::ImageFormat value);
        RenderPassBuilder&                      SetOffset(uint32_t input_attribute_index, uint32_t offset);

        RenderPassBuilder&                      UseShader(std::string_view name);
        RenderPassBuilder&                      UseRenderTarget(const Textures::TextureHandle& target);
        RenderPassBuilder&                      AddRenderTarget(const Specifications::TextureSpecification& target_spec);
        RenderPassBuilder&                      AddInputAttachment(const Textures::TextureHandle& target);
        RenderPassBuilder&                      AddInputTexture(std::string_view key, const Rendering::Textures::TextureHandle& input);
        RenderPassBuilder&                      UseSwapchainAsRenderTarget();

        Specifications::RenderPassSpecification Detach();

    private:
        Specifications::RenderPassSpecification m_spec{};
    };
} // namespace ZEngine::Rendering::Renderers::RenderPasses
