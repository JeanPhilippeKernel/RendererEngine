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

    struct FrameContext
    {
        uint32_t                          Index      = std::numeric_limits<uint32_t>::max();
        uint32_t                          ImageIndex = std::numeric_limits<uint32_t>::max();
        Rendering::Primitives::Semaphore* Acquired   = nullptr;
        Rendering::Primitives::Fence*     Fence      = nullptr;
    };
    ZDEFINE_PTR(FrameContext);

    struct DeviceSwapchain
    {
        Core::Memory::ArenaAllocator                               Arena                          = {};
        VulkanDevice*                                              Device                         = nullptr;
        bool                                                       HasRecreationPending           = false;
        uint32_t                                                   BufferredFrameCount            = 0;
        uint32_t                                                   SwapchainImageCount            = 3;
        uint32_t                                                   PreviousSwapchainImageCount    = 3;
        uint32_t                                                   SwapchainImageCountChangeCount = 0;

        uint32_t                                                   SwapchainImageWidth            = std::numeric_limits<uint32_t>::max();
        uint32_t                                                   SwapchainImageHeight           = std::numeric_limits<uint32_t>::max();
        uint32_t                                                   FrameContextOffset             = 0;
        uint32_t                                                   FrameContextPoolSize           = 0;
        const uint32_t                                             FrameContextPoolSizeFactor     = 4;
        uint64_t                                                   IdleFrameThreshold             = 0;
        PaddedAtomic<uint64_t>                                     IdleFrameCount                 = {.value = 0};
        uint64_t                                                   RenderTimelineNextValue        = 0;
        VkSwapchainKHR                                             SwapchainHandle                = VK_NULL_HANDLE;
        FrameContextPtr                                            CurrentFrame                   = nullptr;
        Rendering::Primitives::Semaphore*                          RenderTimeline                 = nullptr;
        Rendering::Renderers::RenderPasses::Attachment*            SwapchainAttachment            = nullptr;
        Core::Containers::Array<FrameContext>                      FrameContexts                  = {};
        Core::Containers::Array<VkImageView>                       SwapchainImageViews            = {};
        Core::Containers::Array<VkFramebuffer>                     SwapchainFramebuffers          = {};
        Core::Containers::Array<Rendering::Primitives::Fence*>     ImageInFlights                 = {};
        Core::Containers::Array<Rendering::Primitives::Fence*>     PresentCompletes               = {};
        Core::Containers::Array<Rendering::Primitives::Semaphore*> RenderCompletes                = {};

        void                                                       Initialize(VulkanDevice* const device, uint32_t buffered_frame_size);
        void                                                       Create();
        void                                                       Clear();
        void                                                       Dispose();

        void                                                       AcquireNextImage(uint32_t frame_context_idx);
        void                                                       Present();
    };
    ZDEFINE_PTR(DeviceSwapchain);
} // namespace ZEngine::Hardwares
