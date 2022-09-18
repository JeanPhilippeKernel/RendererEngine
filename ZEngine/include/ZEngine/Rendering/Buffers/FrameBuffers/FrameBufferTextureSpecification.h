#pragma once
#include <Rendering/Buffers/FrameBuffers/FrameBufferTextureFormatEnums.h>

namespace ZEngine::Rendering::Buffers::FrameBuffers
{
    template <typename T>
    struct FrameBufferTextureSpecification
    {
        FrameBufferTextureSpecification() = default;
        FrameBufferTextureSpecification(T format) : TextureFormat(format) {}

        T TextureFormat;
    };

} // namespace ZEngine::Rendering::Buffers::FrameBuffers
