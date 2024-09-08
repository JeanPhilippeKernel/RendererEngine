#pragma once
#include <Rendering/Specifications/ImageMemoryBarrierSpecification.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Primitives
{
    struct ImageMemoryBarrier
    {
    public:
        ImageMemoryBarrier(const Specifications::ImageMemoryBarrierSpecification& specification);

        const Specifications::ImageMemoryBarrierSpecification& GetSpecification() const;
        const VkImageMemoryBarrier&                            GetHandle() const;

    private:
        VkImageMemoryBarrier                            m_handle{};
        Specifications::ImageMemoryBarrierSpecification m_specification;
    };
} // namespace ZEngine::Rendering::Primitives