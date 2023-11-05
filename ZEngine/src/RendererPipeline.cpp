#include <pch.h>
#include <Rendering/Renderers/Pipelines/RendererPipeline.h>
#include <Helpers/RendererHelper.h>
#include <Hardwares/VulkanDevice.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    GraphicPipeline::GraphicPipeline(Specifications::GraphicRendererPipelineSpecification&& spec) : m_pipeline_specification(std::move(spec))
    {
        m_shader = Shaders::Shader::Create(m_pipeline_specification.ShaderSpecification);
    }

    Specifications::GraphicRendererPipelineSpecification& GraphicPipeline::GetSpecification()
    {
        return m_pipeline_specification;
    }

    void GraphicPipeline::SetSpecification(Specifications::GraphicRendererPipelineSpecification& spec)
    {
        m_pipeline_specification = std::move(spec);
    }

    void GraphicPipeline::Bake()
    {
        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();

        /*Framebuffer Creation*/
        m_target_framebuffer = m_pipeline_specification.TargetFrameBufferSpecification;

        /*Pipeline fixed states*/
        Helpers::FillDefaultPipelineFixedStates(m_pipeline_specification.StateSpecification);

        /*Pipeline layout Creation*/
        auto descriptor_set_layout_collection                                       = m_shader->GetDescriptorSetLayout();
        m_pipeline_specification.StateSpecification.LayoutCreateInfo.setLayoutCount = descriptor_set_layout_collection.size();
        m_pipeline_specification.StateSpecification.LayoutCreateInfo.pSetLayouts    = descriptor_set_layout_collection.data();
        ZENGINE_VALIDATE_ASSERT(
            vkCreatePipelineLayout(device, &(m_pipeline_specification.StateSpecification.LayoutCreateInfo), nullptr, &m_pipeline_layout) == VK_SUCCESS,
            "Failed to create pipeline layout")

        /*Graphic Pipeline Creation */
        const auto&                  shader_create_info_collection = m_shader->GetStageCreateInfoCollection();
        VkGraphicsPipelineCreateInfo graphic_pipeline_create_info  = {};
        graphic_pipeline_create_info.sType                         = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        graphic_pipeline_create_info.stageCount                    = shader_create_info_collection.size();
        graphic_pipeline_create_info.pStages                       = shader_create_info_collection.data();
        graphic_pipeline_create_info.pVertexInputState             = &(m_pipeline_specification.StateSpecification.VertexInputStateCreateInfo);
        graphic_pipeline_create_info.pInputAssemblyState           = &(m_pipeline_specification.StateSpecification.InputAssemblyStateCreateInfo);
        graphic_pipeline_create_info.pViewportState                = &(m_pipeline_specification.StateSpecification.ViewportStateCreateInfo);
        graphic_pipeline_create_info.pRasterizationState           = &(m_pipeline_specification.StateSpecification.RasterizationStateCreateInfo);
        graphic_pipeline_create_info.pMultisampleState             = &(m_pipeline_specification.StateSpecification.MultisampleStateCreateInfo);
        graphic_pipeline_create_info.pDepthStencilState            = &(m_pipeline_specification.StateSpecification.DepthStencilStateCreateInfo);
        graphic_pipeline_create_info.pColorBlendState              = &(m_pipeline_specification.StateSpecification.ColorBlendStateCreateInfo);
        graphic_pipeline_create_info.pDynamicState                 = &(m_pipeline_specification.StateSpecification.DynamicStateCreateInfo);
        graphic_pipeline_create_info.layout                        = m_pipeline_layout;
        graphic_pipeline_create_info.renderPass                    = m_target_framebuffer->GetRenderPass();
        graphic_pipeline_create_info.subpass                       = 0;
        graphic_pipeline_create_info.basePipelineHandle            = VK_NULL_HANDLE; // Optional
        graphic_pipeline_create_info.basePipelineIndex             = -1;             // Optional
        graphic_pipeline_create_info.flags                         = 0;              // Optional
        graphic_pipeline_create_info.pNext                         = nullptr;        // Optional
        ZENGINE_VALIDATE_ASSERT(
            vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphic_pipeline_create_info, nullptr, &m_pipeline_handle) == VK_SUCCESS, "Failed to create Graphics Pipeline")
    }

    void GraphicPipeline::Dispose()
    {
        m_shader->Dispose();
        Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::PIPELINE_LAYOUT, m_pipeline_layout);
        Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::PIPELINE, m_pipeline_handle);
        m_pipeline_layout = VK_NULL_HANDLE;
        m_pipeline_handle = VK_NULL_HANDLE;
    }

    VkPipeline GraphicPipeline::GetHandle() const
    {
        return m_pipeline_handle;
    }

    VkPipelineLayout GraphicPipeline::GetPipelineLayout() const
    {
        return m_pipeline_layout;
    }

    VkRenderPass GraphicPipeline::GetRenderPassHandle() const
    {
        if (!m_target_framebuffer)
        {
            return VK_NULL_HANDLE;
        }
        return m_target_framebuffer->GetRenderPass();
    }

    Ref<Buffers::FramebufferVNext> GraphicPipeline::GetTargetFrameBuffer() const
    {
        return m_target_framebuffer;
    }

    Ref<Shaders::Shader> GraphicPipeline::GetShader() const
    {
        return m_shader;
    }

    Ref<GraphicPipeline> GraphicPipeline::Create(Specifications::GraphicRendererPipelineSpecification& spec)
    {
        auto pipeline = CreateRef<GraphicPipeline>(std::move(spec));
        return pipeline;
    }
} // namespace ZEngine::Rendering::Renderers::Pipelines