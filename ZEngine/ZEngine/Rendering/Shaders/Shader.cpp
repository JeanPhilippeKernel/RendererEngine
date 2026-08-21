#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Shaders/Shader.h>
#include <ZEngine/Rendering/Shaders/ShaderReader.h>
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

        ShaderCreateInfos.init(m_device->Arena, 4);
        ShaderModules.init(m_device->Arena, 3);
        PushConstants.init(m_device->Arena, 4);
        PushConstantSpecifications.init(m_device->Arena, 4);
        LayoutBindingSpecificationMap.init(m_device->Arena, 4);
        LayoutBindingSpecifications.init(m_device->Arena, 5);
        SetLayouts.init(m_device->Arena, 5);
        InternalDescriptorSetLayoutMap.init(m_device->Arena, 5);
        DescriptorSetMap.init(m_device->Arena, 5);

        CreateModule();
        CreateDescriptorSetLayouts();
        CreatePushConstantRange();

        {
            uint32_t max_set = 0;
            for (auto [i, _] : InternalDescriptorSetLayoutMap)
            {
                if (i > max_set)
                {
                    max_set = i;
                }
            }
            // Pipeline layout requires a contiguous array from set 0 to max_set.
            // Fill any gap with the device's empty layout so Vulkan sees no holes.
            // Register gap indices in DescriptorSetMap with EmptyDescriptorSet so
            // BindDescriptorSets can bind a valid (zero-binding) set at those slots.
            for (uint32_t i = 0; i <= max_set; ++i)
            {
                if (InternalDescriptorSetLayoutMap.contains(i))
                {
                    SetLayouts.push(InternalDescriptorSetLayoutMap.at(i));
                }
                else
                {
                    SetLayouts.push(m_device->EmptyDescriptorSetLayout);
                    DescriptorSetMap[i].init(m_device->Arena, m_device->SwapchainPtr->BufferredFrameCount, m_device->SwapchainPtr->BufferredFrameCount);
                    for (uint32_t f = 0; f < m_device->SwapchainPtr->BufferredFrameCount; ++f)
                    {
                        DescriptorSetMap[i][f] = m_device->EmptyDescriptorSet;
                    }
                }
            }
        }

        for (const auto layout_binding : LayoutBindingSpecificationMap)
        {
            for (const auto& spec : layout_binding.second)
            {
                LayoutBindingSpecifications.push(spec);
            }
        }

        BindingsByName.init(&LocalArena, static_cast<size_t>(LayoutBindingSpecifications.size() * 2) + 4);
        for (const auto& spec : LayoutBindingSpecifications)
        {
            if (spec.Name)
                BindingsByName[spec.Name] = spec;
        }

        // We remove the Set to avoid double release from the Device and Shader owned resource
        for (const auto [set, _] : m_device->ShaderReservedDescriptorSetLayoutMap)
        {
            if (InternalDescriptorSetLayoutMap.contains(set))
            {
                InternalDescriptorSetLayoutMap.remove(set);
            }
        }
    }

    void Shader::CreateModule()
    {
        Scope<spirv_cross::Compiler> spirv_compiler = nullptr;

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
            spirv_compiler                       = CreateScope<spirv_cross::Compiler>(vertex_shader_binary_code);
            auto vertex_resources                = spirv_compiler->get_shader_resources();
            for (const auto& UB_resource : vertex_resources.uniform_buffers)
            {
                uint32_t set     = spirv_compiler->get_decoration(UB_resource.id, spv::DecorationDescriptorSet);
                uint32_t binding = spirv_compiler->get_decoration(UB_resource.id, spv::DecorationBinding);

                if (!LayoutBindingSpecificationMap.contains(set) || (LayoutBindingSpecificationMap.at(set).capacity() <= 0))
                {
                    LayoutBindingSpecificationMap[set].init(m_device->Arena, 10);
                }

                auto name_c_size = (UB_resource.name.size() + 1u);
                auto name_c_str  = ZPushString(&LocalArena, name_c_size);
                Helpers::secure_strcpy(name_c_str, name_c_size, UB_resource.name.c_str());
                // UBCamera at set=0, binding=0 is accessed via a dynamic offset
                // (the per-frame FrameHeap offset). Declare DYNAMIC here so the
                // pool and descriptor set layout are correct from the start.
                DescriptorType ub_type = (set == 0 && binding == 0) ? DescriptorType::UNIFORM_BUFFER_DYNAMIC : DescriptorType::UNIFORM_BUFFER;
                LayoutBindingSpecificationMap[set].push(LayoutBindingSpecification{.Set = set, .Binding = binding, .Name = name_c_str, .DescriptorTypeValue = ub_type, .Flags = ShaderStageFlags::VERTEX});
            }

            // Collect reflected storage buffers.
            for (const auto& SB_resource : vertex_resources.storage_buffers)
            {
                uint32_t set     = spirv_compiler->get_decoration(SB_resource.id, spv::DecorationDescriptorSet);
                uint32_t binding = spirv_compiler->get_decoration(SB_resource.id, spv::DecorationBinding);

                if (!LayoutBindingSpecificationMap.contains(set) || (LayoutBindingSpecificationMap.at(set).capacity() <= 0))
                {
                    LayoutBindingSpecificationMap[set].init(m_device->Arena, 10);
                }

                auto name_c_size = (SB_resource.name.size() + 1u);
                auto name_c_str  = ZPushString(&LocalArena, name_c_size);
                Helpers::secure_strcpy(name_c_str, name_c_size, SB_resource.name.c_str());
                LayoutBindingSpecificationMap[set].push(LayoutBindingSpecification{.Set = set, .Binding = binding, .Name = name_c_str, .DescriptorTypeValue = DescriptorType::STORAGE_BUFFER, .Flags = ShaderStageFlags::VERTEX});
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

                    auto name_c_size = (pushConstant_resource.name.size() + 1u);
                    auto name_c_str  = ZPushString(&LocalArena, name_c_size);
                    Helpers::secure_strcpy(name_c_str, name_c_size, pushConstant_resource.name.c_str());
                    PushConstantSpecifications.push(PushConstantSpecification{.Name = name_c_str, .Size = struct_total_size, .Offset = struct_offset, .Flags = ShaderStageFlags::VERTEX});
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
            spirv_compiler                       = CreateScope<spirv_cross::Compiler>(fragment_shader_binary_code);
            auto fragment_resources              = spirv_compiler->get_shader_resources();
            for (const auto& UB_resource : fragment_resources.uniform_buffers)
            {
                uint32_t set     = spirv_compiler->get_decoration(UB_resource.id, spv::DecorationDescriptorSet);
                uint32_t binding = spirv_compiler->get_decoration(UB_resource.id, spv::DecorationBinding);

                if (LayoutBindingSpecificationMap[set].capacity() <= 0)
                {
                    LayoutBindingSpecificationMap[set].init(m_device->Arena, 10);
                }

                auto name_c_size = (UB_resource.name.size() + 1u);
                auto name_c_str  = ZPushString(&LocalArena, name_c_size);
                Helpers::secure_strcpy(name_c_str, name_c_size, UB_resource.name.c_str());

                DescriptorType ub_frag_type = (set == 0 && binding == 0) ? DescriptorType::UNIFORM_BUFFER_DYNAMIC : DescriptorType::UNIFORM_BUFFER;
                LayoutBindingSpecificationMap[set].push(LayoutBindingSpecification{.Set = set, .Binding = binding, .Name = name_c_str, .DescriptorTypeValue = ub_frag_type, .Flags = ShaderStageFlags::FRAGMENT});
            }

            for (const auto& SB_resource : fragment_resources.storage_buffers)
            {
                uint32_t set     = spirv_compiler->get_decoration(SB_resource.id, spv::DecorationDescriptorSet);
                uint32_t binding = spirv_compiler->get_decoration(SB_resource.id, spv::DecorationBinding);

                if (LayoutBindingSpecificationMap[set].capacity() <= 0)
                {
                    LayoutBindingSpecificationMap[set].init(m_device->Arena, 10);
                }
                auto name_c_size = (SB_resource.name.size() + 1u);
                auto name_c_str  = ZPushString(&LocalArena, name_c_size);
                Helpers::secure_strcpy(name_c_str, name_c_size, SB_resource.name.c_str());
                LayoutBindingSpecificationMap[set].push(LayoutBindingSpecification{.Set = set, .Binding = binding, .Name = name_c_str, .DescriptorTypeValue = DescriptorType::STORAGE_BUFFER, .Flags = ShaderStageFlags::FRAGMENT});
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
                    auto name_c_size = (pushConstant_resource.name.size() + 1u);
                    auto name_c_str  = ZPushString(&LocalArena, name_c_size);
                    Helpers::secure_strcpy(name_c_str, name_c_size, pushConstant_resource.name.c_str());

                    PushConstantSpecifications.push(PushConstantSpecification{.Name = name_c_str, .Size = struct_total_size, .Offset = struct_offset, .Flags = ShaderStageFlags::FRAGMENT});
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

                if (m_device->ShaderReservedLayoutBindingSpecificationMap.contains(set))
                {
                    const auto&                binding_specifications = m_device->ShaderReservedLayoutBindingSpecificationMap.at(set);
                    LayoutBindingSpecification binding_spec           = {};
                    for (size_t i = 0; i < binding_specifications.size(); ++i)
                    {
                        if (binding_specifications[i].Binding == binding)
                        {
                            binding_spec = binding_specifications[i];
                            break;
                        }
                    }

                    if (LayoutBindingSpecificationMap[set].capacity() <= 0)
                    {
                        LayoutBindingSpecificationMap[set].init(m_device->Arena, 2);
                    }

                    LayoutBindingSpecificationMap[set].push(std::move(binding_spec));

                    continue;
                }

                const auto& type  = spirv_compiler->get_type(SI_resource.type_id);
                uint32_t    count = std::min(type.array.empty() ? 1 : type.array[0], 256u);

                if (LayoutBindingSpecificationMap[set].capacity() <= 0)
                {
                    LayoutBindingSpecificationMap[set].init(m_device->Arena, 10);
                }
                auto name_c_size = (SI_resource.name.size() + 1u);
                auto name_c_str  = ZPushString(&LocalArena, name_c_size);
                Helpers::secure_strcpy(name_c_str, name_c_size, SI_resource.name.c_str());

                LayoutBindingSpecificationMap[set].push(LayoutBindingSpecification{.Set = set, .Binding = binding, .Count = count, .Name = name_c_str, .DescriptorTypeValue = DescriptorType::COMBINED_IMAGE_SAMPLER, .Flags = ShaderStageFlags::FRAGMENT});
            }

            for (const auto& SI_resource : fragment_resources.separate_images)
            {
                uint32_t set     = spirv_compiler->get_decoration(SI_resource.id, spv::DecorationDescriptorSet);
                uint32_t binding = spirv_compiler->get_decoration(SI_resource.id, spv::DecorationBinding);

                if (m_device->ShaderReservedLayoutBindingSpecificationMap.contains(set))
                {
                    const auto&                binding_specifications = m_device->ShaderReservedLayoutBindingSpecificationMap.at(set);
                    LayoutBindingSpecification binding_spec           = {};
                    for (size_t i = 0; i < binding_specifications.size(); ++i)
                    {
                        if (binding_specifications[i].Binding == binding)
                        {
                            binding_spec = binding_specifications[i];
                            break;
                        }
                    }

                    if (LayoutBindingSpecificationMap[set].capacity() <= 0)
                    {
                        LayoutBindingSpecificationMap[set].init(m_device->Arena, 2);
                    }

                    LayoutBindingSpecificationMap[set].push(std::move(binding_spec));

                    continue;
                }

                const auto& type  = spirv_compiler->get_type(SI_resource.type_id);

                uint32_t    count = std::min(type.array.empty() ? 1 : type.array[0], 256u);

                if (LayoutBindingSpecificationMap[set].capacity() <= 0)
                {
                    LayoutBindingSpecificationMap[set].init(m_device->Arena, 10);
                }
                auto name_c_size = (SI_resource.name.size() + 1u);
                auto name_c_str  = ZPushString(&LocalArena, name_c_size);
                Helpers::secure_strcpy(name_c_str, name_c_size, SI_resource.name.c_str());

                LayoutBindingSpecificationMap[set].push(LayoutBindingSpecification{.Set = set, .Binding = binding, .Count = count, .Name = name_c_str, .DescriptorTypeValue = DescriptorType::SAMPLED_IMAGE, .Flags = ShaderStageFlags::FRAGMENT});
            }

            for (const auto& SI_resource : fragment_resources.separate_samplers)
            {
                uint32_t set     = spirv_compiler->get_decoration(SI_resource.id, spv::DecorationDescriptorSet);
                uint32_t binding = spirv_compiler->get_decoration(SI_resource.id, spv::DecorationBinding);

                if (m_device->ShaderReservedLayoutBindingSpecificationMap.contains(set))
                {
                    const auto&                binding_specifications = m_device->ShaderReservedLayoutBindingSpecificationMap.at(set);
                    LayoutBindingSpecification binding_spec           = {};
                    for (size_t i = 0; i < binding_specifications.size(); ++i)
                    {
                        if (binding_specifications[i].Binding == binding)
                        {
                            binding_spec = binding_specifications[i];
                            break;
                        }
                    }

                    if (LayoutBindingSpecificationMap[set].capacity() <= 0)
                    {
                        LayoutBindingSpecificationMap[set].init(m_device->Arena, 2);
                    }

                    LayoutBindingSpecificationMap[set].push(std::move(binding_spec));

                    continue;
                }

                const auto& type  = spirv_compiler->get_type(SI_resource.type_id);
                uint32_t    count = std::min(type.array.empty() ? 1 : type.array[0], 256u);

                if (LayoutBindingSpecificationMap[set].capacity() <= 0)
                {
                    LayoutBindingSpecificationMap[set].init(m_device->Arena, 10);
                }
                auto name_c_size = (SI_resource.name.size() + 1u);
                auto name_c_str  = ZPushString(&LocalArena, name_c_size);
                Helpers::secure_strcpy(name_c_str, name_c_size, SI_resource.name.c_str());

                LayoutBindingSpecificationMap[set].push(LayoutBindingSpecification{.Set = set, .Binding = binding, .Count = count, .Name = name_c_str, .DescriptorTypeValue = DescriptorType::SAMPLER, .Flags = ShaderStageFlags::FRAGMENT});
            }
        }
    }

    Specifications::LayoutBindingSpecification Shader::GetLayoutBindingSpecification(cstring name)
    {
        if (!Helpers::secure_strlen(name))
            return {};

        const auto* spec = BindingsByName.find(name);
        return spec ? *spec : LayoutBindingSpecification{};
    }

    void Shader::Dispose()
    {
        for (auto& shader_module : ShaderModules)
        {
            vkDestroyShaderModule(m_device->LogicalDevice, shader_module, nullptr);
        }
        ShaderModules.clear();

        for (auto set_layout : InternalDescriptorSetLayoutMap)
        {
            Hardwares::DeferredFreeEntry e = {};
            e.EntryKind                    = Hardwares::DeferredFreeEntry::Kind::VkHandle;
            e.Data.Vk                      = {set_layout.second, Rendering::DeviceResourceType::DESCRIPTORSETLAYOUT, nullptr};
            m_device->DeferFree(e);
        }
        InternalDescriptorSetLayoutMap.clear();

        if (m_descriptor_pool)
        {
            Hardwares::DeferredFreeEntry e = {};
            e.EntryKind                    = Hardwares::DeferredFreeEntry::Kind::VkHandle;
            e.Data.Vk                      = {m_descriptor_pool, Rendering::DeviceResourceType::DESCRIPTORPOOL, nullptr};
            m_device->DeferFree(e);
            m_descriptor_pool = VK_NULL_HANDLE;
        }
    }

    void Shader::CreateDescriptorSetLayouts()
    {
        auto                        scratch              = ZGetScratch(&LocalArena);

        Array<VkDescriptorPoolSize> pool_size_collection = {};
        pool_size_collection.init(scratch.Arena, 10);
        bool                                pool_needs_update_after_bind = false; // set true when a non-reserved binding uses UPDATE_AFTER_BIND_BIT

        Array<VkDescriptorSetLayoutBinding> layout_binding_collection    = {};
        layout_binding_collection.init(scratch.Arena, 10);

        for (const auto layout_binding_set : LayoutBindingSpecificationMap)
        {
            uint32_t binding_set = layout_binding_set.first;

            if (m_device->ShaderReservedLayoutBindingSpecificationMap.contains(binding_set))
            {
                // Since it's a reserved Set The device already created its SetLayout
                InternalDescriptorSetLayoutMap[binding_set] = m_device->ShaderReservedDescriptorSetLayoutMap.at(binding_set);
                continue;
            }

            layout_binding_collection.clear();
            for (uint32_t i = 0; i < layout_binding_set.second.size(); ++i)
            {
                layout_binding_collection.push(VkDescriptorSetLayoutBinding{.binding = layout_binding_set.second[i].Binding, .descriptorType = DescriptorTypeMap[static_cast<uint32_t>(layout_binding_set.second[i].DescriptorTypeValue)], .descriptorCount = layout_binding_set.second[i].Count, .stageFlags = ShaderStageFlagsMap[static_cast<uint32_t>(layout_binding_set.second[i].Flags)], .pImmutableSamplers = nullptr});
            }

            for (const auto& lb : layout_binding_collection)
            {
                auto it = std::find_if(pool_size_collection.begin(), pool_size_collection.end(), [&](const VkDescriptorPoolSize& ps) { return ps.type == lb.descriptorType; });
                if (it == pool_size_collection.end())
                    pool_size_collection.push(VkDescriptorPoolSize{.type = lb.descriptorType, .descriptorCount = lb.descriptorCount});
                else
                    it->descriptorCount += lb.descriptorCount;
            }

            /*
             * Binding flag extension
             */
            // UNIFORM_BUFFER_DYNAMIC is incompatible with UPDATE_AFTER_BIND_POOL_BIT.
            // If this set has a dynamic UBO, disable all UPDATE_AFTER_BIND flags for this set.
            bool has_dynamic_ubo = false;
            for (uint32_t i = 0; i < layout_binding_collection.size(); ++i)
                if (layout_binding_collection[i].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
                {
                    has_dynamic_ubo = true;
                    break;
                }

            Array<VkDescriptorBindingFlags> binding_flags_collection = {};
            binding_flags_collection.init(scratch.Arena, layout_binding_collection.size(), layout_binding_collection.size());
            for (uint32_t i = 0; i < layout_binding_collection.size(); ++i)
            {
                binding_flags_collection[i] = 0;

                if (!has_dynamic_ubo)
                {
                    if (m_device->PhysicalDeviceSupportSampledImageBindless && ((layout_binding_collection[i].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) || (layout_binding_collection[i].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)))
                    {
                        binding_flags_collection[i]  = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
                        pool_needs_update_after_bind = true;
                    }
                    else if (m_device->PhysicalDeviceSupportStorageBufferBindless && (layout_binding_collection[i].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER))
                    {
                        binding_flags_collection[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
                    }
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

            // Only set UPDATE_AFTER_BIND_POOL_BIT if at least one binding in this set
            // actually uses UPDATE_AFTER_BIND_BIT — UNIFORM_BUFFER_DYNAMIC is incompatible
            // with layouts that have this flag.
            bool has_update_after_bind                                            = false;
            for (uint32_t k = 0; k < binding_flags_collection.size(); ++k)
                if (binding_flags_collection[k] & VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                {
                    has_update_after_bind = true;
                    break;
                }

            if (m_device->PhysicalDeviceSupportSampledImageBindless && has_update_after_bind)
            {
                descriptor_set_layout_create_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
                descriptor_set_layout_create_info.pNext = &binding_flags_create_info;
            }

            VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
            ZENGINE_VALIDATE_ASSERT(vkCreateDescriptorSetLayout(m_device->LogicalDevice, &descriptor_set_layout_create_info, nullptr, &descriptor_set_layout) == VK_SUCCESS, "Failed to create DescriptorSetLayout")

            InternalDescriptorSetLayoutMap[binding_set] = std::move(descriptor_set_layout);
        }

        /*
         * Ensure correctness with number of frame count
         */
        for (auto& pool_size : pool_size_collection)
        {
            pool_size.descriptorCount *= m_device->SwapchainPtr->BufferredFrameCount;
        }
        // Reserved sets never contribute pool sizes, so populate their DescriptorSetMap
        // entries unconditionally - before the early return below.
        for (const auto layout : InternalDescriptorSetLayoutMap)
        {
            if (m_device->ShaderReservedDescriptorSetMap.contains(layout.first))
            {
                DescriptorSetMap[layout.first].init(m_device->Arena, m_device->SwapchainPtr->BufferredFrameCount, m_device->SwapchainPtr->BufferredFrameCount);
                for (uint32_t i = 0; i < m_device->SwapchainPtr->BufferredFrameCount; ++i)
                {
                    DescriptorSetMap[layout.first][i] = m_device->ShaderReservedDescriptorSetMap.at(layout.first)[i];
                }
            }
        }

        /*
         * Create DescriptorPool
         */
        if (pool_size_collection.empty())
        {
            ZReleaseScratch(scratch);
            return;
        }

        uint32_t non_reserved_set_count = 0;
        for (const auto& layout : InternalDescriptorSetLayoutMap)
        {
            if (!m_device->ShaderReservedDescriptorSetMap.contains(layout.first))
                ++non_reserved_set_count;
        }

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags                      = pool_needs_update_after_bind ? VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT : 0;
        pool_info.maxSets                    = non_reserved_set_count * m_device->SwapchainPtr->BufferredFrameCount;
        pool_info.poolSizeCount              = pool_size_collection.size();
        pool_info.pPoolSizes                 = pool_size_collection.data();

        ZENGINE_VALIDATE_ASSERT(vkCreateDescriptorPool(m_device->LogicalDevice, &pool_info, nullptr, &m_descriptor_pool) == VK_SUCCESS, "Failed to create DescriptorPool")

        ZReleaseScratch(scratch);

        /*
         * Create DescriptorSet for non-reserved sets (reserved sets handled above)
         */
        for (const auto layout : InternalDescriptorSetLayoutMap)
        {
            if (m_device->ShaderReservedDescriptorSetMap.contains(layout.first))
            {
                continue;
            }

            DescriptorSetMap[layout.first].init(m_device->Arena, m_device->SwapchainPtr->BufferredFrameCount, m_device->SwapchainPtr->BufferredFrameCount);

            auto                         scratch    = ZGetScratch(&LocalArena);

            Array<VkDescriptorSetLayout> layout_set = {};
            layout_set.init(scratch.Arena, m_device->SwapchainPtr->BufferredFrameCount, m_device->SwapchainPtr->BufferredFrameCount);
            for (uint32_t i = 0; i < m_device->SwapchainPtr->BufferredFrameCount; ++i)
            {
                layout_set[i] = layout.second;
            }

            VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {};
            descriptor_set_allocate_info.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptor_set_allocate_info.descriptorPool              = m_descriptor_pool;
            descriptor_set_allocate_info.descriptorSetCount          = m_device->SwapchainPtr->BufferredFrameCount;
            descriptor_set_allocate_info.pSetLayouts                 = layout_set.data();
            ZENGINE_VALIDATE_ASSERT(vkAllocateDescriptorSets(m_device->LogicalDevice, &descriptor_set_allocate_info, DescriptorSetMap[layout.first].data()) == VK_SUCCESS, "Failed to create DescriptorSet")

            ZReleaseScratch(scratch);
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
