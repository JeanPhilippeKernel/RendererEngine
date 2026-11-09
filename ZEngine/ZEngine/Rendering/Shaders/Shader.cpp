#include <ZEngine/Engine.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Pipelines/PSOCache.h>
#include <ZEngine/Rendering/Shaders/Shader.h>
#include <ZEngine/Rendering/Shaders/ShaderReader.h>
#include <spirv_cross.hpp>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <utility>

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

    ShaderGenerationLease::ShaderGenerationLease(Shader* shader, uint32_t generation) : m_shader(shader), m_generation(generation) {}

    ShaderGenerationLease::~ShaderGenerationLease()
    {
        Reset();
    }

    ShaderGenerationLease::ShaderGenerationLease(ShaderGenerationLease&& other) noexcept : m_shader(std::exchange(other.m_shader, nullptr)), m_generation(std::exchange(other.m_generation, 0)) {}

    ShaderGenerationLease& ShaderGenerationLease::operator=(ShaderGenerationLease&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_shader     = std::exchange(other.m_shader, nullptr);
            m_generation = std::exchange(other.m_generation, 0);
        }
        return *this;
    }

    void ShaderGenerationLease::Reset()
    {
        if (m_shader)
        {
            ZENGINE_VALIDATE_ASSERT(m_shader->m_generation_pins != 0, "Shader generation lease underflow")
            --m_shader->m_generation_pins;
            m_shader     = nullptr;
            m_generation = 0;
        }
    }

    ShaderGenerationLease Shader::RetainGeneration()
    {
        if (ShaderCreateInfos.empty() || m_generation_pins == UINT32_MAX)
        {
            return {};
        }
        ++m_generation_pins;
        return ShaderGenerationLease(this, Generation);
    }

    bool Shader::FlushDeferredReload()
    {
        if (!m_reload_deferred || m_generation_pins != 0)
        {
            return false;
        }
        const auto generation = Generation;
        Reload();
        return Generation != generation;
    }

    void Shader::Initialize(Hardwares::VulkanDevice* device, const Specifications::ShaderSpecification& spec)
    {
        ZENGINE_VALIDATE_ASSERT(TryInitialize(device, spec), "Failed to initialize shader")
    }

    bool Shader::TryInitialize(Hardwares::VulkanDevice* device, const Specifications::ShaderSpecification& spec, VkResult* status, const ShaderInitializationServices& services)
    {
        if (status)
        {
            *status = VK_ERROR_INITIALIZATION_FAILED;
        }
        if (!device || !device->Arena || !device->LogicalDevice || !device->SwapchainPtr || !device->SwapchainPtr->BufferredFrameCount || device->SwapchainPtr->BufferredFrameCount > MaxFrames || m_device || m_generation_pins || (m_storage_parent && m_storage_parent != device->Arena))
        {
            return false;
        }
        if (device->IsDeviceLost.load(std::memory_order_acquire))
        {
            if (status)
            {
                *status = VK_ERROR_DEVICE_LOST;
            }
            return false;
        }
        // Copy before touching the owned strings: reload may pass our own recipe.
        decltype(m_source_strings) strings    = {};
        const cstring              sources[4] = {spec.Name, spec.VertexFilename, spec.FragmentFilename, spec.ComputeFilename};
        for (uint32_t i = 0; i < 4; ++i)
        {
            if (!sources[i])
            {
                continue;
            }
            size_t length = 0;
            while (length < strings[i].size() && sources[i][length])
            {
                ++length;
            }
            if (length == strings[i].size())
            {
                return false;
            }
            Helpers::secure_memcpy(strings[i].data(), strings[i].size(), sources[i], length + 1);
        }
        if (!strings[0][0] || (!strings[1][0] && !strings[2][0] && !strings[3][0]) || (strings[3][0] && (strings[1][0] || strings[2][0])))
        {
            return false;
        }
        if (!LocalArena.m_memory)
        {
            if (!device->Arena->TryCreateSubArena(StorageBytes, &LocalArena, "VulkanDevice/Shader"))
            {
                if (status)
                {
                    *status = VK_ERROR_OUT_OF_HOST_MEMORY;
                }
                return false;
            }
            m_storage_parent = device->Arena;
        }
        m_source_strings                 = strings;

        m_device                         = device;
        m_specification                  = spec;
        m_specification.Name             = m_source_strings[0].data();
        m_specification.VertexFilename   = m_source_strings[1].data();
        m_specification.FragmentFilename = m_source_strings[2].data();
        m_specification.ComputeFilename  = m_source_strings[3].data();
        auto* context                    = Engine::GetContext();
        m_source_vfs                     = services.VFS ? services.VFS : context ? context->VFS : nullptr;
        const auto fail                  = [&](VkResult result) {
            device->CheckDeviceLost(result, "Shader initialization");
            if (status)
            {
                *status = result;
            }
            ResetInitialization();
            return false;
        };
        if (!m_source_vfs || !LocalArena.TryCreateSubArena(BinaryScratchBytes, &m_binary_arena, "Shader/BinaryScratch"))
        {
            return fail(m_source_vfs ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED);
        }

        ShaderCreateInfos.m_allocator           = &LocalArena;
        ShaderModules.m_allocator               = &LocalArena;
        PushConstants.m_allocator               = &LocalArena;
        PushConstantSpecifications.m_allocator  = &LocalArena;
        LayoutBindingSpecifications.m_allocator = &LocalArena;
        SetLayouts.m_allocator                  = &LocalArena;
        if (!ShaderCreateInfos.try_reserve(3) || !ShaderModules.try_reserve(3) || !PushConstants.try_reserve(3) || !PushConstantSpecifications.try_reserve(3) || !LayoutBindingSpecificationMap.try_init(&LocalArena, 16) || !SetLayouts.try_reserve(Renderers::Pipelines::kMaxPSOSetLayouts) || !InternalDescriptorSetLayoutMap.try_init(&LocalArena, 16) || !DescriptorSetMap.try_init(&LocalArena, 16))
        {
            return fail(VK_ERROR_OUT_OF_HOST_MEMORY);
        }

        VkResult result = VK_ERROR_INITIALIZATION_FAILED;
        try
        {
            if (!CreateModule(&result, services) || !CreateDescriptorSetLayouts(&result, services))
            {
                return fail(result);
            }
        }
        catch (const std::bad_alloc&)
        {
            return fail(VK_ERROR_OUT_OF_HOST_MEMORY);
        }
        catch (const std::exception&)
        {
            return fail(VK_ERROR_INITIALIZATION_FAILED);
        }
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
            if (max_set >= Renderers::Pipelines::kMaxPSOSetLayouts)
            {
                return fail(VK_ERROR_INITIALIZATION_FAILED);
            }
            // Pipeline layout requires a contiguous array from set 0 to max_set.
            // Fill any gap with the device's empty layout so Vulkan sees no holes.
            // Register gap indices in DescriptorSetMap with EmptyDescriptorSet so
            // BindDescriptorSets can bind a valid (zero-binding) set at those slots.
            for (uint32_t i = 0; !InternalDescriptorSetLayoutMap.empty() && i <= max_set; ++i)
            {
                if (InternalDescriptorSetLayoutMap.contains(i))
                {
                    SetLayouts.push(InternalDescriptorSetLayoutMap.at(i));
                }
                else
                {
                    if (!m_device->EmptyDescriptorSetLayout || !m_device->EmptyDescriptorSet)
                    {
                        return fail(VK_ERROR_INITIALIZATION_FAILED);
                    }
                    SetLayouts.push(m_device->EmptyDescriptorSetLayout);
                    auto& sets       = DescriptorSetMap[i];
                    sets.m_allocator = &LocalArena;
                    if (!sets.try_reserve(m_device->SwapchainPtr->BufferredFrameCount))
                    {
                        return fail(VK_ERROR_OUT_OF_HOST_MEMORY);
                    }
                    sets.m_size = m_device->SwapchainPtr->BufferredFrameCount;
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
                if (!LayoutBindingSpecifications.try_push(spec))
                {
                    return fail(VK_ERROR_OUT_OF_HOST_MEMORY);
                }
            }
        }

        if (!BindingsByName.try_init(&LocalArena, LayoutBindingSpecifications.size() * 2 + 4))
        {
            return fail(VK_ERROR_OUT_OF_HOST_MEMORY);
        }
        for (const auto& spec : LayoutBindingSpecifications)
        {
            if (spec.Name)
            {
                BindingsByName[spec.Name] = spec;
            }
        }

        // We remove the Set to avoid double release from the Device and Shader owned resource
        for (const auto [set, _] : m_device->ShaderReservedDescriptorSetLayoutMap)
        {
            if (InternalDescriptorSetLayoutMap.contains(set))
            {
                InternalDescriptorSetLayoutMap.remove(set);
            }
        }
        if (status)
        {
            *status = VK_SUCCESS;
        }
        return true;
    }

    bool Shader::CreateModule(VkResult* status, const ShaderInitializationServices& services)
    {
        *status = VK_ERROR_INITIALIZATION_FAILED;
        if (!services.CreateModule)
        {
            return false;
        }
        const auto copy_name = [&](const std::string& name) -> const char* {
            auto* owned = static_cast<char*>(LocalArena.AllocateNoZero(name.size() + 1, alignof(char)));
            if (!owned)
            {
                *status = VK_ERROR_OUT_OF_HOST_MEMORY;
                return nullptr;
            }
            Helpers::secure_memcpy(owned, name.size() + 1, name.c_str(), name.size() + 1);
            return owned;
        };
        const cstring             paths[]  = {m_specification.VertexFilename, m_specification.FragmentFilename, m_specification.ComputeFilename};
        const ShaderStageFlags    stages[] = {ShaderStageFlags::VERTEX, ShaderStageFlags::FRAGMENT, ShaderStageFlags::COMPUTE};
        const spv::ExecutionModel models[] = {spv::ExecutionModelVertex, spv::ExecutionModelFragment, spv::ExecutionModelGLCompute};
        for (uint32_t stage = 0; stage < 3; ++stage)
        {
            if (!paths[stage][0])
            {
                continue;
            }
            m_binary_arena.Clear();
            Array<uint32_t> binary = {};
            if (!ShaderReader::TryReadAsBinary(*m_source_vfs, &m_binary_arena, paths[stage], BinaryScratchBytes, &binary))
            {
                return false;
            }
            spirv_cross::Compiler compiler(binary.data(), binary.size());
            bool                  entry_found = false;
            for (const auto& entry : compiler.get_entry_points_and_stages())
            {
                entry_found = entry_found || (entry.name == "main" && entry.execution_model == models[stage]);
            }
            if (!entry_found)
            {
                return false;
            }
            compiler.set_entry_point("main", models[stage]);
            const auto resources = compiler.get_shader_resources();
            // Unsupported descriptor classes cannot silently disappear from the layout.
            if (!resources.subpass_inputs.empty() || !resources.atomic_counters.empty() || !resources.acceleration_structures.empty())
            {
                return false;
            }
            const auto reflect = [&](const auto& collection, DescriptorType kind) {
                for (const auto& resource : collection)
                {
                    const auto set     = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                    const auto binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                    if (set >= Renderers::Pipelines::kMaxPSOSetLayouts)
                    {
                        return false;
                    }
                    auto type = kind;
                    if (type == DescriptorType::UNIFORM_BUFFER && set == 0 && binding == 0)
                    {
                        type = DescriptorType::UNIFORM_BUFFER_DYNAMIC;
                    }
                    const auto& reflected_type = compiler.get_type(resource.type_id);
                    // A texel-buffer image needs a different descriptor class.
                    if ((kind == DescriptorType::SAMPLED_IMAGE || kind == DescriptorType::COMBINED_IMAGE_SAMPLER || kind == DescriptorType::STORAGE_IMAGE) && reflected_type.image.dim == spv::DimBuffer)
                    {
                        return false;
                    }
                    uint32_t count         = 1;
                    bool     runtime_array = false;
                    for (size_t dimension = 0; dimension < reflected_type.array.size(); ++dimension)
                    {
                        const auto extent = reflected_type.array[dimension];
                        if (!reflected_type.array_size_literal[dimension] || (extent && count > UINT32_MAX / extent))
                        {
                            return false;
                        }
                        runtime_array  = runtime_array || !extent;
                        count         *= extent;
                    }
                    LayoutBindingSpecification specification = {.Set = set, .Binding = binding, .Count = count, .DescriptorTypeValue = type, .Flags = stages[stage]};
                    if (m_device->ShaderReservedLayoutBindingSpecificationMap.contains(set))
                    {
                        const LayoutBindingSpecification* reserved = nullptr;
                        for (const auto& candidate : m_device->ShaderReservedLayoutBindingSpecificationMap.at(set))
                        {
                            if (candidate.Binding == binding)
                            {
                                reserved = &candidate;
                                break;
                            }
                        }
                        if (!reserved || reserved->DescriptorTypeValue != type || !reserved->Count || (!runtime_array && reserved->Count < count) || static_cast<uint32_t>(reserved->Flags) >= std::size(ShaderStageFlagsMap) || !(ShaderStageFlagsMap[static_cast<uint32_t>(reserved->Flags)] & ShaderStageFlagsMap[static_cast<uint32_t>(stages[stage])]))
                        {
                            return false;
                        }
                        specification.Count = reserved->Count;
                    }
                    else if (runtime_array || !count)
                    {
                        return false;
                    }
                    auto& bindings = LayoutBindingSpecificationMap[set];
                    // Up to three stages may describe each bounded native binding.
                    if (bindings.size() >= 3 * Renderers::Pipelines::kMaxPSOLayoutBindings)
                    {
                        return false;
                    }
                    specification.Name = copy_name(resource.name);
                    if (!specification.Name)
                    {
                        return false;
                    }
                    bindings.m_allocator = &LocalArena;
                    if (!bindings.try_push(specification))
                    {
                        *status = VK_ERROR_OUT_OF_HOST_MEMORY;
                        return false;
                    }
                }
                return true;
            };
            if (!reflect(resources.uniform_buffers, DescriptorType::UNIFORM_BUFFER) || !reflect(resources.storage_buffers, DescriptorType::STORAGE_BUFFER) || !reflect(resources.sampled_images, DescriptorType::COMBINED_IMAGE_SAMPLER) || !reflect(resources.separate_images, DescriptorType::SAMPLED_IMAGE) || !reflect(resources.separate_samplers, DescriptorType::SAMPLER) || !reflect(resources.storage_images, DescriptorType::STORAGE_IMAGE))
            {
                return false;
            }
            if (resources.push_constant_buffers.size() > 1)
            {
                return false;
            }
            for (const auto& resource : resources.push_constant_buffers)
            {
                const auto& type = compiler.get_type(resource.base_type_id);
                if (type.basetype != spirv_cross::SPIRType::Struct || type.member_types.empty())
                {
                    return false;
                }
                uint32_t begin = UINT32_MAX, end = 0;
                for (uint32_t member = 0; member < type.member_types.size(); ++member)
                {
                    const auto offset = compiler.type_struct_member_offset(type, member);
                    const auto bytes  = compiler.get_declared_struct_member_size(type, member);
                    if (bytes > UINT32_MAX - offset)
                    {
                        return false;
                    }
                    begin = std::min(begin, offset);
                    end   = std::max(end, offset + static_cast<uint32_t>(bytes));
                }
                if (begin >= end || begin % 4 || end % 4 || end > m_device->PhysicalDeviceProperties.properties.limits.maxPushConstantsSize)
                {
                    return false;
                }
                const auto* name = copy_name(resource.name);
                if (!name || !PushConstantSpecifications.try_push({.Name = name, .Size = end - begin, .Offset = begin, .Flags = stages[stage]}))
                {
                    *status = VK_ERROR_OUT_OF_HOST_MEMORY;
                    return false;
                }
            }
            VkShaderModule                 module = VK_NULL_HANDLE;
            const VkShaderModuleCreateInfo info   = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = binary.size() * sizeof(uint32_t), .pCode = binary.data()};
            *status                               = m_device->PendingFree.CreateNative(Rendering::DeviceResourceType::SHADERMODULE, &module, [&](VkShaderModule* out) { return services.CreateModule(m_device->LogicalDevice, &info, nullptr, out); });
            if (*status != VK_SUCCESS)
            {
                return false;
            }
            ShaderModules.push(module);
            ShaderCreateInfos.push({.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = static_cast<VkShaderStageFlagBits>(ShaderStageFlagsMap[static_cast<uint32_t>(stages[stage])]), .module = module, .pName = "main"});
            *status = VK_ERROR_INITIALIZATION_FAILED;
        }
        *status = ShaderCreateInfos.empty() ? VK_ERROR_INITIALIZATION_FAILED : VK_SUCCESS;
        return *status == VK_SUCCESS;
    }

    uint32_t Shader::DynamicDescriptorCount() const
    {
        uint32_t count = 0;
        for (const auto& set : LayoutBindingSpecificationMap)
        {
            const auto& bindings = set.second;
            for (uint32_t i = 0; i < bindings.size(); ++i)
            {
                const auto& binding = bindings[i];
                if (binding.DescriptorTypeValue != DescriptorType::UNIFORM_BUFFER_DYNAMIC && binding.DescriptorTypeValue != DescriptorType::STORAGE_BUFFER_DYNAMIC)
                {
                    continue;
                }
                bool duplicate = false;
                for (uint32_t earlier = 0; earlier < i; ++earlier)
                {
                    duplicate |= bindings[earlier].Binding == binding.Binding;
                }
                if (!duplicate)
                {
                    count += binding.Count;
                }
            }
        }
        return count;
    }

    bool Shader::HasStages(VkShaderStageFlags required) const
    {
        VkShaderStageFlags available = 0;
        for (const auto& stage : ShaderCreateInfos)
        {
            if (stage.module != VK_NULL_HANDLE)
            {
                available |= stage.stage;
            }
        }
        return (available & required) == required;
    }

    Specifications::LayoutBindingSpecification Shader::GetLayoutBindingSpecification(cstring name)
    {
        if (!Helpers::secure_strlen(name))
        {
            return {};
        }

        const auto* spec = BindingsByName.find(name);
        return spec ? *spec : LayoutBindingSpecification{};
    }

    void Shader::Dispose()
    {
        ZENGINE_VALIDATE_ASSERT(m_generation_pins == 0, "Scene preparation must release shader generations before shader teardown")
        m_reload_deferred = false;
        if (m_device)
        {
            DestroyModules();
            RetireDescriptorPool();
        }
        ResetInitialization();
        LocalArena.Shutdown();
        m_storage_parent = nullptr;
        m_source_vfs     = nullptr;
    }

    void Shader::ResetInitialization()
    {
        if (m_device)
        {
            // Unpublished initialization has no GPU readers or async PSO jobs.
            // Refused candidates must not grow the accepted-work retirement queue.
            for (auto module : ShaderModules)
            {
                if (module)
                {
                    m_device->PendingFree.DestroyNative(Rendering::DeviceResourceType::SHADERMODULE, module, [&] { vkDestroyShaderModule(m_device->LogicalDevice, module, nullptr); });
                }
            }
            if (m_descriptor_pool)
            {
                m_device->PendingFree.DestroyNative(Rendering::DeviceResourceType::DESCRIPTORPOOL, m_descriptor_pool, [&] { vkDestroyDescriptorPool(m_device->LogicalDevice, m_descriptor_pool, nullptr); });
                m_descriptor_pool = VK_NULL_HANDLE;
            }
        }

        // Descriptor-set layouts are borrowed from the device PSO cache. The cache
        // outlives shaders and retires them during VulkanDevice shutdown.
        InternalDescriptorSetLayoutMap = {};
        ShaderModules                  = {};
        ShaderCreateInfos              = {};
        PushConstants                  = {};
        PushConstantSpecifications     = {};
        LayoutBindingSpecificationMap  = {};
        LayoutBindingSpecifications    = {};
        SetLayouts                     = {};
        DescriptorSetMap               = {};
        BindingsByName                 = {};
        m_binary_arena.Shutdown();
        if (LocalArena.m_memory)
        {
            LocalArena.Clear();
        }
        m_device = nullptr;
    }

    void Shader::Reload()
    {
        if (m_generation_pins != 0)
        {
            m_reload_deferred = true;
            return;
        }
        m_reload_deferred = false;
        if (Generation == UINT32_MAX)
        {
            ZENGINE_CORE_ERROR("Shader generation counter exhausted; refusing reload")
            return;
        }
        ZENGINE_VALIDATE_ASSERT(m_device != nullptr, "Shader::Reload requires an initialized shader")
        ZENGINE_VALIDATE_ASSERT(m_device->PipelineStateCache != nullptr, "Shader::Reload requires the PSO cache")

        const uint32_t retired_generation = Generation;
        const auto     specification      = m_specification;
        auto*          device             = m_device;
        auto*          vfs                = m_source_vfs;
        ++Generation;

        // Descriptor sets remain usable until the render timeline reaches this frame.
        RetireDescriptorPool();
        m_device->PipelineStateCache->InvalidateShaderModules(ShaderModules.data(), static_cast<uint32_t>(ShaderModules.size()), retired_generation);
        DestroyModules();
        ResetInitialization();
        ZENGINE_VALIDATE_ASSERT(TryInitialize(device, specification, nullptr, {.VFS = vfs}), "Failed to reload shader")
    }

    void Shader::DestroyModules()
    {
        for (auto& shader_module : ShaderModules)
        {
            if (!shader_module)
            {
                continue;
            }
            if (m_device->PipelineStateCache)
            {
                m_device->PipelineStateCache->RetireShaderModule(shader_module);
            }
            else
            {
                m_device->PendingFree.DestroyNative(Rendering::DeviceResourceType::SHADERMODULE, shader_module, [&] { vkDestroyShaderModule(m_device->LogicalDevice, shader_module, nullptr); });
            }
        }
        ShaderModules.clear();
    }

    void Shader::RetireDescriptorPool()
    {
        if (m_descriptor_pool == VK_NULL_HANDLE)
        {
            return;
        }

        Hardwares::DeferredFreeEntry entry = {};
        entry.EntryKind                    = Hardwares::DeferredFreeEntry::Kind::VkHandle;
        entry.Data.Vk                      = {m_descriptor_pool, Rendering::DeviceResourceType::DESCRIPTORPOOL, nullptr};
        m_device->DeferFree(entry);
        m_descriptor_pool = VK_NULL_HANDLE;
    }

    bool Shader::CreateDescriptorSetLayouts(VkResult* status, const ShaderInitializationServices& services)
    {
        *status = VK_ERROR_INITIALIZATION_FAILED;
        if (!m_device->PipelineStateCache || !services.CreateLayout || !services.CreatePool || !services.AllocateSets)
        {
            return false;
        }
        std::array<VkDescriptorPoolSize, std::size(DescriptorTypeMap)>                        pool_sizes                   = {};
        uint32_t                                                                              pool_size_count              = 0;
        bool                                                                                  pool_needs_update_after_bind = false; // set true when a non-reserved binding uses UPDATE_AFTER_BIND_BIT

        Array<VkDescriptorSetLayoutBinding>                                                   layout_binding_collection    = {};
        std::array<VkDescriptorSetLayoutBinding, Renderers::Pipelines::kMaxPSOLayoutBindings> bindings                     = {};
        layout_binding_collection.m_data                                                                                   = bindings.data();
        layout_binding_collection.m_capacity                                                                               = bindings.size();

        for (const auto layout_binding_set : LayoutBindingSpecificationMap)
        {
            uint32_t binding_set = layout_binding_set.first;
            if (binding_set >= Renderers::Pipelines::kMaxPSOSetLayouts)
            {
                return false;
            }

            if (m_device->ShaderReservedLayoutBindingSpecificationMap.contains(binding_set))
            {
                if (!m_device->ShaderReservedDescriptorSetLayoutMap.contains(binding_set) || !m_device->ShaderReservedDescriptorSetLayoutMap.at(binding_set) || !m_device->ShaderReservedDescriptorSetMap.contains(binding_set))
                {
                    return false;
                }
                // Since it's a reserved Set The device already created its SetLayout
                InternalDescriptorSetLayoutMap[binding_set] = m_device->ShaderReservedDescriptorSetLayoutMap.at(binding_set);
                continue;
            }

            layout_binding_collection.clear();
            for (uint32_t i = 0; i < layout_binding_set.second.size(); ++i)
            {
                const auto& specification = layout_binding_set.second[i];
                if (static_cast<uint32_t>(specification.DescriptorTypeValue) >= std::size(DescriptorTypeMap) || static_cast<uint32_t>(specification.Flags) >= std::size(ShaderStageFlagsMap) || !specification.Count)
                {
                    return false;
                }
                VkDescriptorSetLayoutBinding binding = {.binding = specification.Binding, .descriptorType = DescriptorTypeMap[static_cast<uint32_t>(specification.DescriptorTypeValue)], .descriptorCount = specification.Count, .stageFlags = ShaderStageFlagsMap[static_cast<uint32_t>(specification.Flags)], .pImmutableSamplers = nullptr};
                bool                         merged  = false;
                for (uint32_t binding_index = 0; binding_index < layout_binding_collection.size(); ++binding_index)
                {
                    auto& existing = layout_binding_collection[binding_index];
                    if (existing.binding != binding.binding)
                    {
                        continue;
                    }

                    if (existing.descriptorType != binding.descriptorType || existing.descriptorCount != binding.descriptorCount)
                    {
                        return false;
                    }
                    existing.stageFlags |= binding.stageFlags;
                    merged               = true;
                    break;
                }
                if (!merged)
                {
                    if (layout_binding_collection.size() == bindings.size())
                    {
                        return false;
                    }
                    layout_binding_collection.push(binding);
                }
            }

            for (const auto& lb : layout_binding_collection)
            {
                auto it = std::find_if(pool_sizes.begin(), pool_sizes.begin() + pool_size_count, [&](const VkDescriptorPoolSize& ps) { return ps.type == lb.descriptorType; });
                if (it == pool_sizes.begin() + pool_size_count)
                {
                    if (pool_size_count == pool_sizes.size())
                    {
                        return false;
                    }
                    pool_sizes[pool_size_count++] = {.type = lb.descriptorType, .descriptorCount = lb.descriptorCount};
                }
                else
                {
                    if (it->descriptorCount > UINT32_MAX - lb.descriptorCount)
                    {
                        return false;
                    }
                    it->descriptorCount += lb.descriptorCount;
                }
            }

            /*
             * Binding flag extension
             */
            // UNIFORM_BUFFER_DYNAMIC is incompatible with UPDATE_AFTER_BIND_POOL_BIT.
            // If this set has a dynamic UBO, disable all UPDATE_AFTER_BIND flags for this set.
            bool has_dynamic_ubo = false;
            for (uint32_t i = 0; i < layout_binding_collection.size(); ++i)
            {
                if (layout_binding_collection[i].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
                {
                    has_dynamic_ubo = true;
                    break;
                }
            }

            std::array<VkDescriptorBindingFlags, Renderers::Pipelines::kMaxPSOLayoutBindings> binding_flags_collection = {};
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
            binding_flags_create_info.bindingCount                                = static_cast<uint32_t>(layout_binding_collection.size());
            binding_flags_create_info.pBindingFlags                               = binding_flags_collection.data();
            /*
             * Creating SetLayout
             */
            VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info     = {};
            descriptor_set_layout_create_info.sType                               = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptor_set_layout_create_info.bindingCount                        = static_cast<uint32_t>(layout_binding_collection.size());
            descriptor_set_layout_create_info.pBindings                           = layout_binding_collection.data();

            // Only set UPDATE_AFTER_BIND_POOL_BIT if at least one binding in this set
            // actually uses UPDATE_AFTER_BIND_BIT — UNIFORM_BUFFER_DYNAMIC is incompatible
            // with layouts that have this flag.
            bool has_update_after_bind                                            = false;
            for (uint32_t k = 0; k < layout_binding_collection.size(); ++k)
            {
                if (binding_flags_collection[k] & VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                {
                    has_update_after_bind = true;
                    break;
                }
            }

            if (m_device->PhysicalDeviceSupportSampledImageBindless && has_update_after_bind)
            {
                descriptor_set_layout_create_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            }
            descriptor_set_layout_create_info.pNext = &binding_flags_create_info;

            auto layout                             = m_device->PipelineStateCache->TryGetOrCreateDescriptorSetLayout(descriptor_set_layout_create_info, status, services.CreateLayout);
            if (!layout)
            {
                return false;
            }
            InternalDescriptorSetLayoutMap[binding_set] = layout;
            // Later recipe validation must not inherit a successful native status.
            *status                                     = VK_ERROR_INITIALIZATION_FAILED;
        }

        /*
         * Ensure correctness with number of frame count
         */
        for (uint32_t i = 0; i < pool_size_count; ++i)
        {
            auto& pool_size = pool_sizes[i];
            if (pool_size.descriptorCount > UINT32_MAX / m_device->SwapchainPtr->BufferredFrameCount)
            {
                return false;
            }
            pool_size.descriptorCount *= m_device->SwapchainPtr->BufferredFrameCount;
        }
        /*
         * Create DescriptorPool
         */
        if (!pool_size_count)
        {
            *status = VK_SUCCESS;
            return true;
        }

        uint32_t non_reserved_set_count = 0;
        for (const auto& layout : InternalDescriptorSetLayoutMap)
        {
            if (!m_device->ShaderReservedDescriptorSetMap.contains(layout.first))
            {
                ++non_reserved_set_count;
            }
        }

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags                      = pool_needs_update_after_bind ? VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT : 0;
        pool_info.maxSets                    = non_reserved_set_count * m_device->SwapchainPtr->BufferredFrameCount;
        pool_info.poolSizeCount              = pool_size_count;
        pool_info.pPoolSizes                 = pool_sizes.data();

        *status                              = m_device->PendingFree.CreateNative(Rendering::DeviceResourceType::DESCRIPTORPOOL, &m_descriptor_pool, [&](VkDescriptorPool* out) { return services.CreatePool(m_device->LogicalDevice, &pool_info, nullptr, out); });
        if (*status != VK_SUCCESS)
        {
            return false;
        }

        /*
         * Create DescriptorSet for non-reserved sets (reserved sets handled above)
         */
        for (const auto layout : InternalDescriptorSetLayoutMap)
        {
            if (m_device->ShaderReservedDescriptorSetMap.contains(layout.first))
            {
                continue;
            }

            auto& sets       = DescriptorSetMap[layout.first];
            sets.m_allocator = &LocalArena;
            if (!sets.try_reserve(m_device->SwapchainPtr->BufferredFrameCount))
            {
                *status = VK_ERROR_OUT_OF_HOST_MEMORY;
                return false;
            }
            sets.m_size                                             = m_device->SwapchainPtr->BufferredFrameCount;
            std::array<VkDescriptorSetLayout, MaxFrames> layout_set = {};
            for (uint32_t i = 0; i < m_device->SwapchainPtr->BufferredFrameCount; ++i)
            {
                layout_set[i] = layout.second;
            }

            VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {};
            descriptor_set_allocate_info.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptor_set_allocate_info.descriptorPool              = m_descriptor_pool;
            descriptor_set_allocate_info.descriptorSetCount          = m_device->SwapchainPtr->BufferredFrameCount;
            descriptor_set_allocate_info.pSetLayouts                 = layout_set.data();
            *status                                                  = services.AllocateSets(m_device->LogicalDevice, &descriptor_set_allocate_info, sets.data());
            if (*status != VK_SUCCESS)
            {
                return false;
            }
        }
        *status = VK_SUCCESS;
        return true;
    }

    void Shader::CreatePushConstantRange()
    {
        for (const auto& specification : PushConstantSpecifications)
        {
            const VkShaderStageFlags stage_flags = ShaderStageFlagsMap[VALUE_FROM_SPEC_MAP(specification.Flags)];
            bool                     merged      = false;
            for (VkPushConstantRange& range : PushConstants)
            {
                if (range.offset != specification.Offset || range.size != specification.Size)
                {
                    continue;
                }

                range.stageFlags |= stage_flags;
                merged            = true;
                break;
            }

            if (!merged)
            {
                PushConstants.push(VkPushConstantRange{.stageFlags = stage_flags, .offset = specification.Offset, .size = specification.Size});
            }
        }
        PushConstantSpecifications.clear();
    }
} // namespace ZEngine::Rendering::Shaders
