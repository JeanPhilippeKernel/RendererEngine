#pragma once
#include <ZEngine/Rendering/Specifications/MemoryBarrierSpecification.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Primitives
{
    struct MemoryBarrier
    {
        MemoryBarrier(const Specifications::MemoryBarrierSpecification& specification);

        const Specifications::MemoryBarrierSpecification& GetSpecification() const;
        const VkMemoryBarrier&                            GetHandle() const;

    private:
        VkMemoryBarrier                            m_handle{};
        Specifications::MemoryBarrierSpecification m_specification;
    };
} // namespace ZEngine::Rendering::Primitives
