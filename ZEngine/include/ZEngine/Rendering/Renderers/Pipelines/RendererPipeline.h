#pragma once
#include <vulkan/vulkan.h>
#include <ZEngineDef.h>
#include <Rendering/Buffers/Framebuffer.h>
#include <Rendering/Buffers/UniformBuffer.h>
#include <Rendering/Buffers/StorageBuffer.h>
#include <Rendering/Buffers/GraphicBuffer.h>
#include <Rendering/Specifications/GraphicRendererPipelineSpecification.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct BufferPassInput
    {
        uint32_t                     Binding{0};
        std::string                  DebugName;
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
        VkDescriptorSet                GetDescriptorSet() const;
        Ref<Buffers::FramebufferVNext> GetTargetFrameBuffer() const;
        const VkDescriptorSetLayout    GetDescriptorSetLayout() const;

        void SetUniformBuffer(std::string_view key_name, const Ref<Rendering::Buffers::UniformBuffer>& buffer);
        void SetStorageBuffer(std::string_view key_name, const Ref<Rendering::Buffers::StorageBuffer>& buffer);

        void UpdateDescriptorSets();

    public:
        static Ref<GraphicPipeline> Create(Specifications::GraphicRendererPipelineSpecification& spec);

    protected:
        VkDescriptorSet                                                    m_descriptor_set{VK_NULL_HANDLE};
        VkDescriptorPool                                                   m_descriptor_pool{VK_NULL_HANDLE};
        VkPipeline                                                         m_pipeline_handle{VK_NULL_HANDLE};
        VkPipelineLayout                                                   m_pipeline_layout{VK_NULL_HANDLE};
        VkDescriptorSetLayout                                              m_descriptor_set_layout{VK_NULL_HANDLE};
        std::vector<VkDescriptorSet>                                       m_descriptor_set_collection;
        Ref<Buffers::FramebufferVNext>                                     m_target_framebuffer;
        std::unordered_map<VkDescriptorType, uint32_t>                     m_pool_type_count_map;
        std::unordered_map<VkDescriptorType, std::vector<BufferPassInput>> m_type_buffer_pass_input_map = {};
        Specifications::GraphicRendererPipelineSpecification               m_pipeline_specification;

    private:
        struct BindingDescriptorEntry
        {
            bool             IsAvailable{true};
            uint32_t         Binding;
            VkDescriptorType Type;
        };
        void __CreateOrIncrementPoolTypeCount(VkDescriptorType type);
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines