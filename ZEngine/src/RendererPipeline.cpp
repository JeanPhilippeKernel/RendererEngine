#include <pch.h>
#include <Rendering/Renderers/Pipelines/RendererPipeline.h>
#include <Rendering/Shaders/ShaderReader.h>
#include <Helpers/RendererHelper.h>
#include <Hardwares/VulkanDevice.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    GraphicPipeline::GraphicPipeline(Specifications::GraphicRendererPipelineSpecification&& spec) : m_pipeline_specification(std::move(spec)) {}

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

        /*Shader definition*/
        VkShaderModule                  vertex_module                   = VK_NULL_HANDLE;
        VkPipelineShaderStageCreateInfo vertex_shader_stage_create_info = {};
        std::vector<char>               vertex_shader_binary_code       = Rendering::Shaders::ShaderReader::ReadAsBinary(m_pipeline_specification.VertexShaderFilename);
        VkShaderModuleCreateInfo        vertex_shader_create_info       = {};
        vertex_shader_create_info.sType                                 = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vertex_shader_create_info.codeSize                              = vertex_shader_binary_code.size();
        vertex_shader_create_info.pCode                                 = reinterpret_cast<const uint32_t*>(vertex_shader_binary_code.data());
        ZENGINE_VALIDATE_ASSERT(vkCreateShaderModule(device, &vertex_shader_create_info, nullptr, &vertex_module) == VK_SUCCESS, "Failed to create ShaderModule")
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
        ZENGINE_VALIDATE_ASSERT(vkCreateShaderModule(device, &fragment_shader_create_info, nullptr, &fragment_module) == VK_SUCCESS, "Failed to create ShaderModule")
        fragment_shader_stage_create_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragment_shader_stage_create_info.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragment_shader_stage_create_info.module = fragment_module;
        fragment_shader_stage_create_info.pName  = "main";

        std::vector<BindingDescriptorEntry> binding_entries = {};

        std::vector<VkDescriptorSetLayoutBinding> layout_binding_collection = {};
        for (const auto& layout_binding_pair : m_pipeline_specification.LayoutBindingMap)
        {
            const auto& layout_binding = layout_binding_pair.second;
            layout_binding_collection.emplace_back(VkDescriptorSetLayoutBinding{
                .binding            = layout_binding.Binding,
                .descriptorType     = Specifications::DescriptorTypeMap[static_cast<uint32_t>(layout_binding.DescriptorType)],
                .descriptorCount    = layout_binding.Count,
                .stageFlags         = Specifications::ShaderStageFlagsMap[static_cast<uint32_t>(layout_binding.Flags)],
                .pImmutableSamplers = nullptr});

            binding_entries.emplace_back(
                BindingDescriptorEntry{.Binding = layout_binding.Binding, .Type = Specifications::DescriptorTypeMap[static_cast<uint32_t>(layout_binding.DescriptorType)]});
        }
        VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {};
        descriptor_set_layout_create_info.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptor_set_layout_create_info.bindingCount                    = layout_binding_collection.size();
        descriptor_set_layout_create_info.pBindings                       = layout_binding_collection.data();
        ZENGINE_VALIDATE_ASSERT(
            vkCreateDescriptorSetLayout(device, &descriptor_set_layout_create_info, nullptr, &m_descriptor_set_layout) == VK_SUCCESS, "Failed to create DescriptorSetLayout")

        /*Pipeline layout Creation*/
        std::array<VkDescriptorSetLayout, 1> descriptor_set_layout_collection       = {m_descriptor_set_layout};
        m_pipeline_specification.StateSpecification.LayoutCreateInfo.setLayoutCount = descriptor_set_layout_collection.size();
        m_pipeline_specification.StateSpecification.LayoutCreateInfo.pSetLayouts    = descriptor_set_layout_collection.data();
        m_pipeline_layout                                                           = Helpers::CreatePipelineLayout(m_pipeline_specification.StateSpecification.LayoutCreateInfo);

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
            vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphic_pipeline_create_info, nullptr, &m_pipeline_handle) == VK_SUCCESS, "Failed to create Graphics Pipeline")

        // Cleanup ShaderModules
        vkDestroyShaderModule(device, vertex_module, nullptr);
        vkDestroyShaderModule(device, fragment_module, nullptr);
        /*
         * Create DescriptorPool
         */
        std::vector<VkDescriptorPoolSize> pool_sizes = {};
        for (const auto& pool_type_count_pair : m_pool_type_count_map)
        {
            pool_sizes.emplace_back(VkDescriptorPoolSize{.type = static_cast<VkDescriptorType>(pool_type_count_pair.first), .descriptorCount = pool_type_count_pair.second});
        }
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags                      = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets                    = 10 * pool_sizes.size();
        pool_info.poolSizeCount              = pool_sizes.size();
        pool_info.pPoolSizes                 = pool_sizes.data();

        ZENGINE_VALIDATE_ASSERT(vkCreateDescriptorPool(device, &pool_info, nullptr, &m_descriptor_pool) == VK_SUCCESS, "Failed to create DescriptorPool")
        /*
         * Updating Binding index
         */
        for (auto& pass_input_pair : m_type_buffer_pass_input_map)
        {
            auto  descriptor_type  = pass_input_pair.first;
            auto& input_collection = pass_input_pair.second;

            for (auto& input : input_collection)
            {
                auto find_it = std::find_if(binding_entries.begin(), binding_entries.end(), [&](const GraphicPipeline::BindingDescriptorEntry& entry) {
                    return entry.IsAvailable && (entry.Type == descriptor_type);
                });
                if (find_it != binding_entries.end())
                {
                    input.Binding      = find_it->Binding;
                    find_it->IsAvailable = false;
                }
            }
        }
    }

    VkDescriptorSet GraphicPipeline::GetDescriptorSet() const
    {
        return m_descriptor_set;
    }

    void GraphicPipeline::Dispose()
    {
        Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::PIPELINE_LAYOUT, m_pipeline_layout);
        Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::PIPELINE, m_pipeline_handle);
        Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::DESCRIPTORSETLAYOUT, m_descriptor_set_layout);
        Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::DESCRIPTORPOOL, m_descriptor_pool);
        m_pipeline_layout       = VK_NULL_HANDLE;
        m_pipeline_handle       = VK_NULL_HANDLE;
        m_descriptor_set_layout = VK_NULL_HANDLE;
        m_descriptor_pool       = VK_NULL_HANDLE;
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

    const VkDescriptorSetLayout GraphicPipeline::GetDescriptorSetLayout() const
    {
        return m_descriptor_set_layout;
    }

    void GraphicPipeline::SetUniformBuffer(std::string_view key_name, const Ref<Rendering::Buffers::UniformBuffer>& buffer)
    {
        auto type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        __CreateOrIncrementPoolTypeCount(type);
        auto& pass_input = m_type_buffer_pass_input_map[type].emplace_back(BufferPassInput{.DebugName = key_name.data(), .Buffer = buffer});
    }

    void GraphicPipeline::SetStorageBuffer(std::string_view key_name, const Ref<Rendering::Buffers::StorageBuffer>& buffer)
    {
        auto type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        __CreateOrIncrementPoolTypeCount(type);
        auto& pass_input = m_type_buffer_pass_input_map[type].emplace_back(BufferPassInput{.DebugName = key_name.data(), .Buffer = buffer});
    }

    void GraphicPipeline::UpdateDescriptorSets()
    {
        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        /*
         * Ensure that Buffer Handle is valid
         */
        bool found_invalid_handle = false;
        for (const auto& pass_input_pair : m_type_buffer_pass_input_map)
        {
            auto& input_collection = pass_input_pair.second;

            for (uint32_t i = 0; i < input_collection.size(); ++i)
            {
                if (input_collection[i].Buffer->GetNativeBufferHandle() == nullptr)
                {
                    found_invalid_handle = true;
                    break;
                }
            }
        }
        if (found_invalid_handle)
        {
            return;
        }
        /*
         * Create DescriptorSet
         */
        if (m_descriptor_set != VK_NULL_HANDLE)
        {
            return;
        }

        VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {};
        descriptor_set_allocate_info.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptor_set_allocate_info.descriptorPool              = m_descriptor_pool;
        descriptor_set_allocate_info.descriptorSetCount          = 1;
        descriptor_set_allocate_info.pSetLayouts                 = &(m_descriptor_set_layout);
        ZENGINE_VALIDATE_ASSERT(vkAllocateDescriptorSets(device, &descriptor_set_allocate_info, &m_descriptor_set) == VK_SUCCESS, "Failed to create DescriptorSet")
        /*
         * Write Buffers
         */
        for (const auto& pass_input_pair : m_type_buffer_pass_input_map)
        {
            auto  descriptor_type  = pass_input_pair.first;
            auto& input_collection = pass_input_pair.second;

            std::vector<uint32_t>               binding_index_collection        = {};
            std::vector<VkDescriptorBufferInfo> buffer_info_collection          = {};
            std::vector<VkWriteDescriptorSet>   write_descriptor_set_collection = {};

            for (uint32_t i = 0; i < input_collection.size(); ++i)
            {
                auto native_buffer      = reinterpret_cast<VkBuffer>(input_collection[i].Buffer->GetNativeBufferHandle());
                auto native_buffer_size = static_cast<VkDeviceSize>(input_collection[i].Buffer->GetByteSize());
                buffer_info_collection.emplace_back(VkDescriptorBufferInfo{.buffer = native_buffer, .offset = 0, .range = native_buffer_size});
                binding_index_collection.push_back(input_collection[i].Binding);
            }

            for (uint32_t i = 0; i < buffer_info_collection.size(); ++i)
            {
                write_descriptor_set_collection.emplace_back(VkWriteDescriptorSet{
                    .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .pNext            = nullptr,
                    .dstSet           = m_descriptor_set,
                    .dstBinding       = binding_index_collection[i],
                    .dstArrayElement  = 0,
                    .descriptorCount  = 1,
                    .descriptorType   = descriptor_type,
                    .pImageInfo       = nullptr,
                    .pBufferInfo      = &buffer_info_collection[i],
                    .pTexelBufferView = nullptr});
            }
            if (!write_descriptor_set_collection.empty())
            {
                vkUpdateDescriptorSets(device, write_descriptor_set_collection.size(), write_descriptor_set_collection.data(), 0, nullptr);
            }
        }
    }

    Ref<GraphicPipeline> GraphicPipeline::Create(Specifications::GraphicRendererPipelineSpecification& spec)
    {
        auto pipeline = CreateRef<GraphicPipeline>(std::move(spec));
        return pipeline;
    }

    void GraphicPipeline::__CreateOrIncrementPoolTypeCount(VkDescriptorType type)
    {
        if (!m_pool_type_count_map.contains(type))
        {
            m_pool_type_count_map[type] = 1;
            return;
        }
        auto count                  = m_pool_type_count_map[type];
        m_pool_type_count_map[type] = ++count;
    }
} // namespace ZEngine::Rendering::Renderers::Pipelines