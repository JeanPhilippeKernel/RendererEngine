#pragma once
#include <ZEngine/Helpers/HandleManager.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Rendering/Specifications/TextureSpecification.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Hardwares
{
    struct ImageBuffer;
}

namespace ZEngine::Rendering::Textures
{
    struct Texture
    {
        Texture() = default;
        ~Texture();

        bool                                    IsDepthTexture = false;
        uint32_t                                Width          = 1;
        uint32_t                                Height         = 1;
        uint32_t                                BytePerPixel   = 0;
        VkDeviceSize                            BufferSize     = 0;
        Specifications::TextureSpecification    Specification  = {};
        Helpers::Handle<Hardwares::ImageBuffer> BufferHandle   = {};

        void                                    Dispose();
    };

    using TextureHandle        = Helpers::Handle<Texture>;
    using TextureHandleManager = Helpers::HandleManager<Texture>;
} // namespace ZEngine::Rendering::Textures

namespace ZEngine::Helpers
{
    template <>
    inline void HandleManager<Rendering::Textures::Texture>::Dispose()
    {
        for (size_t i = 0; i < m_count; ++i)
        {
            m_memory[i].Dispose();
        }
    }
} // namespace ZEngine::Helpers