#include <pch.h>
#include <Rendering/Textures/Texture2D.h>
#include <Helpers/BufferHelper.h>
#include <Helpers/RendererHelper.h>
#include <Engine.h>

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

            auto&       performant_device = Engine::GetVulkanInstance()->GetHighPerformantDevice();
            const auto& memory_properties = performant_device.GetPhysicalDeviceMemoryProperties();

            m_buffer_size = width * height * m_byte_per_pixel;

            VkBuffer       staging_buffer{VK_NULL_HANDLE};
            VkDeviceMemory staging_buffer_memory{VK_NULL_HANDLE};
            Helpers::CreateBuffer(performant_device, staging_buffer, staging_buffer_memory, m_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, memory_properties,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            void* memory_region;
            ZENGINE_VALIDATE_ASSERT(
                vkMapMemory(performant_device.GetNativeDeviceHandle(), staging_buffer_memory, 0, m_buffer_size, 0, &memory_region) == VK_SUCCESS, "Failed to map the memory")
            std::memcpy(memory_region, image_data, static_cast<size_t>(m_buffer_size));
            vkUnmapMemory(performant_device.GetNativeDeviceHandle(), staging_buffer_memory);


            /* Create VkImage */
            m_texture_image = Helpers::CreateImage(performant_device, width, height, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SHARING_MODE_EXCLUSIVE, VK_SAMPLE_COUNT_1_BIT, m_texture_memory, memory_properties,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            /*Transition Image to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL and Copy buffer to image*/
            Helpers::TransitionImageLayout(performant_device, m_texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            Helpers::CopyBufferToImage(performant_device, staging_buffer, m_texture_image, width, height);

            /* Transition to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL */
            Helpers::TransitionImageLayout(
                performant_device, m_texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            /* Create ImageView */
            m_texture_image_view = Helpers::CreateImageView(performant_device, m_texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

            /* Create Sampler */
            m_texture_sampler = Helpers::CreateTextureSampler(performant_device);

            /* Cleanup resource */
            vkDestroyBuffer(performant_device.GetNativeDeviceHandle(), staging_buffer, nullptr);
            vkFreeMemory(performant_device.GetNativeDeviceHandle(), staging_buffer_memory, nullptr);
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
        auto&       performant_device = Engine::GetVulkanInstance()->GetHighPerformantDevice();
        const auto& memory_properties = performant_device.GetPhysicalDeviceMemoryProperties();

        VkBuffer       staging_buffer{VK_NULL_HANDLE};
        VkDeviceMemory staging_buffer_memory{VK_NULL_HANDLE};
        Helpers::CreateBuffer(performant_device, staging_buffer, staging_buffer_memory, m_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, memory_properties,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        void* memory_region;
        ZENGINE_VALIDATE_ASSERT(
            vkMapMemory(performant_device.GetNativeDeviceHandle(), staging_buffer_memory, 0, m_buffer_size, 0, &memory_region) == VK_SUCCESS, "Failed to map the memory")
        std::memcpy(memory_region, data, static_cast<size_t>(m_buffer_size));
        vkUnmapMemory(performant_device.GetNativeDeviceHandle(), staging_buffer_memory);

        /* Create VkImage */
        if (m_texture_image)
        {
            vkDestroyImage(performant_device.GetNativeDeviceHandle(), m_texture_image, nullptr);
        }
        if (m_texture_memory)
        {
            vkFreeMemory(performant_device.GetNativeDeviceHandle(), m_texture_memory, nullptr);
        }

        m_texture_image = Helpers::CreateImage(performant_device , m_width, m_height, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SHARING_MODE_EXCLUSIVE, VK_SAMPLE_COUNT_1_BIT, m_texture_memory, memory_properties,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        /*Transition Image to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL and Copy buffer to image*/
        Helpers::TransitionImageLayout(performant_device, m_texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        Helpers::CopyBufferToImage(performant_device, staging_buffer, m_texture_image, m_width, m_height);

        /* Transition to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL */
        Helpers::TransitionImageLayout(performant_device, m_texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        /* Create ImageView */
        m_texture_image_view = Helpers::CreateImageView(performant_device, m_texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

        /* Create Sampler */
        m_texture_sampler = Helpers::CreateTextureSampler(performant_device);

        m_is_from_file = false;

        auto data_ptr = reinterpret_cast<unsigned char*>(data);
        m_r           = *(data_ptr + 0);
        m_g           = *(data_ptr + 1);
        m_b           = *(data_ptr + 2);
        m_a           = *(data_ptr + 3);

        /* Cleanup resource */
        vkDestroyBuffer(performant_device.GetNativeDeviceHandle(), staging_buffer, nullptr);
        vkFreeMemory(performant_device.GetNativeDeviceHandle(), staging_buffer_memory, nullptr);
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
        auto device_handle = Engine::GetVulkanInstance()->GetHighPerformantDevice().GetNativeDeviceHandle();
        vkDestroyImageView(device_handle, m_texture_image_view, nullptr);
        vkDestroyImage(device_handle, m_texture_image, nullptr);
        vkFreeMemory(device_handle, m_texture_memory, nullptr);
    }

    void Texture2D::Bind(int slot) const {}

    void Texture2D::Unbind(int slot) const {}

} // namespace ZEngine::Rendering::Textures
