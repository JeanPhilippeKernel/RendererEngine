#pragma once
namespace ZEngine::Rendering::Buffers::FrameBuffers
{
    enum class FrameBufferColorAttachmentTextureFormat
    {
        // Todo: these values need to be updated by OPENGL correct value
        None = 0,
        RGBA8, // color
        RED_INTEGER
    };

    enum class FrameBufferDepthAttachmentTextureFormat
    {
        None = 0,
        DEPTH32FSTENCIL8 // Depth
    };
} // namespace ZEngine::Rendering::Buffers::FrameBuffers
