#pragma once
#include <vector>
#include <ZEngineDef.h>
#include <vulkan/vulkan.h>
#include <Rendering/Specifications/RenderPassSpecification.h>
#include <Rendering/Buffers/UniformBuffer.h>
#include <Rendering/Buffers/StorageBuffer.h>
#include <Rendering/Buffers/GraphicBuffer.h>
#include <Rendering/Textures/Texture.h>
#include <Rendering/Buffers/Framebuffer.h>
#include <Rendering/Renderers/Pipelines/RendererPipeline.h>

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    enum PassInputType
    {
        UNIFORM_BUFFER_SET,
        STORAGE_BUFFER_SET,
        TEXTURE_ARRAY,
        UNIFORM_BUFFER,
        STORAGE_BUFFER,
        TEXTURE
    };

    union InputData
    {
        void* Data{nullptr};
    };

    struct PassInput
    {
        uint32_t      Set{0};
        uint32_t      Binding{0};
        std::string   DebugName;
        PassInputType Type;
        InputData     Input;
    };

    struct RenderPass : public Helpers::RefCounted
    {
        RenderPass() = default;
        RenderPass(const Specifications::RenderPassSpecification& specification);
        ~RenderPass();

        void Dispose();

        Ref<Pipelines::GraphicPipeline> GetPipeline() const;
        void                            Bake();
        bool                            Verify(std::string_view key);
        void                            Update();
        void                            MarkDirty();
        void                            SetInput(std::string_view key_name, const Ref<Rendering::Buffers::UniformBufferSet>& buffer);
        void                            SetInput(std::string_view key_name, const Ref<Rendering::Buffers::StorageBufferSet>& buffer);
        void                            SetInput(std::string_view key_name, const Ref<Textures::TextureArray>& textures);
        void                            SetInput(std::string_view key_name, const Ref<Rendering::Buffers::UniformBuffer>& buffer);
        void                            SetInput(std::string_view key_name, const Ref<Rendering::Buffers::StorageBuffer>& buffer);
        void                            SetInput(std::string_view key_name, const Ref<Textures::Texture>& buffer);
        void                            UpdateInputBinding();
        Ref<Textures::Texture>          GetOutputColor(uint32_t color_index);
        Ref<Textures::Texture>          GetOutputDepth();

        const Specifications::RenderPassSpecification& GetSpecification() const;
        Specifications::RenderPassSpecification&       GetSpecification();

        Ref<Renderers::RenderPasses::Attachment> GetAttachment() const;

        void          ResizeFramebuffer();
        VkFramebuffer GetFramebuffer() const;
        uint32_t      GetRenderAreaWidth() const;
        uint32_t      GetRenderAreaHeight() const;

        static Ref<RenderPass> Create(const Specifications::RenderPassSpecification& specification);

    private:
        std::pair<bool, Specifications::LayoutBindingSpecification> ValidateInput(std::string_view key);
        void                                                        InitializeExpectedInputs();

    private:
        bool                                     m_perform_update{false};
        Specifications::RenderPassSpecification  m_specification;
        std::vector<PassInput>                   m_input_collection;
        Ref<Pipelines::GraphicPipeline>          m_pipeline;
        Ref<Renderers::RenderPasses::Attachment> m_attachment;
        Ref<Buffers::FramebufferVNext>           m_framebuffer;
        std::set<std::string>                    m_expected_inputs;
    };

    struct RenderPassBuilder : public Helpers::RefCounted
    {
        RenderPassBuilder& SetName(std::string_view name);
        RenderPassBuilder& SetPipelineName(std::string_view name);
        RenderPassBuilder& EnablePipelineBlending(bool value);
        RenderPassBuilder& EnablePipelineDepthTest(bool value);
        RenderPassBuilder& EnablePipelineDepthWrite(bool value);
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
        RenderPassBuilder& UseRenderTarget(const Ref<Rendering::Textures::Texture>& target);
        RenderPassBuilder& AddRenderTarget(const Specifications::TextureSpecification& target_spec);
        RenderPassBuilder& AddInputAttachment(const Ref<Rendering::Textures::Texture>& target);
        RenderPassBuilder& AddInputTexture(std::string_view key, const Ref<Rendering::Textures::Texture>& input);
        RenderPassBuilder& UseSwapchainAsRenderTarget();

        Ref<RenderPass> Create();

    private:
        Specifications::RenderPassSpecification m_spec{};
    };
} // namespace ZEngine::Rendering::Renderers::RenderPasses