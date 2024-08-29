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
        uint32_t              Width;
        uint32_t              Height;
        ImageViewType         ImageViewType;
        VkFormat              ImageFormat;
        VkImageUsageFlags     ImageUsage;
        VkImageAspectFlagBits ImageAspectFlag;
        uint32_t              LayerCount           = 1U;
        ImageCreateFlag       ImageCreateFlag = ImageCreateFlag::SPARSE_BINDING_BIT;
    };

} // namespace ZEngine::Rendering::Specifications