#pragma once
#include <vector>

namespace ZEngine::Helpers
{
    void CreateFrameBufferTexture2DMultiSampleColorAttachment(unsigned int texture_handle, unsigned int texture_slot_unit, unsigned int texture_internal_format,
        unsigned int sample, unsigned int texture_width, unsigned int texture_height, unsigned int color_attachment_slot = 0);
    void CreateFrameBufferTexture2DMultiSampleDepthAttachment(unsigned int texture_handle, unsigned int texture_slot_unit, unsigned int texture_internal_format,
        unsigned int sample, unsigned int texture_width, unsigned int texture_height, unsigned int depth_attachment);
    void CreateFrameBufferTexture2DColorAttachment(unsigned int texture_handle, unsigned int texture_slot_unit, unsigned int texture_internal_format, unsigned int texture_format,
        unsigned int texture_width, unsigned int texture_height, unsigned int color_attachment_slot = 0);
    void CreateFrameBufferTexture2DDepthAttachment(unsigned int texture_handle, unsigned int texture_slot_unit, unsigned int texture_internal_format, unsigned int texture_width,
        unsigned int texture_height, unsigned int depth_attachment);
} // namespace ZEngine::Helpers
