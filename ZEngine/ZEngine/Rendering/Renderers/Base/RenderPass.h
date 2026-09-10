#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/HashSet.h>
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Rendering/Buffers/Framebuffer.h>
#include <ZEngine/Rendering/Renderers/Pipelines/RendererPipeline.h>
#include <ZEngine/Rendering/Specifications/RenderPassSpecification.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    // Base node stored by the render graph. The concrete subtype is determined by
    // Specification.Type at creation time (VulkanDevice::CreateRenderPass).
    struct RenderPass
    {
        RenderPass()                                                                                                                               = default;
        virtual ~RenderPass()                                                                                                                      = default;

        Specifications::RenderPassSpecification Specification                                                                                      = {};

        virtual void                            Initialize(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification) = 0;
        virtual void                            Dispose()                                                                                          = 0;
        virtual void                            Bake()                                                                                             = 0;
        virtual bool                            Verify()
        {
            return true;
        }
    };
    ZDEFINE_PTR(RenderPass);

    struct GraphicPass : RenderPass
    {
    public:
        ~GraphicPass();

        uint32_t                           RenderAreaWidth  = 0;
        uint32_t                           RenderAreaHeight = 0;

        Core::Containers::HashSet<cstring> BoundBindings    = {};
        Core::Containers::Array<uint32_t>  RenderTargets    = {};
        struct Attachment*                 Attachment       = {nullptr};
        Pipelines::GraphicPipeline*        Pipeline         = {nullptr};

        void                               Initialize(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification) override;
        void                               Dispose() override;
        void                               Bake() override;
        bool                               Verify() override;

        void                               SetStorageBuffer(std::string_view name, const Core::Memory::BufferView* buffer);
        void                               SetDynamicUniform(std::string_view name, VkDeviceSize range);
        void                               SetTexture(std::string_view name, const Textures::TextureHandle& texture);
        void                               SetSampler(cstring name, const VkDescriptorImageInfo& sampler_info);
        void                               UseTextureArray(std::string_view name);

        void                               UpdateInputBinding();
        struct Attachment*                 GetAttachment() const;
        void                               UpdateRenderTargets();
        uint32_t                           GetRenderAreaWidth() const;
        uint32_t                           GetRenderAreaHeight() const;

    private:
        std::pair<bool, Specifications::LayoutBindingSpecification> ValidateInput(std::string_view key);

    private:
        Hardwares::VulkanDevice* m_device = nullptr;
    };
    ZDEFINE_PTR(GraphicPass);

    struct ComputePass : RenderPass
    {
    public:
        ~ComputePass();

        Pipelines::ComputePipeline* Pipeline = {nullptr};

        void                        Initialize(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification) override;
        void                        Dispose() override;
        void                        Bake() override;

    private:
        Hardwares::VulkanDevice* m_device = nullptr;
    };
    ZDEFINE_PTR(ComputePass);

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
        RenderPassBuilder&                      UseComputeShader(cstring name, uint32_t push_constant_size = 0);
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
