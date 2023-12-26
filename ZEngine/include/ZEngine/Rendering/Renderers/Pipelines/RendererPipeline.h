#pragma once
#include <vulkan/vulkan.h>
#include <ZEngineDef.h>
#include <Rendering/Buffers/Framebuffer.h>
#include <Rendering/Shaders/Shader.h>
#include <Rendering/Specifications/GraphicRendererPipelineSpecification.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct GraphicPipeline : public Helpers::RefCounted
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
        Ref<Rendering::Swapchain>      GetTargetSwapchain() const;
        Ref<Shaders::Shader>           GetShader() const;

    public:
        static Ref<GraphicPipeline> Create(Specifications::GraphicRendererPipelineSpecification& spec);

    protected:
        VkPipeline                                           m_pipeline_handle{VK_NULL_HANDLE};
        VkPipelineLayout                                     m_pipeline_layout{VK_NULL_HANDLE};
        Specifications::GraphicRendererPipelineSpecification m_pipeline_specification;
        Ref<Shaders::Shader>                                 m_shader;
        Ref<Buffers::FramebufferVNext>                       m_target_framebuffer;
        Ref<Rendering::Swapchain>                            m_target_swapchain;
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines