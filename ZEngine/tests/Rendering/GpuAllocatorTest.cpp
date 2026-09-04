#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/Logging/LoggerConfiguration.h>
#include <gtest/gtest.h>
#include <filesystem>

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

            VkDeviceCreateInfo device_info      = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
            device_info.queueCreateInfoCount    = 1;
            device_info.pQueueCreateInfos       = &queue_info;
            device_info.enabledExtensionCount   = device_extension_count;
            device_info.ppEnabledExtensionNames = device_extension_count > 0 ? device_extensions : nullptr;

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
