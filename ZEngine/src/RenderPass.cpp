#include <pch.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <Hardwares/VulkanDevice.h>
#include <Engine.h>

using namespace ZEngine::Rendering::Buffers;

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    RenderPass::~RenderPass()
    {
        Dispose();
    }

    void RenderPass::Dispose()
    {
        m_pipeline->Dispose();
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

                    for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index)
                    {
                        for (uint32_t index = 0; index < texture_collection.size(); ++index)
                        {
                            const auto& image_info = texture_collection[index]->GetDescriptorImageInfo();
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

    Ref<Textures::Texture> RenderPass::GetOutputColor(uint32_t color_index)
    {
        if (!m_pipeline)
        {
            return nullptr;
        }

        auto        framebuffer      = m_pipeline->GetTargetFrameBuffer();
        const auto& color_attachment = framebuffer->GetColorAttachmentCollection();
        return color_attachment.at(color_index);
    }

    Ref<Textures::Texture> RenderPass::GetOutputDepth()
    {
        if (!m_pipeline)
        {
            return nullptr;
        }

        auto framebuffer = m_pipeline->GetTargetFrameBuffer();
        return framebuffer->GetDepthAttachment();
    }

    void RenderPass::ResizeRenderTarget(uint32_t width, uint32_t height)
    {
        if (m_pipeline)
        {
            auto framebuffer = m_pipeline->GetTargetFrameBuffer();
            return framebuffer->Resize(width, height);
        }
    }

    Ref<RenderPass> RenderPass::Create(const RenderPassSpecification& specification)
    {
        Ref<RenderPass> render_pass = CreateRef<RenderPass>();
        render_pass->m_pipeline     = specification.Pipeline;
        return render_pass;
    }

    std::pair<bool, Specifications::LayoutBindingSpecification> RenderPass::ValidateInput(std::string_view key)
    {
        bool        valid{true};
        const auto& shader       = m_pipeline->GetShader();
        auto        binding_spec = shader->GetLayoutBindingSpecification(key);
        if ((binding_spec.Set == 0xFFFFFFFF) && (binding_spec.Binding == 0xFFFFFFFF))
        {
            ZENGINE_CORE_ERROR("Shader input not found : {0}", key)
            valid = false;
        }
        return {valid, binding_spec};
    }
} // namespace ZEngine::Rendering::Renderers::RenderPasses