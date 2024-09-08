#pragma once
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Specifications/TextureSpecification.h>
#include <vulkan/vulkan.h>

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

        virtual const VkDescriptorImageInfo& GetDescriptorImageInfo()
        {
            const auto& buffer_image = GetBuffer();
            m_descriptor_image_info  = {.sampler = buffer_image.Sampler, .imageView = buffer_image.ViewHandle, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            return m_descriptor_image_info;
        }

        virtual const Specifications::TextureSpecification& GetSpecification() const
        {
            return m_specification;
        }

        virtual Specifications::TextureSpecification& GetSpecification()
        {
            return m_specification;
        }

        virtual bool IsDepthTexture() const
        {
            return m_is_depth;
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
        bool                                 m_is_depth{false};
        uint32_t                             m_width{0};
        uint32_t                             m_height{0};
        uint32_t                             m_byte_per_pixel{0};
        VkDeviceSize                         m_buffer_size{0};
        VkDescriptorImageInfo                m_descriptor_image_info{};
        Specifications::TextureSpecification m_specification{};
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

        int Add(const Ref<Texture>& texture)
        {
            int output_index = -1;

            if (m_free_slot_index < m_count)
            {
                output_index                         = m_free_slot_index;
                m_texture_array[m_free_slot_index++] = texture;
            }

            return output_index;
        }

        int Add(Ref<Texture>&& texture)
        {
            int output_index = -1;

            if (m_free_slot_index < m_count)
            {
                output_index                         = m_free_slot_index;
                m_texture_array[m_free_slot_index++] = std::move(texture);
            }

            return output_index;
        }

        size_t Size() const
        {
            return m_count;
        }

        int GetUsedSlotCount() const
        {
            return m_free_slot_index;
        }

        void Dispose()
        {
            for (size_t u = 0; u < m_free_slot_index; ++u)
            {
                m_texture_array[u]->Dispose();
            }
        }

    private:
        uint32_t                  m_count{0};
        uint32_t                  m_free_slot_index{0};
        std::vector<Ref<Texture>> m_texture_array;
    };

    /*
     * To do : Should be deprecated
     */
    Texture* CreateTexture(const char* path);
    Texture* CreateTexture(unsigned int width, unsigned int height);
    Texture* CreateTexture(unsigned int width, unsigned int height, float r, float g, float b, float a);
} // namespace ZEngine::Rendering::Textures
