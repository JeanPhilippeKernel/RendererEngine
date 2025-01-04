#pragma once
#include <Helpers/HandleManager.h>
#include <Rendering/Buffers/Image2DBuffer.h>
#include <Rendering/Specifications/TextureSpecification.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Textures
{
    struct Texture : public Helpers::RefCounted
    {
        Texture() = default;
        Texture(const Specifications::TextureSpecification& spec, const Helpers::Ref<Buffers::Image2DBuffer>& buffer) : Specification(spec), ImageBuffer(buffer)
        {
            Width          = spec.Width;
            Height         = spec.Height;
            BytePerPixel   = spec.BytePerPixel;
            BufferSize     = spec.Width * spec.Height * spec.BytePerPixel * spec.LayerCount;
            IsDepthTexture = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE);
        }

        Texture(Specifications::TextureSpecification&& spec, Helpers::Ref<Buffers::Image2DBuffer>&& buffer) : Specification(std::move(spec)), ImageBuffer(std::move(buffer))
        {
            Width          = spec.Width;
            Height         = spec.Height;
            BytePerPixel   = spec.BytePerPixel;
            BufferSize     = spec.Width * spec.Height * spec.BytePerPixel * spec.LayerCount;
            IsDepthTexture = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE);
        }

        bool                                 IsDepthTexture = false;
        uint32_t                             Width          = 1;
        uint32_t                             Height         = 1;
        uint32_t                             BytePerPixel   = 0;
        VkDeviceSize                         BufferSize     = 0;
        Specifications::TextureSpecification Specification  = {};
        Helpers::Ref<Buffers::Image2DBuffer> ImageBuffer    = nullptr;

        ~Texture()
        {
            Dispose();
        }

        void Dispose()
        {
            if (ImageBuffer)
            {
                ImageBuffer->Dispose();
                ImageBuffer = nullptr;
            }
        }
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