#pragma once
#include <vector>
#include <Maths/Math.h>
#include <Rendering/Buffers/FrameBuffers/FrameBufferTextureSpecification.h>

namespace ZEngine::Rendering::Buffers::FrameBuffers
{
    struct FrameBufferColorAttachmentSpecification
    {
        FrameBufferColorAttachmentSpecification() = default;
        FrameBufferColorAttachmentSpecification(
            FrameBufferTextureSpecification<FrameBufferColorAttachmentTextureFormat> specification, void* const clear_color, unsigned int clear_color_type)
            : ClearColor(clear_color), ClearColorType(clear_color_type), TextureSpecification(specification)
        {
        }

        void*                                                                    ClearColor{nullptr};
        unsigned int                                                             ClearColorType{0};
        FrameBufferTextureSpecification<FrameBufferColorAttachmentTextureFormat> TextureSpecification;
    };

    struct FrameBufferDepthAttachmentSpecification
    {
        FrameBufferDepthAttachmentSpecification() = default;
        FrameBufferDepthAttachmentSpecification(FrameBufferTextureSpecification<FrameBufferDepthAttachmentTextureFormat> specification)
            : DepthAttachmentTextureSpecifications(specification)
        {
        }

        FrameBufferTextureSpecification<FrameBufferDepthAttachmentTextureFormat> DepthAttachmentTextureSpecifications;
    };
} // namespace ZEngine::Rendering::Buffers::FrameBuffers
