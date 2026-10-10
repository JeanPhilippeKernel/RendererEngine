#pragma once
#include <ZEngine/Rendering/Specifications/FrameBufferSpecification.h>
#include <vulkan/vulkan.h>
namespace ZEngine::Hardwares
{
    struct VulkanDevice;
} // namespace ZEngine::Hardwares

namespace ZEngine::Rendering::Buffers
{
    struct FramebufferVNext
    {
        FramebufferVNext(Hardwares::VulkanDevice* device, Specifications::FrameBufferSpecificationVNext&&);
        explicit FramebufferVNext(Hardwares::VulkanDevice* device);
        ~FramebufferVNext();

        VkFramebuffer                                        Handle{VK_NULL_HANDLE};

        void                                                 Create();
        void                                                 Resize(uint32_t width = 1, uint32_t height = 1);
        void                                                 Dispose();
        void                                                 Reset(VkFramebuffer handle, uint32_t width, uint32_t height);
        uint32_t                                             GetWidth() const;
        uint32_t                                             GetHeight() const;
        Specifications::FrameBufferSpecificationVNext&       GetSpecification();
        const Specifications::FrameBufferSpecificationVNext& GetSpecification() const;

    private:
        Specifications::FrameBufferSpecificationVNext m_specification{};
        Hardwares::VulkanDevice*                      m_device{nullptr};
    };
} // namespace ZEngine::Rendering::Buffers
