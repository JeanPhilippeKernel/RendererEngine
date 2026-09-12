#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>

using namespace ZEngine::Rendering::Buffers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Helpers;

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    namespace
    {
        bool IsSameDescriptorBinding(const Specifications::LayoutBindingSpecification& expected, const Specifications::LayoutBindingSpecification& current)
        {
            return expected.Set == current.Set && expected.Binding == current.Binding && expected.Count == current.Count && expected.DescriptorTypeValue == current.DescriptorTypeValue && expected.Flags == current.Flags;
        }

        void StoreDescriptorReplayRecord(Hardwares::VulkanDevice* device, DescriptorReplayRecord* records, uint32_t* record_count, const DescriptorReplayRecord& record)
        {
            ZENGINE_VALIDATE_ASSERT(device != nullptr && records != nullptr && record_count != nullptr, "Descriptor replay record storage is invalid")
            ZENGINE_VALIDATE_ASSERT(record.Binding.Name != nullptr, "Descriptor replay record requires a binding name")

            for (uint32_t index = 0; index < *record_count; ++index)
            {
                DescriptorReplayRecord& existing = records[index];
                if (existing.Kind != record.Kind || existing.FrameIndex != record.FrameIndex || Helpers::secure_strcmp(existing.Name, record.Binding.Name) != 0)
                    continue;

                const cstring stable_name = existing.Name;
                existing                  = record;
                existing.Name             = stable_name;
                existing.Binding.Name     = stable_name;
                return;
            }

            if (*record_count == kMaxDescriptorReplayBindings)
            {
                ZENGINE_CORE_WARN("Descriptor replay record capacity reached; this pass will safely skip after shader interface changes")
                return;
            }

            const size_t name_length = Helpers::secure_strlen(record.Binding.Name);
            if (name_length == 0)
                return;
            char* stable_name = ZPushString(device->Arena, name_length + 1);
            Helpers::secure_strcpy(stable_name, name_length + 1, record.Binding.Name);

            DescriptorReplayRecord& destination = records[(*record_count)++];
            destination                         = record;
            destination.Name                    = stable_name;
            destination.Binding.Name            = stable_name;
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
                    return false;
            }

            for (const auto& binding : shader->LayoutBindingSpecifications)
            {
                if (!HasDescriptorReplayRecord(records, record_count, binding.Name))
                    return false;
            }
            return true;
        }
    } // namespace

    /*
     * GraphicPass
     */

    GraphicPass::~GraphicPass()
    {
        Dispose();
    }

    void GraphicPass::Initialize(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification)
    {
        m_device      = device;
        Specification = std::move(specification);

        RenderTargets.init(m_device->Arena, 4);
        BoundBindings.init(m_device->Arena, 16);

        if (Specification.SwapchainAsRenderTarget)
        {
            Attachment = m_device->SwapchainPtr->SwapchainAttachment;
            Pipeline   = ZPushStructCtorArgs(m_device->Arena, Pipelines::GraphicPipeline);
            Pipeline->Initialize(m_device, std::move(Specification.PipelineDescription), Attachment);
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

            Attachment = ZPushStructCtorArgs(m_device->Arena, RenderPasses::Attachment, m_device, std::move(attachment_specification));
            Pipeline   = ZPushStructCtorArgs(m_device->Arena, Pipelines::GraphicPipeline);
            Pipeline->Initialize(m_device, std::move(Specification.PipelineDescription), Attachment);

            UpdateRenderTargets();
        }

        if (Pipeline)
        {
            Pipeline->ReplayDescriptors       = &GraphicPass::ReplayDescriptorBindings;
            Pipeline->DescriptorReplayContext = this;
        }
    }

    void GraphicPass::Dispose()
    {
        // NOTE: dead code today. If wired up later, route through VulkanDevice::DestroyTexture.
        for (auto& handle : Specification.ExternalOutputs)
        {
            m_device->GlobalTextures.Remove(handle);
        }

        if (Pipeline)
        {
            Pipeline->Dispose();
        }

        if (!Specification.SwapchainAsRenderTarget && Attachment)
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
        bool        verify                       = true;
        const auto& layout_binding_specification = Pipeline->Shader->LayoutBindingSpecifications;

        if (BoundBindings.size() != layout_binding_specification.size())
        {
            uint32_t missing_count = 0;
            for (const auto& specification : layout_binding_specification)
            {
                if (!BoundBindings.contains(specification.Name))
                {
                    ++missing_count;
                    ZENGINE_CORE_WARN("Pipeline '{}': unset input '{}'", Specification.PipelineDescription.DebugName, specification.Name)
                }
            }
            ZENGINE_CORE_WARN("Pipeline '{}': {} unset input(s)", Specification.PipelineDescription.DebugName, missing_count)

            verify = false;
        }

        return verify;
    }

    void GraphicPass::SetDynamicUniform(std::string_view key_name, VkDeviceSize range)
    {
        auto validity_output = ValidateInput(key_name);
        if (!validity_output.first)
            return;

        const auto& spec      = validity_output.second;
        auto        shader    = Pipeline->Shader;
        const auto* set_array = shader->DescriptorSetMap.find(spec.Set);
        if (!set_array)
            set_array = m_device->ShaderReservedDescriptorSetMap.find(spec.Set);
        if (!set_array)
        {
            ZENGINE_CORE_ERROR("SetDynamicUniform: descriptor set {} not found for key '{}'", spec.Set, key_name.data())
            return;
        }

        const uint32_t frame_index = m_device->SwapchainPtr->CurrentFrame->Index;
        if (frame_index >= set_array->size())
        {
            ZENGINE_CORE_ERROR("SetDynamicUniform: descriptor frame {} not found for key '{}'", frame_index, key_name.data())
            return;
        }

        VkDescriptorBufferInfo buffer_info = {.buffer = m_device->FrameHeaps[frame_index].Handle, .offset = 0, .range = range};
        VkWriteDescriptorSet   write       = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = (*set_array)[frame_index],
            .dstBinding      = spec.Binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .pBufferInfo     = &buffer_info,
        };
        vkUpdateDescriptorSets(m_device->LogicalDevice, 1, &write, 0, nullptr);
        BoundBindings.insert(key_name.data());
        RecordDescriptorBinding({.Binding = spec, .Range = range, .Kind = DescriptorReplayKind::DynamicUniform});
    }

    void GraphicPass::SetStorageBuffer(std::string_view key_name, const Core::Memory::BufferView* buffer)
    {
        if (!buffer || !buffer->Handle)
        {
            ZENGINE_CORE_WARN("SetStorageBuffer: null buffer for key '{}'", key_name.data())
            return;
        }

        auto validity_output = ValidateInput(key_name);
        if (!validity_output.first)
            return;

        const auto& spec      = validity_output.second;
        auto        shader    = Pipeline->Shader;
        const auto* set_array = shader->DescriptorSetMap.find(spec.Set);
        if (!set_array)
            set_array = m_device->ShaderReservedDescriptorSetMap.find(spec.Set);
        if (!set_array)
        {
            ZENGINE_CORE_ERROR("SetStorageBuffer: descriptor set {} not found for key '{}'", spec.Set, key_name.data())
            return;
        }

        const uint32_t frame_index = m_device->SwapchainPtr->CurrentFrame->Index;
        if (frame_index >= set_array->size())
        {
            ZENGINE_CORE_ERROR("SetStorageBuffer: descriptor frame {} not found for key '{}'", frame_index, key_name.data())
            return;
        }

        VkDescriptorBufferInfo buffer_info = {.buffer = buffer->Handle, .offset = 0, .range = VK_WHOLE_SIZE};
        VkWriteDescriptorSet   write       = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = (*set_array)[frame_index],
            .dstBinding      = spec.Binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo     = &buffer_info,
        };
        vkUpdateDescriptorSets(m_device->LogicalDevice, 1, &write, 0, nullptr);
        BoundBindings.insert(key_name.data());
        RecordDescriptorBinding({.Binding = spec, .Buffer = buffer, .Kind = DescriptorReplayKind::StorageBuffer});
    }

    void GraphicPass::SetStorageBufferForFrame(cstring key_name, uint32_t frame_index, const Core::Memory::BufferView* buffer)
    {
        if (!buffer || !buffer->Handle)
        {
            ZENGINE_CORE_WARN("SetStorageBufferForFrame: null buffer for key '{}'", key_name)
            return;
        }

        auto validity_output = ValidateInput(key_name);
        if (!validity_output.first)
            return;

        const auto& spec      = validity_output.second;
        auto        shader    = Pipeline->Shader;
        const auto* set_array = shader->DescriptorSetMap.find(spec.Set);
        if (!set_array)
            set_array = m_device->ShaderReservedDescriptorSetMap.find(spec.Set);
        if (!set_array || frame_index >= set_array->size())
        {
            ZENGINE_CORE_ERROR("SetStorageBufferForFrame: descriptor set {} or frame {} not found for key '{}'", spec.Set, frame_index, key_name)
            return;
        }

        VkDescriptorBufferInfo buffer_info = {.buffer = buffer->Handle, .offset = 0, .range = VK_WHOLE_SIZE};
        VkWriteDescriptorSet   write       = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = (*set_array)[frame_index],
            .dstBinding      = spec.Binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo     = &buffer_info,
        };
        vkUpdateDescriptorSets(m_device->LogicalDevice, 1, &write, 0, nullptr);
        BoundBindings.insert(key_name);
        RecordDescriptorBinding({.Binding = spec, .Buffer = buffer, .FrameIndex = frame_index, .Kind = DescriptorReplayKind::StorageBuffer});
    }

    void GraphicPass::SetTexture(std::string_view key_name, const Textures::TextureHandle& handle)
    {
        auto validity_output = ValidateInput(key_name);
        if (!validity_output.first)
            return;

        const auto& spec      = validity_output.second;
        auto        shader    = Pipeline->Shader;
        const auto* set_array = shader->DescriptorSetMap.find(spec.Set);
        if (!set_array)
            set_array = m_device->ShaderReservedDescriptorSetMap.find(spec.Set);
        if (!set_array)
        {
            ZENGINE_CORE_ERROR("SetTexture: descriptor set {} not found for key '{}'", spec.Set, key_name.data())
            return;
        }

        const uint32_t frame_index = m_device->SwapchainPtr->CurrentFrame->Index;
        if (frame_index >= set_array->size())
        {
            ZENGINE_CORE_ERROR("SetTexture: descriptor frame {} not found for key '{}'", frame_index, key_name.data())
            return;
        }

        const VkDescriptorType       vk_type    = Specifications::DescriptorTypeMap[VALUE_FROM_SPEC_MAP(spec.DescriptorTypeValue)];
        auto                         tex_buf    = m_device->GlobalTextures.Access(handle);
        auto                         img_buf    = m_device->ImageBufferManager.Access(tex_buf->BufferHandle);
        const VkDescriptorImageInfo& image_info = img_buf->GetDescriptorImageInfo();
        VkWriteDescriptorSet         write      = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = (*set_array)[frame_index],
            .dstBinding      = spec.Binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk_type,
            .pImageInfo      = &image_info,
        };
        vkUpdateDescriptorSets(m_device->LogicalDevice, 1, &write, 0, nullptr);
        BoundBindings.insert(key_name.data());
        RecordDescriptorBinding({.Binding = spec, .Texture = handle, .Kind = DescriptorReplayKind::Texture});
    }

    void GraphicPass::SetSampler(cstring key_name, const VkDescriptorImageInfo& sampler_info)
    {
        auto validity_output = ValidateInput(key_name);
        if (!validity_output.first)
            return;

        const auto& spec      = validity_output.second;
        auto        shader    = Pipeline->Shader;
        const auto* set_array = shader->DescriptorSetMap.find(spec.Set);
        if (!set_array)
            set_array = m_device->ShaderReservedDescriptorSetMap.find(spec.Set);
        if (!set_array)
        {
            ZENGINE_CORE_ERROR("SetSampler: descriptor set {} not found for key '{}'", spec.Set, key_name)
            return;
        }
        const uint32_t frame_index = m_device->SwapchainPtr->CurrentFrame->Index;
        if (frame_index >= set_array->size())
        {
            ZENGINE_CORE_ERROR("SetSampler: descriptor frame {} not found for key '{}'", frame_index, key_name)
            return;
        }

        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = (*set_array)[frame_index],
            .dstBinding      = spec.Binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER,
            .pImageInfo      = &sampler_info,
        };
        vkUpdateDescriptorSets(m_device->LogicalDevice, 1, &write, 0, nullptr);
        BoundBindings.insert(key_name);
        RecordDescriptorBinding({.Binding = spec, .Sampler = sampler_info, .Kind = DescriptorReplayKind::Sampler});
    }

    void GraphicPass::UseTextureArray(std::string_view key_name)
    {
        auto validity_output = ValidateInput(key_name);
        if (!validity_output.first)
            return;

        const auto& binding_spec = validity_output.second;
        ZENGINE_VALIDATE_ASSERT(binding_spec.DescriptorTypeValue == Specifications::DescriptorType::SAMPLED_IMAGE, "UseTextureArray: binding is not a SAMPLED_IMAGE array — use SetSampler() for samplers")

        auto        shader    = Pipeline->Shader;
        const auto* set_array = shader->DescriptorSetMap.find(binding_spec.Set);
        if (!set_array)
            set_array = m_device->ShaderReservedDescriptorSetMap.find(binding_spec.Set);
        if (!set_array)
        {
            ZENGINE_CORE_ERROR("UseTextureArray: descriptor set {} not found for key '{}'", binding_spec.Set, key_name.data())
            return;
        }

        auto frame_count = m_device->SwapchainPtr->BufferredFrameCount;
        for (unsigned i = 0; i < frame_count; ++i)
        {
            Hardwares::WriteDescriptorSetRequestKey key = {.Binding = binding_spec.Binding, .DstSet = (*set_array)[i]};
            m_device->AddBindlessTextureSlotRequest(key);
        }

        BoundBindings.insert(key_name.data());
        RecordDescriptorBinding({.Binding = binding_spec, .Kind = DescriptorReplayKind::TextureArray});
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

    std::pair<bool, Specifications::LayoutBindingSpecification> GraphicPass::ValidateInput(std::string_view key)
    {
        bool        valid{true};
        const auto& shader       = Pipeline->Shader;
        auto        binding_spec = shader->GetLayoutBindingSpecification(key.data());
        if ((binding_spec.Set == 0xFFFFFFFF) && (binding_spec.Binding == 0xFFFFFFFF))
        {
            const auto* pipeline_name = Specification.PipelineDescription.DebugName;
            const auto* shader_name   = shader->m_specification.Name;
            ZENGINE_CORE_ERROR("[{}] Shader input not found: '{}' (shader: {})", pipeline_name ? pipeline_name : "?", key.data(), shader_name ? shader_name : "?")
            valid = false;
        }
        return {valid, binding_spec};
    }

    bool GraphicPass::ReplayDescriptorBindings(void* context)
    {
        return static_cast<GraphicPass*>(context)->ReplayDescriptorBindings();
    }

    bool GraphicPass::ReplayDescriptorBindings()
    {
        if (!Pipeline || !AreDescriptorReplayRecordsCompatible(DescriptorReplayRecords, DescriptorReplayRecordCount, Pipeline->Shader))
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
                    SetDynamicUniform(record.Name, record.Range);
                    break;
                case DescriptorReplayKind::StorageBuffer:
                    if (record.FrameIndex == UINT32_MAX)
                        SetStorageBuffer(record.Name, record.Buffer);
                    else
                        SetStorageBufferForFrame(record.Name, record.FrameIndex, record.Buffer);
                    break;
                case DescriptorReplayKind::Texture:
                    SetTexture(record.Name, record.Texture);
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
        m_device      = device;
        Specification = std::move(specification);

        Pipeline      = ZPushStructCtorArgs(m_device->Arena, Pipelines::ComputePipeline);
        Pipeline->Initialize(m_device, Specification.ComputeShaderName, Specification.ComputePushConstantSize);
        Pipeline->ReplayDescriptors       = &ComputePass::ReplayDescriptorBindings;
        Pipeline->DescriptorReplayContext = this;
    }

    void ComputePass::Dispose()
    {
        if (Pipeline)
            Pipeline->Dispose();
    }

    void ComputePass::Bake()
    {
        if (Pipeline)
            Pipeline->Bake();
    }

    void ComputePass::SetStorageBuffer(cstring key_name, const Core::Memory::BufferView* buffer)
    {
        if (!Pipeline || !Pipeline->Shader || !buffer || !buffer->Handle)
        {
            ZENGINE_CORE_WARN("ComputePass::SetStorageBuffer: null pipeline or buffer for key '{}'", key_name)
            return;
        }

        const auto binding_spec = Pipeline->Shader->GetLayoutBindingSpecification(key_name);
        if (binding_spec.Set == 0xFFFFFFFF || binding_spec.Binding == 0xFFFFFFFF)
        {
            ZENGINE_CORE_ERROR("ComputePass::SetStorageBuffer: shader input not found: '{}'", key_name)
            return;
        }
        if (binding_spec.DescriptorTypeValue != DescriptorType::STORAGE_BUFFER)
        {
            ZENGINE_CORE_ERROR("ComputePass::SetStorageBuffer: shader input '{}' is not a storage buffer", key_name)
            return;
        }

        const auto* set_array = Pipeline->Shader->DescriptorSetMap.find(binding_spec.Set);
        if (!set_array)
        {
            ZENGINE_CORE_ERROR("ComputePass::SetStorageBuffer: descriptor set {} not found for key '{}'", binding_spec.Set, key_name)
            return;
        }

        const uint32_t frame_index = m_device->SwapchainPtr->CurrentFrame->Index;
        if (frame_index >= set_array->size())
        {
            ZENGINE_CORE_ERROR("ComputePass::SetStorageBuffer: descriptor frame {} not found for key '{}'", frame_index, key_name)
            return;
        }

        VkDescriptorBufferInfo buffer_info = {.buffer = buffer->Handle, .offset = 0, .range = VK_WHOLE_SIZE};
        VkWriteDescriptorSet   write       = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = (*set_array)[frame_index],
            .dstBinding      = binding_spec.Binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo     = &buffer_info,
        };
        vkUpdateDescriptorSets(m_device->LogicalDevice, 1, &write, 0, nullptr);
        RecordDescriptorBinding({.Binding = binding_spec, .Buffer = buffer, .Kind = DescriptorReplayKind::StorageBuffer});
    }

    void ComputePass::SetStorageBufferForFrame(cstring key_name, uint32_t frame_index, const Core::Memory::BufferView* buffer)
    {
        if (!Pipeline || !Pipeline->Shader || !buffer || !buffer->Handle)
        {
            ZENGINE_CORE_WARN("ComputePass::SetStorageBufferForFrame: null pipeline or buffer for key '{}'", key_name)
            return;
        }

        const auto binding_spec = Pipeline->Shader->GetLayoutBindingSpecification(key_name);
        if (binding_spec.Set == 0xFFFFFFFF || binding_spec.Binding == 0xFFFFFFFF || binding_spec.DescriptorTypeValue != DescriptorType::STORAGE_BUFFER)
        {
            ZENGINE_CORE_ERROR("ComputePass::SetStorageBufferForFrame: storage-buffer shader input not found: '{}'", key_name)
            return;
        }

        const auto* set_array = Pipeline->Shader->DescriptorSetMap.find(binding_spec.Set);
        if (!set_array || frame_index >= set_array->size())
        {
            ZENGINE_CORE_ERROR("ComputePass::SetStorageBufferForFrame: descriptor set {} or frame {} not found for key '{}'", binding_spec.Set, frame_index, key_name)
            return;
        }

        VkDescriptorBufferInfo buffer_info = {.buffer = buffer->Handle, .offset = 0, .range = VK_WHOLE_SIZE};
        VkWriteDescriptorSet   write       = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = (*set_array)[frame_index],
            .dstBinding      = binding_spec.Binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo     = &buffer_info,
        };
        vkUpdateDescriptorSets(m_device->LogicalDevice, 1, &write, 0, nullptr);
        RecordDescriptorBinding({.Binding = binding_spec, .Buffer = buffer, .FrameIndex = frame_index, .Kind = DescriptorReplayKind::StorageBuffer});
    }

    bool ComputePass::ReplayDescriptorBindings(void* context)
    {
        return static_cast<ComputePass*>(context)->ReplayDescriptorBindings();
    }

    bool ComputePass::ReplayDescriptorBindings()
    {
        if (!Pipeline || !AreDescriptorReplayRecordsCompatible(DescriptorReplayRecords, DescriptorReplayRecordCount, Pipeline->Shader))
        {
            ZENGINE_CORE_ERROR("Compute pipeline: shader descriptor interface changed; pass execution is disabled until the pass is rebuilt")
            return false;
        }

        for (uint32_t index = 0; index < DescriptorReplayRecordCount; ++index)
        {
            const DescriptorReplayRecord& record = DescriptorReplayRecords[index];
            if (record.Kind != DescriptorReplayKind::StorageBuffer)
                return false;
            if (record.FrameIndex == UINT32_MAX)
                SetStorageBuffer(record.Name, record.Buffer);
            else
                SetStorageBufferForFrame(record.Name, record.FrameIndex, record.Buffer);
        }
        return true;
    }

    void GraphicPass::RecordDescriptorBinding(const DescriptorReplayRecord& record)
    {
        StoreDescriptorReplayRecord(m_device, DescriptorReplayRecords, &DescriptorReplayRecordCount, record);
    }

    void ComputePass::RecordDescriptorBinding(const DescriptorReplayRecord& record)
    {
        StoreDescriptorReplayRecord(m_device, DescriptorReplayRecords, &DescriptorReplayRecordCount, record);
    }

} // namespace ZEngine::Rendering::Renderers::RenderPasses
