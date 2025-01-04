#pragma once
#include <Helpers/IntrusivePtr.h>
#include <Rendering/Buffers/Framebuffer.h>
#include <Rendering/Buffers/GraphicBuffer.h>
#include <Rendering/Buffers/IndirectBuffer.h>
#include <Rendering/Buffers/StorageBuffer.h>
#include <Rendering/Buffers/UniformBuffer.h>
#include <Rendering/Renderers/Pipelines/RendererPipeline.h>
#include <Rendering/Specifications/RenderPassSpecification.h>
#include <Rendering/Textures/Texture.h>
#include <vulkan/vulkan.h>
#include <unordered_set>
#include <vector>

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    enum PassInputType
    {
        UNIFORM_BUFFER_SET,
        STORAGE_BUFFER_SET,
        BINDLESS_TEXTURE,
        UNIFORM_BUFFER,
        STORAGE_BUFFER,
        TEXTURE
    };

    struct PassInput
    {
        uint32_t                         Set{0};
        uint32_t                         Binding{0};
        std::string                      DebugName;
        PassInputType                    Type;
        Textures::TextureHandle          TextureHandle;
        Buffers::UniformBufferSetHandle  UniformBufferSetHandle;
        Buffers::StorageBufferSetHandle  BufferSetHandle;
        Buffers::IndirectBufferSetHandle IndirectBufferSetHandle;
    };

    struct RenderPass : public Helpers::RefCounted
    {
        RenderPass(Hardwares::VulkanDevice* device, const Specifications::RenderPassSpecification& specification);
        ~RenderPass();

        uint32_t                                          RenderAreaWidth                    = 0;
        uint32_t                                          RenderAreaHeight                   = 0;
        Specifications::RenderPassSpecification           Specification                      = {};
        std::map<std::string, PassInput>                  Inputs                             = {};
        std::unordered_set<std::string>                   EnqueuedUpdateInputs               = {};
        std::vector<Hardwares::WriteDescriptorSetRequest> EnqueuedWriteDescriptorSetRequests = {};
        std::vector<uint32_t>                             RenderTargets                      = {};
        Helpers::Ref<Renderers::RenderPasses::Attachment> Attachment                         = {nullptr};
        Helpers::Ref<Pipelines::GraphicPipeline>          Pipeline                           = {nullptr};
        void                                              Dispose();
        void                                              Bake();
        bool                                              Verify();
        void                                              Update(uint32_t frame_index);
        void                                              MarkDirty();
        void                                              SetInput(std::string_view key_name, const Rendering::Buffers::UniformBufferSetHandle& buffer);
        void                                              SetInput(std::string_view key_name, const Rendering::Buffers::StorageBufferSetHandle& buffer);
        void                                              SetInput(std::string_view key_name, const Textures::TextureHandle& texture);
        void                                              SetBindlessInput(std::string_view key_name);
        void                                              UpdateInputBinding();
        Helpers::Ref<Renderers::RenderPasses::Attachment> GetAttachment() const;
        void                                              UpdateRenderTargets();
        uint32_t                                          GetRenderAreaWidth() const;
        uint32_t                                          GetRenderAreaHeight() const;

    private:
        std::pair<bool, Specifications::LayoutBindingSpecification> ValidateInput(std::string_view key);

    private:
        bool                     m_perform_update{false};
        Hardwares::VulkanDevice* m_device;
    };

    struct RenderPassBuilder : public Helpers::RefCounted
    {
        RenderPassBuilder& SetName(std::string_view name);
        RenderPassBuilder& SetPipelineName(std::string_view name);
        RenderPassBuilder& EnablePipelineBlending(bool value);
        RenderPassBuilder& EnablePipelineDepthTest(bool value);
        RenderPassBuilder& EnablePipelineDepthWrite(bool value);
        RenderPassBuilder& PipelineDepthCompareOp(uint32_t value);
        RenderPassBuilder& SetShaderOverloadMaxSet(uint32_t count);
        RenderPassBuilder& SetOverloadPoolSize(uint32_t count);

        RenderPassBuilder& SetInputBindingCount(uint32_t count);
        RenderPassBuilder& SetStride(uint32_t input_binding_index, uint32_t value);
        RenderPassBuilder& SetRate(uint32_t input_binding_index, uint32_t value);

        RenderPassBuilder& SetInputAttributeCount(uint32_t count);
        RenderPassBuilder& SetLocation(uint32_t input_attribute_index, uint32_t value);
        RenderPassBuilder& SetBinding(uint32_t input_attribute_index, uint32_t input_binding_index);
        RenderPassBuilder& SetFormat(uint32_t input_attribute_index, Specifications::ImageFormat value);
        RenderPassBuilder& SetOffset(uint32_t input_attribute_index, uint32_t offset);

        RenderPassBuilder& UseShader(std::string_view name);
        RenderPassBuilder& UseRenderTarget(const Textures::TextureHandle& target);
        RenderPassBuilder& AddRenderTarget(const Specifications::TextureSpecification& target_spec);
        RenderPassBuilder& AddInputAttachment(const Textures::TextureHandle& target);
        RenderPassBuilder& AddInputTexture(std::string_view key, const Rendering::Textures::TextureHandle& input);
        RenderPassBuilder& UseSwapchainAsRenderTarget();

        Specifications::RenderPassSpecification Detach();

    private:
        Specifications::RenderPassSpecification m_spec{};
    };
} // namespace ZEngine::Rendering::Renderers::RenderPasses