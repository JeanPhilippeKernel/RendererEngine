#pragma once
#include <map>
#include <vulkan/vulkan.h>


namespace ZEngine::Rendering::Specifications
{
    #define VALUE_FROM_SPEC_MAP(x) static_cast<uint32_t>(x)

    enum class ImageFormat : uint32_t
    {
        UNDEFINED = 0,
        R8G8B8A8_UNORM, // color
        R8G8B8A8_SRGB,
        R32G32B32A32_SFLOAT,
        R32G32_SFLOAT,
        DEPTH16_UNORM,
        DEPTH16_UNORM_S8_UINT,
        DEPTH24_UNORM_S8_UINT,
        DEPTH32_SFLOAT_S8_UINT,
        /*Special value to allow fetch from VulkanDevice*/
        FORMAT_FROM_DEVICE,
        DEPTH_STENCIL_FROM_DEVICE
    };

    /*
    * BytePerChannelMap follows ImageFormat enum alignment value
    */
    static uint32_t BytePerChannelMap[] = {0u, 4u, 4u, (4u * sizeof(float))};

    static VkFormat ImageFormatMap[] = {
        VK_FORMAT_UNDEFINED,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_FORMAT_R32G32_SFLOAT,
        VK_FORMAT_D16_UNORM,
        VK_FORMAT_D16_UNORM_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT};

    enum class LoadOperation : uint32_t
    {
        LOAD = 0,
        CLEAR,
        DONT_CARE
    };

    static VkAttachmentLoadOp AttachmentLoadOperationMap[] = {VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_DONT_CARE};

    enum class StoreOperation : uint32_t
    {
        STORE = 0,
        DONT_CARE,
        NONE,
    };

    static VkAttachmentStoreOp AttachmentStoreOperationMap[] = {
        VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_NONE,
    };

    enum class ImageLayout : uint32_t
    {
        UNDEFINED = 0,
        GENERAL,
        COLOR_ATTACHMENT_OPTIMAL,
        DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        SHADER_READ_ONLY_OPTIMAL,
        TRANSFER_SRC_OPTIMAL,
        TRANSFER_DST_OPTIMAL,
        PREINITIALIZED,
        DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL,
        DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,
        DEPTH_ATTACHMENT_OPTIMAL,
        DEPTH_READ_ONLY_OPTIMAL,
        STENCIL_ATTACHMENT_OPTIMAL,
        STENCIL_READ_ONLY_OPTIMAL,
        READ_ONLY_OPTIMAL,
        ATTACHMENT_OPTIMAL,
        PRESENT_SRC
    };

    static VkImageLayout ImageLayoutMap[] = {
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_PREINITIALIZED,
        VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    enum class PipelineBindPoint : uint32_t
    {
        GRAPHIC,
        COMPUTE
    };

} // namespace ZEngine::Rendering::Specifications
