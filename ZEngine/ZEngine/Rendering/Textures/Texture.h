#pragma once
#include <Hardwares/VulkanDevice.h>
#include <Helpers/HandleManager.h>
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

    using TextureRef           = Helpers::Ref<Texture>;
    using TextureHandle        = Helpers::Handle<TextureRef>;
    using TextureHandleManager = Helpers::HandleManager<TextureRef>;

    /*
     * To do : Should be deprecated
     */
    Texture* CreateTexture(const char* path);
    Texture* CreateTexture(unsigned int width, unsigned int height);
    Texture* CreateTexture(unsigned int width, unsigned int height, float r, float g, float b, float a);
} // namespace ZEngine::Rendering::Textures

namespace ZEngine::Helpers
{
    template <>
    inline void HandleManager<Helpers::Ref<Rendering::Textures::Texture>>::Dispose()
    {
        for (size_t i = 0; i < m_count; ++i)
        {
            if (m_data[i].Data)
            {
                m_data[i].Data->Dispose();
            }
        }
    }
} // namespace ZEngine::Helpers