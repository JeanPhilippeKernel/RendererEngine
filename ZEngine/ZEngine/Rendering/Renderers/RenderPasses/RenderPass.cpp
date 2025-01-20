#include <pch.h>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <fmt/format.h>

using namespace ZEngine::Rendering::Buffers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Helpers;

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    RenderPass::RenderPass(Hardwares::VulkanDevice* device, const RenderPassSpecification& specification) : m_device(device), Specification(specification)
    {

        if (Specification.SwapchainAsRenderTarget)
        {
            Specification.PipelineSpecification.Attachment = m_device->SwapchainAttachment; // Todo : Can potential Dispose() issue
            Pipeline                                       = CreateRef<Pipelines::GraphicPipeline>(m_device, std::move(Specification.PipelineSpecification));
        }
        else
        {
            Specifications::AttachmentSpecification attachment_specification = {};
            attachment_specification.BindPoint                               = PipelineBindPoint::GRAPHIC;

            uint32_t color_map_index                                         = 0;
            for (const auto& handle : Specification.Inputs)
            {
                const auto& texture                                                 = device->GlobalTextures->Access(handle);

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

            for (const auto& handle : Specification.ExternalOutputs)
            {
                const auto& texture                                                 = device->GlobalTextures->Access(handle);
                auto&       output_spec                                             = texture->Specification;
                bool        is_depth_image_format                                   = (output_spec.Format == ImageFormat::DEPTH_STENCIL_FROM_DEVICE);
                ImageLayout initial_layout                                          = (output_spec.LoadOp == LoadOperation::CLEAR) ? ImageLayout::UNDEFINED : is_depth_image_format ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
                ImageLayout final_layout                                            = is_depth_image_format ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
                ImageLayout reference_layout                                        = is_depth_image_format ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::COLOR_ATTACHMENT_OPTIMAL;

                attachment_specification.ColorsMap[color_map_index]                 = {};
                attachment_specification.ColorsMap[color_map_index].Format          = output_spec.Format;
                attachment_specification.ColorsMap[color_map_index].Load            = output_spec.LoadOp;
                attachment_specification.ColorsMap[color_map_index].Store           = StoreOperation::STORE;
                attachment_specification.ColorsMap[color_map_index].Initial         = initial_layout;
                attachment_specification.ColorsMap[color_map_index].Final           = final_layout;
                attachment_specification.ColorsMap[color_map_index].ReferenceLayout = reference_layout;

                color_map_index++;
            }

            Attachment                                     = CreateRef<RenderPasses::Attachment>(m_device, attachment_specification);
            Specification.PipelineSpecification.Attachment = Attachment; // Todo : Can potential Dispose() issue
            Pipeline                                       = CreateRef<Pipelines::GraphicPipeline>(m_device, std::move(Specification.PipelineSpecification));

            UpdateRenderTargets();
            UpdateInputBinding();
        }
    }

    RenderPass::~RenderPass()
    {
        Dispose();
    }

    void RenderPass::Dispose()
    {
        for (auto& handle : Specification.ExternalOutputs)
        {
            m_device->GlobalTextures->Remove(handle);
        }

        Pipeline->Dispose();

        if (!(Specification.SwapchainAsRenderTarget) && Attachment)
        {
            Attachment->Dispose();
        }
    }

    void RenderPass::Bake()
    {
        Pipeline->Bake();
    }

    bool RenderPass::Verify()
    {
        const auto& m_layout_bindings = Pipeline->GetShader()->GetLayoutBindingSpecificationCollection();

        if (Inputs.size() != m_layout_bindings.size())
        {
            std::vector<std::string> missing_names;
            for (const auto& binding : m_layout_bindings)
            {
                if (!Inputs.count(binding.Name))
                {
                    missing_names.emplace_back(binding.Name);
                }
            }
            auto        start        = missing_names.begin();
            auto        end          = missing_names.end();
            std::string unset_inputs = std::accumulate(std::next(start), end, *start, [](std::string_view a, std::string_view b) { return fmt::format("{}, {}", a, b); });

            ZENGINE_CORE_WARN("Shader '{}': {} unset input(s): {}", Specification.PipelineSpecification.DebugName, missing_names.size(), unset_inputs);

            return false;
        }
        return true;
    }

    void RenderPass::SetInput(std::string_view key_name, const Hardwares::UniformBufferSetHandle& handle)
    {
        auto validity_output = ValidateInput(key_name);
        if (!validity_output.first)
        {
            return;
        }

        const auto& spec               = validity_output.second;
        auto        shader             = Pipeline->GetShader();
        auto        descriptor_set_map = shader->GetDescriptorSetMap();
        auto        frame_count        = m_device->SwapchainImageCount;
        auto&       ubo_buf            = m_device->UniformBufferSetManager.Access(handle);
        auto        write_reqs         = std::vector<VkWriteDescriptorSet>(frame_count);

        for (unsigned i = 0; i < frame_count; ++i)
        {
            auto  set      = descriptor_set_map.at(spec.Set)[i];
            auto& buf      = ubo_buf->At(i);
            auto& buf_info = buf.GetDescriptorBufferInfo();

            ZENGINE_VALIDATE_ASSERT((buf_info.buffer), "UniformBuffer can't be null")

            write_reqs[i] = VkWriteDescriptorSet{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext = nullptr, .dstSet = set, .dstBinding = spec.Binding, .dstArrayElement = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pImageInfo = nullptr, .pBufferInfo = &(buf_info), .pTexelBufferView = nullptr};
        }

        vkUpdateDescriptorSets(m_device->LogicalDevice, write_reqs.size(), write_reqs.data(), 0, nullptr);

        Inputs.insert(key_name.data());
    }

    void RenderPass::SetInput(std::string_view key_name, const Hardwares::StorageBufferSetHandle& handle)
    {
        auto validity_output = ValidateInput(key_name);
        if (!validity_output.first)
        {
            return;
        }

        const auto& spec               = validity_output.second;
        auto        shader             = Pipeline->GetShader();
        auto        descriptor_set_map = shader->GetDescriptorSetMap();
        auto        frame_count        = m_device->SwapchainImageCount;
        auto&       sbo_buf            = m_device->StorageBufferSetManager.Access(handle);
        auto        write_reqs         = std::vector<VkWriteDescriptorSet>(frame_count);

        for (unsigned i = 0; i < frame_count; ++i)
        {
            auto  set      = descriptor_set_map.at(spec.Set)[i];
            auto& buf      = sbo_buf->At(i);
            auto& buf_info = buf.GetDescriptorBufferInfo();

            ZENGINE_VALIDATE_ASSERT((buf_info.buffer), "StorageBuffer can't be null")

            write_reqs[i] = VkWriteDescriptorSet{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext = nullptr, .dstSet = set, .dstBinding = spec.Binding, .dstArrayElement = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pImageInfo = nullptr, .pBufferInfo = &(buf_info), .pTexelBufferView = nullptr};
        }

        vkUpdateDescriptorSets(m_device->LogicalDevice, write_reqs.size(), write_reqs.data(), 0, nullptr);

        Inputs.insert(key_name.data());
    }

    void RenderPass::SetInput(std::string_view key_name, const Textures::TextureHandle& handle)
    {
        auto validity_output = ValidateInput(key_name);
        if (!validity_output.first)
        {
            return;
        }

        const auto& spec               = validity_output.second;

        auto        shader             = Pipeline->GetShader();
        auto        descriptor_set_map = shader->GetDescriptorSetMap();
        auto        frame_count        = m_device->SwapchainImageCount;
        auto&       tex_buf            = m_device->GlobalTextures->Access(handle);
        auto        write_reqs         = std::vector<VkWriteDescriptorSet>(frame_count);

        for (unsigned i = 0; i < frame_count; ++i)
        {
            auto  set        = descriptor_set_map.at(spec.Set)[i];
            auto& image_info = tex_buf->ImageBuffer->GetDescriptorImageInfo();

            write_reqs[i]    = VkWriteDescriptorSet{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext = nullptr, .dstSet = set, .dstBinding = spec.Binding, .dstArrayElement = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &(image_info), .pBufferInfo = nullptr, .pTexelBufferView = nullptr};
        }
        vkUpdateDescriptorSets(m_device->LogicalDevice, write_reqs.size(), write_reqs.data(), 0, nullptr);

        Inputs.insert(key_name.data());
    }

    void RenderPass::SetBindlessInput(std::string_view key_name)
    {
        auto validity_output = ValidateInput(key_name);
        if (!validity_output.first)
        {
            return;
        }
        const auto& binding_spec       = validity_output.second;

        auto        shader             = Pipeline->GetShader();
        auto        descriptor_set_map = shader->GetDescriptorSetMap();
        auto        frame_count        = m_device->SwapchainImageCount;

        for (unsigned i = 0; i < frame_count; ++i)
        {
            auto                                    set  = descriptor_set_map.at(binding_spec.Set)[i];
            Hardwares::WriteDescriptorSetRequestKey key  = {.Binding = binding_spec.Binding, .DstSet = set};
            auto&                                   reqs = m_device->WriteBindlessDescriptorSetRequests;
            reqs.insert(key);
        }

        Inputs.insert(key_name.data());
    }

    void RenderPass::UpdateInputBinding()
    {
        for (auto& [binding_name, texture] : Specification.InputTextures)
        {
            SetInput(binding_name, texture);
        }
    }

    void RenderPass::UpdateRenderTargets()
    {
        RenderTargets.clear();

        uint32_t width  = 0;
        uint32_t height = 0;
        for (const auto& input : Specification.Inputs)
        {
            auto texture = m_device->GlobalTextures->Access(input);

            if (width == 0)
            {
                width = texture->Width;
            }
            else
            {
                ZENGINE_VALIDATE_ASSERT(width == texture->Width, "Render Target Width is invalid for Framebuffer creation")
            }

            if (height == 0)
            {
                height = texture->Height;
            }
            else
            {
                ZENGINE_VALIDATE_ASSERT(height == texture->Height, "Render Target Height is invalid for Framebuffer creation")
            }

            RenderTargets.emplace_back(input.Index);
        }

        for (const auto& output : Specification.ExternalOutputs)
        {
            auto texture = m_device->GlobalTextures->Access(output);

            if (width == 0)
            {
                width = texture->Width;
            }
            else
            {
                ZENGINE_VALIDATE_ASSERT(width == texture->Width, "Render Target Width is invalid for Framebuffer creation")
            }

            if (height == 0)
            {
                height = texture->Height;
            }
            else
            {
                ZENGINE_VALIDATE_ASSERT(height == texture->Height, "Render Target Height is invalid for Framebuffer creation")
            }

            RenderTargets.emplace_back(output.Index);
        }

        RenderAreaWidth  = width;
        RenderAreaHeight = height;
    }

    Ref<Renderers::RenderPasses::Attachment> RenderPass::GetAttachment() const
    {
        return Specification.SwapchainAsRenderTarget ? m_device->SwapchainAttachment : Attachment;
    }

    uint32_t RenderPass::GetRenderAreaWidth() const
    {
        return Specification.SwapchainAsRenderTarget ? m_device->SwapchainImageWidth : RenderAreaWidth;
    }

    uint32_t RenderPass::GetRenderAreaHeight() const
    {
        return Specification.SwapchainAsRenderTarget ? m_device->SwapchainImageHeight : RenderAreaHeight;
    }

    std::pair<bool, Specifications::LayoutBindingSpecification> RenderPass::ValidateInput(std::string_view key)
    {
        bool        valid{true};
        const auto& shader       = Pipeline->GetShader();
        auto        binding_spec = shader->GetLayoutBindingSpecification(key);
        if ((binding_spec.Set == 0xFFFFFFFF) && (binding_spec.Binding == 0xFFFFFFFF))
        {
            ZENGINE_CORE_ERROR("Shader input not found : {}", key.data())
            valid = false;
        }
        return {valid, binding_spec};
    }

    /*
     * RenderPassBuilder
     */
    RenderPassBuilder& RenderPassBuilder::SetName(std::string_view name)
    {
        m_spec.DebugName = name.data();
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::SetPipelineName(std::string_view name)
    {
        m_spec.PipelineSpecification.DebugName = name.data();
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::EnablePipelineDepthTest(bool value)
    {
        m_spec.PipelineSpecification.EnableDepthTest = value;
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::EnablePipelineDepthWrite(bool value)
    {
        m_spec.PipelineSpecification.EnableDepthWrite = value;
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::PipelineDepthCompareOp(uint32_t value)
    {
        m_spec.PipelineSpecification.DepthCompareOp = value;
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::EnablePipelineBlending(bool value)
    {
        m_spec.PipelineSpecification.EnableBlending = value;
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::SetShaderOverloadMaxSet(uint32_t count)
    {
        m_spec.PipelineSpecification.ShaderSpecification.OverloadMaxSet = count;
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::SetOverloadPoolSize(uint32_t count)
    {
        m_spec.PipelineSpecification.ShaderSpecification.OverloadPoolSize = count;
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::SetInputBindingCount(uint32_t count)
    {
        m_spec.PipelineSpecification.VertexInputBindingSpecifications.resize(count);
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::SetStride(uint32_t input_binding_index, uint32_t value)
    {
        m_spec.PipelineSpecification.VertexInputBindingSpecifications[input_binding_index].Stride = value;
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::SetRate(uint32_t input_binding_index, uint32_t value)
    {
        m_spec.PipelineSpecification.VertexInputBindingSpecifications[input_binding_index].Rate = value;
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::SetInputAttributeCount(uint32_t count)
    {
        m_spec.PipelineSpecification.VertexInputAttributeSpecifications.resize(count);
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::SetLocation(uint32_t input_attribute_index, uint32_t value)
    {
        m_spec.PipelineSpecification.VertexInputAttributeSpecifications[input_attribute_index].Location = value;
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::SetBinding(uint32_t input_attribute_index, uint32_t input_binding_index)
    {
        m_spec.PipelineSpecification.VertexInputAttributeSpecifications[input_attribute_index].Binding = m_spec.PipelineSpecification.VertexInputBindingSpecifications[input_binding_index].Binding;
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::SetFormat(uint32_t input_attribute_index, Specifications::ImageFormat value)
    {
        m_spec.PipelineSpecification.VertexInputAttributeSpecifications[input_attribute_index].Format = value;
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::SetOffset(uint32_t input_attribute_index, uint32_t offset)
    {
        m_spec.PipelineSpecification.VertexInputAttributeSpecifications[input_attribute_index].Offset = offset;
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::UseShader(std::string_view name)
    {
        m_spec.PipelineSpecification.ShaderSpecification.Name = name.data();
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::UseRenderTarget(const Textures::TextureHandle& target)
    {
        m_spec.ExternalOutputs.push_back(target);
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::AddRenderTarget(const Specifications::TextureSpecification& target_spec)
    {
        m_spec.Outputs.push_back(target_spec);
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::AddInputAttachment(const Textures::TextureHandle& input)
    {
        m_spec.Inputs.push_back(input);
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::AddInputTexture(std::string_view key, const Textures::TextureHandle& input)
    {
        m_spec.InputTextures[key.data()] = input;
        return *this;
    }

    RenderPassBuilder& RenderPassBuilder::UseSwapchainAsRenderTarget()
    {
        m_spec.SwapchainAsRenderTarget = true;
        return *this;
    }

    Specifications::RenderPassSpecification RenderPassBuilder::Detach()
    {
        RenderPassSpecification spec{};
        std::swap(spec, m_spec);
        return spec;
    }
} // namespace ZEngine::Rendering::Renderers::RenderPasses