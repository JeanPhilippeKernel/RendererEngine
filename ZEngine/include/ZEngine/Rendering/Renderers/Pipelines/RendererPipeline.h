#pragma once
#include <vulkan/vulkan.h>
#include <ZEngineDef.h>
#include <Rendering/Buffers/Framebuffer.h>
#include <Rendering/Buffers/UniformBuffer.h>
#include <Rendering/Buffers/StorageBuffer.h>
#include <Rendering/Buffers/GraphicBuffer.h>
#include <Rendering/Shaders/Shader.h>
#include <Rendering/Specifications/GraphicRendererPipelineSpecification.h>


namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct RenderPass;
}

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct BufferPassInput
    {
        uint32_t                     Set{0};
        uint32_t                     Binding{0};
        uint32_t                     DescriptorType;
        std::string                  DebugName;
        Hardwares::BufferImage       BufferImage;
        Ref<Buffers::IGraphicBuffer> Buffer;
    };

    struct GraphicPipeline
    {
    public:
        GraphicPipeline() = default;
        GraphicPipeline(Specifications::GraphicRendererPipelineSpecification&& spec);
        ~GraphicPipeline() = default;

        Specifications::GraphicRendererPipelineSpecification& GetSpecification();
        void                                                  SetSpecification(Specifications::GraphicRendererPipelineSpecification& spec);
        void                                                  Bake();
        void                                                  Dispose();

        VkPipeline                     GetHandle() const;
        VkPipelineLayout               GetPipelineLayout() const;
        VkRenderPass                   GetRenderPassHandle() const;
        Ref<Buffers::FramebufferVNext> GetTargetFrameBuffer() const;
        Ref<Shaders::Shader>           GetShader() const;

    public:
        static Ref<GraphicPipeline> Create(Specifications::GraphicRendererPipelineSpecification& spec);

    protected:
        Ref<Shaders::Shader>                                                 m_shader;
        VkPipeline                                                           m_pipeline_handle{VK_NULL_HANDLE};
        VkPipelineLayout                                                     m_pipeline_layout{VK_NULL_HANDLE};
        Ref<Buffers::FramebufferVNext>                                       m_target_framebuffer;
        std::map<uint32_t, std::unordered_map<std::string, BufferPassInput>> m_type_buffer_pass_input_map;
        Specifications::GraphicRendererPipelineSpecification                 m_pipeline_specification;
        friend struct Rendering::Renderers::RenderPasses::RenderPass;

    private:
        /*
         *
         */
        std::unordered_map<VkDescriptorSet, std::vector<uint64_t>> m_descriptor_set_binding_buffer_map;
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines