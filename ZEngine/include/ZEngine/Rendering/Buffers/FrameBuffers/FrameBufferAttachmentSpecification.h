#pragma once
#include <vector>
#include <Maths/Math.h>
#include <Rendering/Buffers/FrameBuffers/FrameBufferTextureSpecification.h>

namespace ZEngine::Rendering::Buffers::FrameBuffers
{
    struct FrameBufferAttachmentSpecificationVNext
    {
        FrameBufferAttachmentSpecificationVNext() = default;
        FrameBufferAttachmentSpecificationVNext(FrambufferImageSpecification specification) : Specification(specification) {}

        FrambufferImageSpecification Specification;
    };
} // namespace ZEngine::Rendering::Buffers::FrameBuffers
