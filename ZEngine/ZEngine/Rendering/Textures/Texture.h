#pragma once
#include <ZEngine/Helpers/HandleManager.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Rendering/Specifications/TextureSpecification.h>
#include <vulkan/vulkan.h>
#include <limits>

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

        // The existing buffer-to-image copy covers mip zero, all layers and depth.
        // BufferSize includes every mip and is not the minimum copy size.
        [[nodiscard]] bool                      ContainsBaseMip(VkDeviceSize size, VkDeviceSize* base_mip_bytes = nullptr) const noexcept
        {
            // CopyBufferToImage currently records a color-aspect copy. The
            // declared texel size must match the Vulkan format it will copy.
            const auto format = Specification.Format;
            const auto index  = static_cast<uint32_t>(format);
            const bool color  = (format >= Specifications::ImageFormat::R8G8B8A8_UNORM && format <= Specifications::ImageFormat::R32G32B32_SFLOAT) || format == Specifications::ImageFormat::R32_UINT;
            if (!color || BytePerPixel != Specifications::BytePerChannelMap[index])
            {
                return false;
            }
            VkDeviceSize   required     = 1;
            const uint32_t dimensions[] = {Width, Height, Specification.Depth, Specification.LayerCount, BytePerPixel};
            for (const uint32_t dimension : dimensions)
            {
                if (!dimension || required > std::numeric_limits<VkDeviceSize>::max() / dimension)
                {
                    return false;
                }
                required *= dimension;
            }
            if (size < required || size > BufferSize)
            {
                return false;
            }
            if (base_mip_bytes)
            {
                *base_mip_bytes = required;
            }
            return true;
        }

        void Dispose();
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
