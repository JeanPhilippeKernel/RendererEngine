// #include <pch.h>
// #include <Helpers/FrameBufferTextureHelpers.h>

// void ZEngine::Helpers::CreateFrameBufferTexture2DMultiSampleColorAttachment(unsigned int texture_handle, unsigned int texture_slot_unit, unsigned int texture_internal_format,
//     unsigned int sample, unsigned int texture_width, unsigned int texture_height, unsigned int color_attachment_slot)
// {
// #ifdef _WIN32
//     glBindTextureUnit(texture_slot_unit, texture_handle);
//     glTextureStorage2DMultisample(texture_handle, sample, texture_internal_format, texture_width, texture_height, GL_FALSE);
//     glBindTextureUnit(texture_slot_unit, 0);
// #else
//     glActiveTexture(GL_TEXTURE0 + texture_slot_unit);
//     glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, texture_handle);
//     glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, sample, texture_internal_format, texture_width, texture_height, GL_FALSE);
//     glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
// #endif

//     glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + color_attachment_slot, GL_TEXTURE_2D_MULTISAMPLE, texture_handle, 0);
// }

// void ZEngine::Helpers::CreateFrameBufferTexture2DMultiSampleDepthAttachment(unsigned int texture_handle, unsigned int texture_slot_unit, unsigned int texture_internal_format,
//     unsigned int sample, unsigned int texture_width, unsigned int texture_height, unsigned int depth_attachment)
// {
// #ifdef _WIN32
//     glBindTextureUnit(texture_slot_unit, texture_handle);
//     glTextureStorage2DMultisample(texture_handle, sample, texture_internal_format, texture_width, texture_height, GL_FALSE);
//     glBindTextureUnit(texture_slot_unit, 0);
// #else
//     glActiveTexture(GL_TEXTURE0 + texture_slot_unit);
//     glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, texture_slot_unit);
//     glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, sample, texture_internal_format, texture_width, texture_height, GL_FALSE);
//     glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
// #endif

//     glFramebufferTexture2D(GL_FRAMEBUFFER, depth_attachment, GL_TEXTURE_2D_MULTISAMPLE, texture_handle, 0);
// }

// void ZEngine::Helpers::CreateFrameBufferTexture2DColorAttachment(unsigned int texture_handle, unsigned int texture_slot_unit, unsigned int texture_internal_format,
//     unsigned int texture_format, unsigned int texture_width, unsigned int texture_height, unsigned int color_attachment_slot)
// {
// #ifdef _WIN32
//     glBindTextureUnit(texture_slot_unit, texture_handle);
//     glTextureStorage2D(texture_handle, 1, texture_internal_format, texture_width, texture_height);
//     glTexImage2D(GL_TEXTURE_2D, 0, texture_internal_format, texture_width, texture_height, 0, texture_format, GL_UNSIGNED_BYTE, NULL);
//     // ToDo: We need to abstract this parameteri....
//     glTextureParameteri(texture_handle, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//     glTextureParameteri(texture_handle, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
//     glBindTextureUnit(texture_slot_unit, 0);
// #else
//     glActiveTexture(GL_TEXTURE0 + texture_slot_unit);
//     glBindTexture(GL_TEXTURE_2D, texture_handle);
//     glTexImage2D(GL_TEXTURE_2D, 0, texture_internal_format, texture_width, texture_height, 0, texture_format, GL_UNSIGNED_BYTE, NULL);
//     glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//     glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
//     glBindTexture(GL_TEXTURE_2D, 0);
// #endif

//     glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + color_attachment_slot, GL_TEXTURE_2D, texture_handle, 0);
// }

// void ZEngine::Helpers::CreateFrameBufferTexture2DDepthAttachment(unsigned int texture_handle, unsigned int texture_slot_unit, unsigned int texture_internal_format,
//     unsigned int texture_width, unsigned int texture_height, unsigned int depth_attachment)
// {
// #ifdef _WIN32
//     glBindTextureUnit(texture_slot_unit, texture_handle);
//     glTextureStorage2D(texture_handle, 1, texture_internal_format, texture_width, texture_height);
//     glBindTextureUnit(texture_slot_unit, 0);
// #else
//     glActiveTexture(GL_TEXTURE0 + texture_slot_unit);
//     glBindTexture(GL_TEXTURE_2D, texture_slot_unit);
//     glTexImage2D(GL_TEXTURE_2D, 0, texture_internal_format, texture_width, texture_height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
//     glBindTexture(GL_TEXTURE_2D, 0);
// #endif

//     glFramebufferTexture2D(GL_FRAMEBUFFER, depth_attachment, GL_TEXTURE_2D, texture_handle, 0);
// }
