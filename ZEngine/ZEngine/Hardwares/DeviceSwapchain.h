#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Rendering/Primitives/Fence.h>
#include <ZEngine/Rendering/Primitives/Semaphore.h>
#include <ZEngine/Rendering/Renderers/Base/Attachment.h>
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
    using SwapchainResizedFn    = void (*)(uint32_t width, uint32_t height, void* ctx);
    /// @brief Called after this frame's graphics command buffers have been submitted.
    /// @param timeline Timeline semaphore signalled by the accepted graphics submission.
    /// @param timeline_value Exact value signalled by that submission.
    using RenderWorkSubmittedFn = void (*)(void* ctx, Rendering::Primitives::Semaphore* timeline, uint64_t timeline_value);

    /// @brief Render-thread callback associated with one pending graphics submission.
    struct RenderWorkSubmissionCallback
    {
        RenderWorkSubmittedFn Function = nullptr;
        void*                 Context  = nullptr;
    };

    struct FrameContext
    {
        uint32_t                          Index      = std::numeric_limits<uint32_t>::max();
        uint32_t                          ImageIndex = std::numeric_limits<uint32_t>::max();
        Rendering::Primitives::Semaphore* Acquired   = nullptr;
        Rendering::Primitives::Fence*     Fence      = nullptr;
    };
    ZDEFINE_PTR(FrameContext);

    struct FrameAsyncOperation
    {
        VkPipelineStageFlags2             StageFlags  = 0;
        uint64_t                          SignalValue = 0;
        Rendering::Primitives::Semaphore* Timeline    = nullptr;
    };

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
        Core::Containers::Array<VkImage>                           SwapchainImages                = {};
        Core::Containers::Array<VkImageView>                       SwapchainImageViews            = {};
        Core::Containers::Array<VkFramebuffer>                     SwapchainFramebuffers          = {};
        Core::Containers::Array<VkImageLayout>                     SwapchainImageLayouts          = {};
        Core::Containers::Array<Rendering::Primitives::Fence*>     ImageInFlights                 = {};
        Core::Containers::Array<Rendering::Primitives::Fence*>     PresentCompletes               = {};
        Core::Containers::Array<Rendering::Primitives::Semaphore*> RenderCompletes                = {};
        // Render-thread-owned snapshot of asynchronous GPU work relevant to the
        // current frame. Both graph batches and Present() consume this list.
        Core::Containers::Array<FrameAsyncOperation>               FrameAsyncOperations           = {};
        // Render-thread-owned callbacks. A callback is delivered only once
        // vkQueueSubmit2 has accepted this frame's graphics command buffers.
        Core::Containers::Array<RenderWorkSubmissionCallback>      RenderWorkSubmittedCallbacks   = {};

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
        void CollectAsyncGPUOperations();
        /// @brief Delivers `fn` after this frame's graphics work has been accepted by Vulkan.
        /// @details The caller owns `context` until delivery or cancellation. Callbacks are
        /// discarded, without invocation, when the frame cannot be submitted.
        void EnqueueRenderWorkSubmittedCallback(RenderWorkSubmittedFn fn, void* context);
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
