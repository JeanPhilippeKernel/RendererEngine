#pragma once
#include <Rendering/Buffers/FrameBuffers/FrameBufferTextureFormatEnums.h>

namespace ZEngine::Rendering::Buffers::FrameBuffers
{
    //template <typename T>
    //struct FrameBufferTextureSpecification
    //{
    //    FrameBufferTextureSpecification() = default;
    //    FrameBufferTextureSpecification(T format) : TextureFormat(format) {}

    //    T TextureFormat;
    //};

    struct FrambufferImageSpecification
    {
        uint32_t ImageType;
        uint32_t Format;
        uint32_t Tiling;
        uint32_t InitialLayout;
        uint32_t SampleCount;
    };

} // namespace ZEngine::Rendering::Buffers::FrameBuffers
