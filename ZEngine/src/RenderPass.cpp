#include <pch.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <Hardwares/VulkanDevice.h>
#include <Engine.h>
#include <fmt/format.h>
#include <Rendering/Textures/Texture2D.h>

using namespace ZEngine::Rendering::Buffers;
using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    RenderPass::RenderPass(const RenderPassSpecification& specification) : m_specification(specification)
    {
        if (m_specification.SwapchainAsRenderTarget)
        {
            m_specification.PipelineSpecification.Attachment = Engine::GetWindow()->GetSwapchain()->GetAttachment(); // Todo : Can potential Dispose() issue
            m_pipeline                                       = Pipelines::GraphicPipeline::Create(m_specification.PipelineSpecification);
        }
        else
        {
            Specifications::AttachmentSpecification attachment_specification = {};
            attachment_specification.BindPoint                               = PipelineBindPoint::GRAPHIC;

            uint32_t color_map_index = 0;
            for (const auto& input : m_specification.Inputs)
            {
                bool        is_depth_texture = input->IsDepthTexture();
                ImageLayout initial_layout   = is_depth_texture ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
                ImageLayout final_layout     = is_depth_texture ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
                ImageLayout reference_layout = is_depth_texture ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::COLOR_ATTACHMENT_OPTIMAL;

                const auto& texture_spec                                            = input->GetSpecification();
                attachment_specification.ColorsMap[color_map_index]                 = {};
                attachment_specification.ColorsMap[color_map_index].Format          = texture_spec.Format;
                attachment_specification.ColorsMap[color_map_index].Load            = LoadOperation::LOAD;
                attachment_specification.ColorsMap[color_map_index].Store           = StoreOperation::STORE;
                attachment_specification.ColorsMap[color_map_index].Initial         = initial_layout;
                attachment_specification.ColorsMap[color_map_index].Final           = final_layout;
                attachment_specification.ColorsMap[color_map_index].ReferenceLayout = reference_layout;

                color_map_index++;
            }

            for (const auto& output : m_specification.ExternalOutputs)
            {
                auto&       output_spec           = output->GetSpecification();
                bool        is_depth_image_format = (output_spec.Format == ImageFormat::DEPTH_STENCIL_FROM_DEVICE);
                ImageLayout initial_layout        = (output_spec.LoadOp == LoadOperation::CLEAR) ? ImageLayout::UNDEFINED
                                                    : is_depth_image_format                      ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                                                                 : ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
                ImageLayout final_layout          = is_depth_image_format ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
                ImageLayout reference_layout      = is_depth_image_format ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::COLOR_ATTACHMENT_OPTIMAL;

                attachment_specification.ColorsMap[color_map_index]                 = {};
                attachment_specification.ColorsMap[color_map_index].Format          = output_spec.Format;
                attachment_specification.ColorsMap[color_map_index].Load            = output_spec.LoadOp;
                attachment_specification.ColorsMap[color_map_index].Store           = StoreOperation::STORE;
                attachment_specification.ColorsMap[color_map_index].Initial         = initial_layout;
                attachment_specification.ColorsMap[color_map_index].Final           = final_layout;
                attachment_specification.ColorsMap[color_map_index].ReferenceLayout = reference_layout;

                color_map_index++;
            }

            m_attachment = Attachment::Create(attachment_specification);

            m_specification.PipelineSpecification.Attachment = m_attachment; // Todo : Can potential Dispose() issue
            m_pipeline                                       = Pipelines::GraphicPipeline::Create(m_specification.PipelineSpecification);

            ResizeFramebuffer();
            UpdateInputBinding();
        }
    }

    RenderPass::~RenderPass()
    {
        Dispose();
    }

    void RenderPass::Dispose()
    {
        for (Ref<Textures::Texture>& buffer : m_specification.ExternalOutputs)
        {
            buffer->Dispose();
        }

        m_pipeline->Dispose();

        if (m_attachment)
        {
            m_attachment->Dispose();
        }

        if (m_framebuffer)
        {
            m_framebuffer->Dispose();
        }
    }

    Ref<Pipelines::GraphicPipeline> RenderPass::GetPipeline() const
    {
        return m_pipeline;
    }

    void RenderPass::Bake()
    {
        m_pipeline->Bake();
    }

    bool RenderPass::Verify()
    {
        return false;
    }

    void RenderPass::Update()
    {
        if (!m_perform_update)
        {
            return;
        }

        const uint32_t                    frame_count                     = Engine::GetWindow()->GetSwapchain()->GetImageCount();
        const auto&                       shader                          = m_pipeline->GetShader();
        const auto&                       descriptor_set_map              = shader->GetDescriptorSetMap();
        std::vector<VkWriteDescriptorSet> write_descriptor_set_collection = {};
        for (const auto& input : m_input_collection)
        {
            switch (input.Type)
            {
                case UNIFORM_BUFFER_SET:
                {
                    auto  buffer                    = reinterpret_cast<UniformBufferSet*>(input.Input.Data);
                    auto& uniform_buffer_collection = buffer->Data();

                    uint32_t index{0};
                    for (auto& uniform_buffer : uniform_buffer_collection)
                    {
                        const auto& buffer_info = uniform_buffer.GetDescriptorBufferInfo();

                        if (buffer_info.buffer == nullptr)
                        {
                            // invalid input
                            continue;
                        }
                        write_descriptor_set_collection.emplace_back(VkWriteDescriptorSet{
                            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .pNext            = nullptr,
                            .dstSet           = descriptor_set_map.at(input.Set)[index],
                            .dstBinding       = input.Binding,
                            .dstArrayElement  = 0,
                            .descriptorCount  = 1,
                            .descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                            .pImageInfo       = nullptr,
                            .pBufferInfo      = &(buffer_info),
                            .pTexelBufferView = nullptr});
                        index++;
                    }
                }
                break;
                case STORAGE_BUFFER_SET:
                {
                    auto  buffer                    = reinterpret_cast<StorageBufferSet*>(input.Input.Data);
                    auto& storage_buffer_collection = buffer->Data();

                    uint32_t index{0};
                    for (auto& storage_buffer : storage_buffer_collection)
                    {
                        const auto& buffer_info = storage_buffer.GetDescriptorBufferInfo();

                        if (buffer_info.buffer == nullptr)
                        {
                            // invalid input
                            continue;
                        }

                        write_descriptor_set_collection.emplace_back(VkWriteDescriptorSet{
                            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .pNext            = nullptr,
                            .dstSet           = descriptor_set_map.at(input.Set)[index],
                            .dstBinding       = input.Binding,
                            .dstArrayElement  = 0,
                            .descriptorCount  = 1,
                            .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .pImageInfo       = nullptr,
                            .pBufferInfo      = &(buffer_info),
                            .pTexelBufferView = nullptr});
                        index++;
                    }
                }
                break;
                case TEXTURE_ARRAY:
                {
                    auto  texture_array      = reinterpret_cast<Textures::TextureArray*>(input.Input.Data);
                    auto& texture_collection = texture_array->Data();
                    uint32_t slot_count = texture_array->GetUsedSlotCount();

                    for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index)
                    {
                        for (uint32_t index = 0; index < slot_count; ++index)
                        {
                            auto texture = texture_collection[index];
                            if (!texture)
                            {
                                continue;
                            }

                            const auto& image_info = texture->GetDescriptorImageInfo();
                            write_descriptor_set_collection.emplace_back(VkWriteDescriptorSet{
                                .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .pNext            = nullptr,
                                .dstSet           = descriptor_set_map.at(input.Set)[frame_index],
                                .dstBinding       = input.Binding,
                                .dstArrayElement  = index,
                                .descriptorCount  = 1,
                                .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .pImageInfo       = &(image_info),
                                .pBufferInfo      = nullptr,
                                .pTexelBufferView = nullptr});
                        }
                    }
                }
                break;
                case TEXTURE:
                {
                    auto buffer = reinterpret_cast<Textures::Texture*>(input.Input.Data);
                    for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index)
                    {
                        const auto& image_info = buffer->GetDescriptorImageInfo();
                        write_descriptor_set_collection.emplace_back(VkWriteDescriptorSet{
                            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .pNext            = nullptr,
                            .dstSet           = descriptor_set_map.at(input.Set)[frame_index],
                            .dstBinding       = input.Binding,
                            .dstArrayElement  = 0,
                            .descriptorCount  = 1,
                            .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .pImageInfo       = &(image_info),
                            .pBufferInfo      = nullptr,
                            .pTexelBufferView = nullptr});
                    }
                }
                break;
                case UNIFORM_BUFFER:
                {
                    auto        buffer      = reinterpret_cast<UniformBuffer*>(input.Input.Data);
                    const auto& buffer_info = buffer->GetDescriptorBufferInfo();
                    write_descriptor_set_collection.emplace_back(VkWriteDescriptorSet{
                        .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .pNext            = nullptr,
                        .dstSet           = descriptor_set_map.at(input.Set)[0],
                        .dstBinding       = input.Binding,
                        .dstArrayElement  = 0,
                        .descriptorCount  = 1,
                        .descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        .pImageInfo       = nullptr,
                        .pBufferInfo      = &(buffer_info),
                        .pTexelBufferView = nullptr});
                }
                break;
                case STORAGE_BUFFER:
                {
                    auto        buffer      = reinterpret_cast<StorageBuffer*>(input.Input.Data);
                    const auto& buffer_info = buffer->GetDescriptorBufferInfo();
                    write_descriptor_set_collection.emplace_back(VkWriteDescriptorSet{
                        .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .pNext            = nullptr,
                        .dstSet           = descriptor_set_map.at(input.Set)[0],
                        .dstBinding       = input.Binding,
                        .dstArrayElement  = 0,
                        .descriptorCount  = 1,
                        .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .pImageInfo       = nullptr,
                        .pBufferInfo      = &(buffer_info),
                        .pTexelBufferView = nullptr});
                }
                break;
            }
        }

        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        if (!write_descriptor_set_collection.empty())
        {
            vkUpdateDescriptorSets(device, write_descriptor_set_collection.size(), write_descriptor_set_collection.data(), 0, nullptr);
            m_perform_update = false;
        }
    }

    void RenderPass::MarkDirty()
    {
        m_perform_update = true;
    }

    void RenderPass::SetInput(std::string_view key_name, const Ref<UniformBufferSet>& buffer)
    {
        auto validity_output = ValidateInput(key_name);
        if (validity_output.first)
        {
            const auto& binding_spec = validity_output.second;
            auto        find_it      = std::find_if(std::begin(m_input_collection), std::end(m_input_collection), [&](const auto& input) {
                return (input.Set == binding_spec.Set) && (input.Binding == binding_spec.Binding);
            });

            if (find_it != std::end(m_input_collection))
            {
                find_it->Input.Data = buffer.get();
                return;
            }
            m_input_collection.emplace_back(PassInput{
                .Set = binding_spec.Set, .Binding = binding_spec.Binding, .DebugName = binding_spec.Name, .Type = PassInputType::UNIFORM_BUFFER_SET, .Input = buffer.get()});

            m_perform_update = true;
        }
    }

    void RenderPass::SetInput(std::string_view key_name, const Ref<StorageBufferSet>& buffer)
    {
        auto validity_output = ValidateInput(key_name);
        if (validity_output.first)
        {
            const auto& binding_spec = validity_output.second;
            auto        find_it      = std::find_if(std::begin(m_input_collection), std::end(m_input_collection), [&](const auto& input) {
                return (input.Set == binding_spec.Set) && (input.Binding == binding_spec.Binding);
            });

            if (find_it != std::end(m_input_collection))
            {
                find_it->Input.Data = buffer.get();
                return;
            }
            m_input_collection.emplace_back(PassInput{
                .Set = binding_spec.Set, .Binding = binding_spec.Binding, .DebugName = binding_spec.Name, .Type = PassInputType::STORAGE_BUFFER_SET, .Input = buffer.get()});

            m_perform_update = true;
        }
    }

    void RenderPass::SetInput(std::string_view key_name, const Ref<Textures::TextureArray>& textures)
    {
        auto validity_output = ValidateInput(key_name);
        if (validity_output.first)
        {
            const auto& binding_spec = validity_output.second;
            auto        find_it      = std::find_if(std::begin(m_input_collection), std::end(m_input_collection), [&](const auto& input) {
                return (input.Set == binding_spec.Set) && (input.Binding == binding_spec.Binding);
            });

            if (find_it != std::end(m_input_collection))
            {
                find_it->Input.Data = textures.get();
                return;
            }
            m_input_collection.emplace_back(
                PassInput{.Set = binding_spec.Set, .Binding = binding_spec.Binding, .DebugName = binding_spec.Name, .Type = PassInputType::TEXTURE_ARRAY, .Input = textures.get()});

            m_perform_update = true;
        }
    }

    void RenderPass::SetInput(std::string_view key_name, const Ref<UniformBuffer>& buffer)
    {
        auto validity_output = ValidateInput(key_name);
        if (validity_output.first)
        {
            const auto& binding_spec = validity_output.second;
            auto        find_it      = std::find_if(std::begin(m_input_collection), std::end(m_input_collection), [&](const auto& input) {
                return (input.Set == binding_spec.Set) && (input.Binding == binding_spec.Binding);
            });

            if (find_it != std::end(m_input_collection))
            {
                find_it->Input.Data = buffer.get();
                return;
            }
            m_input_collection.emplace_back(
                PassInput{.Set = binding_spec.Set, .Binding = binding_spec.Binding, .DebugName = binding_spec.Name, .Type = PassInputType::UNIFORM_BUFFER, .Input = buffer.get()});

            m_perform_update = true;
        }
    }

    void RenderPass::SetInput(std::string_view key_name, const Ref<StorageBuffer>& buffer)
    {
        auto validity_output = ValidateInput(key_name);
        if (validity_output.first)
        {
            const auto& binding_spec = validity_output.second;
            auto        find_it      = std::find_if(std::begin(m_input_collection), std::end(m_input_collection), [&](const auto& input) {
                return (input.Set == binding_spec.Set) && (input.Binding == binding_spec.Binding);
            });

            if (find_it != std::end(m_input_collection))
            {
                find_it->Input.Data = buffer.get();
                return;
            }
            m_input_collection.emplace_back(
                PassInput{.Set = binding_spec.Set, .Binding = binding_spec.Binding, .DebugName = binding_spec.Name, .Type = PassInputType::STORAGE_BUFFER, .Input = buffer.get()});

            m_perform_update = true;
        }
    }

    void RenderPass::SetInput(std::string_view key_name, const Ref<Textures::Texture>& buffer)
    {
        auto validity_output = ValidateInput(key_name);
        if (validity_output.first)
        {
            const auto& binding_spec = validity_output.second;
            auto        find_it      = std::find_if(std::begin(m_input_collection), std::end(m_input_collection), [&](const auto& input) {
                return (input.Set == binding_spec.Set) && (input.Binding == binding_spec.Binding);
            });

            if (find_it != std::end(m_input_collection))
            {
                find_it->Input.Data = buffer.get();
                return;
            }
            m_input_collection.emplace_back(
                PassInput{.Set = binding_spec.Set, .Binding = binding_spec.Binding, .DebugName = binding_spec.Name, .Type = PassInputType::TEXTURE, .Input = buffer.get()});

            m_perform_update = true;
        }
    }

    void RenderPass::UpdateInputBinding()
    {
        for (auto& [binding_name, texture] : m_specification.InputTextures)
        {
            SetInput(binding_name, texture);
        }
    }

    Ref<Textures::Texture> RenderPass::GetOutputColor(uint32_t color_index)
    {
        if (m_specification.ExternalOutputs.empty())
        {
            return nullptr;
        }
        return m_specification.ExternalOutputs.at(color_index);
    }

    Ref<Textures::Texture> RenderPass::GetOutputDepth()
    {
        Ref<Textures::Texture> depth = {};

        for (auto& texture : m_specification.ExternalOutputs)
        {
            if (texture->IsDepthTexture())
            {
                depth = texture;
            }
        }
        return depth;
    }

    void RenderPass::ResizeFramebuffer()
    {
        if (m_framebuffer)
        {
            m_framebuffer->Dispose();
        }

        uint32_t                 framebuffer_width             = 0;
        uint32_t                 framebuffer_height            = 0;
        std::vector<VkImageView> render_target_view_collection = {};

        for (const auto& input : m_specification.Inputs)
        {
            auto width = input->GetWidth();
            if (framebuffer_width == 0)
            {
                framebuffer_width = width;
            }
            else
            {
                ZENGINE_VALIDATE_ASSERT(framebuffer_width == width, "Render Target Width is invalid for Framebuffer creation")
            }

            auto height = input->GetHeight();
            if (framebuffer_height == 0)
            {
                framebuffer_height = height;
            }
            else
            {
                ZENGINE_VALIDATE_ASSERT(framebuffer_height == height, "Render Target Height is invalid for Framebuffer creation")
            }

            render_target_view_collection.emplace_back(input->GetBuffer().ViewHandle);
        }

        for (const auto& output : m_specification.ExternalOutputs)
        {
            auto width = output->GetWidth();
            if (framebuffer_width == 0)
            {
                framebuffer_width = width;
            }
            else
            {
                ZENGINE_VALIDATE_ASSERT(framebuffer_width == width, "Render Target Width is invalid for Framebuffer creation")
            }

            auto height = output->GetHeight();
            if (framebuffer_height == 0)
            {
                framebuffer_height = height;
            }
            else
            {
                ZENGINE_VALIDATE_ASSERT(framebuffer_height == height, "Render Target Height is invalid for Framebuffer creation")
            }

            render_target_view_collection.emplace_back(output->GetBuffer().ViewHandle);
        }

        Specifications::FrameBufferSpecificationVNext framebuffer_spec = {};
        framebuffer_spec.Width                                         = framebuffer_width;
        framebuffer_spec.Height                                        = framebuffer_height;
        framebuffer_spec.RenderTargetViews                             = std::move(render_target_view_collection);
        framebuffer_spec.Attachment                                    = m_attachment;
        m_framebuffer                                                  = Buffers::FramebufferVNext::Create(framebuffer_spec);
    }

    const Specifications::RenderPassSpecification& RenderPass::GetSpecification() const
    {
        return m_specification;
    }

    Specifications::RenderPassSpecification& RenderPass::GetSpecification()
    {
        return m_specification;
    }

    Ref<Renderers::RenderPasses::Attachment> RenderPass::GetAttachment() const
    {
        if (m_specification.SwapchainAsRenderTarget)
        {
            return Engine::GetWindow()->GetSwapchain()->GetAttachment();
        }
        else
        {
            return m_attachment;
        }
    }

    VkFramebuffer RenderPass::GetFramebuffer() const
    {
        VkFramebuffer output = VK_NULL_HANDLE;

        if (m_specification.SwapchainAsRenderTarget)
        {
            output = Engine::GetWindow()->GetSwapchain()->GetCurrentFramebuffer();
        }
        else
        {
            ZENGINE_VALIDATE_ASSERT(m_framebuffer, "Framebuffer can't be null")
            output = m_framebuffer->GetHandle();
        }
        return output;
    }

    uint32_t RenderPass::GetRenderAreaWidth() const
    {
        if (m_specification.SwapchainAsRenderTarget)
        {
            return Engine::GetWindow()->GetWidth();
        }
        else
        {
            ZENGINE_VALIDATE_ASSERT(m_framebuffer, "Framebuffer can't be null")
            return m_framebuffer->GetWidth();
        }
    }

    uint32_t RenderPass::GetRenderAreaHeight() const
    {
        if (m_specification.SwapchainAsRenderTarget)
        {
            return Engine::GetWindow()->GetHeight();
        }
        else
        {
            ZENGINE_VALIDATE_ASSERT(m_framebuffer, "Framebuffer can't be null")
            return m_framebuffer->GetHeight();
        }
    }

    Ref<RenderPass> RenderPass::Create(const RenderPassSpecification& specification)
    {
        Ref<RenderPass> render_pass = CreateRef<RenderPass>(specification);
        return render_pass;
    }

    std::pair<bool, Specifications::LayoutBindingSpecification> RenderPass::ValidateInput(std::string_view key)
    {
        bool        valid{true};
        const auto& shader       = m_pipeline->GetShader();
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
        m_spec.PipelineSpecification.VertexInputAttributeSpecifications[input_attribute_index].Binding =
            m_spec.PipelineSpecification.VertexInputBindingSpecifications[input_binding_index].Binding;
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

     RenderPassBuilder& RenderPassBuilder::UseRenderTarget(const Ref<Rendering::Textures::Texture>& target)
     {
        m_spec.ExternalOutputs.push_back(target);
        return *this;
     }

     RenderPassBuilder& RenderPassBuilder::AddRenderTarget(const Specifications::TextureSpecification& target_spec)
     {
        m_spec.Outputs.push_back(target_spec);
        return *this;
     }

     RenderPassBuilder& RenderPassBuilder::AddInputAttachment(const Ref<Rendering::Textures::Texture>& input)
     {
        m_spec.Inputs.push_back(input);
        return *this;
     }

     RenderPassBuilder& RenderPassBuilder::AddInputTexture(std::string_view key, const Ref<Rendering::Textures::Texture>& input)
     {
         m_spec.InputTextures[key.data()] = input;
         return *this;
     }

     RenderPassBuilder& RenderPassBuilder::UseSwapchainAsRenderTarget()
     {
        m_spec.SwapchainAsRenderTarget = true;
        return *this;
     }

     Ref<RenderPass> RenderPassBuilder::Create()
     {
        auto pass = RenderPass::Create(m_spec);
        m_spec    = {};
        return pass;
     }
} // namespace ZEngine::Rendering::Renderers::RenderPasses