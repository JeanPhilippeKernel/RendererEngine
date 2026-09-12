#pragma once
#include <ZEngine/Rendering/Specifications/FormatSpecification.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::Rendering::Specifications
{
    struct TextureSpecification
    {
        bool          IsUsageSampled        = true;
        bool          IsUsageStorage        = false;
        bool          IsUsageTransfert      = true;
        bool          IsUsageTransferSource = false;
        /// @brief Allows this image to share an allocation with a non-overlapping image.
        bool          IsAliasable           = false;
        /*Deprecated property - please avoid using it*/
        bool          PerformTransition     = true;
        bool          IsCubemap             = false;
        uint32_t      Width                 = 0;
        uint32_t      Height                = 0;
        uint32_t      BytePerPixel          = 4;
        uint32_t      MipLevelCount         = 1;
        uint32_t      LayerCount            = 1;
        ImageFormat   Format                = ImageFormat::UNDEFINED;
        LoadOperation LoadOp                = LoadOperation::CLEAR;
        float         ClearColor[4]         = {0.0f, 0.0f, 0.0f, 0.0f};
        float         ClearDepth            = 1.0f;
        uint32_t      ClearStencil          = 0;
        /*Deprecated property - please avoid using it*/
        const void*   Data                  = nullptr;
    };

    enum class ImageBufferUsageType
    {
        CUBEMAP = 0,
        SINGLE_2D_IMAGE,
        SINGLE_3D_IMAGE,
        ARRAYOF_2D_IMAGE
    };

    struct ImageBufferSpecification
    {
        uint32_t              Width;
        uint32_t              Height;

        ImageViewType         ImageViewTypeValue = ImageViewType::TYPE_2D;
        ImageBufferUsageType  BufferUsageType;
        VkFormat              ImageFormat;
        VkImageUsageFlags     ImageUsage;
        VkImageAspectFlagBits ImageAspectFlag;
        uint32_t              MipLevelCount        = 1U;
        uint32_t              LayerCount           = 1U;
        ImageCreateFlag       ImageCreateFlagValue = ImageCreateFlag::NONE;
        /// @brief Adds VK_IMAGE_CREATE_ALIAS_BIT when constructing the Vulkan image.
        bool                  IsAliasable          = false;
    };

} // namespace ZEngine::Rendering::Specifications
