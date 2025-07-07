#include <pch.h>
#include <Hardwares/VulkanDevice.h>
#include <Texture.h>

namespace ZEngine::Rendering::Textures
{
    void Texture::Initialize(const Specifications::TextureSpecification& spec, Hardwares::Image2DBuffer* const buffer)
    {
        Specification  = spec;
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

    void Texture::Dispose() {}
} // namespace ZEngine::Rendering::Textures