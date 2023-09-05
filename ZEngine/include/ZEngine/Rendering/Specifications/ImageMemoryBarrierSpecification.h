#pragma once
#include <vulkan/vulkan.h>
#include <Rendering/Specifications/FormatSpecification.h>

namespace ZEngine::Rendering::Specifications
{
    struct ImageMemoryBarrierSpecification
    {
        ImageLayout             OldLayout;
        ImageLayout             NewLayout;
        VkImage                 ImageHandle;
        VkAccessFlags           SourceAccessMask;
        VkAccessFlags           DestinationAccessMask;
        VkImageAspectFlagBits   ImageAspectMask;
        VkPipelineStageFlagBits SourceStageMask;
        VkPipelineStageFlagBits DestinationStageMask;
    };
}