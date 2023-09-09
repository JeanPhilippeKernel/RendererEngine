#pragma once
#include <vector>
#include <initializer_list>
#include <Rendering/Specifications/FormatSpecification.h>

namespace ZEngine::Rendering::Specifications
{
    struct FrameBufferAttachmentSpecificationVNext
    {
        FrameBufferAttachmentSpecificationVNext() = default;
        FrameBufferAttachmentSpecificationVNext(std::initializer_list<ImageFormat> format_collection) : FormatCollection(format_collection) {}
        std::vector<ImageFormat> FormatCollection;
    };
} // namespace ZEngine::Rendering::Specifications
