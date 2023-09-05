#include <Rendering/Primitives/ImageMemoryBarrier.h>

using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Primitives
{
    ImageMemoryBarrier::ImageMemoryBarrier(const ImageMemoryBarrierSpecification& specification) : m_specification(specification)
    {
        m_handle.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        m_handle.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        m_handle.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        m_handle.subresourceRange.aspectMask     = specification.ImageAspectMask;
        m_handle.subresourceRange.baseMipLevel   = 0;
        m_handle.subresourceRange.baseArrayLayer = 0;
        m_handle.subresourceRange.layerCount     = 1;
        m_handle.subresourceRange.levelCount     = 1;
        m_handle.srcAccessMask                   = 0;
        m_handle.image                           = specification.Image;
        m_handle.oldLayout                       = ImageLayoutMap[static_cast<uint32_t>(specification.OldLayout)];
        m_handle.newLayout                       = ImageLayoutMap[static_cast<uint32_t>(specification.NewLayout)];
        m_handle.srcAccessMask                   = specification.SourceAccessMask;
        m_handle.dstAccessMask                   = specification.DestinationAccessMask;
    }

    const Specifications::ImageMemoryBarrierSpecification& ImageMemoryBarrier::GetSpecification() const
    {
        return m_specification;
    }

    const VkImageMemoryBarrier& ImageMemoryBarrier::GetHandle() const
    {
        return m_handle;
    }
} // namespace ZEngine::Rendering::Primitives