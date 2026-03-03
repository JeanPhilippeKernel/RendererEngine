#pragma once
#include <Core/Containers/Array.h>
#include <Core/Memory/Allocator.h>
#include <Rendering/Primitives/Fence.h>
#include <Rendering/Primitives/Semaphore.h>
#include <Rendering/Renderers/RenderPasses/Attachment.h>
#include <ZEngineDef.h>
#include <vulkan/vulkan.h>
#include <limits>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
    struct DeviceSwapchain
    {
        Core::Memory::ArenaAllocator                               Arena                             = {};
        VulkanDevice*                                              Device                            = nullptr;
        uint32_t                                                   SwapchainImageCount               = 3;
        uint32_t                                                   PreviousSwapchainImageCount       = 3;
        uint32_t                                                   SwapchainImageCountChangeCount    = 0;
        uint32_t                                                   SwapchainImageIndex               = std::numeric_limits<uint8_t>::max();
        uint32_t                                                   CurrentFrameIndex                 = std::numeric_limits<uint8_t>::max();
        uint32_t                                                   PreviousFrameIndex                = std::numeric_limits<uint8_t>::max();
        uint32_t                                                   SwapchainImageWidth               = std::numeric_limits<uint32_t>::max();
        uint32_t                                                   SwapchainImageHeight              = std::numeric_limits<uint32_t>::max();
        std::atomic_uint                                           IdleFrameCount                    = 0;
        std::atomic_uint                                           IdleFrameThreshold                = SwapchainImageCount * 3 * 3;
        VkSwapchainKHR                                             SwapchainHandle                   = VK_NULL_HANDLE;
        Rendering::Renderers::RenderPasses::Attachment*            SwapchainAttachment               = {};
        Core::Containers::Array<VkImageView>                       SwapchainImageViews               = {};
        Core::Containers::Array<VkFramebuffer>                     SwapchainFramebuffers             = {};
        Core::Containers::Array<Rendering::Primitives::Semaphore*> SwapchainAcquiredSemaphores       = {};
        Core::Containers::Array<Rendering::Primitives::Semaphore*> SwapchainRenderCompleteSemaphores = {};
        Core::Containers::Array<Rendering::Primitives::Fence*>     SwapchainSignalFences             = {};

        void                                                       Initialize(VulkanDevice* const device);
        void                                                       Create();
        void                                                       Clear();
        void                                                       Dispose();

        void                                                       AcquireNextImage();
        void                                                       IncrementFrameImageCount();
        void                                                       AsPresentSource();
    };
    ZDEFINE_PTR(DeviceSwapchain);
} // namespace ZEngine::Hardwares
