#include <pch.h>
#include <Rendering/Renderers/Pipelines/RendererPipeline.h>
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
        /*Pipeline fixed states*/
        /*
         * Dynamic State
         */
        std::array<VkDynamicState, 2>    dynamic_state_collection  = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {};
        dynamic_state_create_info.sType                            = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state_create_info.dynamicStateCount                = dynamic_state_collection.size();
        dynamic_state_create_info.pDynamicStates                   = dynamic_state_collection.data();
        /*
         * Viewports and Scissors
         */
        VkPipelineViewportStateCreateInfo viewport_state_create_info = {};
        viewport_state_create_info.sType                             = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state_create_info.viewportCount                     = 1;
        viewport_state_create_info.scissorCount                      = 1;
        /*
         * Input Assembly
         */
        VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info = {};
        input_assembly_state_create_info.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly_state_create_info.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_assembly_state_create_info.primitiveRestartEnable                 = VK_FALSE;
        input_assembly_state_create_info.flags                                  = 0;
        input_assembly_state_create_info.pNext                                  = nullptr;
        /*
         * Vertex Input
         */
        std::vector<VkVertexInputBindingDescription> vertex_input_bindings = {};
        std::transform(
            m_pipeline_specification.VertexInputBindingSpecifications.begin(),
            m_pipeline_specification.VertexInputBindingSpecifications.end(),
            std::back_inserter(vertex_input_bindings),
            [](const Specifications::VertexInputBindingSpecification& input) {
                return VkVertexInputBindingDescription{.binding = input.Binding, .stride = input.Stride, .inputRate = (VkVertexInputRate) input.Rate};
            });

        std::vector<VkVertexInputAttributeDescription> vertex_input_attributes = {};
        std::transform(
            m_pipeline_specification.VertexInputAttributeSpecifications.begin(),
            m_pipeline_specification.VertexInputAttributeSpecifications.end(),
            std::back_inserter(vertex_input_attributes),
            [](const Specifications::VertexInputAttributeSpecification& input) {
                return VkVertexInputAttributeDescription{
                    .location = input.Location, .binding = input.Binding, .format = Specifications::ImageFormatMap[VALUE_FROM_SPEC_MAP(input.Format)], .offset = input.Offset};
            });

        VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info = {};
        vertex_input_state_create_info.sType                                = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input_state_create_info.vertexBindingDescriptionCount        = vertex_input_bindings.size();
        vertex_input_state_create_info.pVertexBindingDescriptions           = vertex_input_bindings.data();
        vertex_input_state_create_info.vertexAttributeDescriptionCount      = vertex_input_attributes.size();
        vertex_input_state_create_info.pVertexAttributeDescriptions         = vertex_input_attributes.data();
        /*
         * Rasterizer
         */
        VkPipelineRasterizationStateCreateInfo rasterization_create_info = {};
        rasterization_create_info.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization_create_info.depthClampEnable                       = VK_FALSE;
        rasterization_create_info.rasterizerDiscardEnable                = VK_FALSE;
        rasterization_create_info.polygonMode                            = VK_POLYGON_MODE_FILL;
        rasterization_create_info.lineWidth                              = 1.0f;
        rasterization_create_info.cullMode                               = VK_CULL_MODE_NONE;
        rasterization_create_info.frontFace                              = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization_create_info.depthBiasEnable                        = VK_FALSE;
        rasterization_create_info.depthBiasConstantFactor                = 0.0f; // Optional
        rasterization_create_info.depthBiasClamp                         = 0.0f; // Optional
        rasterization_create_info.depthBiasSlopeFactor                   = 0.0f; // Optional
        rasterization_create_info.pNext                                  = nullptr;
        /*
         * Multisampling
         */
        VkPipelineMultisampleStateCreateInfo multisample_state_create_info = {};
        multisample_state_create_info.sType                                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample_state_create_info.sampleShadingEnable                  = VK_FALSE;
        multisample_state_create_info.rasterizationSamples                 = VK_SAMPLE_COUNT_1_BIT;
        multisample_state_create_info.minSampleShading                     = 1.0f;
        multisample_state_create_info.pNext                                = nullptr;
        /*
         * Depth and Stencil testing
         */
        VkPipelineDepthStencilStateCreateInfo depth_stencil_state_create_info = {};
        depth_stencil_state_create_info.sType                                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil_state_create_info.depthTestEnable                       = m_pipeline_specification.EnableDepthTest ? VK_TRUE : VK_FALSE;
        depth_stencil_state_create_info.depthWriteEnable                      = m_pipeline_specification.EnableDepthTest ? VK_TRUE : VK_FALSE;
        depth_stencil_state_create_info.depthCompareOp                        = VK_COMPARE_OP_LESS;
        depth_stencil_state_create_info.depthBoundsTestEnable                 = VK_FALSE;
        depth_stencil_state_create_info.minDepthBounds                        = 0.0f;
        depth_stencil_state_create_info.maxDepthBounds                        = 1.0f;
        depth_stencil_state_create_info.stencilTestEnable                     = m_pipeline_specification.EnableStencilTest ? VK_TRUE : VK_FALSE;
        /*
         * Color blend state and attachment
         */
        VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
        color_blend_attachment_state.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        color_blend_attachment_state.blendEnable         = VK_TRUE;
        color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color_blend_attachment_state.colorBlendOp        = VK_BLEND_OP_ADD;
        color_blend_attachment_state.srcAlphaBlendFactor = m_pipeline_specification.EnableBlending ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
        color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_blend_attachment_state.alphaBlendOp        = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo color_blend_state_create_info = {};
        color_blend_state_create_info.sType                               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blend_state_create_info.logicOpEnable                       = VK_FALSE;
        color_blend_state_create_info.logicOp                             = VK_LOGIC_OP_COPY; // Optional
        color_blend_state_create_info.attachmentCount                     = 1;
        color_blend_state_create_info.pAttachments                        = &color_blend_attachment_state;
        color_blend_state_create_info.blendConstants[0]                   = 0.0f; // Optional
        color_blend_state_create_info.blendConstants[1]                   = 0.0f; // Optional
        color_blend_state_create_info.blendConstants[2]                   = 0.0f; // Optional
        color_blend_state_create_info.blendConstants[3]                   = 0.0f; // Optional
        color_blend_state_create_info.flags                               = 0;
        color_blend_state_create_info.pNext                               = nullptr;
        /*
         * Pipeline layout
         */
        const auto                 descriptor_set_layout_collection = m_shader->GetDescriptorSetLayout();
        const auto&                push_constant_collection         = m_shader->GetPushConstants();
        VkPipelineLayoutCreateInfo pipeline_layout_create_info      = {};
        pipeline_layout_create_info.sType                           = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_create_info.setLayoutCount                  = descriptor_set_layout_collection.size(); // Optional
        pipeline_layout_create_info.pSetLayouts                     = descriptor_set_layout_collection.data(); // Optional
        pipeline_layout_create_info.pushConstantRangeCount          = push_constant_collection.size();
        pipeline_layout_create_info.pPushConstantRanges             = push_constant_collection.data();
        pipeline_layout_create_info.flags                           = 0;
        pipeline_layout_create_info.pNext                           = nullptr;
        ZENGINE_VALIDATE_ASSERT(vkCreatePipelineLayout(device, &(pipeline_layout_create_info), nullptr, &m_pipeline_layout) == VK_SUCCESS, "Failed to create pipeline layout")
        /*
         * Graphic Pipeline Creation
         */
        const auto&                  shader_create_info_collection = m_shader->GetStageCreateInfoCollection();
        VkGraphicsPipelineCreateInfo graphic_pipeline_create_info  = {};
        graphic_pipeline_create_info.sType                         = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        graphic_pipeline_create_info.stageCount                    = shader_create_info_collection.size();
        graphic_pipeline_create_info.pStages                       = shader_create_info_collection.data();
        graphic_pipeline_create_info.pVertexInputState             = &(vertex_input_state_create_info);
        graphic_pipeline_create_info.pInputAssemblyState           = &(input_assembly_state_create_info);
        graphic_pipeline_create_info.pViewportState                = &(viewport_state_create_info);
        graphic_pipeline_create_info.pRasterizationState           = &(rasterization_create_info);
        graphic_pipeline_create_info.pMultisampleState             = &(multisample_state_create_info);
        graphic_pipeline_create_info.pDepthStencilState            = m_pipeline_specification.EnableDepthTest ? &(depth_stencil_state_create_info) : nullptr;
        graphic_pipeline_create_info.pColorBlendState              = &(color_blend_state_create_info);
        graphic_pipeline_create_info.pDynamicState                 = &(dynamic_state_create_info);
        graphic_pipeline_create_info.layout                        = m_pipeline_layout;

        ZENGINE_VALIDATE_ASSERT(m_pipeline_specification.Attachment, "Target framebuffer can't be null")
        graphic_pipeline_create_info.renderPass = m_pipeline_specification.Attachment->GetHandle();

        graphic_pipeline_create_info.subpass            = 0;
        graphic_pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE; // Optional
        graphic_pipeline_create_info.basePipelineIndex  = -1;             // Optional
        graphic_pipeline_create_info.flags              = 0;              // Optional
        graphic_pipeline_create_info.pNext              = nullptr;        // Optional
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