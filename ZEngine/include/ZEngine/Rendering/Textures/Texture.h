#pragma once
#include <vulkan/vulkan.h>
#include <Hardwares/VulkanDevice.h>

namespace ZEngine::Rendering::Textures
{
    struct Texture : public Helpers::RefCounted
    {
    public:
        Texture()          = default;
        virtual ~Texture() = default;

        virtual Hardwares::BufferImage&       GetBuffer()       = 0;
        virtual const Hardwares::BufferImage& GetBuffer() const = 0;
        virtual void                          Dispose()         = 0;
        virtual const VkDescriptorImageInfo&  GetDescriptorImageInfo()
        {
            const auto& buffer_image = GetBuffer();
            m_descriptor_image_info  = {.sampler = buffer_image.Sampler, .imageView = buffer_image.ViewHandle, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            return m_descriptor_image_info;
        }

        virtual bool operator==(const Texture& right)
        {
            return false;
            // return m_texture_image == right.m_texture_image;
        }

        virtual bool operator!=(const Texture& right)
        {
            return false;
            // return m_texture_image != right.m_texture_image;
        }

        unsigned int GetWidth() const
        {
            return m_width;
        }

        unsigned int GetHeight() const
        {
            return m_height;
        }

    protected:
        unsigned int          m_width{0};
        unsigned int          m_height{0};
        unsigned int          m_byte_per_pixel{0};
        VkDeviceSize          m_buffer_size{0};
        VkDescriptorImageInfo m_descriptor_image_info;
    };

    struct TextureArray : public Helpers::RefCounted
    {
        TextureArray(uint32_t count = 0) : m_texture_array(count), m_count(count) {}

        Ref<Texture>& operator[](uint32_t index)
        {
            assert(index < m_texture_array.size());
            return m_texture_array[index];
        }

        const std::vector<Ref<Texture>>& Data() const
        {
            return m_texture_array;
        }

        std::vector<Ref<Texture>>& Data()
        {
            return m_texture_array;
        }

        void Add(const Ref<Texture>& texture)
        {
            m_texture_array.push_back(texture);
        }

        void Add(Ref<Texture>&& texture)
        {
            m_texture_array.emplace_back(texture);
        }

        size_t Size() const
        {
            return m_texture_array.size();
        }

        void Dispose()
        {
            for (auto& texture : m_texture_array)
            {
                texture->Dispose();
            }
        }

    private:
        uint32_t                  m_count{0};
        std::vector<Ref<Texture>> m_texture_array;
    };

    /*
     * To do : Should be deprecated
     */
    Texture* CreateTexture(const char* path);
    Texture* CreateTexture(unsigned int width, unsigned int height);
    Texture* CreateTexture(unsigned int width, unsigned int height, float r, float g, float b, float a);
} // namespace ZEngine::Rendering::Textures
