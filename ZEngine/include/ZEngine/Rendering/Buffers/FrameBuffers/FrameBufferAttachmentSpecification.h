#pragma once
#include <vector>
#include <initializer_list>

namespace ZEngine::Rendering::Buffers::FrameBuffers
{
    enum class ImageFormat
    {
        None = 0,
        R8G8B8A8_UNORM, // color
        DEPTH_STENCIL
    };

    struct FrameBufferAttachmentSpecificationVNext
    {
        FrameBufferAttachmentSpecificationVNext() = default;
        FrameBufferAttachmentSpecificationVNext(std::initializer_list<ImageFormat> format_collection) : FormatCollection(format_collection) {}
        std::vector<ImageFormat> FormatCollection;
    };
} // namespace ZEngine::Rendering::Buffers::FrameBuffers
