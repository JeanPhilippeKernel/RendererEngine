#pragma once
#include <Helpers/HandleManager.h>
#include <Helpers/IntrusivePtr.h>
#include <Specifications/TextureSpecification.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Hardwares
{
    struct Image2DBuffer;
}

namespace ZEngine::Rendering::Textures
{
    struct Texture : public Helpers::RefCounted
    {
        Texture() = default;
        Texture(const Specifications::TextureSpecification& spec, const Helpers::Ref<Hardwares::Image2DBuffer>& buffer);
        Texture(Specifications::TextureSpecification&& spec, Helpers::Ref<Hardwares::Image2DBuffer>&& buffer);
        ~Texture();

        bool                                   IsDepthTexture = false;
        uint32_t                               Width          = 1;
        uint32_t                               Height         = 1;
        uint32_t                               BytePerPixel   = 0;
        VkDeviceSize                           BufferSize     = 0;
        Specifications::TextureSpecification   Specification  = {};
        Helpers::Ref<Hardwares::Image2DBuffer> ImageBuffer;

        void                                   Dispose();
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