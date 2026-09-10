#pragma once
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Specifications
{
    struct MemoryBarrierSpecification
    {
        VkPipelineStageFlags SourceStageMask       = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags DestinationStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        VkAccessFlags        SourceAccessMask      = 0;
        VkAccessFlags        DestinationAccessMask = 0;
    };
} // namespace ZEngine::Rendering::Specifications
