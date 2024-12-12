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

        void                                                 Create();
        void                                                 Resize(uint32_t width = 1, uint32_t height = 1);
        void                                                 Dispose();
        VkFramebuffer                                        GetHandle() const;
        uint32_t                                             GetWidth() const;
        uint32_t                                             GetHeight() const;
        Specifications::FrameBufferSpecificationVNext&       GetSpecification();
        const Specifications::FrameBufferSpecificationVNext& GetSpecification() const;

    private:
        VkFramebuffer                                 m_handle{VK_NULL_HANDLE};
        Specifications::FrameBufferSpecificationVNext m_specification{};
        Hardwares::VulkanDevice*                      m_device{nullptr};
    };
} // namespace ZEngine::Rendering::Buffers
