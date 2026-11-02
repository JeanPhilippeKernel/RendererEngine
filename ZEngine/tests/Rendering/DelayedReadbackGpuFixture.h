#pragma once
#include <ZEngine/Rendering/Renderers/Readback/RGReadbackRing.h>
#include <chrono>
#include <cstring>
#include <thread>
#include "AssetPreviewPassTestAccess.h"

// Included by GpuAllocatorTest.cpp to reuse its real, validation-capable
// HeadlessVulkan fixture. These tests gate an actual submitted GPU copy; they
// do not substitute frame counts or a fabricated completion counter.
namespace
{
    struct DelayedReadbackGpuFixture : ::testing::Test
    {
        HeadlessVulkan                                             Vulkan;
        MemoryManager                                              Memory;
        ArenaAllocator                                             LoggerArena;
        ZEngine::Hardwares::DeviceSwapchain                        Swapchain;
        std::unique_ptr<ZEngine::Hardwares::VulkanDevice>          Device = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
        ZEngine::Rendering::Renderers::RGReadbackRing              Ring;
        std::unique_ptr<ZEngine::Rendering::Primitives::Semaphore> Gate, Completion;
        VkCommandPool                                              Pool           = VK_NULL_HANDLE;
        VkQueue                                                    Queue          = VK_NULL_HANDLE;
        bool                                                       AllocatorReady = false;
        bool                                                       LoggerReady    = false;
        bool                                                       Submitted      = false;
        bool                                                       Released       = false;
        uint32_t                                                   Deliveries = 0, Cancellations = 0, Pixel = 0;
        static constexpr uint32_t                                  ExpectedPixel = 0x12345678;

        static void                                                Delivered(const void* data, size_t size, void* context)
        {
            auto& self = *static_cast<DelayedReadbackGpuFixture*>(context);
            ++self.Deliveries;
            EXPECT_EQ(size, sizeof(self.Pixel));
            if (size == sizeof(self.Pixel))
                std::memcpy(&self.Pixel, data, size);
        }

        static void Cancelled(void* context)
        {
            ++static_cast<DelayedReadbackGpuFixture*>(context)->Cancellations;
        }

        void SetUp() override
        {
            if (!Vulkan.Create(true, false, true))
                GTEST_SKIP() << "No headless Vulkan device";
            RecordProperty("validation_enabled", Vulkan.ValidationEnabled ? 1 : 0);
            RecordProperty("synchronization_validation_enabled", Vulkan.SynchronizationValidationEnabled ? 1 : 0);
            Memory.Initialize(ZMega(4), {});
            Memory.MainArena.CreateSubArena(ZMega(2), &LoggerArena);
            LoggerConfiguration logger;
            logger.OutputDirectory = (std::filesystem::temp_directory_path() / "zengine_delayed_readback_test_logs").string();
            logger.RingBufferSize  = 64;
            std::filesystem::create_directories(logger.OutputDirectory);
            Logger::Initialize(&LoggerArena, logger);
            LoggerReady           = true;
            Device->Arena         = &Memory.MainArena;
            Device->SwapchainPtr  = &Swapchain;
            Device->LogicalDevice = Vulkan.Device;
            Device->GpuMem.Initialize(Vulkan.PhysicalDevice, Vulkan.Device, Vulkan.Instance, false, false);
            AllocatorReady = true;
            Ring.Initialize(Device.get());
            Gate       = std::make_unique<ZEngine::Rendering::Primitives::Semaphore>(Device.get(), true);
            Completion = std::make_unique<ZEngine::Rendering::Primitives::Semaphore>(Device.get(), true);
            vkGetDeviceQueue(Vulkan.Device, Vulkan.QueueFamily, 0, &Queue);
            const VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = Vulkan.QueueFamily};
            ASSERT_EQ(vkCreateCommandPool(Vulkan.Device, &pool, nullptr, &Pool), VK_SUCCESS);
        }

        void ReleaseGate()
        {
            if (!Released && Gate)
            {
                const VkSemaphoreSignalInfo signal = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, .semaphore = Gate->GetHandle(), .value = 1};
                EXPECT_EQ(vkSignalSemaphore(Vulkan.Device, &signal), VK_SUCCESS);
                Released = true;
            }
        }

        void TearDown() override
        {
            // Always unblock a submitted producer, including after ASSERT exits,
            // before destroying any semaphore, staging allocation, or command pool.
            if (Submitted)
                ReleaseGate();
            if (Vulkan.Device)
                EXPECT_EQ(vkDeviceWaitIdle(Vulkan.Device), VK_SUCCESS);
            Ring.Dispose();
            Gate.reset();
            Completion.reset();
            if (Vulkan.Device)
                Device->PendingFree.Drain(&Device->GpuMem, Vulkan.Device, UINT64_MAX);
            if (Pool)
                vkDestroyCommandPool(Vulkan.Device, Pool, nullptr);
            if (AllocatorReady)
                Device->GpuMem.Shutdown();
            if (LoggerReady)
                Logger::Dispose();
            LoggerArena.Shutdown();
            Memory.Shutdown();
            Vulkan.Destroy();
            RecordProperty("validation_errors", Vulkan.ValidationErrors.load());
            EXPECT_EQ(Vulkan.ValidationErrors.load(), 0u);
        }

        void SubmitBlockedCopy(uint32_t index)
        {
            const auto* buffer = Ring.GetBuffer(index);
            ASSERT_NE(buffer, nullptr);
            VkCommandBuffer                   command  = VK_NULL_HANDLE;
            const VkCommandBufferAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = Pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
            ASSERT_EQ(vkAllocateCommandBuffers(Vulkan.Device, &allocate, &command), VK_SUCCESS);
            const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
            ASSERT_EQ(vkBeginCommandBuffer(command, &begin), VK_SUCCESS);
            vkCmdFillBuffer(command, buffer->Handle, 0, sizeof(Pixel), ExpectedPixel);
            const VkBufferMemoryBarrier2 barrier    = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT, .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT, .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .buffer = buffer->Handle, .offset = 0, .size = sizeof(Pixel)};
            const VkDependencyInfo       dependency = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier};
            vkCmdPipelineBarrier2(command, &dependency);
            ASSERT_EQ(vkEndCommandBuffer(command), VK_SUCCESS);
            const VkSemaphoreSubmitInfo     wait     = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = Gate->GetHandle(), .value = 1, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
            const VkSemaphoreSubmitInfo     signal   = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = Completion->GetHandle(), .value = 3, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
            const VkCommandBufferSubmitInfo commands = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = command};
            const VkSubmitInfo2             submit   = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .waitSemaphoreInfoCount = 1, .pWaitSemaphoreInfos = &wait, .commandBufferInfoCount = 1, .pCommandBufferInfos = &commands, .signalSemaphoreInfoCount = 1, .pSignalSemaphoreInfos = &signal};
            ASSERT_EQ(vkQueueSubmit2(Queue, 1, &submit, VK_NULL_HANDLE), VK_SUCCESS);
            Submitted = true;
            ASSERT_TRUE(Ring.Submit(index, Completion.get(), 3, sizeof(Pixel), &Delivered, this, &Cancelled));
        }

        void WaitForCopy()
        {
            const VkSemaphore         semaphore = Completion->GetHandle();
            const uint64_t            value     = 3;
            const VkSemaphoreWaitInfo wait      = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, .semaphoreCount = 1, .pSemaphores = &semaphore, .pValues = &value};
            ASSERT_EQ(vkWaitSemaphores(Vulkan.Device, &wait, 5'000'000'000ull), VK_SUCCESS);
        }
    };

    TEST_F(DelayedReadbackGpuFixture, PreviewCompletionWaitsForItsExactRealSubmissionTimeline)
    {
        using namespace ZEngine::Rendering;
        using namespace ZEngine::Rendering::Renderers;
        const uint32_t index = Ring.Acquire(sizeof(Pixel));
        ASSERT_NE(index, UINT32_MAX);
        SubmitBlockedCopy(index);
        ASSERT_FALSE(HasFatalFailure());
        // The handle is an identity only: this test exercises the preview's
        // production completion polling, not a second draw or target allocation.
        const Textures::TextureHandle texture = {.Index = 7, .Generation = 2};
        AssetPreviewPass              preview;
        AssetPreviewPassTestAccess::Arm(preview, texture, Device.get());
        AssetPreviewPassTestAccess::Submitted(preview, Completion.get(), 3);
        const VkSemaphoreSignalInfo partial = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, .semaphore = Completion->GetHandle(), .value = 2};
        ASSERT_EQ(vkSignalSemaphore(Vulkan.Device, &partial), VK_SUCCESS);
        for (uint32_t frame = 0; frame < 256; ++frame)
        {
            preview.BeginFrame();
            EXPECT_EQ(preview.Observe(texture), AssetPreviewPass::Result::Pending);
        }
        ReleaseGate();
        WaitForCopy();
        ASSERT_FALSE(HasFatalFailure());
        preview.BeginFrame();
        EXPECT_EQ(preview.Observe(texture), AssetPreviewPass::Result::Succeeded);
        preview.Forget(texture);
        EXPECT_TRUE(AssetPreviewPassTestAccess::Empty(preview));
        preview.Deinitialize(Device.get());
    }

    TEST_F(DelayedReadbackGpuFixture, IncompleteProducerNeverDeliversOrRecyclesItsReadback)
    {
        const uint32_t index = Ring.Acquire(sizeof(Pixel));
        ASSERT_NE(index, UINT32_MAX);
        SubmitBlockedCopy(index);
        ASSERT_FALSE(HasFatalFailure());
        // A lower completed value is not completion of this request.
        const VkSemaphoreSignalInfo partial = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, .semaphore = Completion->GetHandle(), .value = 2};
        ASSERT_EQ(vkSignalSemaphore(Vulkan.Device, &partial), VK_SUCCESS);
        for (uint32_t frame = 0; frame < 256; ++frame)
        {
            Ring.Poll();
            ASSERT_TRUE(Ring.IsReserved(index));
            EXPECT_EQ(Deliveries, 0u);
            EXPECT_EQ(Cancellations, 0u);
        }
        Ring.Cancel(index);
        Ring.CancelUnsubmitted();
        EXPECT_TRUE(Ring.IsReserved(index));
        const uint32_t other = Ring.Acquire(sizeof(Pixel));
        EXPECT_NE(other, index);
        Ring.Cancel(other);
        ReleaseGate();
        WaitForCopy();
        ASSERT_FALSE(HasFatalFailure());
        Ring.Poll();
        EXPECT_EQ(Deliveries, 1u);
        EXPECT_EQ(Cancellations, 0u);
        EXPECT_EQ(Pixel, ExpectedPixel);
        EXPECT_FALSE(Ring.IsReserved(index));
        Ring.Poll();
        EXPECT_EQ(Deliveries, 1u);
        EXPECT_EQ(Ring.Acquire(sizeof(Pixel)), index);
    }

    TEST_F(DelayedReadbackGpuFixture, ShutdownRetiresDelayedGpuWorkBeforeCancellingUndeliveredPixels)
    {
        const uint32_t index = Ring.Acquire(sizeof(Pixel));
        ASSERT_NE(index, UINT32_MAX);
        SubmitBlockedCopy(index);
        ASSERT_FALSE(HasFatalFailure());
        Ring.Poll();
        EXPECT_EQ(Deliveries, 0u);
        // Mirror the renderer shutdown precondition: retire the real queue
        // before Dispose. The host gate deliberately prevents an immediate idle.
        std::thread    release([this] {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            ReleaseGate();
        });
        const VkResult retired = vkQueueWaitIdle(Queue);
        release.join();
        ASSERT_EQ(retired, VK_SUCCESS);
        Ring.Dispose();
        EXPECT_EQ(Deliveries, 0u);
        EXPECT_EQ(Cancellations, 1u);
        EXPECT_TRUE(Ring.Allocations.empty());
        Ring.Dispose();
        EXPECT_EQ(Cancellations, 1u);
    }

    TEST_F(DelayedReadbackGpuFixture, SimulatedDeviceLostFlagSuppressesDeliveryAndDisposalCancelsOnce)
    {
        const uint32_t index = Ring.Acquire(sizeof(Pixel));
        ASSERT_NE(index, UINT32_MAX);
        SubmitBlockedCopy(index);
        ASSERT_FALSE(HasFatalFailure());
        ReleaseGate();
        WaitForCopy();
        ASSERT_FALSE(HasFatalFailure());
        // The real device remains healthy. This validates the production loss
        // latch/readback policy, not physical device removal or a driver reset.
        ASSERT_TRUE(Device->CheckDeviceLost(VK_ERROR_DEVICE_LOST, "simulated readback test"));
        Ring.Poll();
        EXPECT_EQ(Deliveries, 0u);
        EXPECT_TRUE(Ring.IsReserved(index));
        Ring.Dispose();
        EXPECT_EQ(Cancellations, 1u);
        Ring.Dispose();
        EXPECT_EQ(Cancellations, 1u);
    }
} // namespace
