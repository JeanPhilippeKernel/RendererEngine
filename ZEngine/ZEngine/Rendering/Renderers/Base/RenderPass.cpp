#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <algorithm>
#include <iterator>

using namespace ZEngine::Rendering::Buffers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Helpers;

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    namespace
    {
        bool IsSharedDescriptorSet(Hardwares::VulkanDevice* device, Shaders::Shader* shader, uint32_t set)
        {
            const auto* layout = device->ShaderReservedDescriptorSetLayoutMap.find(set);
            // A gap can occupy the reserved index without using that ABI.
            return layout && set < shader->SetLayouts.size() && *layout == shader->SetLayouts[set] && device->ShaderReservedDescriptorSetMap.contains(set);
        }

        bool IsSameDescriptorBinding(const Specifications::LayoutBindingSpecification& expected, const Specifications::LayoutBindingSpecification& current)
        {
            return expected.Set == current.Set && expected.Binding == current.Binding && expected.Count == current.Count && expected.DescriptorTypeValue == current.DescriptorTypeValue && expected.Flags == current.Flags;
        }

        bool ResolveStorageImageInfo(Hardwares::ImageBuffer* image, const VkImageSubresourceRange& requested_range, VkDescriptorImageInfo* out_info, VkImageSubresourceRange* out_range)
        {
            if (!image || !out_info || !out_range || !(image->Specification.ImageUsage & VK_IMAGE_USAGE_STORAGE_BIT))
            {
                return false;
            }

            VkImageSubresourceRange image_range = requested_range;
            if (image_range.aspectMask == 0)
                image_range.aspectMask = image->Specification.ImageAspectFlag;
            if (image_range.levelCount == 0)
                image_range.levelCount = image->Specification.MipLevelCount;
            if (image_range.layerCount == 0)
                image_range.layerCount = image->Specification.LayerCount;
            if (image_range.levelCount != 1 || image_range.baseMipLevel >= image->Specification.MipLevelCount || image_range.baseArrayLayer >= image->Specification.LayerCount || image_range.layerCount > image->Specification.LayerCount - image_range.baseArrayLayer || (image_range.aspectMask & image->Specification.ImageAspectFlag) != image_range.aspectMask)
            {
                return false;
            }

            VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D;
            if (image->Specification.BufferUsageType == ImageBufferUsageType::SINGLE_3D_IMAGE)
                view_type = VK_IMAGE_VIEW_TYPE_3D;
            else if (image->Specification.BufferUsageType == ImageBufferUsageType::CUBEMAP || image_range.layerCount > 1)
                view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;

            *out_info             = image->GetDescriptorImageInfo();
            out_info->sampler     = VK_NULL_HANDLE;
            out_info->imageView   = image->GetImageViewHandle(view_type, image_range);
            out_info->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            *out_range            = image_range;
            return out_info->imageView != VK_NULL_HANDLE;
        }

        bool StoreDescriptorReplayRecord(DescriptorReplayRecord* records, uint32_t* record_count, char* names, uint32_t* name_bytes, const DescriptorReplayRecord& record)
        {
            if (!record.Binding.Name)
            {
                return false;
            }

            for (uint32_t index = 0; index < *record_count; ++index)
            {
                DescriptorReplayRecord& existing = records[index];
                if (existing.Kind != record.Kind || existing.FrameIndex != record.FrameIndex || existing.ArrayElement != record.ArrayElement || Helpers::secure_strcmp(existing.Name, record.Binding.Name) != 0)
                {
                    continue;
                }

                const cstring stable_name = existing.Name;
                existing                  = record;
                existing.Name             = stable_name;
                existing.Binding.Name     = stable_name;
                return true;
            }

            if (*record_count == kMaxDescriptorReplayBindings)
            {
                return false;
            }

            uint32_t name_length = 0;
            while (name_length < kMaxDescriptorReplayNameBytes && record.Binding.Name[name_length])
            {
                ++name_length;
            }
            if (!name_length || name_length >= kMaxDescriptorReplayNameBytes - *name_bytes)
            {
                return false;
            }
            char* stable_name = names + *name_bytes;
            Helpers::secure_strcpy(stable_name, name_length + 1, record.Binding.Name);
            *name_bytes                         += name_length + 1;

            DescriptorReplayRecord& destination  = records[(*record_count)++];
            destination                          = record;
            destination.Name                     = stable_name;
            destination.Binding.Name             = stable_name;
            return true;
        }

        bool HasDescriptorReplayRecord(const DescriptorReplayRecord* records, uint32_t record_count, cstring name)
        {
            for (uint32_t index = 0; index < record_count; ++index)
            {
                if (Helpers::secure_strcmp(records[index].Name, name) == 0)
                    return true;
            }
            return false;
        }

        bool AreDescriptorReplayRecordsCompatible(const DescriptorReplayRecord* records, uint32_t record_count, Shaders::Shader* shader)
        {
            if (!shader)
                return false;

            for (uint32_t index = 0; index < record_count; ++index)
            {
                const DescriptorReplayRecord& record  = records[index];
                const auto                    current = shader->GetLayoutBindingSpecification(record.Name);
                if (current.Set == UINT32_MAX || current.Binding == UINT32_MAX || !IsSameDescriptorBinding(record.Binding, current))
                {
                    ZENGINE_CORE_ERROR("Descriptor replay: input '{}' no longer matches its recorded interface (set={}, binding={}, count={}; current set={}, binding={}, count={})", record.Name, record.Binding.Set, record.Binding.Binding, record.Binding.Count, current.Set, current.Binding, current.Count)
                    return false;
                }
            }

            for (const auto& binding : shader->LayoutBindingSpecifications)
            {
                if (!HasDescriptorReplayRecord(records, record_count, binding.Name))
                {
                    ZENGINE_CORE_ERROR("Descriptor replay: input '{}' has no recorded binding ({} records)", binding.Name, record_count)
                    return false;
                }
            }
            return true;
        }
    } // namespace

    PassDescriptorBank::~PassDescriptorBank()
    {
        Reset();
    }

    bool PassDescriptorBank::Prepare(Hardwares::VulkanDevice* device, Shaders::Shader* shader)
    {
        if (!device || !device->LogicalDevice || !device->SwapchainPtr || !shader)
        {
            return false;
        }
        const uint32_t frames = device->SwapchainPtr->BufferredFrameCount;
        const uint32_t sets   = static_cast<uint32_t>(shader->SetLayouts.size());
        if (!frames || frames > MaxFrames || sets > MaxSets)
        {
            return false;
        }
        if (m_device == device && m_shader == shader && m_generation == shader->Generation && m_frame_count == frames && m_set_count == sets)
        {
            return true;
        }

        Specifications::LayoutBindingSpecification bindings[MaxBindings]    = {};
        uint32_t                                   binding_count            = 0;
        VkDescriptorPoolSize                       sizes[MaxBindings]       = {};
        uint32_t                                   size_count               = 0;
        bool                                       dynamic_uniform[MaxSets] = {};
        bool                                       sampled_image[MaxSets]   = {};
        // The canonical name map is also used by every setter. Deduplicate by
        // physical binding, since reflection can alias a binding across stages.
        for (const auto& named : shader->BindingsByName)
        {
            const auto& binding = named.second;
            if (binding.Set >= sets || !binding.Count)
            {
                return false;
            }
            if (IsSharedDescriptorSet(device, shader, binding.Set))
            {
                continue;
            }
            if (binding.Count > MaxBindings || static_cast<uint32_t>(binding.DescriptorTypeValue) >= std::size(DescriptorTypeMap))
            {
                return false;
            }
            bool duplicate = false;
            for (uint32_t index = 0; index < binding_count; ++index)
            {
                if (bindings[index].Set == binding.Set && bindings[index].Binding == binding.Binding)
                {
                    if (bindings[index].Count != binding.Count || bindings[index].DescriptorTypeValue != binding.DescriptorTypeValue)
                    {
                        return false;
                    }
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
            {
                continue;
            }
            if (binding_count == MaxBindings)
            {
                return false;
            }
            bindings[binding_count++]     = binding;
            const auto type               = DescriptorTypeMap[static_cast<uint32_t>(binding.DescriptorTypeValue)];
            dynamic_uniform[binding.Set] |= type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            sampled_image[binding.Set]   |= type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            uint32_t index                = 0;
            while (index < size_count && sizes[index].type != type)
            {
                ++index;
            }
            if (index == size_count)
            {
                sizes[size_count++] = {.type = type};
            }
            sizes[index].descriptorCount += binding.Count * frames;
        }

        uint32_t local_sets        = 0;
        bool     update_after_bind = false;
        for (uint32_t set = 0; set < sets; ++set)
        {
            const auto* shared = IsSharedDescriptorSet(device, shader, set) ? device->ShaderReservedDescriptorSetMap.find(set) : nullptr;
            if (shared)
            {
                if (shared->size() != frames)
                {
                    return false;
                }
                for (const auto handle : *shared)
                {
                    if (!handle)
                    {
                        return false;
                    }
                }
                continue;
            }
            if (!shader->SetLayouts[set])
            {
                return false;
            }
            ++local_sets;
            update_after_bind |= device->PhysicalDeviceSupportSampledImageBindless && sampled_image[set] && !dynamic_uniform[set];
        }

        VkDescriptorPool                                            pool       = VK_NULL_HANDLE;
        std::array<std::array<VkDescriptorSet, MaxFrames>, MaxSets> prepared   = {};
        auto                                                        retirement = local_sets ? device->PendingFree.Reserve() : Hardwares::DeferredFreeQueue::Lease{};
        if (local_sets && !retirement)
        {
            return false;
        }
        if (local_sets)
        {
            const VkDescriptorPoolCreateInfo info = {
                .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .flags         = update_after_bind ? VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT : VkDescriptorPoolCreateFlags{0},
                .maxSets       = local_sets * frames,
                .poolSizeCount = size_count,
                .pPoolSizes    = sizes,
            };
            if (vkCreateDescriptorPool(device->LogicalDevice, &info, nullptr, &pool) != VK_SUCCESS)
            {
                return false;
            }
            for (uint32_t set = 0; set < sets; ++set)
            {
                if (IsSharedDescriptorSet(device, shader, set))
                {
                    continue;
                }
                VkDescriptorSetLayout layouts[MaxFrames] = {};
                for (uint32_t frame = 0; frame < frames; ++frame)
                {
                    layouts[frame] = shader->SetLayouts[set];
                }
                const VkDescriptorSetAllocateInfo allocation = {
                    .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .descriptorPool     = pool,
                    .descriptorSetCount = frames,
                    .pSetLayouts        = layouts,
                };
                if (vkAllocateDescriptorSets(device->LogicalDevice, &allocation, prepared[set].data()) != VK_SUCCESS)
                {
                    // This draft was never exposed or submitted.
                    vkDestroyDescriptorPool(device->LogicalDevice, pool, nullptr);
                    return false;
                }
            }
        }
        for (uint32_t set = 0; set < sets; ++set)
        {
            if (IsSharedDescriptorSet(device, shader, set))
            {
                const auto& shared = device->ShaderReservedDescriptorSetMap.at(set);
                for (uint32_t frame = 0; frame < frames; ++frame)
                {
                    prepared[set][frame] = shared[frame];
                }
            }
        }
        Reset();
        m_device                 = device;
        m_shader                 = shader;
        m_pool                   = pool;
        m_retirement_reservation = retirement.Detach();
        m_generation             = shader->Generation;
        m_frame_count            = frames;
        m_set_count              = sets;
        m_sets                   = prepared;
        return true;
    }

    std::span<const VkDescriptorSet> PassDescriptorBank::GetSets(uint32_t set) const
    {
        if (!m_device || !m_shader || m_shader->Generation != m_generation || set >= m_set_count)
        {
            return {};
        }
        return {m_sets[set].data(), m_frame_count};
    }

    void PassDescriptorBank::Reset()
    {
        if (m_pool)
        {
            Hardwares::DeferredFreeEntry entry = {};
            entry.EntryKind                    = Hardwares::DeferredFreeEntry::Kind::VkHandle;
            entry.Reservation                  = m_retirement_reservation;
            entry.Data.Vk                      = {m_pool, Rendering::DeviceResourceType::DESCRIPTORPOOL, nullptr};
            m_device->DeferFree(entry);
        }
        m_device                 = nullptr;
        m_shader                 = nullptr;
        m_pool                   = VK_NULL_HANDLE;
        m_retirement_reservation = 0;
        m_generation             = UINT32_MAX;
        m_frame_count            = 0;
        m_set_count              = 0;
        m_sets                   = {};
    }

    void PassDescriptorBank::Swap(PassDescriptorBank& other) noexcept
    {
        std::swap(m_device, other.m_device);
        std::swap(m_shader, other.m_shader);
        std::swap(m_pool, other.m_pool);
        std::swap(m_retirement_reservation, other.m_retirement_reservation);
        std::swap(m_generation, other.m_generation);
        std::swap(m_frame_count, other.m_frame_count);
        std::swap(m_set_count, other.m_set_count);
        std::swap(m_sets, other.m_sets);
    }

    std::span<const VkDescriptorSet> DescriptorBoundPass::PrepareDescriptorSets(uint32_t set)
    {
        auto* pipeline = GetPipeline();
        if (!pipeline || !m_descriptor_bank.Prepare(m_device, pipeline->Shader))
        {
            return {};
        }
        return m_descriptor_bank.GetSets(set);
    }

    VkDescriptorSet DescriptorBoundPass::ResolveDescriptorSet(void* context, uint32_t set, uint32_t frame)
    {
        const auto sets = static_cast<DescriptorBoundPass*>(context)->m_descriptor_bank.GetSets(set);
        return frame < sets.size() ? sets[frame] : VK_NULL_HANDLE;
    }

    void DescriptorBoundPass::ReleaseDescriptorStorage()
    {
        m_descriptor_preparation.Reset();
        m_descriptor_interface_verified      = false;
        m_unpublished_descriptor_preparation = false;
        m_descriptor_frame_index             = UINT32_MAX;
        m_prepared_descriptor_frames         = 0;
        m_prepared_descriptor_shader         = nullptr;
        m_prepared_descriptor_generation     = UINT32_MAX;
        m_descriptor_bank.Reset();
        for (auto& frame : m_retained_textures)
        {
            for (auto& binding : frame)
            {
                binding = {};
            }
        }
    }

    /*
     * DescriptorBoundPass
     */

    void DescriptorBoundPass::InitializeDescriptorBindings(Hardwares::VulkanDevice* device)
    {
        ZENGINE_VALIDATE_ASSERT(TryInitializeDescriptorBindings(device), "Descriptor bindings require available device arena storage")
    }

    bool DescriptorBoundPass::TryInitializeDescriptorBindings(Hardwares::VulkanDevice* device)
    {
        if (!device || !device->Arena || !BoundBindings.try_init(device->Arena, 2 * kMaxDescriptorReplayBindings))
        {
            return false;
        }
        m_device = device;
        return true;
    }

    void DescriptorBoundPass::AttachPipelineCallbacks()
    {
        if (auto* pipeline = GetPipeline())
        {
            pipeline->ReplayDescriptors       = &DescriptorBoundPass::ReplayDescriptorBindings;
            pipeline->ResolveDescriptorSet    = &DescriptorBoundPass::ResolveDescriptorSet;
            pipeline->DescriptorReplayContext = this;
        }
    }

    void DescriptorBoundPass::ResetSceneBindings()
    {
        ReleaseDescriptorStorage();
        BoundBindings.clear();
        ResetDescriptorReplayRecords();
        if (auto* pipeline = GetPipeline())
        {
            pipeline->DescriptorBindingsValid = true;
        }
    }

    void DescriptorBoundPass::ResetDescriptorReplayRecords()
    {
        DescriptorReplayRecordCount = 0;
        DescriptorReplayNameBytes   = 0;
        DescriptorReplayValid       = true;
        for (auto& record : DescriptorReplayRecords)
        {
            record = {};
        }
    }

    bool DescriptorBoundPass::VerifyDescriptorBindings()
    {
        const auto* pipeline = GetPipeline();
        if (!pipeline || !pipeline->Shader || !DescriptorReplayValid)
        {
            return false;
        }

        const auto& layout_binding_specification = pipeline->Shader->LayoutBindingSpecifications;
        if (m_descriptor_preparation.Active())
        {
            const auto frame = DescriptorFrameIndex();
            if (!m_descriptor_preparation.Verify({layout_binding_specification.data(), layout_binding_specification.size()}, frame, pipeline->Shader->Generation))
            {
                ZENGINE_CORE_WARN("Pipeline '{}': required descriptors were not completely prepared for the active frame/generation", Specification.PipelineDescription.DebugName ? Specification.PipelineDescription.DebugName : "?")
                return false;
            }
        }
        // A shared binding can occur in multiple reflected shader stages while
        // BoundBindings is a set of names. Verify membership, not array length.
        uint32_t missing_count = 0;
        for (const auto& specification : layout_binding_specification)
        {
            if (!BoundBindings.contains(specification.Name))
            {
                ++missing_count;
                ZENGINE_CORE_WARN("Pipeline '{}': unset input '{}'", Specification.PipelineDescription.DebugName ? Specification.PipelineDescription.DebugName : "?", specification.Name)
            }
        }
        if (missing_count)
        {
            ZENGINE_CORE_WARN("Pipeline '{}': {} unset input(s)", Specification.PipelineDescription.DebugName ? Specification.PipelineDescription.DebugName : "?", missing_count)
        }
        if (missing_count == 0)
        {
            const auto frame = DescriptorFrameIndex();
            if (m_unpublished_descriptor_preparation)
            {
                if (frame >= PassDescriptorBank::MaxFrames || m_prepared_descriptor_shader != pipeline->Shader || m_prepared_descriptor_generation != pipeline->Shader->Generation)
                {
                    return false;
                }
                m_prepared_descriptor_frames |= uint32_t(1) << frame;
            }
            else if (frame < PassDescriptorBank::MaxFrames)
            {
                auto* resources = static_cast<RenderResourceManager*>(m_device->RRM);
                for (const auto& binding : m_retained_textures[frame])
                {
                    if (!binding.Pin)
                    {
                        continue;
                    }
                    RenderResourceManager::TextureVersionSnapshot snapshot = {};
                    if (!resources || !resources->GetFrameTextureVersion(binding.Pin, &snapshot))
                    {
                        return false;
                    }
                }
            }
            m_descriptor_interface_verified = true;
            return true;
        }
        return false;
    }

    GraphicPass::~GraphicPass()
    {
        Dispose();
    }

    void DescriptorBoundPass::BeginDescriptorPreparation()
    {
        const auto* frame = m_device && m_device->SwapchainPtr ? m_device->SwapchainPtr->CurrentFrame : nullptr;
        BeginDescriptorPreparation(frame ? frame->Index : UINT32_MAX, false);
    }

    uint32_t DescriptorBoundPass::DescriptorFrameIndex() const
    {
        if (m_descriptor_preparation.Active())
        {
            return m_descriptor_frame_index;
        }
        const auto* frame = m_device && m_device->SwapchainPtr ? m_device->SwapchainPtr->CurrentFrame : nullptr;
        return frame ? frame->Index : UINT32_MAX;
    }

    void DescriptorBoundPass::BeginPreparedDescriptorPreparation(uint32_t frame)
    {
        BeginDescriptorPreparation(frame, true);
    }

    void DescriptorBoundPass::BeginDescriptorPreparation(uint32_t frame, bool unpublished)
    {
        BoundBindings.clear();
        m_descriptor_frame_index             = frame;
        m_unpublished_descriptor_preparation = unpublished;
        const auto* pipeline                 = GetPipeline();
        auto*       shader                   = pipeline ? pipeline->Shader : nullptr;
        if (unpublished)
        {
            if (m_prepared_descriptor_shader != shader || !shader || m_prepared_descriptor_generation != shader->Generation)
            {
                m_prepared_descriptor_frames = 0;
            }
            m_prepared_descriptor_shader     = shader;
            m_prepared_descriptor_generation = shader ? shader->Generation : UINT32_MAX;
            if (frame < PassDescriptorBank::MaxFrames)
            {
                m_prepared_descriptor_frames &= ~(uint32_t(1) << frame);
            }
        }
        m_descriptor_preparation.Begin(frame, shader ? shader->Generation : UINT32_MAX);
        if (!m_device || !m_device->SwapchainPtr || frame >= m_device->SwapchainPtr->BufferredFrameCount || !shader)
        {
            m_descriptor_preparation.Reject();
        }
        else if (!m_descriptor_bank.Prepare(m_device, pipeline->Shader))
        {
            m_descriptor_preparation.Reject();
        }
    }

    bool DescriptorBoundPass::AdoptPreparedDescriptors(DescriptorBoundPass& prepared)
    {
        const auto* pipeline = GetPipeline();
        const auto  frames   = m_device && m_device->SwapchainPtr ? m_device->SwapchainPtr->BufferredFrameCount : 0;
        if (&prepared == this || !frames || frames > PassDescriptorBank::MaxFrames || prepared.m_device != m_device || !pipeline || !pipeline->Shader || !prepared.m_unpublished_descriptor_preparation || prepared.m_prepared_descriptor_shader != pipeline->Shader || prepared.m_prepared_descriptor_generation != pipeline->Shader->Generation || prepared.m_prepared_descriptor_frames != (uint32_t(1) << frames) - 1 || !prepared.VerifyDescriptorBindings())
        {
            return false;
        }
        m_descriptor_bank.Swap(prepared.m_descriptor_bank);
        for (uint32_t frame = 0; frame < PassDescriptorBank::MaxFrames; ++frame)
        {
            for (uint32_t binding = 0; binding < kMaxDescriptorReplayBindings; ++binding)
            {
                std::swap(m_retained_textures[frame][binding], prepared.m_retained_textures[frame][binding]);
            }
        }
        prepared.ReleaseDescriptorStorage(); // Defers the replaced pool; no GPU wait.
        m_descriptor_preparation.Reset();
        m_unpublished_descriptor_preparation = false;
        m_descriptor_frame_index             = UINT32_MAX;
        m_prepared_descriptor_frames         = 0;
        m_prepared_descriptor_shader         = nullptr;
        m_prepared_descriptor_generation     = UINT32_MAX;
        // Do not copy candidate replay pointers: frame-local scene buffers can
        // retire after first recording. Normal graph preparation records fresh
        // inputs before this bank is used or a shader generation can replay it.
        return true;
    }

    void PreparedDescriptorPass::InitializeForPipeline(Hardwares::VulkanDevice* device, Pipelines::IPipeline* pipeline)
    {
        ZENGINE_VALIDATE_ASSERT(TryInitializeForPipeline(device, pipeline), "Prepared descriptor metadata allocation failed")
    }

    bool PreparedDescriptorPass::TryInitializeForPipeline(Hardwares::VulkanDevice* device, Pipelines::IPipeline* pipeline)
    {
        if (!pipeline || !TryInitializeDescriptorBindings(device))
        {
            return false;
        }
        m_pipeline = pipeline;
        return true;
    }

    void PreparedDescriptorPass::Initialize(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification)
    {
        Specification = std::move(specification);
        InitializeDescriptorBindings(device);
    }

    void PreparedDescriptorPass::Dispose()
    {
        ReleaseDescriptorStorage();
    }

    bool PreparedDescriptorPass::Verify()
    {
        return VerifyDescriptorBindings();
    }

    void GraphicPass::Initialize(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification)
    {
        InitializeWithAttachment(device, std::move(specification), nullptr);
    }

    void GraphicPass::InitializePrepared(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification, struct Attachment* attachment)
    {
        ZENGINE_VALIDATE_ASSERT(attachment && !specification.SwapchainAsRenderTarget, "Prepared graphics backend requires a graph-owned attachment")
        InitializeWithAttachment(device, std::move(specification), attachment);
    }

    bool GraphicPass::TryInitializePrepared(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification, struct Attachment* attachment, Shaders::Shader* shader)
    {
        if (!attachment || !shader || specification.SwapchainAsRenderTarget || !TryInitializeDescriptorBindings(device))
        {
            return false;
        }
        RenderTargets.m_allocator = device->Arena;
        if (!RenderTargets.try_reserve(8))
        {
            return false;
        }
        auto* storage = device->Arena->TryResize(nullptr, 0, sizeof(Pipelines::GraphicPipeline), alignof(Pipelines::GraphicPipeline));
        if (!storage)
        {
            return false;
        }
        Specification     = std::move(specification);
        m_owns_attachment = false;
        Attachment        = attachment;
        Pipeline          = new (storage) Pipelines::GraphicPipeline{};
        Pipeline->InitializePrepared(device, std::move(Specification.PipelineDescription), attachment, shader);
        AttachPipelineCallbacks();
        return true;
    }

    void GraphicPass::InitializeWithAttachment(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification, struct Attachment* prepared_attachment)
    {
        Specification     = std::move(specification);
        m_owns_attachment = prepared_attachment == nullptr;

        InitializeDescriptorBindings(device);
        RenderTargets.init(m_device->Arena, prepared_attachment ? 8 : 4);

        if (Specification.SwapchainAsRenderTarget)
        {
            Attachment = m_device->SwapchainPtr->SwapchainAttachment;
            Pipeline   = ZPushStructCtor<Pipelines::GraphicPipeline>(m_device->Arena, std::source_location::current());
            Pipeline->Initialize(m_device, std::move(Specification.PipelineDescription), Attachment);
        }
        else
        {
            if (prepared_attachment)
            {
                Attachment = prepared_attachment;
            }
            else
            {
                Specifications::AttachmentSpecification attachment_specification = {};
                attachment_specification.BindPoint                               = PipelineBindPoint::GRAPHIC;
                attachment_specification.ColorAttachements.init(device->Arena, 4);
                attachment_specification.SubpassSpecifications.init(device->Arena, 4);
                attachment_specification.ColorsMap.init(device->Arena, 4);
                attachment_specification.DependenciesMap.init(device->Arena, 4);
                attachment_specification.SubpassDependencies.init(device->Arena, 4);

                uint32_t color_map_index = 0;
                for (const auto& handle : Specification.Inputs)
                {
                    const auto& texture                                                 = device->GlobalTextures.Access(handle);
                    bool        is_depth_texture                                        = texture->IsDepthTexture;
                    ImageLayout initial_layout                                          = is_depth_texture ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
                    ImageLayout final_layout                                            = is_depth_texture ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
                    ImageLayout reference_layout                                        = is_depth_texture ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::COLOR_ATTACHMENT_OPTIMAL;

                    attachment_specification.ColorsMap[color_map_index]                 = {};
                    attachment_specification.ColorsMap[color_map_index].Format          = texture->Specification.Format;
                    attachment_specification.ColorsMap[color_map_index].Load            = LoadOperation::LOAD;
                    attachment_specification.ColorsMap[color_map_index].Store           = StoreOperation::STORE;
                    attachment_specification.ColorsMap[color_map_index].Initial         = initial_layout;
                    attachment_specification.ColorsMap[color_map_index].Final           = final_layout;
                    attachment_specification.ColorsMap[color_map_index].ReferenceLayout = reference_layout;
                    color_map_index++;
                }

                for (uint32_t output_index = 0; output_index < Specification.ExternalOutputs.size(); ++output_index)
                {
                    const auto&   handle                                                = Specification.ExternalOutputs[output_index];
                    auto          texture                                               = device->GlobalTextures.Access(handle);
                    auto&         output_spec                                           = texture->Specification;
                    bool          is_depth_image_format                                 = (output_spec.Format == ImageFormat::DEPTH_STENCIL_FROM_DEVICE);
                    LoadOperation load_op                                               = output_index < Specification.ExternalOutputLoadOps.size() ? Specification.ExternalOutputLoadOps[output_index] : output_spec.LoadOp;
                    ImageLayout   initial_layout                                        = (load_op == LoadOperation::CLEAR) ? ImageLayout::UNDEFINED : is_depth_image_format ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
                    ImageLayout   final_layout                                          = is_depth_image_format ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
                    ImageLayout   reference_layout                                      = is_depth_image_format ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::COLOR_ATTACHMENT_OPTIMAL;

                    attachment_specification.ColorsMap[color_map_index]                 = {};
                    attachment_specification.ColorsMap[color_map_index].Format          = output_spec.Format;
                    attachment_specification.ColorsMap[color_map_index].Load            = load_op;
                    attachment_specification.ColorsMap[color_map_index].Store           = StoreOperation::STORE;
                    attachment_specification.ColorsMap[color_map_index].Initial         = initial_layout;
                    attachment_specification.ColorsMap[color_map_index].Final           = final_layout;
                    attachment_specification.ColorsMap[color_map_index].ReferenceLayout = reference_layout;
                    color_map_index++;
                }

                Attachment = ZPushStructCtorArgs<RenderPasses::Attachment>(m_device->Arena, m_device, std::move(attachment_specification));
            }
            Pipeline = ZPushStructCtor<Pipelines::GraphicPipeline>(m_device->Arena);
            Pipeline->Initialize(m_device, std::move(Specification.PipelineDescription), Attachment);

            UpdateRenderTargets();
        }

        AttachPipelineCallbacks();
    }

    void GraphicPass::Dispose()
    {
        ReleaseDescriptorStorage();
        // Render targets are borrowed. The graph/import owner, not the backend
        // pass, retires their exact image versions.

        if (Pipeline)
        {
            Pipeline->Dispose();
        }

        if (m_owns_attachment && !Specification.SwapchainAsRenderTarget && Attachment)
        {
            Attachment->Dispose();
        }
    }

    void GraphicPass::Bake()
    {
        Pipeline->Bake();
    }

    bool GraphicPass::Verify()
    {
        return Pipeline && Pipeline->EnsureCurrent() && VerifyDescriptorBindings();
    }

    void DescriptorBoundPass::SetDynamicUniform(cstring key_name, VkDeviceSize range, const Core::Memory::BufferView* source)
    {
        DescriptorWriteScope attempt{m_descriptor_preparation};
        auto                 validity_output = ValidateInput(key_name, DescriptorReplayKind::DynamicUniform);
        if (!validity_output.first)
            return;

        const auto& spec     = validity_output.second;
        auto*       pipeline = GetPipeline();
        auto*       shader   = pipeline ? pipeline->Shader : nullptr;
        if (!shader)
            return;
        const auto set_array = PrepareDescriptorSets(spec.Set);
        if (set_array.empty())
        {
            ZENGINE_CORE_ERROR("SetDynamicUniform: descriptor set {} not found for key '{}'", spec.Set, key_name)
            return;
        }

        const uint32_t frame_index = DescriptorFrameIndex();
        if (!source && m_scene_camera_source && Helpers::secure_strcmp(key_name, "UBCamera") == 0)
        {
            source = m_scene_camera_source;
        }
        if (frame_index >= set_array.size() || !set_array[frame_index] || !range)
        {
            ZENGINE_CORE_ERROR("SetDynamicUniform: descriptor frame {} not found for key '{}'", frame_index, key_name)
            return;
        }

        VkBuffer handle = VK_NULL_HANDLE;
        if (source)
        {
            if (!source->Handle || !(source->Usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) || range > source->Size)
            {
                return;
            }
            handle = source->Handle;
        }
        else
        {
            if (frame_index >= std::size(m_device->FrameHeaps) || !m_device->FrameHeaps[frame_index].Backing.Handle || range > Hardwares::PerFrameUploadHeap::kCapacity)
            {
                return;
            }
            handle = m_device->FrameHeaps[frame_index].Backing.Handle;
        }
        VkDescriptorBufferInfo buffer_info = {.buffer = handle, .offset = 0, .range = range};
        VkWriteDescriptorSet   write       = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = set_array[frame_index],
            .dstBinding      = spec.Binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .pBufferInfo     = &buffer_info,
        };
        vkUpdateDescriptorSets(m_device->LogicalDevice, 1, &write, 0, nullptr);
        BoundBindings.insert(key_name);
        RecordDescriptorBinding({.Binding = spec, .Buffer = source, .Range = range, .Kind = DescriptorReplayKind::DynamicUniform});
        attempt.Accepted = true;
    }

    void DescriptorBoundPass::SetStorageBuffer(cstring key_name, const Core::Memory::BufferView* buffer)
    {
        DescriptorWriteScope attempt{m_descriptor_preparation};
        if (!buffer || !buffer->Handle)
        {
            ZENGINE_CORE_WARN("SetStorageBuffer: null buffer for key '{}'", key_name)
            return;
        }

        auto validity_output = ValidateInput(key_name, DescriptorReplayKind::StorageBuffer);
        if (!validity_output.first)
            return;

        const auto& spec     = validity_output.second;
        auto*       pipeline = GetPipeline();
        auto*       shader   = pipeline ? pipeline->Shader : nullptr;
        if (!shader)
            return;
        const auto set_array = PrepareDescriptorSets(spec.Set);
        if (set_array.empty())
        {
            ZENGINE_CORE_ERROR("SetStorageBuffer: descriptor set {} not found for key '{}'", spec.Set, key_name)
            return;
        }

        const uint32_t frame_index = DescriptorFrameIndex();
        if (frame_index >= set_array.size())
        {
            ZENGINE_CORE_ERROR("SetStorageBuffer: descriptor frame {} not found for key '{}'", frame_index, key_name)
            return;
        }

        VkDescriptorBufferInfo buffer_info = {.buffer = buffer->Handle, .offset = 0, .range = VK_WHOLE_SIZE};
        VkWriteDescriptorSet   write       = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = set_array[frame_index],
            .dstBinding      = spec.Binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo     = &buffer_info,
        };
        vkUpdateDescriptorSets(m_device->LogicalDevice, 1, &write, 0, nullptr);
        BoundBindings.insert(key_name);
        RecordDescriptorBinding({.Binding = spec, .Buffer = buffer, .Kind = DescriptorReplayKind::StorageBuffer});
        attempt.Accepted = true;
    }

    void DescriptorBoundPass::SetStorageBufferForFrame(cstring key_name, uint32_t frame_index, const Core::Memory::BufferView* buffer)
    {
        DescriptorWriteScope attempt{m_descriptor_preparation};
        if (!buffer || !buffer->Handle)
        {
            ZENGINE_CORE_WARN("SetStorageBufferForFrame: null buffer for key '{}'", key_name)
            return;
        }

        auto validity_output = ValidateInput(key_name, DescriptorReplayKind::StorageBuffer);
        if (!validity_output.first)
            return;

        const auto& spec     = validity_output.second;
        auto*       pipeline = GetPipeline();
        auto*       shader   = pipeline ? pipeline->Shader : nullptr;
        if (!shader)
            return;
        const auto set_array = PrepareDescriptorSets(spec.Set);
        if (set_array.empty() || frame_index >= set_array.size())
        {
            ZENGINE_CORE_ERROR("SetStorageBufferForFrame: descriptor set {} or frame {} not found for key '{}'", spec.Set, frame_index, key_name)
            return;
        }

        VkDescriptorBufferInfo buffer_info = {.buffer = buffer->Handle, .offset = 0, .range = VK_WHOLE_SIZE};
        VkWriteDescriptorSet   write       = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = set_array[frame_index],
            .dstBinding      = spec.Binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo     = &buffer_info,
        };
        vkUpdateDescriptorSets(m_device->LogicalDevice, 1, &write, 0, nullptr);
        BoundBindings.insert(key_name);
        RecordDescriptorBinding({.Binding = spec, .Buffer = buffer, .FrameIndex = frame_index, .Kind = DescriptorReplayKind::StorageBuffer});
        attempt.Accepted = true;
    }

    DescriptorBoundPass::TextureSourceScope::TextureSourceScope(DescriptorBoundPass* pass, std::span<const RenderResourceManager::TextureVersionPin> sources) : m_pass(pass)
    {
        if (m_pass)
        {
            m_previous                = m_pass->m_texture_sources;
            m_pass->m_texture_sources = sources;
        }
    }

    DescriptorBoundPass::TextureSourceScope::~TextureSourceScope()
    {
        if (m_pass)
        {
            m_pass->m_texture_sources = m_previous;
        }
    }

    DescriptorBoundPass::CameraSourceScope::CameraSourceScope(DescriptorBoundPass* pass, const Core::Memory::BufferView* camera) : m_pass(pass)
    {
        if (m_pass)
        {
            m_previous                    = m_pass->m_scene_camera_source;
            m_pass->m_scene_camera_source = camera;
        }
    }

    DescriptorBoundPass::CameraSourceScope::~CameraSourceScope()
    {
        if (m_pass)
        {
            m_pass->m_scene_camera_source = m_previous;
        }
    }

    const RenderResourceManager::TextureVersionPin* DescriptorBoundPass::FindTextureSource(const Textures::TextureHandle& handle) const
    {
        for (const auto& source : m_texture_sources)
        {
            const auto* snapshot = source.Get();
            if (snapshot && snapshot->Handle.Index == handle.Index && snapshot->Handle.Generation == handle.Generation)
            {
                return &source;
            }
        }
        return nullptr;
    }

    void DescriptorBoundPass::SetTexture(cstring key_name, const Textures::TextureHandle& handle, VkImageLayout image_layout)
    {
        SetTextureBinding(key_name, handle, image_layout, FindTextureSource(handle));
    }

    void DescriptorBoundPass::SetRetainedTexture(cstring key_name, const RenderResourceManager::TextureVersionPin& retained, VkImageLayout image_layout)
    {
        const auto* source = retained.Get();
        SetTextureBinding(key_name, source ? source->Handle : Textures::TextureHandle{}, image_layout, &retained);
    }

    bool DescriptorBoundPass::PrepareImageRetention(const Specifications::LayoutBindingSpecification& spec, const Textures::TextureHandle& handle, uint32_t frame_index, uint32_t array_element, const RenderResourceManager::TextureVersionPin* retained, std::span<const uint32_t> reserved_indices, uint32_t* retained_index, RenderResourceManager::TextureVersionPin* pin)
    {
        if (frame_index >= PassDescriptorBank::MaxFrames)
        {
            return false;
        }
        *retained_index       = UINT32_MAX;
        uint32_t vacant_index = UINT32_MAX;
        for (uint32_t index = 0; index < kMaxDescriptorReplayBindings; ++index)
        {
            if (std::find(reserved_indices.begin(), reserved_indices.end(), index) != reserved_indices.end())
            {
                continue;
            }
            const auto& binding = m_retained_textures[frame_index][index];
            if (binding.Set == spec.Set && binding.Binding == spec.Binding && binding.ArrayElement == array_element)
            {
                *retained_index = index;
                break;
            }
            if (!binding.Pin && vacant_index == UINT32_MAX)
            {
                vacant_index = index;
            }
        }
        if (!retained && *retained_index != UINT32_MAX)
        {
            // Repeated raw callback writes must not discard an exact lease
            // prepared for this image and array element before publication.
            const auto& previous = m_retained_textures[frame_index][*retained_index].Pin;
            const auto* source   = previous.Get();
            if (source && source->Handle.Index == handle.Index && source->Handle.Generation == handle.Generation)
            {
                retained = &previous;
            }
        }
        if (!retained)
        {
            return true;
        }
        const auto* source    = retained->Get();
        const auto* texture   = m_device->GlobalTextures.Access(handle);
        auto*       resources = static_cast<RenderResourceManager*>(m_device->RRM);
        if (!resources || !source || !texture || source->Handle.Index != handle.Index || source->Handle.Generation != handle.Generation || source->Image.Index != texture->BufferHandle.Index || source->Image.Generation != texture->BufferHandle.Generation)
        {
            return false;
        }
        if (*retained_index == UINT32_MAX)
        {
            *retained_index = vacant_index;
        }
        if (*retained_index == UINT32_MAX)
        {
            return false;
        }
        *pin = resources->RetainTextureVersion(*retained);
        return bool(*pin);
    }

    void DescriptorBoundPass::SetTextureBinding(cstring key_name, const Textures::TextureHandle& handle, VkImageLayout image_layout, const RenderResourceManager::TextureVersionPin* retained)
    {
        DescriptorWriteScope attempt{m_descriptor_preparation};
        auto                 validity_output = ValidateInput(key_name, DescriptorReplayKind::Texture);
        if (!validity_output.first)
            return;

        const auto& spec = validity_output.second;
        if (spec.DescriptorTypeValue != DescriptorType::COMBINED_IMAGE_SAMPLER && spec.DescriptorTypeValue != DescriptorType::SAMPLED_IMAGE)
        {
            ZENGINE_CORE_ERROR("SetTexture: shader input '{}' is not a sampled image", key_name)
            return;
        }

        auto* pipeline = GetPipeline();
        auto* shader   = pipeline ? pipeline->Shader : nullptr;
        if (!shader)
            return;
        const auto set_array = PrepareDescriptorSets(spec.Set);
        if (set_array.empty())
        {
            ZENGINE_CORE_ERROR("SetTexture: descriptor set {} not found for key '{}'", spec.Set, key_name)
            return;
        }

        const uint32_t frame_index = DescriptorFrameIndex();
        if (frame_index >= set_array.size() || frame_index >= PassDescriptorBank::MaxFrames)
        {
            ZENGINE_CORE_ERROR("SetTexture: descriptor frame {} not found for key '{}'", frame_index, key_name)
            return;
        }

        const VkDescriptorType vk_type = Specifications::DescriptorTypeMap[VALUE_FROM_SPEC_MAP(spec.DescriptorTypeValue)];
        auto*                  tex_buf = m_device->GlobalTextures.Access(handle);
        auto*                  img_buf = tex_buf ? m_device->ImageBufferManager.Access(tex_buf->BufferHandle) : nullptr;
        if (!img_buf)
        {
            ZENGINE_CORE_ERROR("SetTexture: texture for key '{}' is not resident", key_name)
            return;
        }
        RenderResourceManager::TextureVersionPin pin            = {};
        uint32_t                                 retained_index = UINT32_MAX;
        if (!PrepareImageRetention(spec, handle, frame_index, 0, retained, {}, &retained_index, &pin))
        {
            return;
        }
        VkDescriptorImageInfo image_info = img_buf->GetDescriptorImageInfo();
        if (image_layout != VK_IMAGE_LAYOUT_MAX_ENUM)
            image_info.imageLayout = image_layout;
        if (!image_info.imageView || (vk_type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && !image_info.sampler) || (image_info.imageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && image_info.imageLayout != VK_IMAGE_LAYOUT_GENERAL && image_info.imageLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL && image_info.imageLayout != VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL))
        {
            ZENGINE_CORE_ERROR("SetTexture: invalid sampled descriptor contents for '{}'", key_name)
            return;
        }
        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = set_array[frame_index],
            .dstBinding      = spec.Binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk_type,
            .pImageInfo      = &image_info,
        };
        vkUpdateDescriptorSets(m_device->LogicalDevice, 1, &write, 0, nullptr);
        BoundBindings.insert(key_name);
        const bool retained_texture = bool(pin);
        if (retained_index != UINT32_MAX)
        {
            auto& binding        = m_retained_textures[frame_index][retained_index];
            binding.Pin          = std::move(pin);
            binding.Set          = spec.Set;
            binding.Binding      = spec.Binding;
            binding.ArrayElement = 0;
        }
        RecordDescriptorBinding({.Binding = spec, .Texture = handle, .ImageLayout = image_layout, .RetainedTexture = retained_texture, .Kind = DescriptorReplayKind::Texture});
        attempt.Accepted = true;
    }

    void DescriptorBoundPass::SetStorageImage(cstring key_name, const Textures::TextureHandle& handle, const VkImageSubresourceRange& requested_range, uint32_t array_element)
    {
        SetStorageImageBindings(key_name, handle, &requested_range, 1, array_element, false, FindTextureSource(handle));
    }

    void DescriptorBoundPass::SetRetainedStorageImage(cstring key_name, const RenderResourceManager::TextureVersionPin& retained, const VkImageSubresourceRange& requested_range, uint32_t array_element)
    {
        const auto* source = retained.Get();
        SetStorageImageBindings(key_name, source ? source->Handle : Textures::TextureHandle{}, &requested_range, 1, array_element, false, &retained);
    }

    void DescriptorBoundPass::SetStorageImageArray(cstring key_name, const Textures::TextureHandle& handle, const VkImageSubresourceRange* ranges, uint32_t range_count)
    {
        SetStorageImageBindings(key_name, handle, ranges, range_count, 0, true, FindTextureSource(handle));
    }

    void DescriptorBoundPass::SetRetainedStorageImageArray(cstring key_name, const RenderResourceManager::TextureVersionPin& retained, const VkImageSubresourceRange* ranges, uint32_t range_count)
    {
        const auto* source = retained.Get();
        SetStorageImageBindings(key_name, source ? source->Handle : Textures::TextureHandle{}, ranges, range_count, 0, true, &retained);
    }

    void DescriptorBoundPass::SetStorageImageBindings(cstring key_name, const Textures::TextureHandle& handle, const VkImageSubresourceRange* ranges, uint32_t range_count, uint32_t first_element, bool complete_array, const RenderResourceManager::TextureVersionPin* retained)
    {
        DescriptorWriteScope attempt{m_descriptor_preparation};
        auto                 validity_output = ValidateInput(key_name, DescriptorReplayKind::StorageImage);
        if (!validity_output.first || !ranges || range_count == 0)
        {
            return;
        }

        const auto& spec = validity_output.second;
        if (spec.DescriptorTypeValue != DescriptorType::STORAGE_IMAGE || first_element >= spec.Count || range_count > spec.Count - first_element || (complete_array && range_count != spec.Count) || range_count > kMaxDescriptorReplayBindings)
        {
            ZENGINE_CORE_ERROR("SetStorageImageArray: invalid storage-image array binding for '{}'", key_name)
            return;
        }

        auto* pipeline = GetPipeline();
        auto* shader   = pipeline ? pipeline->Shader : nullptr;
        if (!shader)
        {
            return;
        }
        const auto set_array = PrepareDescriptorSets(spec.Set);
        if (set_array.empty())
        {
            ZENGINE_CORE_ERROR("SetStorageImageArray: descriptor set {} not found for key '{}'", spec.Set, key_name)
            return;
        }

        const uint32_t frame_index = DescriptorFrameIndex();
        if (frame_index >= set_array.size() || frame_index >= PassDescriptorBank::MaxFrames)
        {
            ZENGINE_CORE_ERROR("SetStorageImageArray: descriptor frame {} not found for key '{}'", frame_index, key_name)
            return;
        }

        auto* tex_buf = m_device->GlobalTextures.Access(handle);
        auto* img_buf = tex_buf ? m_device->ImageBufferManager.Access(tex_buf->BufferHandle) : nullptr;
        if (!img_buf)
        {
            ZENGINE_CORE_ERROR("SetStorageImageArray: texture for key '{}' is not resident", key_name)
            return;
        }

        VkDescriptorImageInfo                    image_infos[kMaxDescriptorReplayBindings]      = {};
        VkImageSubresourceRange                  image_ranges[kMaxDescriptorReplayBindings]     = {};
        RenderResourceManager::TextureVersionPin pins[kMaxDescriptorReplayBindings]             = {};
        uint32_t                                 retained_indices[kMaxDescriptorReplayBindings] = {};
        for (uint32_t index = 0; index < range_count; ++index)
        {
            if (!ResolveStorageImageInfo(img_buf, ranges[index], &image_infos[index], &image_ranges[index]))
            {
                ZENGINE_CORE_ERROR("SetStorageImageArray: unable to create storage view for key '{}'", key_name)
                return;
            }
            if (!PrepareImageRetention(spec, handle, frame_index, first_element + index, retained, {retained_indices, index}, &retained_indices[index], &pins[index]))
            {
                return;
            }
        }

        const VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = set_array[frame_index],
            .dstBinding      = spec.Binding,
            .dstArrayElement = first_element,
            .descriptorCount = range_count,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo      = image_infos,
        };
        vkUpdateDescriptorSets(m_device->LogicalDevice, 1, &write, 0, nullptr);
        BoundBindings.insert(key_name);
        for (uint32_t index = 0; index < range_count; ++index)
        {
            const bool retained_texture = bool(pins[index]);
            if (retained_indices[index] != UINT32_MAX)
            {
                auto& binding        = m_retained_textures[frame_index][retained_indices[index]];
                binding.Pin          = std::move(pins[index]);
                binding.Set          = spec.Set;
                binding.Binding      = spec.Binding;
                binding.ArrayElement = first_element + index;
            }
            RecordDescriptorBinding({.Binding = spec, .Texture = handle, .ImageRange = image_ranges[index], .ArrayElement = first_element + index, .RetainedTexture = retained_texture, .Kind = DescriptorReplayKind::StorageImage});
        }
        attempt.Accepted = true;
    }

    void DescriptorBoundPass::SetSampler(cstring key_name, const VkDescriptorImageInfo& sampler_info)
    {
        DescriptorWriteScope attempt{m_descriptor_preparation};
        auto                 validity_output = ValidateInput(key_name, DescriptorReplayKind::Sampler);
        if (!validity_output.first || !sampler_info.sampler)
        {
            return;
        }

        const auto& spec = validity_output.second;
        if (spec.DescriptorTypeValue != DescriptorType::SAMPLER)
        {
            ZENGINE_CORE_ERROR("SetSampler: shader input '{}' is not a sampler", key_name)
            return;
        }

        auto* pipeline = GetPipeline();
        auto* shader   = pipeline ? pipeline->Shader : nullptr;
        if (!shader)
            return;
        const auto set_array = PrepareDescriptorSets(spec.Set);
        if (set_array.empty())
        {
            ZENGINE_CORE_ERROR("SetSampler: descriptor set {} not found for key '{}'", spec.Set, key_name)
            return;
        }
        const uint32_t frame_index = DescriptorFrameIndex();
        if (frame_index >= set_array.size())
        {
            ZENGINE_CORE_ERROR("SetSampler: descriptor frame {} not found for key '{}'", frame_index, key_name)
            return;
        }

        if (IsSharedDescriptorSet(m_device, shader, spec.Set))
        {
            const auto* immutable = m_device->ResolveReservedImmutableSampler(spec.Set, spec.Binding);
            if (!immutable || *immutable != sampler_info.sampler || spec.Count != 1)
            {
                return;
            }
            // These bindings are initialized by layout creation and may be in
            // use by any buffered frame. Even rewriting the same sampler here
            // would violate pending-descriptor lifetime rules.
            BoundBindings.insert(key_name);
            RecordDescriptorBinding({.Binding = spec, .Sampler = sampler_info, .Kind = DescriptorReplayKind::Sampler});
            attempt.Accepted = true;
            return;
        }

        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = set_array[frame_index],
            .dstBinding      = spec.Binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER,
            .pImageInfo      = &sampler_info,
        };
        vkUpdateDescriptorSets(m_device->LogicalDevice, 1, &write, 0, nullptr);
        BoundBindings.insert(key_name);
        RecordDescriptorBinding({.Binding = spec, .Sampler = sampler_info, .Kind = DescriptorReplayKind::Sampler});
        attempt.Accepted = true;
    }

    void DescriptorBoundPass::UseTextureArray(cstring key_name)
    {
        DescriptorWriteScope attempt{m_descriptor_preparation};
        auto                 validity_output = ValidateInput(key_name, DescriptorReplayKind::TextureArray);
        if (!validity_output.first)
            return;

        const auto& binding_spec = validity_output.second;
        ZENGINE_VALIDATE_ASSERT(binding_spec.DescriptorTypeValue == Specifications::DescriptorType::SAMPLED_IMAGE, "UseTextureArray: binding is not a SAMPLED_IMAGE array — use SetSampler() for samplers")

        auto* pipeline = GetPipeline();
        auto* shader   = pipeline ? pipeline->Shader : nullptr;
        if (!shader)
            return;
        const auto set_array = PrepareDescriptorSets(binding_spec.Set);
        if (set_array.empty())
        {
            ZENGINE_CORE_ERROR("UseTextureArray: descriptor set {} not found for key '{}'", binding_spec.Set, key_name)
            return;
        }

        auto frame_count = m_device->SwapchainPtr->BufferredFrameCount;
        if (!frame_count || set_array.size() < frame_count)
        {
            return;
        }
        for (unsigned i = 0; i < frame_count; ++i)
        {
            Hardwares::WriteDescriptorSetRequestKey key = {.Binding = binding_spec.Binding, .DstSet = set_array[i]};
            if (!m_device->AddBindlessTextureSlotRequest(key))
            {
                ZENGINE_CORE_ERROR("UseTextureArray: target is outside the sealed shared descriptor ABI")
                return;
            }
        }

        BoundBindings.insert(key_name);
        RecordDescriptorBinding({.Binding = binding_spec, .Kind = DescriptorReplayKind::TextureArray});
        attempt.Accepted = true;
    }

    void GraphicPass::UpdateRenderTargets()
    {
        RenderTargets.clear();

        uint32_t width  = 0;
        uint32_t height = 0;
        for (const auto& input : Specification.Inputs)
        {
            auto texture = m_device->GlobalTextures.Access(input);

            if (width == 0)
                width = texture->Width;
            else
                ZENGINE_VALIDATE_ASSERT(width == texture->Width, "Render Target Width is invalid for Framebuffer creation")

            if (height == 0)
                height = texture->Height;
            else
                ZENGINE_VALIDATE_ASSERT(height == texture->Height, "Render Target Height is invalid for Framebuffer creation")

            RenderTargets.push(input.Index);
        }

        for (const auto& output : Specification.ExternalOutputs)
        {
            auto texture = m_device->GlobalTextures.Access(output);

            if (width == 0)
                width = texture->Width;
            else
                ZENGINE_VALIDATE_ASSERT(width == texture->Width, "Render Target Width is invalid for Framebuffer creation")

            if (height == 0)
                height = texture->Height;
            else
                ZENGINE_VALIDATE_ASSERT(height == texture->Height, "Render Target Height is invalid for Framebuffer creation")

            RenderTargets.push(output.Index);
        }

        RenderAreaWidth  = width;
        RenderAreaHeight = height;
    }

    struct Attachment* GraphicPass::GetAttachment() const
    {
        return Specification.SwapchainAsRenderTarget ? m_device->SwapchainPtr->SwapchainAttachment : Attachment;
    }

    uint32_t GraphicPass::GetRenderAreaWidth() const
    {
        return Specification.SwapchainAsRenderTarget ? m_device->SwapchainPtr->SwapchainImageWidth : RenderAreaWidth;
    }

    uint32_t GraphicPass::GetRenderAreaHeight() const
    {
        return Specification.SwapchainAsRenderTarget ? m_device->SwapchainPtr->SwapchainImageHeight : RenderAreaHeight;
    }

    std::pair<bool, Specifications::LayoutBindingSpecification> DescriptorBoundPass::ValidateInput(cstring key, DescriptorReplayKind kind)
    {
        bool  valid{true};
        auto* pipeline = GetPipeline();
        auto* shader   = pipeline ? pipeline->Shader : nullptr;
        if (!shader)
            return {false, {}};
        auto binding_spec = shader->GetLayoutBindingSpecification(key);
        if ((binding_spec.Set == 0xFFFFFFFF) && (binding_spec.Binding == 0xFFFFFFFF))
        {
            const auto* pipeline_name = Specification.PipelineDescription.DebugName;
            const auto* shader_name   = shader->m_specification.Name;
            ZENGINE_CORE_ERROR("[{}] Shader input not found: '{}' (shader: {})", pipeline_name ? pipeline_name : "?", key, shader_name ? shader_name : "?")
            valid = false;
        }
        if (valid)
        {
            const auto type       = binding_spec.DescriptorTypeValue;
            bool       compatible = false;
            switch (kind)
            {
                case DescriptorReplayKind::DynamicUniform:
                    compatible = type == DescriptorType::UNIFORM_BUFFER_DYNAMIC;
                    break;
                case DescriptorReplayKind::StorageBuffer:
                    compatible = type == DescriptorType::STORAGE_BUFFER;
                    break;
                case DescriptorReplayKind::Texture:
                    compatible = type == DescriptorType::SAMPLED_IMAGE || type == DescriptorType::COMBINED_IMAGE_SAMPLER;
                    break;
                case DescriptorReplayKind::StorageImage:
                    compatible = type == DescriptorType::STORAGE_IMAGE;
                    break;
                case DescriptorReplayKind::Sampler:
                    compatible = type == DescriptorType::SAMPLER;
                    break;
                case DescriptorReplayKind::TextureArray:
                    compatible = type == DescriptorType::SAMPLED_IMAGE;
                    break;
            }
            if (!compatible || !binding_spec.Count)
            {
                ZENGINE_CORE_ERROR("Shader input '{}' has an incompatible descriptor type/count", key)
                valid = false;
            }
        }
        return {valid, binding_spec};
    }

    bool DescriptorBoundPass::ReplayDescriptorBindings(void* context)
    {
        return static_cast<DescriptorBoundPass*>(context)->ReplayDescriptorBindings();
    }

    bool DescriptorBoundPass::ReplayDescriptorBindings()
    {
        // Replay reconstructs the old interface, not a proof for a new frame.
        // The graph starts a fresh preparation after resolving the pipeline.
        m_descriptor_preparation.Reset();
        auto* pipeline = GetPipeline();
        // Resize may bind graph-declared inputs before the first asynchronous
        // bake finishes. Those partial writes are not a verified old interface.
        // Discard their replay metadata; normal Prepare/Verify must still prove
        // the complete current interface before any commands can be recorded.
        if (pipeline && pipeline->Shader && !m_descriptor_interface_verified)
        {
            BoundBindings.clear();
            ResetDescriptorReplayRecords();
            return true;
        }
        if (!pipeline || !AreDescriptorReplayRecordsCompatible(DescriptorReplayRecords, DescriptorReplayRecordCount, pipeline->Shader))
        {
            ZENGINE_CORE_ERROR("Pipeline '{}': shader descriptor interface changed; pass execution is disabled until the pass is rebuilt", Specification.PipelineDescription.DebugName ? Specification.PipelineDescription.DebugName : "?")
            return false;
        }

        BoundBindings.clear();
        for (uint32_t index = 0; index < DescriptorReplayRecordCount; ++index)
        {
            const DescriptorReplayRecord& record = DescriptorReplayRecords[index];
            switch (record.Kind)
            {
                case DescriptorReplayKind::DynamicUniform:
                    SetDynamicUniform(record.Name, record.Range, record.Buffer);
                    break;
                case DescriptorReplayKind::StorageBuffer:
                    if (record.FrameIndex == UINT32_MAX)
                        SetStorageBuffer(record.Name, record.Buffer);
                    else
                        SetStorageBufferForFrame(record.Name, record.FrameIndex, record.Buffer);
                    break;
                case DescriptorReplayKind::Texture:
                case DescriptorReplayKind::StorageImage:
                    if (record.RetainedTexture)
                    {
                        const auto* frame    = m_device->SwapchainPtr->CurrentFrame;
                        bool        restored = false;
                        if (frame && frame->Index < PassDescriptorBank::MaxFrames)
                        {
                            for (const auto& binding : m_retained_textures[frame->Index])
                            {
                                if (binding.Pin && binding.Set == record.Binding.Set && binding.Binding == record.Binding.Binding && binding.ArrayElement == record.ArrayElement)
                                {
                                    if (record.Kind == DescriptorReplayKind::StorageImage)
                                    {
                                        SetRetainedStorageImage(record.Name, binding.Pin, record.ImageRange, record.ArrayElement);
                                    }
                                    else
                                    {
                                        SetRetainedTexture(record.Name, binding.Pin, record.ImageLayout);
                                    }
                                    restored = true;
                                    break;
                                }
                            }
                        }
                        if (!restored)
                        {
                            return false;
                        }
                    }
                    else
                    {
                        if (record.Kind == DescriptorReplayKind::StorageImage)
                        {
                            SetStorageImage(record.Name, record.Texture, record.ImageRange, record.ArrayElement);
                        }
                        else
                        {
                            SetTexture(record.Name, record.Texture, record.ImageLayout);
                        }
                    }
                    break;
                case DescriptorReplayKind::Sampler:
                    SetSampler(record.Name, record.Sampler);
                    break;
                case DescriptorReplayKind::TextureArray:
                    UseTextureArray(record.Name);
                    break;
            }
        }
        return true;
    }

    /*
     * ComputePass
     */

    ComputePass::~ComputePass()
    {
        Dispose();
    }

    void ComputePass::Initialize(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification)
    {
        Specification = std::move(specification);
        InitializeDescriptorBindings(device);

        Pipeline = ZPushStructCtor<Pipelines::ComputePipeline>(m_device->Arena, std::source_location::current());
        Pipeline->Initialize(m_device, Specification.ComputeShaderName, Specification.ComputePushConstantSize);
        AttachPipelineCallbacks();
    }

    bool ComputePass::TryInitializePrepared(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification, Shaders::Shader* shader)
    {
        if (!shader || !TryInitializeDescriptorBindings(device))
        {
            return false;
        }
        auto* storage = device->Arena->TryResize(nullptr, 0, sizeof(Pipelines::ComputePipeline), alignof(Pipelines::ComputePipeline));
        if (!storage)
        {
            return false;
        }
        Specification = std::move(specification);
        Pipeline      = new (storage) Pipelines::ComputePipeline{};
        Pipeline->InitializePrepared(device, shader, Specification.ComputePushConstantSize);
        AttachPipelineCallbacks();
        return true;
    }

    void ComputePass::Dispose()
    {
        ReleaseDescriptorStorage();
        if (Pipeline)
            Pipeline->Dispose();
    }

    void ComputePass::Bake()
    {
        if (Pipeline)
            Pipeline->Bake();
    }

    bool ComputePass::Verify()
    {
        return Pipeline && Pipeline->EnsureCurrent() && VerifyDescriptorBindings();
    }

    void DescriptorBoundPass::RecordDescriptorBinding(const DescriptorReplayRecord& record)
    {
        m_descriptor_preparation.Record(record.Binding, record.FrameIndex == UINT32_MAX ? DescriptorFrameIndex() : record.FrameIndex, record.ArrayElement, record.Kind == DescriptorReplayKind::TextureArray);
        if (!StoreDescriptorReplayRecord(DescriptorReplayRecords, &DescriptorReplayRecordCount, DescriptorReplayNames, &DescriptorReplayNameBytes, record))
        {
            DescriptorReplayValid = false;
            m_descriptor_preparation.Reject();
        }
    }

} // namespace ZEngine::Rendering::Renderers::RenderPasses
