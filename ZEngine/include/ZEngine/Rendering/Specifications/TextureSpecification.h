#pragma once
#include <ZEngineDef.h>
#include <Rendering/Specifications/FormatSpecification.h>

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
    };

} // namespace ZEngine::Rendering::Specifications