#pragma once
#include <vulkan/vulkan.h>
#include <ZEngineDef.h>
#include <Rendering/Buffers/Framebuffer.h>
#include <Rendering/Buffers/UniformBuffer.h>
#include <Rendering/Specifications/GraphicRendererPipelineSpecification.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
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

        void SetUniformBuffer(Ref<Buffers::UniformBuffer>, uint32_t binding, uint32_t set = 0);

        VkPipeline                     GetHandle() const;
        VkPipelineLayout               GetPipelineLayout() const;
        VkRenderPass                   GetRenderPassHandle() const;
        Ref<Buffers::FramebufferVNext> GetTargetFrameBuffer() const;
        std::vector<VkDescriptorSet>   GetDescriptorSetCollection() const;
        const VkDescriptorSet*         GetDescriptorSetCollectionData() const;
        uint32_t                       GetDescriptorSetCollectionCount() const;

    public:
        static Ref<GraphicPipeline> Create(Specifications::GraphicRendererPipelineSpecification& spec);

    protected:
        VkPipeline                                           m_pipeline_handle{VK_NULL_HANDLE};
        VkPipelineLayout                                     m_pipeline_layout{VK_NULL_HANDLE};
        VkDescriptorSetLayout                                m_descriptor_set_layout{VK_NULL_HANDLE};
        std::vector<VkDescriptorSet>                         m_descriptor_set_collection;
        Ref<Buffers::FramebufferVNext>                       m_target_framebuffer;
        Specifications::GraphicRendererPipelineSpecification m_pipeline_specification;
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines