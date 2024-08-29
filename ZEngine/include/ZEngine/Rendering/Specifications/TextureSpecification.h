#pragma once
#include <Rendering/Specifications/FormatSpecification.h>
#include <ZEngineDef.h>

namespace ZEngine::Rendering::Specifications
{
    struct TextureSpecification
    {
        bool          IsUsageSampled    = true;
        bool          IsUsageStorage    = false;
        bool          IsUsageTransfert  = true;
        bool          PerformTransition = true;
        bool          IsCubemap         = false;
        uint32_t      Width             = 0;
        uint32_t      Height            = 0;
        uint32_t      BytePerPixel      = 4;
        uint32_t      LayerCount        = 1;
        ImageFormat   Format            = ImageFormat::UNDEFINED;
        LoadOperation LoadOp            = LoadOperation::CLEAR;
        const void*   Data              = nullptr;
    };

    struct Image2DBufferSpecification
    {
        uint32_t              width;
        uint32_t              height;
        ImageType             image_view_type;
        VkFormat              image_format;
        VkImageUsageFlags     image_usage;
        VkImageAspectFlagBits image_aspect_flag;
        uint32_t              layer_count           = 1U;
        VkImageCreateFlags    image_create_flag_bit = 0;

        Image2DBufferSpecification(
            uint32_t              w,
            uint32_t              h,
            ImageType             view_type,
            VkFormat              format,
            VkImageUsageFlags     usage,
            VkImageAspectFlagBits aspect,
            uint32_t              layers       = 1U,
            VkImageCreateFlags    create_flags = 0)
            : width(w), height(h), image_view_type(view_type), image_format(format), image_usage(usage), image_aspect_flag(aspect), layer_count(layers),
              image_create_flag_bit(create_flags)
        {
        }
    };

} // namespace ZEngine::Rendering::Specifications