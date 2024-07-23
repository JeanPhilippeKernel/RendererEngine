#include <pch.h>
#include <vulkan/vulkan.h>
#include <spirv_cross.hpp>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Shaders/ShaderReader.h>
#include <Rendering/Shaders/Shader.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Renderers/GraphicRenderer.h>


using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Shaders
{

    Shader* CreateShader(const char* filename, bool defer_program_creation)
    {
        return nullptr;
    }
} // namespace ZEngine::Rendering::Shaders

namespace ZEngine::Rendering::Shaders
{

    Shader::Shader(const Specifications::ShaderSpecification& spec) : m_specification(spec)
    {
        CreateModule();
        CreateDescriptorSetLayouts();
        CreatePushConstantRange();
    }

    Shader::~Shader()
    {
        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        for (auto& shader_module : m_shader_module_collection)
        {
            vkDestroyShaderModule(device, shader_module, nullptr);
        }
        m_shader_module_collection.clear();
        m_shader_module_collection.shrink_to_fit();
    }

    void Shader::CreateModule()
    {
        auto                         device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        Scope<spirv_cross::Compiler> spirv_compiler;
        /*
         * Vertex Shader processing
         */
        if (!m_specification.VertexFilename.empty())
        {
            auto& shader_create_info_collection                 = m_shader_create_info_collection.emplace_back();
            auto&                    shader_module                 = m_shader_module_collection.emplace_back();
            std::vector<uint32_t>    vertex_shader_binary_code = Rendering::Shaders::ShaderReader::ReadAsBinary(m_specification.VertexFilename);
            VkShaderModuleCreateInfo vertex_shader_create_info = {};
            vertex_shader_create_info.sType                    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            vertex_shader_create_info.codeSize                 = vertex_shader_binary_code.size() * sizeof(uint32_t);
            vertex_shader_create_info.pCode                    = vertex_shader_binary_code.data();
            ZENGINE_VALIDATE_ASSERT(vkCreateShaderModule(device, &vertex_shader_create_info, nullptr, &shader_module) == VK_SUCCESS, "Failed to create ShaderModule")
            shader_create_info_collection.sType       = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_create_info_collection.stage  = VK_SHADER_STAGE_VERTEX_BIT;
            shader_create_info_collection.module      = shader_module;
            shader_create_info_collection.pName  = "main";
            /*
             * Source Reflection
             */
            spirv_compiler        = CreateScope<spirv_cross::Compiler>(vertex_shader_binary_code);
            auto vertex_resources = spirv_compiler->get_shader_resources();
            for (const auto& UB_resource : vertex_resources.uniform_buffers)
            {
                uint32_t set     = spirv_compiler->get_decoration(UB_resource.id, spv::DecorationDescriptorSet);
                uint32_t binding = spirv_compiler->get_decoration(UB_resource.id, spv::DecorationBinding);

                m_layout_binding_specification_map[set].emplace_back(LayoutBindingSpecification{
                    .Set = set, .Binding = binding, .Name = UB_resource.name, .DescriptorType = DescriptorType::UNIFORM_BUFFER, .Flags = ShaderStageFlags::VERTEX});
            }

            for (const auto& SB_resource : vertex_resources.storage_buffers)
            {
                uint32_t set     = spirv_compiler->get_decoration(SB_resource.id, spv::DecorationDescriptorSet);
                uint32_t binding = spirv_compiler->get_decoration(SB_resource.id, spv::DecorationBinding);

                m_layout_binding_specification_map[set].emplace_back(LayoutBindingSpecification{
                    .Set = set, .Binding = binding, .Name = SB_resource.name, .DescriptorType = DescriptorType::STORAGE_BUFFER, .Flags = ShaderStageFlags::VERTEX});
            }

            for (const auto& pushConstant_resource : vertex_resources.push_constant_buffers)
            {
                const spirv_cross::SPIRType& type   = spirv_compiler->get_type(pushConstant_resource.base_type_id);
                uint32_t                     struct_offset = !m_push_constant_specification_collection.empty() ? m_push_constant_specification_collection.back().Offset : 0;

                if (type.basetype == spirv_cross::SPIRType::Struct)
                {
                    uint32_t struct_total_size = 0;
                    for (uint32_t i = 0; i < type.member_types.size(); ++i)
                    {
                        uint32_t memberSize = spirv_compiler->get_declared_struct_member_size(type, i);
                        struct_total_size += memberSize;
                    }
                    m_push_constant_specification_collection.emplace_back(
                        PushConstantSpecification{.Name = pushConstant_resource.name, .Size = struct_total_size, .Offset = struct_offset, .Flags = ShaderStageFlags::VERTEX});
                    /*
                     * We update the offset for next iteration
                     */
                    struct_offset = struct_total_size;
                }
            }
        }
        /*
         * Fragment Shader processing
         */
        if (!m_specification.FragmentFilename.empty())
        {
            auto&                    shader_create_info_collection = m_shader_create_info_collection.emplace_back();
            auto&                    shader_module                 = m_shader_module_collection.emplace_back();
            std::vector<uint32_t>    fragment_shader_binary_code = Rendering::Shaders::ShaderReader::ReadAsBinary(m_specification.FragmentFilename);
            VkShaderModuleCreateInfo fragment_shader_create_info = {};
            fragment_shader_create_info.sType                    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            fragment_shader_create_info.codeSize                 = fragment_shader_binary_code.size() * sizeof(uint32_t);
            fragment_shader_create_info.pCode                    = fragment_shader_binary_code.data();
            ZENGINE_VALIDATE_ASSERT(vkCreateShaderModule(device, &fragment_shader_create_info, nullptr, &shader_module) == VK_SUCCESS, "Failed to create ShaderModule")
            shader_create_info_collection.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_create_info_collection.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
            shader_create_info_collection.module = shader_module;
            shader_create_info_collection.pName  = "main";
            /*
             * Source Reflection
             */
            spirv_compiler          = CreateScope<spirv_cross::Compiler>(fragment_shader_binary_code);
            auto fragment_resources = spirv_compiler->get_shader_resources();
            for (const auto& UB_resource : fragment_resources.uniform_buffers)
            {
                uint32_t set     = spirv_compiler->get_decoration(UB_resource.id, spv::DecorationDescriptorSet);
                uint32_t binding = spirv_compiler->get_decoration(UB_resource.id, spv::DecorationBinding);

                m_layout_binding_specification_map[set].emplace_back(LayoutBindingSpecification{
                    .Set = set, .Binding = binding, .Name = UB_resource.name, .DescriptorType = DescriptorType::UNIFORM_BUFFER, .Flags = ShaderStageFlags::FRAGMENT});
            }

            for (const auto& SB_resource : fragment_resources.storage_buffers)
            {
                uint32_t set     = spirv_compiler->get_decoration(SB_resource.id, spv::DecorationDescriptorSet);
                uint32_t binding = spirv_compiler->get_decoration(SB_resource.id, spv::DecorationBinding);

                m_layout_binding_specification_map[set].emplace_back(LayoutBindingSpecification{
                    .Set = set, .Binding = binding, .Name = SB_resource.name, .DescriptorType = DescriptorType::STORAGE_BUFFER, .Flags = ShaderStageFlags::FRAGMENT});
            }

            for (const auto& pushConstant_resource : fragment_resources.push_constant_buffers)
            {
                const spirv_cross::SPIRType& type          = spirv_compiler->get_type(pushConstant_resource.base_type_id);
                uint32_t                     struct_offset = !m_push_constant_specification_collection.empty() ? m_push_constant_specification_collection.back().Offset : 0;

                if (type.basetype == spirv_cross::SPIRType::Struct)
                {
                    uint32_t struct_total_size = 0;
                    for (uint32_t i = 0; i < type.member_types.size(); ++i)
                    {
                        uint32_t memberSize = spirv_compiler->get_declared_struct_member_size(type, i);
                        struct_total_size += memberSize;
                    }
                    m_push_constant_specification_collection.emplace_back(
                        PushConstantSpecification{.Name = pushConstant_resource.name, .Size = struct_total_size, .Offset = struct_offset, .Flags = ShaderStageFlags::FRAGMENT});
                    /*
                     * We update the offset for next iteration
                     */
                    struct_offset = struct_total_size;
                }
            }

            for (const auto& SI_resource : fragment_resources.sampled_images)
            {
                uint32_t set     = spirv_compiler->get_decoration(SI_resource.id, spv::DecorationDescriptorSet);
                uint32_t binding = spirv_compiler->get_decoration(SI_resource.id, spv::DecorationBinding);

                uint32_t    count = 1;
                const auto& type  = spirv_compiler->get_type(SI_resource.type_id);
                if (!type.array.empty())
                {
                    count = type.array[0];
                    if (count == 0) // Unsized arrays
                    {
                        const auto& device_property = Hardwares::VulkanDevice::GetPhysicalDeviceProperties();
                        count                       = device_property.limits.maxDescriptorSetSampledImages - 1;
                    }
                }

                m_layout_binding_specification_map[set].emplace_back(LayoutBindingSpecification{
                    .Set            = set,
                    .Binding        = binding,
                    .Count          = count,
                    .Name           = SI_resource.name,
                    .DescriptorType = DescriptorType::COMBINED_IMAGE_SAMPLER,
                    .Flags          = ShaderStageFlags::FRAGMENT});
            }
        }
    }

    const std::vector<VkPipelineShaderStageCreateInfo>& Shader::GetStageCreateInfoCollection() const
    {
        return m_shader_create_info_collection;
    }

    const std::map<uint32_t, std::vector<LayoutBindingSpecification>>& Shader::GetLayoutBindingSetMap() const
    {
        return m_layout_binding_specification_map;
    }

    Specifications::LayoutBindingSpecification Shader::GetLayoutBindingSpecification(std::string_view name) const
    {
        LayoutBindingSpecification binding_spec = {};
        for (const auto& layout_binding : m_layout_binding_specification_map)
        {
            const auto& binding_specification_collection = layout_binding.second;
            auto find_it = std::find_if(binding_specification_collection.begin(), binding_specification_collection.end(), [&](const LayoutBindingSpecification& spec) {
                return spec.Name == name;
            });

            if (find_it != std::end(binding_specification_collection))
            {
                binding_spec = *find_it;
                break;
            }
        }
        return binding_spec;
    }

    std::vector<VkDescriptorSetLayout> Shader::GetDescriptorSetLayout() const
    {
        std::vector<VkDescriptorSetLayout> set_layout_collection = {};
        for (const auto& layout : m_descriptor_set_layout_map)
        {
            set_layout_collection.emplace_back(layout.second);
        }
        return set_layout_collection;
    }

    std::vector<Specifications::LayoutBindingSpecification> Shader::GetLayoutBindingSpecificationList() const
    {
        std::vector<Specifications::LayoutBindingSpecification> m_list = {};

        for (const auto& layout_binding : m_layout_binding_specification_map)
        {
            for (const auto& spec : layout_binding.second)
            m_list.emplace_back(spec);
        }
        return m_list;
    }

    const std::map<uint32_t, std::vector<VkDescriptorSet>>& Shader::GetDescriptorSetMap() const
    {
        return m_descriptor_set_map;
    }

    std::map<uint32_t, std::vector<VkDescriptorSet>>& Shader::GetDescriptorSetMap()
    {
        return m_descriptor_set_map;
    }

    VkDescriptorPool Shader::GetDescriptorPool() const
    {
        return m_descriptor_pool;
    }

    const std::vector<VkPushConstantRange>& Shader::GetPushConstants() const
    {
        return m_push_constant_collection;
    }

    void Shader::Dispose()
    {
        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        for (auto& shader_module : m_shader_module_collection)
        {
            vkDestroyShaderModule(device, shader_module, nullptr);
        }
        m_shader_module_collection.clear();
        m_shader_module_collection.shrink_to_fit();

        for (auto& set_layout : m_descriptor_set_layout_map)
        {
            Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::DESCRIPTORSETLAYOUT, set_layout.second);
        }
        m_descriptor_set_layout_map.clear();

        Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::DESCRIPTORPOOL, m_descriptor_pool);
        m_descriptor_pool = VK_NULL_HANDLE;
    }

    void Shader::CreateDescriptorSetLayouts()
    {
        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        const auto& renderer_info = Renderers::GraphicRenderer::GetRendererInformation();

        std::vector<VkDescriptorPoolSize> pool_size_collection = {};

        for (const auto& layout_binding_set : m_layout_binding_specification_map)
        {
            uint32_t                                  binding_set               = layout_binding_set.first;
            std::vector<VkDescriptorSetLayoutBinding> layout_binding_collection = {};
            for (uint32_t i = 0; i < layout_binding_set.second.size(); ++i)
            {
                layout_binding_collection.emplace_back(VkDescriptorSetLayoutBinding{
                    .binding            = layout_binding_set.second[i].Binding,
                    .descriptorType     = DescriptorTypeMap[static_cast<uint32_t>(layout_binding_set.second[i].DescriptorType)],
                    .descriptorCount    = layout_binding_set.second[i].Count,
                    .stageFlags         = ShaderStageFlagsMap[static_cast<uint32_t>(layout_binding_set.second[i].Flags)],
                    .pImmutableSamplers = nullptr});
            }
            /*
             * Binding flag extension
             */
            std::vector<VkDescriptorBindingFlags> binding_flags_collection = {};
            binding_flags_collection.resize(layout_binding_collection.size());
            for (uint32_t i = 0; i < layout_binding_collection.size(); ++i)
            {
                if ((layout_binding_collection[i].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ||
                    (layout_binding_collection[i].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE))
                {
                    binding_flags_collection[i] =
                        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
                    continue;
                }
                binding_flags_collection[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
            }

            VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_create_info = {};
            binding_flags_create_info.sType                                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            binding_flags_create_info.bindingCount                                = binding_flags_collection.size();
            binding_flags_create_info.pBindingFlags                               = binding_flags_collection.data();
            /*
             * Creating SetLayout
             */
            VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {};
            descriptor_set_layout_create_info.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptor_set_layout_create_info.bindingCount                    = layout_binding_collection.size();
            descriptor_set_layout_create_info.pBindings                       = layout_binding_collection.data();
            descriptor_set_layout_create_info.flags                           = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            descriptor_set_layout_create_info.pNext                           = &binding_flags_create_info;

            VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
            ZENGINE_VALIDATE_ASSERT(
                vkCreateDescriptorSetLayout(device, &descriptor_set_layout_create_info, nullptr, &descriptor_set_layout) == VK_SUCCESS, "Failed to create DescriptorSetLayout")

            m_descriptor_set_layout_map[binding_set] = std::move(descriptor_set_layout);
            /*
             * Packing PoolSize
             */
            for (const auto& layout_binding : layout_binding_collection)
            {
                auto find_pool_size_it = std::find_if(pool_size_collection.begin(), pool_size_collection.end(), [&](const VkDescriptorPoolSize& pool_size) {
                    return (layout_binding.descriptorType == pool_size.type);
                });

                if (find_pool_size_it == std::end(pool_size_collection))
                {
                    pool_size_collection.emplace_back(VkDescriptorPoolSize{.type = layout_binding.descriptorType, .descriptorCount = layout_binding.descriptorCount});
                    continue;
                }
                /*
                 * ToDo: we should check the limit against the device..
                 */
                find_pool_size_it->descriptorCount++;
            }
            /*
             * Ensure correctness with number of frame count
             */
            for (auto& pool_size : pool_size_collection)
            {
                pool_size.descriptorCount *= renderer_info.FrameCount;
                pool_size.descriptorCount += m_specification.OverloadPoolSize;
            }
        }
        /*
         * Create DescriptorPool
         */
        ZENGINE_VALIDATE_ASSERT(!pool_size_collection.empty(), "The pool size can't be empty")

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags                      = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        pool_info.maxSets                    = renderer_info.FrameCount * pool_size_collection.size() * m_specification.OverloadMaxSet;
        pool_info.poolSizeCount              = pool_size_collection.size();
        pool_info.pPoolSizes                 = pool_size_collection.data();

        ZENGINE_VALIDATE_ASSERT(vkCreateDescriptorPool(device, &pool_info, nullptr, &m_descriptor_pool) == VK_SUCCESS, "Failed to create DescriptorPool")
        /*
         * Create DescriptorSet
         */
        for (const auto& layout : m_descriptor_set_layout_map)
        {
            m_descriptor_set_map[layout.first].resize(renderer_info.FrameCount);

            std::vector<VkDescriptorSetLayout> layout_set = {};
            layout_set.resize(renderer_info.FrameCount);
            for (uint32_t i = 0; i < renderer_info.FrameCount; ++i)
            {
                layout_set[i] = layout.second;
            }

            VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {};
            descriptor_set_allocate_info.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptor_set_allocate_info.descriptorPool              = m_descriptor_pool;
            descriptor_set_allocate_info.descriptorSetCount          = renderer_info.FrameCount;
            descriptor_set_allocate_info.pSetLayouts                 = layout_set.data();
            ZENGINE_VALIDATE_ASSERT(
                vkAllocateDescriptorSets(device, &descriptor_set_allocate_info, m_descriptor_set_map[layout.first].data()) == VK_SUCCESS, "Failed to create DescriptorSet")
        }
    }

    void Shader::CreatePushConstantRange()
    {
        for (const auto& push_constant_spec : m_push_constant_specification_collection)
        {
            m_push_constant_collection.emplace_back(VkPushConstantRange{
                .stageFlags = ShaderStageFlagsMap[VALUE_FROM_SPEC_MAP(push_constant_spec.Flags)], .offset = push_constant_spec.Offset, .size = push_constant_spec.Size});
        }
        m_push_constant_specification_collection.clear();
        m_push_constant_specification_collection.shrink_to_fit();
    }

    Ref<Shader> Shader::Create(Specifications::ShaderSpecification&& spec)
    {
        return CreateRef<Shader>(std::move(spec));
    }

    Ref<Shader> Shader::Create(const Specifications::ShaderSpecification& spec)
    {
        return CreateRef<Shader>(spec);
    }
} // namespace ZEngine::Rendering::Shaders
