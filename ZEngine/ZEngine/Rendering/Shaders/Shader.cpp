#include <Hardwares/VulkanDevice.h>
#include <Helpers/MemoryOperations.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Shaders/Shader.h>
#include <Rendering/Shaders/ShaderReader.h>
#include <spirv_cross.hpp>
#include <vulkan/vulkan.h>

using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Helpers;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Shaders
{

    Shader* CreateShader(const char* filename, bool defer_program_creation)
    {
        return nullptr;
    }
} // namespace ZEngine::Rendering::Shaders

namespace ZEngine::Rendering::Shaders
{

    Shader::Shader() {}

    Shader::~Shader() {}

    void Shader::Initialize(Hardwares::VulkanDevice* device, const Specifications::ShaderSpecification& spec)
    {
        device->Arena->CreateSubArena(ZMega(5), &LocalArena);

        m_device        = device;
        m_specification = spec;

        ShaderCreateInfos.init(device->Arena, 4);
        ShaderModules.init(device->Arena, 3);
        PushConstants.init(device->Arena, 4);
        PushConstantSpecifications.init(device->Arena, 4);
        LayoutBindingSpecificationMap.init(device->Arena, 4);
        LayoutBindingSpections.init(device->Arena, 5);
        SetLayouts.init(device->Arena, 5);
        DescriptorSetLayoutMap.init(device->Arena, 5);

        CreateModule();
        CreateDescriptorSetLayouts();
        CreatePushConstantRange();

        for (auto layout : DescriptorSetLayoutMap)
        {
            SetLayouts.push(layout.second);
        }

        for (const auto layout_binding : LayoutBindingSpecificationMap)
        {
            for (const auto& spec : layout_binding.second)
            {
                LayoutBindingSpections.push(spec);
            }
        }
        LocalArena.Clear();
    }

    void Shader::CreateModule()
    {
        ZRawPtr(spirv_cross::Compiler) spirv_compiler = nullptr;

        /*
         * Vertex Shader processing
         */
        if (Helpers::secure_strlen(m_specification.VertexFilename))
        {
            auto&                    shader_create_info_collection = ShaderCreateInfos.push_use({});
            auto&                    shader_module                 = ShaderModules.push_use({});
            std::vector<uint32_t>    vertex_shader_binary_code     = Rendering::Shaders::ShaderReader::ReadAsBinary(m_specification.VertexFilename);
            VkShaderModuleCreateInfo vertex_shader_create_info     = {};
            vertex_shader_create_info.sType                        = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            vertex_shader_create_info.codeSize                     = vertex_shader_binary_code.size() * sizeof(uint32_t);
            vertex_shader_create_info.pCode                        = vertex_shader_binary_code.data();
            ZENGINE_VALIDATE_ASSERT(vkCreateShaderModule(m_device->LogicalDevice, &vertex_shader_create_info, nullptr, &shader_module) == VK_SUCCESS, "Failed to create ShaderModule")
            shader_create_info_collection.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_create_info_collection.stage  = VK_SHADER_STAGE_VERTEX_BIT;
            shader_create_info_collection.module = shader_module;
            shader_create_info_collection.pName  = "main";
            /*
             * Source Reflection
             */
            spirv_compiler                       = ZPushStructCtorArgs(&LocalArena, spirv_cross::Compiler, vertex_shader_binary_code);
            auto vertex_resources                = spirv_compiler->get_shader_resources();
            for (const auto& UB_resource : vertex_resources.uniform_buffers)
            {
                uint32_t set     = spirv_compiler->get_decoration(UB_resource.id, spv::DecorationDescriptorSet);
                uint32_t binding = spirv_compiler->get_decoration(UB_resource.id, spv::DecorationBinding);

                if (!LayoutBindingSpecificationMap.contains(set) || (LayoutBindingSpecificationMap.at(set).capacity() <= 0))
                {
                    LayoutBindingSpecificationMap[set].init(m_device->Arena, 10);
                }

                LayoutBindingSpecificationMap[set].push(LayoutBindingSpecification{.Set = set, .Binding = binding, .Name = UB_resource.name.c_str(), .DescriptorTypeValue = DescriptorType::UNIFORM_BUFFER, .Flags = ShaderStageFlags::VERTEX});
            }

            for (const auto& SB_resource : vertex_resources.storage_buffers)
            {
                uint32_t set     = spirv_compiler->get_decoration(SB_resource.id, spv::DecorationDescriptorSet);
                uint32_t binding = spirv_compiler->get_decoration(SB_resource.id, spv::DecorationBinding);

                if (!LayoutBindingSpecificationMap.contains(set) || (LayoutBindingSpecificationMap.at(set).capacity() <= 0))
                {
                    LayoutBindingSpecificationMap[set].init(m_device->Arena, 10);
                }

                LayoutBindingSpecificationMap[set].push(LayoutBindingSpecification{.Set = set, .Binding = binding, .Name = SB_resource.name.c_str(), .DescriptorTypeValue = DescriptorType::STORAGE_BUFFER, .Flags = ShaderStageFlags::VERTEX});
            }

            for (const auto& pushConstant_resource : vertex_resources.push_constant_buffers)
            {
                const spirv_cross::SPIRType& type          = spirv_compiler->get_type(pushConstant_resource.base_type_id);
                uint32_t                     struct_offset = !PushConstantSpecifications.empty() ? PushConstantSpecifications.back().Offset : 0;

                if (type.basetype == spirv_cross::SPIRType::Struct)
                {
                    uint32_t struct_total_size = 0;
                    for (uint32_t i = 0; i < type.member_types.size(); ++i)
                    {
                        uint32_t memberSize  = spirv_compiler->get_declared_struct_member_size(type, i);
                        struct_total_size   += memberSize;
                    }
                    PushConstantSpecifications.push(PushConstantSpecification{.Name = pushConstant_resource.name.c_str(), .Size = struct_total_size, .Offset = struct_offset, .Flags = ShaderStageFlags::VERTEX});
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
        if (Helpers::secure_strlen(m_specification.FragmentFilename))
        {
            auto&                    shader_create_info_collection = ShaderCreateInfos.push_use({});
            auto&                    shader_module                 = ShaderModules.push_use({});
            std::vector<uint32_t>    fragment_shader_binary_code   = Rendering::Shaders::ShaderReader::ReadAsBinary(m_specification.FragmentFilename);
            VkShaderModuleCreateInfo fragment_shader_create_info   = {};
            fragment_shader_create_info.sType                      = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            fragment_shader_create_info.codeSize                   = fragment_shader_binary_code.size() * sizeof(uint32_t);
            fragment_shader_create_info.pCode                      = fragment_shader_binary_code.data();
            ZENGINE_VALIDATE_ASSERT(vkCreateShaderModule(m_device->LogicalDevice, &fragment_shader_create_info, nullptr, &shader_module) == VK_SUCCESS, "Failed to create ShaderModule")
            shader_create_info_collection.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_create_info_collection.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
            shader_create_info_collection.module = shader_module;
            shader_create_info_collection.pName  = "main";
            /*
             * Source Reflection
             */
            spirv_compiler                       = ZPushStructCtorArgs(&LocalArena, spirv_cross::Compiler, fragment_shader_binary_code);
            auto fragment_resources              = spirv_compiler->get_shader_resources();
            for (const auto& UB_resource : fragment_resources.uniform_buffers)
            {
                uint32_t set     = spirv_compiler->get_decoration(UB_resource.id, spv::DecorationDescriptorSet);
                uint32_t binding = spirv_compiler->get_decoration(UB_resource.id, spv::DecorationBinding);

                if (LayoutBindingSpecificationMap[set].capacity() <= 0)
                {
                    LayoutBindingSpecificationMap[set].init(m_device->Arena, 10);
                }

                LayoutBindingSpecificationMap[set].push(LayoutBindingSpecification{.Set = set, .Binding = binding, .Name = UB_resource.name.c_str(), .DescriptorTypeValue = DescriptorType::UNIFORM_BUFFER, .Flags = ShaderStageFlags::FRAGMENT});
            }

            for (const auto& SB_resource : fragment_resources.storage_buffers)
            {
                uint32_t set     = spirv_compiler->get_decoration(SB_resource.id, spv::DecorationDescriptorSet);
                uint32_t binding = spirv_compiler->get_decoration(SB_resource.id, spv::DecorationBinding);

                if (LayoutBindingSpecificationMap.at(set).capacity() <= 0)
                {
                    LayoutBindingSpecificationMap[set].init(m_device->Arena, 10);
                }

                LayoutBindingSpecificationMap[set].push(LayoutBindingSpecification{.Set = set, .Binding = binding, .Name = SB_resource.name.c_str(), .DescriptorTypeValue = DescriptorType::STORAGE_BUFFER, .Flags = ShaderStageFlags::FRAGMENT});
            }

            for (const auto& pushConstant_resource : fragment_resources.push_constant_buffers)
            {
                const spirv_cross::SPIRType& type          = spirv_compiler->get_type(pushConstant_resource.base_type_id);
                uint32_t                     struct_offset = !PushConstantSpecifications.empty() ? PushConstantSpecifications.back().Offset : 0;

                if (type.basetype == spirv_cross::SPIRType::Struct)
                {
                    uint32_t struct_total_size = 0;
                    for (uint32_t i = 0; i < type.member_types.size(); ++i)
                    {
                        uint32_t memberSize  = spirv_compiler->get_declared_struct_member_size(type, i);
                        struct_total_size   += memberSize;
                    }
                    PushConstantSpecifications.push(PushConstantSpecification{.Name = pushConstant_resource.name.c_str(), .Size = struct_total_size, .Offset = struct_offset, .Flags = ShaderStageFlags::FRAGMENT});
                    /*
                     * We update the offset for next iteration
                     */
                    struct_offset = struct_total_size;
                }
            }

            for (const auto& SI_resource : fragment_resources.sampled_images)
            {
                uint32_t    set     = spirv_compiler->get_decoration(SI_resource.id, spv::DecorationDescriptorSet);
                uint32_t    binding = spirv_compiler->get_decoration(SI_resource.id, spv::DecorationBinding);

                uint32_t    count   = 1;
                const auto& type    = spirv_compiler->get_type(SI_resource.type_id);
                if (!type.array.empty())
                {
                    count = type.array[0];
                    if (count == 0) // Unsized arrays
                    {
                        /*
                         * Unsized arrays require descriptor update-after-bind support
                         * If device doesn't support it, we fall back to a reasonable maximum
                         */
                        if (m_device->PhysicalDeviceSupportDescriptorUpdateAfterBind)
                        {
                            count = m_device->PhysicalDeviceDescriptorIndexingProperties.maxPerStageDescriptorUpdateAfterBindSamplers - 1;
                        }
                        else
                        {
                            /*
                             * Fallback: derive a safe maximum from device properties when possible
                             * Otherwise fall back to a conservative hard limit.
                             */
                            uint32_t fallback_count = 4096;
                            if (m_device->PhysicalDeviceDescriptorIndexingProperties.maxPerStageDescriptorUpdateAfterBindSamplers > 1)
                            {
                                fallback_count = static_cast<uint32_t>(m_device->PhysicalDeviceDescriptorIndexingProperties.maxPerStageDescriptorUpdateAfterBindSamplers - 1);
                                if (fallback_count > 4096)
                                {
                                    fallback_count = 4096; // cap to reasonable upper bound
                                }
                            }

                            count = fallback_count;
                            ZENGINE_CORE_WARN(
                                "Shader {} uses unsized sampler arrays but device does not support descriptor update-after-bind. "
                                "Using fallback size of {} descriptors. For full bindless rendering support, use GPUs with descriptor-indexing support.",
                                m_specification.VertexFilename ? m_specification.VertexFilename : m_specification.FragmentFilename,
                                count)
                        }
                    }
                }

                if (LayoutBindingSpecificationMap[set].capacity() <= 0)
                {
                    LayoutBindingSpecificationMap[set].init(m_device->Arena, 10);
                }

                LayoutBindingSpecificationMap[set].push(LayoutBindingSpecification{.Set = set, .Binding = binding, .Count = count, .Name = SI_resource.name.c_str(), .DescriptorTypeValue = DescriptorType::COMBINED_IMAGE_SAMPLER, .Flags = ShaderStageFlags::FRAGMENT});
            }
        }
    }

    Specifications::LayoutBindingSpecification Shader::GetLayoutBindingSpecification(const char* name)
    {
        LayoutBindingSpecification binding_spec = {};
        for (const auto& layout_binding : LayoutBindingSpecificationMap)
        {
            const auto& binding_specification_collection = layout_binding.second;
            auto        find_it                          = std::find_if(binding_specification_collection.begin(), binding_specification_collection.end(), [&](const LayoutBindingSpecification& spec) { return Helpers::secure_strcmp(spec.Name, name); });

            if (find_it != std::end(binding_specification_collection))
            {
                binding_spec = *find_it;
                break;
            }
        }
        return binding_spec;
    }

    void Shader::Dispose()
    {
        for (auto& shader_module : ShaderModules)
        {
            vkDestroyShaderModule(m_device->LogicalDevice, shader_module, nullptr);
        }
        ShaderModules.clear();

        for (auto set_layout : DescriptorSetLayoutMap)
        {
            m_device->EnqueueForDeletion(Rendering::DeviceResourceType::DESCRIPTORSETLAYOUT, set_layout.second);
        }
        DescriptorSetLayoutMap.clear();

        if (m_descriptor_pool)
        {
            m_device->EnqueueForDeletion(Rendering::DeviceResourceType::DESCRIPTORPOOL, m_descriptor_pool);
            m_descriptor_pool = VK_NULL_HANDLE;
        }
    }

    void Shader::CreateDescriptorSetLayouts()
    {
        Array<VkDescriptorPoolSize> pool_size_collection = {};
        pool_size_collection.init(&LocalArena, 10);

        for (const auto& layout_binding_set : LayoutBindingSpecificationMap)
        {
            uint32_t                            binding_set               = layout_binding_set.first;
            Array<VkDescriptorSetLayoutBinding> layout_binding_collection = {};
            layout_binding_collection.init(&LocalArena, 10);
            for (uint32_t i = 0; i < layout_binding_set.second.size(); ++i)
            {
                layout_binding_collection.push(VkDescriptorSetLayoutBinding{.binding = layout_binding_set.second[i].Binding, .descriptorType = DescriptorTypeMap[static_cast<uint32_t>(layout_binding_set.second[i].DescriptorTypeValue)], .descriptorCount = layout_binding_set.second[i].Count, .stageFlags = ShaderStageFlagsMap[static_cast<uint32_t>(layout_binding_set.second[i].Flags)], .pImmutableSamplers = nullptr});
            }
            /*
             * Binding flag extension
             */
            Array<VkDescriptorBindingFlags> binding_flags_collection = {};
            binding_flags_collection.init(&LocalArena, layout_binding_collection.size(), layout_binding_collection.size());
            for (uint32_t i = 0; i < layout_binding_collection.size(); ++i)
            {
                if (m_device->PhysicalDeviceSupportDescriptorUpdateAfterBind)
                {
                    if ((layout_binding_collection[i].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) || (layout_binding_collection[i].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE))
                    {
                        binding_flags_collection[i] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
                        continue;
                    }
                    binding_flags_collection[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
                }
                else
                {
                    // Fallback for devices that don't support update-after-bind
                    binding_flags_collection[i] = 0;
                }
            }

            VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_create_info = {};
            binding_flags_create_info.sType                                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            binding_flags_create_info.bindingCount                                = binding_flags_collection.size();
            binding_flags_create_info.pBindingFlags                               = binding_flags_collection.data();
            /*
             * Creating SetLayout
             */
            VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info     = {};
            descriptor_set_layout_create_info.sType                               = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptor_set_layout_create_info.bindingCount                        = layout_binding_collection.size();
            descriptor_set_layout_create_info.pBindings                           = layout_binding_collection.data();
            descriptor_set_layout_create_info.flags                               = m_device->PhysicalDeviceSupportDescriptorUpdateAfterBind ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT : 0;
            descriptor_set_layout_create_info.pNext                               = &binding_flags_create_info;

            VkDescriptorSetLayout descriptor_set_layout                           = VK_NULL_HANDLE;
            ZENGINE_VALIDATE_ASSERT(vkCreateDescriptorSetLayout(m_device->LogicalDevice, &descriptor_set_layout_create_info, nullptr, &descriptor_set_layout) == VK_SUCCESS, "Failed to create DescriptorSetLayout")

            DescriptorSetLayoutMap[binding_set] = std::move(descriptor_set_layout);
            /*
             * Packing PoolSize
             */
            for (const auto& layout_binding : layout_binding_collection)
            {
                auto find_pool_size_it = std::find_if(pool_size_collection.begin(), pool_size_collection.end(), [&](const VkDescriptorPoolSize& pool_size) { return (layout_binding.descriptorType == pool_size.type); });

                if (find_pool_size_it == std::end(pool_size_collection))
                {
                    pool_size_collection.push(VkDescriptorPoolSize{.type = layout_binding.descriptorType, .descriptorCount = layout_binding.descriptorCount});
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
                pool_size.descriptorCount *= m_device->SwapchainImageCount;
                pool_size.descriptorCount += m_specification.OverloadPoolSize;
            }
        }
        /*
         * Create DescriptorPool
         */
        ZENGINE_VALIDATE_ASSERT(!pool_size_collection.empty(), "The pool size can't be empty")

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags                      = m_device->PhysicalDeviceSupportDescriptorUpdateAfterBind ? VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT : 0;
        pool_info.maxSets                    = m_device->SwapchainImageCount * pool_size_collection.size() * m_specification.OverloadMaxSet;
        pool_info.poolSizeCount              = pool_size_collection.size();
        pool_info.pPoolSizes                 = pool_size_collection.data();

        ZENGINE_VALIDATE_ASSERT(vkCreateDescriptorPool(m_device->LogicalDevice, &pool_info, nullptr, &m_descriptor_pool) == VK_SUCCESS, "Failed to create DescriptorPool")

        /*
         * Create DescriptorSet
         */
        DescriptorSetMap.init(m_device->Arena, 5);
        for (const auto& layout : DescriptorSetLayoutMap)
        {
            DescriptorSetMap[layout.first].init(m_device->Arena, m_device->SwapchainImageCount, m_device->SwapchainImageCount);

            Array<VkDescriptorSetLayout> layout_set = {};
            layout_set.init(&LocalArena, m_device->SwapchainImageCount, m_device->SwapchainImageCount);
            for (uint32_t i = 0; i < m_device->SwapchainImageCount; ++i)
            {
                layout_set[i] = layout.second;
            }

            VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {};
            descriptor_set_allocate_info.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptor_set_allocate_info.descriptorPool              = m_descriptor_pool;
            descriptor_set_allocate_info.descriptorSetCount          = m_device->SwapchainImageCount;
            descriptor_set_allocate_info.pSetLayouts                 = layout_set.data();
            ZENGINE_VALIDATE_ASSERT(vkAllocateDescriptorSets(m_device->LogicalDevice, &descriptor_set_allocate_info, DescriptorSetMap[layout.first].data()) == VK_SUCCESS, "Failed to create DescriptorSet")
        }
    }

    void Shader::CreatePushConstantRange()
    {
        if (!PushConstantSpecifications.empty())
        {
            VkPushConstantRange& range = PushConstants.push_use(VkPushConstantRange{.offset = 0});
            for (const auto& push_constant_spec : PushConstantSpecifications)
            {
                range.stageFlags |= ShaderStageFlagsMap[VALUE_FROM_SPEC_MAP(push_constant_spec.Flags)];
                range.size       += push_constant_spec.Size;
            }
            PushConstantSpecifications.clear();
        }
    }
} // namespace ZEngine::Rendering::Shaders
