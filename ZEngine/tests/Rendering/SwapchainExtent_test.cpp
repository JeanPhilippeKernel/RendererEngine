#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Hardwares/DeviceSwapchain.h>
#include <gtest/gtest.h>
#include <limits>
#include <memory>

using namespace ZEngine::Hardwares;

namespace
{
    VkSurfaceCapabilitiesKHR VariableExtentCapabilities()
    {
        VkSurfaceCapabilitiesKHR caps{};
        caps.currentExtent  = {std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()};
        caps.minImageExtent = {64, 64};
        caps.maxImageExtent = {4096, 4096};
        return caps;
    }
} // namespace

TEST(SwapchainExtentTest, WaylandUsesPhysicalFramebufferWithinSurfaceLimits)
{
    const auto caps   = VariableExtentCapabilities();
    auto       extent = DeviceSwapchain::ResolveExtent(caps, {2478, 1514});
    EXPECT_EQ(extent.width, 2478u);
    EXPECT_EQ(extent.height, 1514u);

    extent = DeviceSwapchain::ResolveExtent(caps, {32, 8192});
    EXPECT_EQ(extent.width, 64u);
    EXPECT_EQ(extent.height, 4096u);
}

TEST(SwapchainExtentTest, FixedSurfaceExtentRemainsAuthoritative)
{
    auto caps          = VariableExtentCapabilities();
    caps.currentExtent = {1920, 1080};
    const auto extent  = DeviceSwapchain::ResolveExtent(caps, {2478, 1514});
    EXPECT_EQ(extent.width, 1920u);
    EXPECT_EQ(extent.height, 1080u);
}

TEST(SwapchainExtentTest, ZeroSizeIsDeferredRatherThanClampedToMinimum)
{
    auto caps = VariableExtentCapabilities();
    for (const VkExtent2D framebuffer : {
         VkExtent2D{   0,    0},
         VkExtent2D{   0, 1514},
         VkExtent2D{2478,    0}
    })
    {
        const auto extent = DeviceSwapchain::ResolveExtent(caps, framebuffer);
        EXPECT_EQ(extent.width, 0u);
        EXPECT_EQ(extent.height, 0u);
    }
    caps.currentExtent = {0, 0};
    const auto extent  = DeviceSwapchain::ResolveExtent(caps, {2478, 1514});
    EXPECT_EQ(extent.width, 0u);
    EXPECT_EQ(extent.height, 0u);
}

TEST(SwapchainExtentTest, ResizeSchedulesRecreationWithoutVulkanError)
{
    DeviceSwapchain swapchain{};
    swapchain.FramebufferExtent = {1500, 800};
    swapchain.UpdateFramebufferExtent(2478, 1514);
    EXPECT_EQ(swapchain.Recreation, RecreationState::Pending);
    EXPECT_EQ(swapchain.FramebufferExtent.width, 2478u);
    EXPECT_EQ(swapchain.FramebufferExtent.height, 1514u);

    swapchain.Recreation = RecreationState::None;
    swapchain.UpdateFramebufferExtent(2478, 1514);
    EXPECT_EQ(swapchain.Recreation, RecreationState::None);

    swapchain.UpdateFramebufferExtent(2480, 1514);
    EXPECT_EQ(swapchain.Recreation, RecreationState::Pending);
    swapchain.Recreation = RecreationState::None;
    swapchain.UpdateFramebufferExtent(2480, 1516);
    EXPECT_EQ(swapchain.Recreation, RecreationState::Pending);

    // A DPI-only framebuffer change still requests recreation.
    swapchain.UpdateFramebufferExtent(3098, 1893);
    EXPECT_EQ(swapchain.Recreation, RecreationState::Pending);
}

TEST(SwapchainExtentTest, ZeroSizeSkipsAcquisitionAndRestoreSchedulesRecreation)
{
    DeviceSwapchain swapchain{};
    swapchain.FramebufferExtent = {2478, 1514};
    swapchain.UpdateFramebufferExtent(0, 0);
    // No device or GPU objects are needed: zero size must not touch Vulkan/fences.
    swapchain.AcquireNextImage(0);
    EXPECT_FALSE(swapchain.IsFrameValid());
    EXPECT_EQ(swapchain.Recreation, RecreationState::Pending);

    swapchain.UpdateFramebufferExtent(2478, 1514);
    EXPECT_EQ(swapchain.Recreation, RecreationState::Pending);
    EXPECT_EQ(swapchain.FramebufferExtent.width, 2478u);
    EXPECT_EQ(swapchain.FramebufferExtent.height, 1514u);
}

TEST(SwapchainExtentTest, UnchangedExtentDoesNotClearAnAbortedAcquire)
{
    DeviceSwapchain swapchain{};
    swapchain.FramebufferExtent = {2478, 1514};
    swapchain.Recreation        = RecreationState::FrameAborted;
    swapchain.UpdateFramebufferExtent(2478, 1514);
    EXPECT_EQ(swapchain.Recreation, RecreationState::FrameAborted);
}

TEST(SwapchainExtentTest, InvalidFrameSkipsRecordingAndPresentation)
{
    // Device-owned queues exceed the default 1 MB Windows stack.
    auto                                     device = std::make_unique<VulkanDevice>();
    CommandBufferManager                     command_buffers{};
    DeviceSwapchain                          swapchain{};
    ZEngine::Applications::AppRenderPipeline pipeline{};
    device->CommandBufferMgr = &command_buffers;
    device->SwapchainPtr     = &swapchain;
    swapchain.Device         = device.get();
    pipeline.Device          = device.get();

    // A zero-size framebuffer must not dereference GPU resources, including
    // command pools, the render timeline, or CurrentCmdBuf.
    EXPECT_FALSE(pipeline.BeginFrame());
    command_buffers.EnqueuedCommandBufferIndex = 1;
    pipeline.EndFrame();
    EXPECT_EQ(command_buffers.EnqueuedCommandBufferIndex, 0u);
    EXPECT_EQ(pipeline.CurrentCmdBuf, nullptr);

    // A concrete surface can also report zero size during recreation. In that
    // case CurrentFrame exists, but ImageIndex is invalid and state is Pending.
    FrameContext frame{};
    swapchain.CurrentFrame                     = &frame;
    swapchain.Recreation                       = RecreationState::Pending;
    command_buffers.EnqueuedCommandBufferIndex = 1;
    pipeline.EndFrame();
    EXPECT_EQ(command_buffers.EnqueuedCommandBufferIndex, 0u);
}

TEST(SwapchainExtentTest, AcquisitionWaitIsBounded)
{
    EXPECT_GT(DeviceSwapchain::ImageAcquireTimeoutNs, 0u);
    EXPECT_LE(DeviceSwapchain::ImageAcquireTimeoutNs, 100'000'000u);
}

TEST(SwapchainExtentTest, TimeoutAndNotReadyInvalidateTheOldImageWithoutRecreation)
{
    for (VkResult result : {VK_TIMEOUT, VK_NOT_READY})
    {
        DeviceSwapchain swapchain{};
        FrameContext    frame{};
        frame.ImageIndex                  = 2;
        swapchain.RenderTimelineNextValue = 17;
        EXPECT_FALSE(swapchain.ApplyAcquireResult(frame, result, 0));
        EXPECT_FALSE(swapchain.IsFrameValid());
        EXPECT_EQ(frame.ImageIndex, std::numeric_limits<uint32_t>::max());
        EXPECT_EQ(swapchain.Recreation, RecreationState::None);
        EXPECT_EQ(swapchain.RenderTimelineNextValue, 17u);

        EXPECT_TRUE(swapchain.ApplyAcquireResult(frame, VK_SUCCESS, 1));
        EXPECT_TRUE(swapchain.IsFrameValid());
        EXPECT_EQ(frame.ImageIndex, 1u);
        EXPECT_EQ(swapchain.Recreation, RecreationState::None);
    }
}

TEST(SwapchainExtentTest, SuboptimalAcquisitionHasAnImageAndSchedulesRecreation)
{
    DeviceSwapchain swapchain{};
    FrameContext    frame{};
    EXPECT_TRUE(swapchain.ApplyAcquireResult(frame, VK_SUBOPTIMAL_KHR, 2));
    EXPECT_TRUE(swapchain.IsFrameValid());
    EXPECT_EQ(frame.ImageIndex, 2u);
    EXPECT_EQ(swapchain.Recreation, RecreationState::Pending);
}

TEST(SwapchainExtentTest, FailedAcquisitionNeverExposesAnImage)
{
    for (VkResult result : {VK_ERROR_OUT_OF_DATE_KHR, VK_ERROR_DEVICE_LOST, VK_ERROR_SURFACE_LOST_KHR, VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_OUT_OF_DEVICE_MEMORY})
    {
        DeviceSwapchain swapchain{};
        FrameContext    frame{};
        frame.ImageIndex = 2;
        EXPECT_FALSE(swapchain.ApplyAcquireResult(frame, result, 0));
        EXPECT_FALSE(swapchain.IsFrameValid());
        EXPECT_EQ(frame.ImageIndex, std::numeric_limits<uint32_t>::max());
        EXPECT_EQ(swapchain.Recreation, RecreationState::FrameAborted);
    }
}

TEST(SwapchainExtentTest, SuccessWithAnInvalidImageIndexIsRejected)
{
    DeviceSwapchain swapchain{};
    FrameContext    frame{};
    EXPECT_FALSE(swapchain.ApplyAcquireResult(frame, VK_SUCCESS, swapchain.SwapchainImageCount));
    EXPECT_FALSE(swapchain.IsFrameValid());
}

TEST(SwapchainExtentTest, TimeoutSkipsGpuSubmissionAndCancelsPendingCallbacks)
{
    ZEngine::Core::Memory::MemoryManager memory{};
    memory.Initialize(ZMega(4ULL), {});
    auto                                     device = std::make_unique<VulkanDevice>();
    CommandBufferManager                     command_buffers{};
    DeviceSwapchain                          swapchain{};
    ZEngine::Applications::AppRenderPipeline pipeline{};
    device->CommandBufferMgr = &command_buffers;
    device->SwapchainPtr     = &swapchain;
    swapchain.Device         = device.get();
    pipeline.Device          = device.get();
    swapchain.RenderWorkSubmittedCallbacks.init(&memory.MainArena, 1);
    uint32_t cancelled = 0;
    swapchain.EnqueueRenderWorkSubmittedCallback([](void*, ZEngine::Rendering::Primitives::Semaphore*, uint64_t) { ADD_FAILURE() << "A timeout must not submit GPU work"; }, &cancelled, [](void* context) { ++*static_cast<uint32_t*>(context); });

    FrameContext frame{};
    frame.ImageIndex = 2;
    EXPECT_FALSE(swapchain.ApplyAcquireResult(frame, VK_TIMEOUT, 0));
    command_buffers.EnqueuedCommandBufferIndex = 1;
    pipeline.EndFrame(); // No command buffer, semaphore, fence or GPU is needed.
    EXPECT_EQ(command_buffers.EnqueuedCommandBufferIndex, 0u);
    EXPECT_EQ(cancelled, 1u);
    EXPECT_EQ(swapchain.RenderWorkSubmittedCallbacks.size(), 0u);
    EXPECT_EQ(swapchain.RenderTimelineNextValue, 0u);
    memory.Shutdown();
}
