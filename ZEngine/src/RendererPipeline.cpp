#include <pch.h>
#include <Rendering/Renderers/Pipelines/RendererPipeline.h>
#include <Rendering/Shaders/ShaderReader.h>
#include <Helpers/RendererHelper.h>
#include <Hardwares/VulkanDevice.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    GraphicPipeline::GraphicPipeline(Pipelines::GraphicRendererPipelineSpecification&& spec) :m_pipeline_specification(std::move(spec)) {}

    Pipelines::GraphicRendererPipelineSpecification& GraphicPipeline::GetSpecification()
    {
        return m_pipeline_specification;
    }

    void GraphicPipeline::SetSpecification(Pipelines::GraphicRendererPipelineSpecification& spec)
    {
        m_pipeline_specification = std::move(spec);
    }

    void GraphicPipeline::Bake()
    {
        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();

        /*Framebuffer Creation*/
        m_target_framebuffer = CreateRef<Buffers::FramebufferVNext>(m_pipeline_specification.TargetFrameBufferSpecification);

        /*Pipeline fixed states*/
        Helpers::FillDefaultPipelineFixedStates(m_pipeline_specification.StateSpecification);

        /*Shader definition*/
        VkShaderModule                  vertex_module                   = VK_NULL_HANDLE;
        VkPipelineShaderStageCreateInfo vertex_shader_stage_create_info = {};
        std::vector<char>               vertex_shader_binary_code       = Rendering::Shaders::ShaderReader::ReadAsBinary(m_pipeline_specification.VertexShaderFilename);
        VkShaderModuleCreateInfo        vertex_shader_create_info       = {};
        vertex_shader_create_info.sType                                 = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vertex_shader_create_info.codeSize                              = vertex_shader_binary_code.size();
        vertex_shader_create_info.pCode                                 = reinterpret_cast<const uint32_t*>(vertex_shader_binary_code.data());
        ZENGINE_VALIDATE_ASSERT(
            vkCreateShaderModule(device, &vertex_shader_create_info, nullptr, &vertex_module) == VK_SUCCESS, "Failed to create ShaderModule")
        vertex_shader_stage_create_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertex_shader_stage_create_info.stage  = VK_SHADER_STAGE_VERTEX_BIT;
        vertex_shader_stage_create_info.module = vertex_module;
        vertex_shader_stage_create_info.pName  = "main";

        VkShaderModule                  fragment_module                   = VK_NULL_HANDLE;
        VkPipelineShaderStageCreateInfo fragment_shader_stage_create_info = {};
        std::vector<char>               fragment_shader_binary_code       = Rendering::Shaders::ShaderReader::ReadAsBinary(m_pipeline_specification.FragmentShaderFilename);
        VkShaderModuleCreateInfo        fragment_shader_create_info       = {};
        fragment_shader_create_info.sType                                 = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        fragment_shader_create_info.codeSize                              = fragment_shader_binary_code.size();
        fragment_shader_create_info.pCode                                 = reinterpret_cast<const uint32_t*>(fragment_shader_binary_code.data());
        ZENGINE_VALIDATE_ASSERT(
            vkCreateShaderModule(device, &fragment_shader_create_info, nullptr, &fragment_module) == VK_SUCCESS, "Failed to create ShaderModule")
        fragment_shader_stage_create_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragment_shader_stage_create_info.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragment_shader_stage_create_info.module = fragment_module;
        fragment_shader_stage_create_info.pName  = "main";

        VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {};
        descriptor_set_layout_create_info.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptor_set_layout_create_info.bindingCount                    = m_pipeline_specification.LayoutBindingCollection.size();
        descriptor_set_layout_create_info.pBindings                       = m_pipeline_specification.LayoutBindingCollection.data();
        ZENGINE_VALIDATE_ASSERT(
            vkCreateDescriptorSetLayout(device, &descriptor_set_layout_create_info, nullptr, &m_descriptor_set_layout) == VK_SUCCESS,
            "Failed to create DescriptorSetLayout")

        /*Pipeline layout Creation*/
        std::array<VkDescriptorSetLayout, 1> descriptor_set_layout_collection       = {m_descriptor_set_layout};
        m_pipeline_specification.StateSpecification.LayoutCreateInfo.setLayoutCount = descriptor_set_layout_collection.size();
        m_pipeline_specification.StateSpecification.LayoutCreateInfo.pSetLayouts    = descriptor_set_layout_collection.data();
        m_pipeline_layout = Helpers::CreatePipelineLayout(m_pipeline_specification.StateSpecification.LayoutCreateInfo);

        /*Graphic Pipeline Creation */
        std::vector<VkPipelineShaderStageCreateInfo> shader_create_info_collection = {vertex_shader_stage_create_info, fragment_shader_stage_create_info};
        VkGraphicsPipelineCreateInfo                 graphic_pipeline_create_info  = {};
        graphic_pipeline_create_info.sType                                         = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        graphic_pipeline_create_info.stageCount                                    = shader_create_info_collection.size();
        graphic_pipeline_create_info.pStages                                       = shader_create_info_collection.data();
        graphic_pipeline_create_info.pVertexInputState                             = &(m_pipeline_specification.StateSpecification.VertexInputStateCreateInfo);
        graphic_pipeline_create_info.pInputAssemblyState                           = &(m_pipeline_specification.StateSpecification.InputAssemblyStateCreateInfo);
        graphic_pipeline_create_info.pViewportState                                = &(m_pipeline_specification.StateSpecification.ViewportStateCreateInfo);
        graphic_pipeline_create_info.pRasterizationState                           = &(m_pipeline_specification.StateSpecification.RasterizationStateCreateInfo);
        graphic_pipeline_create_info.pMultisampleState                             = &(m_pipeline_specification.StateSpecification.MultisampleStateCreateInfo);
        graphic_pipeline_create_info.pDepthStencilState                            = &(m_pipeline_specification.StateSpecification.DepthStencilStateCreateInfo);
        graphic_pipeline_create_info.pColorBlendState                              = &(m_pipeline_specification.StateSpecification.ColorBlendStateCreateInfo);
        graphic_pipeline_create_info.pDynamicState                                 = &(m_pipeline_specification.StateSpecification.DynamicStateCreateInfo);
        graphic_pipeline_create_info.layout                                        = m_pipeline_layout;
        graphic_pipeline_create_info.renderPass                                    = m_target_framebuffer->GetRenderPass();
        graphic_pipeline_create_info.subpass                                       = 0;
        graphic_pipeline_create_info.basePipelineHandle                            = VK_NULL_HANDLE; // Optional
        graphic_pipeline_create_info.basePipelineIndex                             = -1;             // Optional
        graphic_pipeline_create_info.flags                                         = 0;              // Optional
        graphic_pipeline_create_info.pNext                                         = nullptr;        // Optional
        ZENGINE_VALIDATE_ASSERT(
            vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphic_pipeline_create_info, nullptr, &m_pipeline_handle) == VK_SUCCESS,
            "Failed to create Graphics Pipeline")

        // Cleanup ShaderModules
        vkDestroyShaderModule(device, vertex_module, nullptr);
        vkDestroyShaderModule(device, fragment_module, nullptr);

        /*DescriptorSet Creation*/
        {
            VkDescriptorSet             descriptor_set               = VK_NULL_HANDLE;
            VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {};
            descriptor_set_allocate_info.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptor_set_allocate_info.descriptorPool              = Hardwares::VulkanDevice::GetDescriptorPool();
            descriptor_set_allocate_info.descriptorSetCount          = 1;
            descriptor_set_allocate_info.pSetLayouts                 = &(m_descriptor_set_layout);
            ZENGINE_VALIDATE_ASSERT(
                vkAllocateDescriptorSets(device, &descriptor_set_allocate_info, &descriptor_set) == VK_SUCCESS, "Failed to create DescriptorSet")
            m_descriptor_set_collection.push_back(descriptor_set);
        }
    }

    void GraphicPipeline::Dispose()
    {
        Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::PIPELINE_LAYOUT, m_pipeline_layout);
        Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::PIPELINE, m_pipeline_handle);
        Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::DESCRIPTORSETLAYOUT, m_descriptor_set_layout);
        m_pipeline_layout       = VK_NULL_HANDLE;
        m_pipeline_handle       = VK_NULL_HANDLE;
        m_descriptor_set_layout = VK_NULL_HANDLE;
    }

    void GraphicPipeline::SetUniformBuffer(Ref<Buffers::UniformBuffer> uniform_buffer, uint32_t binding, uint32_t set)
    {
        auto                   device             = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        VkDescriptorBufferInfo buffer_info        = {};
        buffer_info.buffer                        = uniform_buffer->GetNativeBufferHandle();
        buffer_info.range                         = VK_WHOLE_SIZE;
        buffer_info.offset                        = 0;
        VkWriteDescriptorSet write_descriptor_set = {};
        write_descriptor_set.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor_set.dstSet               = m_descriptor_set_collection[set];
        write_descriptor_set.descriptorType       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write_descriptor_set.dstBinding           = binding;
        write_descriptor_set.dstArrayElement      = 0;
        write_descriptor_set.descriptorCount      = 1;
        write_descriptor_set.pBufferInfo          = &buffer_info;
        write_descriptor_set.pImageInfo           = nullptr; // Optional
        write_descriptor_set.pTexelBufferView     = nullptr; // Optional
        write_descriptor_set.pNext                = nullptr;
        vkUpdateDescriptorSets(device, 1, &(write_descriptor_set), 0, nullptr);
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

    std::vector<VkDescriptorSet> GraphicPipeline::GetDescriptorSetCollection() const
    {
        return m_descriptor_set_collection;
    }

    const VkDescriptorSet* GraphicPipeline::GetDescriptorSetCollectionData() const
    {
        return m_descriptor_set_collection.data();
    }

    uint32_t GraphicPipeline::GetDescriptorSetCollectionCount() const
    {
        return m_descriptor_set_collection.size();
    }

    Ref<GraphicPipeline> GraphicPipeline::Create(Pipelines::GraphicRendererPipelineSpecification& spec)
    {
        auto pipeline = CreateRef<GraphicPipeline>(std::move(spec));
        return pipeline;
    }
} // namespace ZEngine::Rendering::Renderers::Pipelines