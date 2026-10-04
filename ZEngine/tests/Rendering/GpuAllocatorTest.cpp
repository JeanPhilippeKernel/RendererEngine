#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/Logging/LoggerConfiguration.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <gtest/gtest.h>
#include <array>
#include <filesystem>
#include <memory>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Logging;

namespace
{
    // Minimal headless Vulkan instance + device — no window, no surface, no swapchain.
    // GpuAllocator only needs a valid VkPhysicalDevice/VkDevice/VkInstance, so this is
    // far lighter than standing up a full VulkanDevice.
    struct HeadlessVulkan
    {
        VkInstance       Instance       = VK_NULL_HANDLE;
        VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
        VkDevice         Device         = VK_NULL_HANDLE;

        bool             Create()
        {
            VkApplicationInfo app_info         = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
            app_info.apiVersion                = VK_API_VERSION_1_3;

            VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
            instance_info.pApplicationInfo     = &app_info;

            const char* extensions[2]          = {};
            uint32_t    extension_count        = 0;
#ifdef __APPLE__
            instance_info.flags           = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            extensions[extension_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
#endif
            instance_info.enabledExtensionCount   = extension_count;
            instance_info.ppEnabledExtensionNames = extension_count > 0 ? extensions : nullptr;

            if (vkCreateInstance(&instance_info, nullptr, &Instance) != VK_SUCCESS)
            {
                return false;
            }

            uint32_t device_count = 0;
            vkEnumeratePhysicalDevices(Instance, &device_count, nullptr);
            if (device_count == 0)
            {
                return false;
            }
            std::vector<VkPhysicalDevice> devices(device_count);
            vkEnumeratePhysicalDevices(Instance, &device_count, devices.data());
            PhysicalDevice                         = devices[0];

            float                   queue_priority = 1.0f;
            VkDeviceQueueCreateInfo queue_info     = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queue_info.queueFamilyIndex            = 0;
            queue_info.queueCount                  = 1;
            queue_info.pQueuePriorities            = &queue_priority;

            const char* device_extensions[1]       = {"VK_KHR_portability_subset"};
            uint32_t    device_extension_count     = 0;
#ifdef __APPLE__
            device_extension_count = 1;
#endif

            VkDeviceCreateInfo device_info              = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
            device_info.queueCreateInfoCount            = 1;
            device_info.pQueueCreateInfos               = &queue_info;
            device_info.enabledExtensionCount           = device_extension_count;
            device_info.ppEnabledExtensionNames         = device_extension_count > 0 ? device_extensions : nullptr;

            VkPhysicalDeviceVulkan13Features features13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .synchronization2 = VK_TRUE};
            VkPhysicalDeviceVulkan12Features features12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &features13, .timelineSemaphore = VK_TRUE};
            device_info.pNext                           = &features12;

            return vkCreateDevice(PhysicalDevice, &device_info, nullptr, &Device) == VK_SUCCESS;
        }

        void Destroy()
        {
            if (Device != VK_NULL_HANDLE)
            {
                vkDestroyDevice(Device, nullptr);
                Device = VK_NULL_HANDLE;
            }
            if (Instance != VK_NULL_HANDLE)
            {
                vkDestroyInstance(Instance, nullptr);
                Instance = VK_NULL_HANDLE;
            }
        }
    };
} // namespace

TEST(GpuAllocatorStatisticsTest, SeparatesVmaAndHeapAccounting)
{
    GpuAllocator allocator{};
    allocator.HeapCount                                 = 2;
    allocator.HasBudgetExt                              = true;
    allocator.HeapBudgets[0].statistics.allocationBytes = 10;
    allocator.HeapBudgets[0].statistics.blockBytes      = 16;
    allocator.HeapBudgets[0].usage                      = 20;
    allocator.HeapBudgets[0].budget                     = 40;
    allocator.HeapBudgets[1].statistics.allocationBytes = 30;
    allocator.HeapBudgets[1].statistics.blockBytes      = 48;
    allocator.HeapBudgets[1].usage                      = 50;
    allocator.HeapBudgets[1].budget                     = 80;

    const GpuMemoryStatistics stats                     = allocator.GetMemoryStatistics();
    EXPECT_EQ(stats.AllocationBytes, 40u);
    EXPECT_EQ(stats.BlockBytes, 64u);
    EXPECT_EQ(stats.HeapUsageBytes, 70u);
    EXPECT_EQ(stats.HeapBudgetBytes, 120u);
    EXPECT_EQ(stats.HeapCount, 2u);
    EXPECT_TRUE(stats.UsesDriverBudgetTelemetry);
}

TEST(StagingRingBufferTest, RefusesAllocationWhenAllRetirementRecordsAreReserved)
{
    StagingRingBuffer                                  ring    = {};
    std::array<uint8_t, StagingRingBuffer::kMaxChunks> backing = {};
    ring.MappedPtr                                             = backing.data();

    for (uint32_t i = 0; i < StagingRingBuffer::kMaxChunks; ++i)
    {
        uint32_t offset = UINT32_MAX;
        ASSERT_NE(ring.Allocate(1, 1, &offset), nullptr);
        EXPECT_EQ(offset, i);
    }

    EXPECT_EQ(ring.ChunkCount, StagingRingBuffer::kMaxChunks);
    uint32_t overflow_offset = UINT32_MAX;
    EXPECT_EQ(ring.Allocate(1, 1, &overflow_offset), nullptr);
    EXPECT_EQ(ring.ChunkCount, StagingRingBuffer::kMaxChunks);

    for (uint32_t i = 0; i < StagingRingBuffer::kMaxChunks; ++i)
        ring.Submit(i, 1, i + 1);
    ring.Drain(StagingRingBuffer::kMaxChunks);

    EXPECT_EQ(ring.ChunkCount, 0u);
    EXPECT_EQ(ring.ChunkHead, ring.ChunkTail);
}

TEST(StagingRingBufferTest, DoesNotRetireAnUnsubmittedReservation)
{
    StagingRingBuffer      ring    = {};
    std::array<uint8_t, 1> backing = {};
    ring.MappedPtr                 = backing.data();

    uint32_t offset                = UINT32_MAX;
    ASSERT_NE(ring.Allocate(1, 1, &offset), nullptr);
    ring.Drain(UINT64_MAX);
    EXPECT_EQ(ring.ChunkCount, 1u);
    EXPECT_EQ(ring.ChunkHead, 0u);

    ring.Submit(offset, 1, 1);
    ring.Drain(1);
    EXPECT_EQ(ring.ChunkCount, 0u);
}

// SetUpTestSuite/TearDownTestSuite (once for the whole suite) rather than per-test
// SetUp/TearDown — cheaper, and avoids creating/destroying a real VkInstance+VkDevice
// 4 times back-to-back.
//
// NOTE: creating a real MoltenVK device in this test binary has a process-wide side
// effect (likely a signal handler MoltenVK/Metal installs and doesn't restore) that can
// make an unrelated, later SIGTRAP-based test (e.g. an EXPECT_DEATH assert, or an
// FSEventStream test elsewhere in the binary) crash the whole process WHEN ALL TESTS RUN
// IN ONE PROCESS (i.e. invoking the ZEngineTests binary directly with no filter). This
// does not affect the actual test-running path: `ctest` (via gtest_discover_tests) runs
// every test as its own process, and is unaffected — confirmed via `ctest -R
// "GpuAllocatorTest|AllocatorTest|VFSFSEventsWatcherTest"`, 45/45 passing. If you need an
// all-in-one-process run for a quick manual check, filter this suite out.
class GpuAllocatorTest : public ::testing::Test
{
protected:
    static MemoryManager*  s_manager;
    static ArenaAllocator* s_logger_arena;
    static HeadlessVulkan* s_vk;
    static GpuAllocator*   s_allocator;

    static void            SetUpTestSuite()
    {
        s_manager = new MemoryManager();
        s_manager->Initialize(ZMega(4), {});
        s_logger_arena = new ArenaAllocator();
        s_manager->MainArena.CreateSubArena(ZMega(2), s_logger_arena);

        // GpuAllocator::Initialize can log warnings on pool-creation failure — Logger::Log
        // indexes an empty ring buffer (mod-by-zero) and crashes via infinite recursion if
        // Logger::Initialize hasn't run in this test binary (e.g. another suite's teardown
        // already called Logger::Dispose). Mirrors ZEngine/tests/Logging/Logger_test.cpp.
        LoggerConfiguration cfg{};
        cfg.OutputDirectory = (std::filesystem::temp_directory_path() / "zengine_gpu_allocator_test_logs").string();
        cfg.RingBufferSize  = 64;
        std::filesystem::create_directories(cfg.OutputDirectory);
        Logger::Initialize(s_logger_arena, cfg);
        Logger::SetMinLevelAllChannels(LogLevel::TRACE);

        s_vk = new HeadlessVulkan();
        if (!s_vk->Create())
        {
            GTEST_SKIP() << "No headless Vulkan device available on this machine";
            return;
        }
        s_allocator = new GpuAllocator();
        s_allocator->Initialize(s_vk->PhysicalDevice, s_vk->Device, s_vk->Instance, /*has_memory_budget_ext=*/false, /*has_buffer_device_address_ext=*/false);
    }

    static void TearDownTestSuite()
    {
        if (s_allocator)
        {
            s_allocator->Shutdown();
            delete s_allocator;
            s_allocator = nullptr;
        }
        if (s_vk)
        {
            s_vk->Destroy();
            delete s_vk;
            s_vk = nullptr;
        }
        Logger::Dispose();
        if (s_logger_arena)
        {
            s_logger_arena->Shutdown();
            delete s_logger_arena;
            s_logger_arena = nullptr;
        }
        if (s_manager)
        {
            s_manager->Shutdown();
            delete s_manager;
            s_manager = nullptr;
        }
    }

    GpuAllocator& allocator()
    {
        return *s_allocator;
    }
};

MemoryManager*  GpuAllocatorTest::s_manager      = nullptr;
ArenaAllocator* GpuAllocatorTest::s_logger_arena = nullptr;
HeadlessVulkan* GpuAllocatorTest::s_vk           = nullptr;
GpuAllocator*   GpuAllocatorTest::s_allocator    = nullptr;

TEST_F(GpuAllocatorTest, PoolsAreCreatedForExpectedDomains)
{
    ASSERT_NE(s_allocator, nullptr);
    EXPECT_NE(allocator().Pools[static_cast<uint8_t>(GpuMemoryDomain::DeviceGeometry)], nullptr);
    EXPECT_NE(allocator().Pools[static_cast<uint8_t>(GpuMemoryDomain::DeviceTexture)], nullptr);
    EXPECT_NE(allocator().Pools[static_cast<uint8_t>(GpuMemoryDomain::HostUniform)], nullptr);
    EXPECT_NE(allocator().Pools[static_cast<uint8_t>(GpuMemoryDomain::HostStaging)], nullptr);
    EXPECT_EQ(allocator().Pools[static_cast<uint8_t>(GpuMemoryDomain::RenderTarget)], nullptr);
}

TEST_F(GpuAllocatorTest, AllocateBufferUsesDomainPool)
{
    ASSERT_NE(s_allocator, nullptr);
    BufferView view = allocator().AllocateBuffer(4096, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, GpuMemoryDomain::DeviceGeometry, "test_geometry");
    ASSERT_TRUE(view);

    VmaPool       pool  = allocator().Pools[static_cast<uint8_t>(GpuMemoryDomain::DeviceGeometry)];
    VmaStatistics stats = {};
    vmaGetPoolStatistics(allocator().Allocator, pool, &stats);
    EXPECT_GT(stats.blockCount, 0u);
    EXPECT_GE(stats.allocationCount, 1u);

    allocator().FreeBuffer(view);
}

TEST_F(GpuAllocatorTest, OversizedAllocationFallsBackToDefaultPoolWithoutCrashing)
{
    ASSERT_NE(s_allocator, nullptr);
    // GeometryBytes is 512 MB per block; a single allocation larger than that cannot
    // fit any block in a fixed-blockSize pool (VMA early-rejects with
    // VK_ERROR_OUT_OF_DEVICE_MEMORY rather than growing the pool) — exactly the case
    // AllocateBuffer's fallback-to-default-pool retry exists for.
    constexpr VkDeviceSize oversized = GeometryBytes + (64ULL << 20);
    BufferView             view      = allocator().AllocateBuffer(oversized, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, GpuMemoryDomain::DeviceGeometry, "test_oversized");
    ASSERT_TRUE(view);
    allocator().FreeBuffer(view);
}

TEST_F(GpuAllocatorTest, StagingRingSharesDeclaredPool)
{
    ASSERT_NE(s_allocator, nullptr);
    VmaAllocationInfo info = {};
    vmaGetAllocationInfo(allocator().Allocator, allocator().Ring.Allocation, &info);
    EXPECT_EQ(info.deviceMemory != VK_NULL_HANDLE, true);

    // Indirect check: allocating another HostStaging buffer should land in the same
    // pool the ring uses, since GpuAllocator::Initialize wires both to Pools[HostStaging].
    BufferView one_shot = allocator().AllocateBuffer(1024, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, GpuMemoryDomain::HostStaging, "test_staging");
    ASSERT_TRUE(one_shot);

    VmaPool       pool  = allocator().Pools[static_cast<uint8_t>(GpuMemoryDomain::HostStaging)];
    VmaStatistics stats = {};
    vmaGetPoolStatistics(allocator().Allocator, pool, &stats);
    EXPECT_GE(stats.allocationCount, 2u); // the ring's own buffer + this one-shot buffer

    allocator().FreeBuffer(one_shot);
}

// Reuse the headless Vulkan fixture to exercise real upload submissions without
// creating a window, swapchain images, or the rest of the resource manager.
struct RRMUploadBatchTestHelper
{
    static void Initialize(ZEngine::Rendering::RenderResourceManager& manager, ZEngine::Hardwares::VulkanDevice& device, ZEngine::Hardwares::CommandBufferManager& commands, ZEngine::Rendering::Primitives::Semaphore* timeline, ArenaAllocator& arena, VkQueue queue)
    {
        device.m_queue_map.init(&arena, 2);
        device.m_queue_map.insert(ZEngine::Rendering::QueueType::GRAPHIC_QUEUE, queue);
        manager.m_device         = &device;
        manager.m_upload_cmd_mgr = &commands;
        manager.m_batch_timeline = timeline;
        manager.m_batch_frames.init(&arena, 3, 3);
        for (auto& frame : manager.m_batch_frames)
            frame = ZEngine::Rendering::RenderResourceManager::BatchFrameState{};
    }

    static void Append(ZEngine::Rendering::RenderResourceManager& manager, BufferView& target, uint8_t frame_index, uint32_t value, VkDeviceSize offset)
    {
        manager.EnsureBatchOpen(frame_index);
        manager.AppendToGlobalBuffer(target, &value, sizeof(value), offset, frame_index);
    }

    static bool IsOpen(const ZEngine::Rendering::RenderResourceManager& manager)
    {
        return manager.m_batch_mode;
    }

    static uint32_t StagingCount(const ZEngine::Rendering::RenderResourceManager& manager, uint32_t frame_index)
    {
        return manager.m_batch_frames[frame_index].StagingCount;
    }

    static uint64_t LastSignal(const ZEngine::Rendering::RenderResourceManager& manager, uint32_t frame_index)
    {
        return manager.m_batch_frames[frame_index].LastSignal;
    }
};

class RenderResourceManagerBatchTest : public GpuAllocatorTest
{
protected:
    MemoryManager                                                     Memory{};
    std::unique_ptr<ZEngine::Hardwares::VulkanDevice>                 Device  = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    std::unique_ptr<ZEngine::Rendering::RenderResourceManager>        Manager = std::make_unique<ZEngine::Rendering::RenderResourceManager>();
    ZEngine::Hardwares::DeviceSwapchain                               Swapchain{};
    ZEngine::Hardwares::CommandBufferManager                          UploadCommands{};
    std::array<std::unique_ptr<ZEngine::Hardwares::CommandBuffer>, 3> Commands{};
    std::unique_ptr<ZEngine::Rendering::Primitives::Semaphore>        Timeline{};
    VkCommandPool                                                     Pool = VK_NULL_HANDLE;
    BufferView                                                        Target{};
    bool                                                              Initialized = false;

    void                                                              SetUp() override
    {
        ASSERT_NE(s_allocator, nullptr);
        Memory.Initialize(ZMega(4), {});
        auto& arena                = Memory.MainArena;
        Device->Arena              = &arena;
        Device->LogicalDevice      = s_vk->Device;
        Device->GraphicFamilyIndex = 0;
        Device->SwapchainPtr       = &Swapchain;
        Device->GpuMem.Allocator   = s_allocator->Allocator;

        VkQueue queue              = VK_NULL_HANDLE;
        vkGetDeviceQueue(s_vk->Device, 0, 0, &queue);
        VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = 0};
        ASSERT_EQ(vkCreateCommandPool(s_vk->Device, &pool_info, nullptr, &Pool), VK_SUCCESS);
        UploadCommands.Device           = Device.get();
        UploadCommands.TotalThreadCount = 1;
        const uint32_t stride           = UploadCommands.MaxBufferPerPool * UploadCommands.MaxBufferPerPool;
        UploadCommands.InstantGraphicsCommandBuffers.init(&arena, 3 * stride, 3 * stride);
        for (uint32_t frame = 0; frame < Commands.size(); ++frame)
        {
            Commands[frame]                                              = std::make_unique<ZEngine::Hardwares::CommandBuffer>(Device.get(), Pool, ZEngine::Rendering::QueueType::GRAPHIC_QUEUE, true);
            UploadCommands.InstantGraphicsCommandBuffers[frame * stride] = Commands[frame].get();
        }
        Timeline = std::make_unique<ZEngine::Rendering::Primitives::Semaphore>(Device.get(), true);
        RRMUploadBatchTestHelper::Initialize(*Manager, *Device, UploadCommands, Timeline.get(), arena, queue);
        Target      = allocator().AllocateBuffer(64, VK_BUFFER_USAGE_TRANSFER_DST_BIT, GpuMemoryDomain::HostReadback, "batch_test_target");
        Initialized = true;
    }

    void TearDown() override
    {
        if (Initialized)
        {
            Manager->EndFrame();
            vkDeviceWaitIdle(s_vk->Device);
            Manager->BeginFrame(0); // Retire all submitted staging allocations.
            allocator().FreeBuffer(Target);
        }
        for (auto& command : Commands)
            command.reset();
        Timeline.reset();
        if (Device->LogicalDevice)
            Device->PendingFree.Drain(&Device->GpuMem, Device->LogicalDevice, UINT64_MAX);
        if (Pool)
            vkDestroyCommandPool(s_vk->Device, Pool, nullptr);
    }

    void Append(uint8_t frame, uint32_t value, VkDeviceSize offset = 0)
    {
        RRMUploadBatchTestHelper::Append(*Manager, Target, frame, value, offset);
    }

    uint32_t Read(uint64_t signal_value, VkDeviceSize offset = 0)
    {
        Timeline->Wait(signal_value);
        uint32_t value = 0;
        EXPECT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, Target.Allocation, offset, &value, sizeof(value)), VK_SUCCESS);
        return value;
    }
};

TEST_F(RenderResourceManagerBatchTest, StartupResizeSubmitsInitializationBeforeSwitchingSlots)
{
    Append(0, 42);
    Manager->BeginFrame(1); // First acquired slot changes after startup recreation.
    ASSERT_FALSE(RRMUploadBatchTestHelper::IsOpen(*Manager));
    EXPECT_EQ(RRMUploadBatchTestHelper::LastSignal(*Manager, 0), 1u);

    ZEngine::Hardwares::AsyncGPUOperationHandle operation{};
    ASSERT_TRUE(Device->AsyncGPUOperations.pop(operation));
    EXPECT_EQ(operation.Timeline, Timeline.get());
    EXPECT_EQ(operation.SignalValue, 1u);
    EXPECT_EQ(operation.StageFlags, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);

    Append(1, 99, sizeof(uint32_t));
    Manager->EndFrame();
    EXPECT_EQ(RRMUploadBatchTestHelper::LastSignal(*Manager, 1), 2u);
    EXPECT_EQ(Read(2), 42u);
    EXPECT_EQ(Read(2, sizeof(uint32_t)), 99u);
}

TEST_F(RenderResourceManagerBatchTest, SameSlotInitializationJoinsTheFirstFrame)
{
    Append(0, 42);
    Manager->BeginFrame(0);
    EXPECT_TRUE(RRMUploadBatchTestHelper::IsOpen(*Manager));
    EXPECT_EQ(RRMUploadBatchTestHelper::StagingCount(*Manager, 0), 1u);
    EXPECT_EQ(RRMUploadBatchTestHelper::LastSignal(*Manager, 0), 0u);

    Append(0, 99, sizeof(uint32_t));
    Manager->EndFrame();
    EXPECT_EQ(RRMUploadBatchTestHelper::LastSignal(*Manager, 0), 1u);
    EXPECT_EQ(Read(1), 42u);
    EXPECT_EQ(Read(1, sizeof(uint32_t)), 99u);
}

TEST_F(RenderResourceManagerBatchTest, UploadJoinSubmitsThePreviousSlotsBatch)
{
    Append(0, 42);
    Append(2, 99, sizeof(uint32_t));
    EXPECT_EQ(RRMUploadBatchTestHelper::LastSignal(*Manager, 0), 1u);
    EXPECT_EQ(RRMUploadBatchTestHelper::StagingCount(*Manager, 2), 1u);
    Manager->EndFrame();
    EXPECT_EQ(RRMUploadBatchTestHelper::LastSignal(*Manager, 2), 2u);
    EXPECT_EQ(Read(2), 42u);
    EXPECT_EQ(Read(2, sizeof(uint32_t)), 99u);
}

TEST_F(RenderResourceManagerBatchTest, PreviousSignalNeverRetiresAnOpenBatchsStaging)
{
    Append(0, 42);
    Manager->EndFrame();
    EXPECT_EQ(Read(1), 42u);
    Manager->BeginFrame(0);
    EXPECT_EQ(RRMUploadBatchTestHelper::StagingCount(*Manager, 0), 0u);

    Append(0, 99);
    Manager->BeginFrame(0); // LastSignal=1 is complete, but the new copy is not submitted.
    ASSERT_EQ(RRMUploadBatchTestHelper::StagingCount(*Manager, 0), 1u);
    Manager->EndFrame();
    EXPECT_EQ(Read(2), 99u);
    Manager->BeginFrame(2);
    EXPECT_EQ(RRMUploadBatchTestHelper::StagingCount(*Manager, 0), 0u);
}
