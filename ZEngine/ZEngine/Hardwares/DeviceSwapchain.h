#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Rendering/Primitives/Fence.h>
#include <ZEngine/Rendering/Primitives/Semaphore.h>
#include <ZEngine/Rendering/Renderers/RenderPasses/Attachment.h>
#include <ZEngine/ZEngineDef.h>
#include <vulkan/vulkan.h>
#include <limits>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;

    // None: normal. Pending: recreate at start of next AcquireNextImage (set by
    // SUBOPTIMAL/OOD at present or zero-size surface). FrameAborted: OOD at acquire —
    // semaphore not signalled, no GPU work submitted, BeginFrame returns false.
    enum class RecreationState : uint8_t
    {
        None         = 0,
        Pending      = 1,
        FrameAborted = 2,
    };

    // Called synchronously after recreation so render targets resize in the same frame.
    using SwapchainResizedFn = void (*)(uint32_t width, uint32_t height, void* ctx);

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
        RecreationState                                            Recreation                     = RecreationState::None;
        SwapchainResizedFn                                         OnSwapchainResized             = nullptr;
        void*                                                      OnSwapchainResizedCtx          = nullptr;
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

        // Returns false when the frame was aborted (OUT_OF_DATE at acquire or
        // zero-size surface). Callers must skip all rendering work for that frame.
        bool                                                       IsFrameValid() const
        {
            return Recreation != RecreationState::FrameAborted && CurrentFrame != nullptr && CurrentFrame->ImageIndex != std::numeric_limits<uint32_t>::max();
        }

        void Initialize(VulkanDevice* const device, uint32_t buffered_frame_size);
        void Create();
        void Clear();
        void Dispose();

        void AcquireNextImage(uint32_t frame_context_idx);
        void Present();

#if !defined(NDEBUG)
        // Test-only: inject a recreation state without going through the Vulkan
        // SUBOPTIMAL/OOD detection path. Used by SwapchainResizeTest to exercise
        // the recreation state machine in isolation.
        void ForceRecreation(RecreationState state)
        {
            Recreation = state;
        }
#endif
    };
    ZDEFINE_PTR(DeviceSwapchain);
} // namespace ZEngine::Hardwares
