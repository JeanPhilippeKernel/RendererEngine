#include <pch.h>
#include <Hardwares/VulkanDevice.h>
#include <Texture.h>

namespace ZEngine::Rendering::Textures
{
    Texture::Texture(const Specifications::TextureSpecification& spec, const Helpers::Ref<Hardwares::Image2DBuffer>& buffer) : Specification(spec), ImageBuffer(buffer)
    {
        Width          = spec.Width;
        Height         = spec.Height;
        BytePerPixel   = spec.BytePerPixel;
        BufferSize     = spec.Width * spec.Height * spec.BytePerPixel * spec.LayerCount;
        IsDepthTexture = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE);
    }

    Texture::Texture(Specifications::TextureSpecification&& spec, Helpers::Ref<Hardwares::Image2DBuffer>&& buffer) : Specification(std::move(spec)), ImageBuffer(std::move(buffer))
    {
        Width          = spec.Width;
        Height         = spec.Height;
        BytePerPixel   = spec.BytePerPixel;
        BufferSize     = spec.Width * spec.Height * spec.BytePerPixel * spec.LayerCount;
        IsDepthTexture = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE);
    }

    Texture::~Texture()
    {
        Dispose();
    }

    void Texture::Dispose()
    {
        if (ImageBuffer)
        {
            ImageBuffer->Dispose();
            ImageBuffer = nullptr;
        }
    }
} // namespace ZEngine::Rendering::Textures