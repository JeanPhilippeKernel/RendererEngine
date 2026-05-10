#include <Hardwares/VulkanDevice.h>
#include <Rendering/Renderers/Pipelines/RendererPipeline.h>
#include <array>

using namespace ZEngine::Helpers;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers::Pipelines
{
    void GraphicPipeline::Initialize(Hardwares::VulkanDevice* device, Specifications::GraphicRendererPipelineSpecification&& spec)
    {
        Device             = device;
        Specification      = std::move(spec);
        auto shader_handle = Device->CompileShader(Specification.ShaderSpecificationValue);
        if (!shader_handle)
        {
            ZENGINE_CORE_ERROR("")
            return;
        }

        Shader = Device->ShaderManager.Access(shader_handle);
    }

    void GraphicPipeline::Bake()
    {

        auto                             scratch                                = ZGetScratch(Device->Arena);
        /*Pipeline fixed states*/
        /*
         * Dynamic State
         */
        std::array<VkDynamicState, 2>    dynamic_state_collection               = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic_state_create_info              = {};
        dynamic_state_create_info.sType                                         = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state_create_info.dynamicStateCount                             = dynamic_state_collection.size();
        dynamic_state_create_info.pDynamicStates                                = dynamic_state_collection.data();
        /*
         * Viewports and Scissors
         */
        VkPipelineViewportStateCreateInfo viewport_state_create_info            = {};
        viewport_state_create_info.sType                                        = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state_create_info.viewportCount                                = 1;
        viewport_state_create_info.scissorCount                                 = 1;
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
        Array<VkVertexInputBindingDescription> vertex_input_bindings            = {};
        vertex_input_bindings.init(scratch.Arena, 5);
        for (unsigned i = 0; i < Specification.VertexInputBindingSpecifications.size(); ++i)
        {
            auto& input = Specification.VertexInputBindingSpecifications[i];
            vertex_input_bindings.push(VkVertexInputBindingDescription{.binding = input.Binding, .stride = input.Stride, .inputRate = (VkVertexInputRate) input.Rate});
        }

        Array<VkVertexInputAttributeDescription> vertex_input_attributes = {};
        vertex_input_attributes.init(scratch.Arena, 5);
        for (unsigned i = 0; i < Specification.VertexInputAttributeSpecifications.size(); ++i)
        {
            auto& input = Specification.VertexInputAttributeSpecifications[i];
            vertex_input_attributes.push(VkVertexInputAttributeDescription{.location = input.Location, .binding = input.Binding, .format = Specifications::ImageFormatMap[VALUE_FROM_SPEC_MAP(input.Format)], .offset = input.Offset});
        }

        VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info   = {};
        vertex_input_state_create_info.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input_state_create_info.vertexBindingDescriptionCount          = vertex_input_bindings.size();
        vertex_input_state_create_info.pVertexBindingDescriptions             = vertex_input_bindings.data();
        vertex_input_state_create_info.vertexAttributeDescriptionCount        = vertex_input_attributes.size();
        vertex_input_state_create_info.pVertexAttributeDescriptions           = vertex_input_attributes.data();
        /*
         * Rasterizer
         */
        VkPipelineRasterizationStateCreateInfo rasterization_create_info      = {};
        rasterization_create_info.sType                                       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization_create_info.depthClampEnable                            = VK_FALSE;
        rasterization_create_info.rasterizerDiscardEnable                     = VK_FALSE;
        rasterization_create_info.polygonMode                                 = VK_POLYGON_MODE_FILL;
        rasterization_create_info.lineWidth                                   = 1.0f;
        rasterization_create_info.cullMode                                    = (VkCullModeFlagBits) Specification.CullMode;
        rasterization_create_info.frontFace                                   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization_create_info.depthBiasEnable                             = VK_FALSE;
        rasterization_create_info.depthBiasConstantFactor                     = 0.0f; // Optional
        rasterization_create_info.depthBiasClamp                              = 0.0f; // Optional
        rasterization_create_info.depthBiasSlopeFactor                        = 0.0f; // Optional
        rasterization_create_info.pNext                                       = nullptr;
        /*
         * Multisampling
         */
        VkPipelineMultisampleStateCreateInfo multisample_state_create_info    = {};
        multisample_state_create_info.sType                                   = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample_state_create_info.sampleShadingEnable                     = VK_FALSE;
        multisample_state_create_info.rasterizationSamples                    = VK_SAMPLE_COUNT_1_BIT;
        multisample_state_create_info.minSampleShading                        = 1.0f;
        multisample_state_create_info.pNext                                   = nullptr;
        /*
         * Depth and Stencil testing
         */
        VkPipelineDepthStencilStateCreateInfo depth_stencil_state_create_info = {};
        depth_stencil_state_create_info.sType                                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil_state_create_info.stencilTestEnable                     = Specification.EnableStencilTest ? VK_TRUE : VK_FALSE;
        depth_stencil_state_create_info.front                                 = {};
        depth_stencil_state_create_info.back                                  = {};
        depth_stencil_state_create_info.depthTestEnable                       = Specification.EnableDepthTest ? VK_TRUE : VK_FALSE;
        depth_stencil_state_create_info.depthCompareOp                        = VkCompareOp(Specification.DepthCompareOp);
        depth_stencil_state_create_info.depthWriteEnable                      = Specification.EnableDepthWrite ? VK_TRUE : VK_FALSE;
        depth_stencil_state_create_info.minDepthBounds                        = 0.0f;
        depth_stencil_state_create_info.maxDepthBounds                        = 1.0f;
        /*
         * Color blend state and attachment
         */
        ZENGINE_VALIDATE_ASSERT(Specification.Attachment, "Attachment can't be null")

        uint32_t                                   attachment_count = Specification.Attachment->GetColorAttachmentCount();
        Array<VkPipelineColorBlendAttachmentState> color_blend_attachment_states{};
        color_blend_attachment_states.init(scratch.Arena, attachment_count, attachment_count);
        for (uint32_t i = 0; i < attachment_count; ++i)
        {
            color_blend_attachment_states[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

            if (Specification.EnableBlending)
            {
                color_blend_attachment_states[i].blendEnable         = VK_TRUE;

                // todo (jpkernel): those config should be expose at the pass builder level
                // Color: Standard Alpha Blending
                color_blend_attachment_states[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                color_blend_attachment_states[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                color_blend_attachment_states[i].colorBlendOp        = VK_BLEND_OP_ADD;

                // Alpha: Accumulate opaqueness
                color_blend_attachment_states[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                color_blend_attachment_states[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                color_blend_attachment_states[i].alphaBlendOp        = VK_BLEND_OP_ADD;
            }
            else
            {
                color_blend_attachment_states[i].blendEnable         = VK_FALSE;
                // Standard "Opaque" settings
                color_blend_attachment_states[i].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                color_blend_attachment_states[i].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
                color_blend_attachment_states[i].colorBlendOp        = VK_BLEND_OP_ADD;
                color_blend_attachment_states[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                color_blend_attachment_states[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                color_blend_attachment_states[i].alphaBlendOp        = VK_BLEND_OP_ADD;
            }
        }

        VkPipelineColorBlendStateCreateInfo color_blend_state_create_info = {};
        color_blend_state_create_info.sType                               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blend_state_create_info.logicOpEnable                       = VK_FALSE;
        color_blend_state_create_info.logicOp                             = VK_LOGIC_OP_COPY; // Optional
        color_blend_state_create_info.attachmentCount                     = color_blend_attachment_states.size();
        color_blend_state_create_info.pAttachments                        = color_blend_attachment_states.data();
        color_blend_state_create_info.blendConstants[0]                   = 0.0f; // Optional
        color_blend_state_create_info.blendConstants[1]                   = 0.0f; // Optional
        color_blend_state_create_info.blendConstants[2]                   = 0.0f; // Optional
        color_blend_state_create_info.blendConstants[3]                   = 0.0f; // Optional
        color_blend_state_create_info.flags                               = 0;
        color_blend_state_create_info.pNext                               = nullptr;
        /*
         * Pipeline layout
         */
        VkPipelineLayoutCreateInfo pipeline_layout_create_info            = {};
        pipeline_layout_create_info.sType                                 = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_create_info.setLayoutCount                        = Shader->SetLayouts.size(); // Optional
        pipeline_layout_create_info.pSetLayouts                           = Shader->SetLayouts.data(); // Optional
        pipeline_layout_create_info.pushConstantRangeCount                = Shader->PushConstants.size();
        pipeline_layout_create_info.pPushConstantRanges                   = Shader->PushConstants.data();
        pipeline_layout_create_info.flags                                 = 0;
        pipeline_layout_create_info.pNext                                 = nullptr;
        ZENGINE_VALIDATE_ASSERT(vkCreatePipelineLayout(Device->LogicalDevice, &(pipeline_layout_create_info), nullptr, &Layout) == VK_SUCCESS, "Failed to create pipeline layout")
        /*
         * Graphic Pipeline Creation
         */
        VkGraphicsPipelineCreateInfo graphic_pipeline_create_info = {};
        graphic_pipeline_create_info.sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        graphic_pipeline_create_info.stageCount                   = Shader->ShaderCreateInfos.size();
        graphic_pipeline_create_info.pStages                      = Shader->ShaderCreateInfos.data();
        graphic_pipeline_create_info.pVertexInputState            = &(vertex_input_state_create_info);
        graphic_pipeline_create_info.pInputAssemblyState          = &(input_assembly_state_create_info);
        graphic_pipeline_create_info.pViewportState               = &(viewport_state_create_info);
        graphic_pipeline_create_info.pRasterizationState          = &(rasterization_create_info);
        graphic_pipeline_create_info.pMultisampleState            = &(multisample_state_create_info);
        graphic_pipeline_create_info.pDepthStencilState           = Specification.EnableDepthTest ? &(depth_stencil_state_create_info) : nullptr;
        graphic_pipeline_create_info.pColorBlendState             = &(color_blend_state_create_info);
        graphic_pipeline_create_info.pDynamicState                = &(dynamic_state_create_info);
        graphic_pipeline_create_info.layout                       = Layout;
        graphic_pipeline_create_info.renderPass                   = Specification.Attachment->GetHandle();
        graphic_pipeline_create_info.subpass                      = 0;
        graphic_pipeline_create_info.basePipelineHandle           = VK_NULL_HANDLE; // Optional
        graphic_pipeline_create_info.basePipelineIndex            = -1;             // Optional
        graphic_pipeline_create_info.flags                        = 0;              // Optional
        graphic_pipeline_create_info.pNext                        = nullptr;        // Optional
        ZENGINE_VALIDATE_ASSERT(vkCreateGraphicsPipelines(Device->LogicalDevice, VK_NULL_HANDLE, 1, &graphic_pipeline_create_info, nullptr, &Handle) == VK_SUCCESS, "Failed to create Graphics Pipeline")

        ZReleaseScratch(scratch);
    }

    void GraphicPipeline::Dispose()
    {
        Shader->Dispose();

        Device->EnqueueForDeletion(Rendering::DeviceResourceType::PIPELINE_LAYOUT, Layout);
        Device->EnqueueForDeletion(Rendering::DeviceResourceType::PIPELINE, Handle);
        Layout = VK_NULL_HANDLE;
        Handle = VK_NULL_HANDLE;
    }
} // namespace ZEngine::Rendering::Renderers::Pipelines
