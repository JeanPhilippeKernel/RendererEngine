#include <ZEngine/Rendering/Primitives/MemoryBarrier.h>

namespace ZEngine::Rendering::Primitives
{
    MemoryBarrier::MemoryBarrier(const Specifications::MemoryBarrierSpecification& specification) : m_specification(specification)
    {
        m_handle.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        m_handle.pNext         = nullptr;
        m_handle.srcAccessMask = specification.SourceAccessMask;
        m_handle.dstAccessMask = specification.DestinationAccessMask;
    }

    const Specifications::MemoryBarrierSpecification& MemoryBarrier::GetSpecification() const
    {
        return m_specification;
    }

    const VkMemoryBarrier& MemoryBarrier::GetHandle() const
    {
        return m_handle;
    }
} // namespace ZEngine::Rendering::Primitives
