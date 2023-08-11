#include <pch.h>
#include <Rendering/Textures/Texture2D.h>
#include <Hardwares/VulkanDevice.h>
#include <Helpers/RendererHelper.h>

#define STB_IMAGE_IMPLEMENTATION
#ifdef __GNUC__
#define STBI_NO_SIMD
#endif
#include <stb/stb_image.h>

namespace ZEngine::Rendering::Textures
{

    Texture* CreateTexture(const char* path)
    {
        return new Texture2D(path);
    }

    Texture* CreateTexture(unsigned int width, unsigned int height)
    {
        return new Texture2D(width, height);
    }
} // namespace ZEngine::Rendering::Textures

namespace ZEngine::Rendering::Textures
{

    Texture2D::Texture2D(const char* path) : Texture(path)
    {
        int width = 0, height = 0, channel = 0;
        stbi_set_flip_vertically_on_load(1);
        stbi_uc* image_data = stbi_load(path, &width, &height, &channel, 0);

        if (image_data != nullptr)
        {
            m_byte_per_pixel = channel;
            m_buffer_size    = width * height * m_byte_per_pixel;

            auto                  device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
            Hardwares::BufferView staging_buffer =
                Hardwares::VulkanDevice::CreateBuffer(m_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            void* memory_region;
            ZENGINE_VALIDATE_ASSERT(vkMapMemory(device, staging_buffer.Memory, 0, m_buffer_size, 0, &memory_region) == VK_SUCCESS, "Failed to map the memory")
            std::memcpy(memory_region, image_data, static_cast<size_t>(m_buffer_size));
            vkUnmapMemory(device, staging_buffer.Memory);

            /* Create VkImage */
            m_image_2d_buffer =
                CreateRef<Buffers::Image2DBuffer>(width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

            /*Transition Image from VK_IMAGE_LAYOUT_UNDEFINED to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL and Copy buffer to image*/
            auto command_buffer = Hardwares::VulkanDevice::BeginInstantCommandBuffer(Rendering::QueueType::GRAPHIC_QUEUE);
            {
                std::array<VkImageMemoryBarrier, 2> memory_barrier = {};
                memory_barrier[0].sType                            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                memory_barrier[0].oldLayout                        = VK_IMAGE_LAYOUT_UNDEFINED;
                memory_barrier[0].newLayout                        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                memory_barrier[0].srcQueueFamilyIndex              = VK_QUEUE_FAMILY_IGNORED;
                memory_barrier[0].dstQueueFamilyIndex              = VK_QUEUE_FAMILY_IGNORED;
                memory_barrier[0].image                            = m_image_2d_buffer->GetHandle();
                memory_barrier[0].subresourceRange.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
                memory_barrier[0].subresourceRange.baseMipLevel    = 0;
                memory_barrier[0].subresourceRange.baseArrayLayer  = 0;
                memory_barrier[0].subresourceRange.layerCount      = 1;
                memory_barrier[0].subresourceRange.levelCount      = 1;
                memory_barrier[0].srcAccessMask                    = 0;
                memory_barrier[0].dstAccessMask                    = VK_ACCESS_TRANSFER_WRITE_BIT;
                VkPipelineStageFlagBits source_stage_mask_0        = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                VkPipelineStageFlagBits destination_stage_mask_0   = VK_PIPELINE_STAGE_TRANSFER_BIT;
                vkCmdPipelineBarrier(command_buffer->GetHandle(), source_stage_mask_0, destination_stage_mask_0, 0, 0, nullptr, 0, nullptr, 1, &memory_barrier[0]);

                memory_barrier[1].sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                memory_barrier[1].oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                memory_barrier[1].newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                memory_barrier[1].srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                memory_barrier[1].dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                memory_barrier[1].image                           = m_image_2d_buffer->GetHandle();
                memory_barrier[1].subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                memory_barrier[1].subresourceRange.baseMipLevel   = 0;
                memory_barrier[1].subresourceRange.baseArrayLayer = 0;
                memory_barrier[1].subresourceRange.layerCount     = 1;
                memory_barrier[1].subresourceRange.levelCount     = 1;
                memory_barrier[1].srcAccessMask                   = 0;
                memory_barrier[1].srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
                memory_barrier[1].dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
                VkPipelineStageFlagBits source_stage_mask_1       = VK_PIPELINE_STAGE_TRANSFER_BIT;
                VkPipelineStageFlagBits destination_stage_mask_1  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                vkCmdPipelineBarrier(command_buffer->GetHandle(), source_stage_mask_1, destination_stage_mask_1, 0, 0, nullptr, 0, nullptr, 1, &memory_barrier[1]);
            }
            Hardwares::VulkanDevice::EndInstantCommandBuffer(command_buffer);

            Hardwares::VulkanDevice::CopyBufferToImage(staging_buffer, m_image_2d_buffer->GetBuffer(), m_width, m_height);

            /* Create Sampler */
            m_texture_sampler = Helpers::CreateTextureSampler();

            /* Cleanup resource */
            Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::BUFFER, staging_buffer.Handle);
            Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::BUFFERMEMORY, staging_buffer.Memory);
        }

        stbi_image_free(image_data);
        m_is_from_file = true;
    }

    Texture2D::Texture2D(unsigned int width, unsigned int height) : Texture(width, height)
    {
        m_byte_per_pixel = 4;
        m_buffer_size    = width * height * m_byte_per_pixel;

        unsigned char data[] = {255, 255, 255, 255, '\0'}; // R:255 G: 255 B: 255 A: 255
        SetData(data);
    }

    void Texture2D::SetData(void* const data)
    {
        m_is_from_file = false;

        auto data_ptr = reinterpret_cast<unsigned char*>(data);
        m_r           = *(data_ptr + 0);
        m_g           = *(data_ptr + 1);
        m_b           = *(data_ptr + 2);
        m_a           = *(data_ptr + 3);
    }

    void Texture2D::SetData(float r, float g, float b, float a)
    {
        unsigned char data[5] = {0, 0, 0, 0, '\0'};
        data[0]               = static_cast<unsigned char>(std::clamp(r, .0f, 255.0f));
        data[1]               = static_cast<unsigned char>(std::clamp(g, .0f, 255.0f));
        data[2]               = static_cast<unsigned char>(std::clamp(b, .0f, 255.0f));
        data[3]               = static_cast<unsigned char>(std::clamp(a, .0f, 255.0f));

        SetData(data);
    }

    Texture2D::~Texture2D()
    {
    }
} // namespace ZEngine::Rendering::Textures
