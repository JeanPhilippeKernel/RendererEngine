#pragma once
#include <Rendering/Specifications/FrameBufferSpecification.h>
#include <vulkan/vulkan.h>
#include <vector>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
} // namespace ZEngine::Hardwares

namespace ZEngine::Rendering::Buffers
{
    struct FramebufferVNext : public Helpers::RefCounted
    {
        FramebufferVNext(Hardwares::VulkanDevice* device, const Specifications::FrameBufferSpecificationVNext&);
        FramebufferVNext(Hardwares::VulkanDevice* device, Specifications::FrameBufferSpecificationVNext&&);
        ~FramebufferVNext();

        VkFramebuffer                                        Handle{VK_NULL_HANDLE};

        void                                                 Create();
        void                                                 Resize(uint32_t width = 1, uint32_t height = 1);
        void                                                 Dispose();
        uint32_t                                             GetWidth() const;
        uint32_t                                             GetHeight() const;
        Specifications::FrameBufferSpecificationVNext&       GetSpecification();
        const Specifications::FrameBufferSpecificationVNext& GetSpecification() const;

    private:
        Specifications::FrameBufferSpecificationVNext m_specification{};
        Hardwares::VulkanDevice*                      m_device{nullptr};
    };
} // namespace ZEngine::Rendering::Buffers
