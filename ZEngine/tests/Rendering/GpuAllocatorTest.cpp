#include <SPIRV/GlslangToSpv.h>
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/SceneGridSettings.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/Logging/LoggerConfiguration.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/Graphics/GizmoPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/GridLabelPass.h>
#include <ZEngine/Rendering/Renderers/Pipelines/PSOCache.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <memory>
#include "GizmoShaderSources.h"
#include "GridLabelShaderSources.h"

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Logging;

namespace
{
    // Minimal headless Vulkan instance + device — no window, no surface, no swapchain.
    // GpuAllocator only needs a valid VkPhysicalDevice/VkDevice/VkInstance, so this is
    // far lighter than standing up a full VulkanDevice.
    struct HeadlessVulkan
    {
        VkInstance                            Instance                         = VK_NULL_HANDLE;
        VkPhysicalDevice                      PhysicalDevice                   = VK_NULL_HANDLE;
        VkDevice                              Device                           = VK_NULL_HANDLE;
        uint32_t                              QueueFamily                      = UINT32_MAX;
        VkDebugUtilsMessengerEXT              Messenger                        = VK_NULL_HANDLE;
        std::atomic<uint32_t>                 ValidationErrors                 = 0;
        bool                                  ValidationEnabled                = false;
        bool                                  DynamicRenderingEnabled          = false;
        bool                                  SynchronizationValidationEnabled = false;

        static VKAPI_ATTR VkBool32 VKAPI_CALL ReportValidation(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* context)
        {
            auto& owner = *static_cast<HeadlessVulkan*>(context);
            owner.ValidationErrors.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "Vulkan validation error: %s\n", data->pMessage);
            return VK_FALSE;
        }

        bool Create(bool validate = false, bool dynamic_rendering = false, bool synchronize_validation = false)
        {
            VkApplicationInfo app_info                                 = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
            app_info.apiVersion                                        = VK_API_VERSION_1_3;

            VkInstanceCreateInfo instance_info                         = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
            instance_info.pApplicationInfo                             = &app_info;

            const char*                        extensions[4]           = {};
            uint32_t                           extension_count         = 0;
            const char*                        validation_layer        = "VK_LAYER_KHRONOS_validation";
            VkDebugUtilsMessengerCreateInfoEXT debug                   = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT, .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT, .pfnUserCallback = &ReportValidation, .pUserData = this};
            const VkValidationFeatureEnableEXT synchronization_feature = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
            VkValidationFeaturesEXT            validation_features     = {.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT, .enabledValidationFeatureCount = 1, .pEnabledValidationFeatures = &synchronization_feature};
            if (validate)
            {
                uint32_t layer_count = 0, available_extension_count = 0;
                if (vkEnumerateInstanceLayerProperties(&layer_count, nullptr) != VK_SUCCESS || vkEnumerateInstanceExtensionProperties(nullptr, &available_extension_count, nullptr) != VK_SUCCESS)
                    return false;
                std::vector<VkLayerProperties>     layers(layer_count);
                std::vector<VkExtensionProperties> available_extensions(available_extension_count);
                if (vkEnumerateInstanceLayerProperties(&layer_count, layers.data()) != VK_SUCCESS || vkEnumerateInstanceExtensionProperties(nullptr, &available_extension_count, available_extensions.data()) != VK_SUCCESS)
                    return false;
                bool layer_available = false, debug_available = false;
                for (uint32_t index = 0; index < layer_count; ++index)
                    layer_available = layer_available || ZEngine::Helpers::secure_strcmp(layers[index].layerName, validation_layer) == 0;
                for (uint32_t index = 0; index < available_extension_count; ++index)
                    debug_available = debug_available || ZEngine::Helpers::secure_strcmp(available_extensions[index].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
                ValidationEnabled = layer_available && debug_available;
                if (ValidationEnabled)
                {
                    extensions[extension_count++]     = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
                    instance_info.enabledLayerCount   = 1;
                    instance_info.ppEnabledLayerNames = &validation_layer;
                    instance_info.pNext               = &debug;
                    if (synchronize_validation)
                    {
                        uint32_t validation_extension_count = 0;
                        if (vkEnumerateInstanceExtensionProperties(validation_layer, &validation_extension_count, nullptr) != VK_SUCCESS)
                            return false;
                        std::vector<VkExtensionProperties> validation_extensions(validation_extension_count);
                        if (vkEnumerateInstanceExtensionProperties(validation_layer, &validation_extension_count, validation_extensions.data()) != VK_SUCCESS)
                            return false;
                        for (uint32_t index = 0; index < validation_extension_count; ++index)
                            SynchronizationValidationEnabled = SynchronizationValidationEnabled || ZEngine::Helpers::secure_strcmp(validation_extensions[index].extensionName, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) == 0;
                        if (SynchronizationValidationEnabled)
                        {
                            extensions[extension_count++] = VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;
                            debug.pNext                   = &validation_features;
                        }
                    }
                }
            }
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
            if (ValidationEnabled)
            {
                // ValidationFeatures belongs to instance creation, not the
                // standalone debug-messenger creation below.
                debug.pNext       = nullptr;
                const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(Instance, "vkCreateDebugUtilsMessengerEXT"));
                if (!create || create(Instance, &debug, nullptr, &Messenger) != VK_SUCCESS)
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
            PhysicalDevice              = devices[0];

            uint32_t queue_family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &queue_family_count, nullptr);
            std::vector<VkQueueFamilyProperties> families(queue_family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &queue_family_count, families.data());
            for (uint32_t index = 0; index < queue_family_count; ++index)
            {
                if (families[index].queueCount && (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                {
                    QueueFamily = index;
                    break;
                }
            }
            if (QueueFamily == UINT32_MAX)
                return false;

            float                   queue_priority = 1.0f;
            VkDeviceQueueCreateInfo queue_info     = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queue_info.queueFamilyIndex            = QueueFamily;
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

            if (dynamic_rendering)
            {
                VkPhysicalDeviceVulkan13Features supported13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
                VkPhysicalDeviceFeatures2        supported   = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &supported13};
                vkGetPhysicalDeviceFeatures2(PhysicalDevice, &supported);
                DynamicRenderingEnabled = supported13.dynamicRendering == VK_TRUE;
            }
            VkPhysicalDeviceVulkan13Features features13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .synchronization2 = VK_TRUE, .dynamicRendering = DynamicRenderingEnabled ? VK_TRUE : VK_FALSE};
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
                if (Messenger)
                {
                    const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT"));
                    if (destroy)
                        destroy(Instance, Messenger, nullptr);
                    Messenger = VK_NULL_HANDLE;
                }
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

TEST_F(GpuAllocatorTest, GizmoPassResourcesRebuildAcrossFreshDevices)
{
    using ZEngine::Rendering::Renderers::GizmoGPUInstance;
    using ZEngine::Rendering::Renderers::GizmoPass;

    // Keep the same callback alive across genuine VkInstance/VkDevice lifetimes.
    // This tests persistent buffer teardown/rebuild, not renderer/device-loss recovery
    // or graphics-pipeline execution. Each device owns its CPU staging arena.
    GizmoPass pass = {};
    struct DeviceCycle
    {
        ArenaAllocator                                    Arena          = {};
        HeadlessVulkan                                    Vulkan         = {};
        ZEngine::Hardwares::DeviceSwapchain               Swapchain      = {};
        std::unique_ptr<ZEngine::Hardwares::VulkanDevice> Device         = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
        GizmoPass*                                        Pass           = nullptr;
        bool                                              AllocatorReady = false;

        ~DeviceCycle()
        {
            // Also clean up after a fatal assertion: the pass must lose its GPU
            // resources before the allocator/device, and staging before the arena.
            if (AllocatorReady)
            {
                Pass->Deinitialize(Device.get());
                Device->GpuMem.Shutdown();
            }
            Vulkan.Destroy();
        }
    };

    constexpr uint32_t cycles      = 3;
    constexpr size_t   slot_bytes  = sizeof(GizmoGPUInstance) * GizmoPass::MaxInstances;
    constexpr size_t   total_bytes = slot_bytes * GizmoPass::FramesInFlight;
    for (uint32_t cycle = 0; cycle < cycles; ++cycle)
    {
        SCOPED_TRACE(cycle);
        DeviceCycle owner = {};
        owner.Pass        = &pass;
        ASSERT_TRUE(owner.Vulkan.Create());
        owner.Arena.Initialize(ZMega(1), 0);
        owner.Device->Arena                 = &owner.Arena;
        owner.Device->LogicalDevice         = owner.Vulkan.Device;
        owner.Device->SwapchainPtr          = &owner.Swapchain;
        owner.Swapchain.BufferredFrameCount = GizmoPass::FramesInFlight;
        owner.Device->GpuMem.Initialize(owner.Vulkan.PhysicalDevice, owner.Vulkan.Device, owner.Vulkan.Instance, false, false);
        owner.AllocatorReady      = true;

        VmaTotalStatistics before = {}, initialized = {}, after = {};
        vmaCalculateStatistics(owner.Device->GpuMem.Allocator, &before);
        const size_t arena_before = owner.Arena.m_current_offset;
        pass.Initialize(owner.Device.get());
        vmaCalculateStatistics(owner.Device->GpuMem.Allocator, &initialized);
        EXPECT_EQ(initialized.total.statistics.allocationCount, before.total.statistics.allocationCount + GizmoPass::FramesInFlight);
        // Drivers may round buffer memory requirements beyond the requested size.
        EXPECT_GE(initialized.total.statistics.allocationBytes, before.total.statistics.allocationBytes + total_bytes);
        RecordProperty("gpu_allocation_bytes_last_cycle", initialized.total.statistics.allocationBytes - before.total.statistics.allocationBytes);
        const size_t staging_end = owner.Arena.m_current_offset;
        EXPECT_GE(staging_end - arena_before, slot_bytes);
        EXPECT_LT(staging_end - arena_before, slot_bytes + DEFAULT_ALIGNMENT);

        pass.Initialize(owner.Device.get()); // Idempotent: no duplicate staging or GPU buffers.
        vmaCalculateStatistics(owner.Device->GpuMem.Allocator, &after);
        EXPECT_EQ(after.total.statistics.allocationCount, initialized.total.statistics.allocationCount);
        EXPECT_EQ(after.total.statistics.allocationBytes, initialized.total.statistics.allocationBytes);
        EXPECT_EQ(owner.Arena.m_current_offset, staging_end);

        pass.Deinitialize(owner.Device.get());
        pass.Deinitialize(owner.Device.get());
        vmaCalculateStatistics(owner.Device->GpuMem.Allocator, &after);
        EXPECT_EQ(after.total.statistics.allocationCount, before.total.statistics.allocationCount);
        EXPECT_EQ(after.total.statistics.allocationBytes, before.total.statistics.allocationBytes);
        EXPECT_FALSE(pass.Register(nullptr, "Gizmo Pass", {}, nullptr, nullptr));
    }
    RecordProperty("fresh_device_cycles", cycles);
    RecordProperty("requested_gpu_buffer_bytes_per_cycle", total_bytes);
    RecordProperty("cpu_staging_bytes_per_cycle", slot_bytes);
}

// Reuse the headless Vulkan fixture to exercise real upload submissions without
// creating a window, swapchain images, or the rest of the resource manager.
struct RRMUploadBatchTestHelper
{
    static void InitializeGraphicsQueue(ZEngine::Hardwares::VulkanDevice& device, ArenaAllocator& arena, VkQueue queue)
    {
        device.m_queue_map.init(&arena, 2);
        device.m_queue_map.insert(ZEngine::Rendering::QueueType::GRAPHIC_QUEUE, queue);
    }

    static void InitializeHostWrites(ZEngine::Rendering::RenderResourceManager& manager, ZEngine::Hardwares::VulkanDevice& device)
    {
        manager.m_device = &device;
    }

    static void Initialize(ZEngine::Rendering::RenderResourceManager& manager, ZEngine::Hardwares::VulkanDevice& device, ZEngine::Hardwares::CommandBufferManager& commands, ZEngine::Rendering::Primitives::Semaphore* timeline, ArenaAllocator& arena, VkQueue queue)
    {
        InitializeGraphicsQueue(device, arena, queue);
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

    static bool InitializeDynamicRendering(ZEngine::Hardwares::VulkanDevice& device)
    {
        device.__beginRenderingPtr = reinterpret_cast<PFN_vkCmdBeginRendering>(vkGetDeviceProcAddr(device.LogicalDevice, "vkCmdBeginRendering"));
        device.__endRenderingPtr   = reinterpret_cast<PFN_vkCmdEndRendering>(vkGetDeviceProcAddr(device.LogicalDevice, "vkCmdEndRendering"));
        return device.__beginRenderingPtr && device.__endRenderingPtr;
    }
};

namespace
{
    namespace Renderers = ZEngine::Rendering::Renderers;
    namespace Specs     = ZEngine::Rendering::Specifications;

    struct GlslangLifetime
    {
        bool Ready = glslang::InitializeProcess();
        ~GlslangLifetime()
        {
            if (Ready)
                glslang::FinalizeProcess();
        }
    };

    bool CompileGizmoStage(const char* text, int length, EShLanguage stage, std::vector<uint32_t>& words)
    {
        glslang::TShader shader(stage);
        shader.setStringsWithLengths(&text, &length, 1);
        shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 460);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
        constexpr auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
        if (!shader.parse(GetDefaultResources(), 460, false, messages))
        {
            ADD_FAILURE() << shader.getInfoLog();
            return false;
        }
        glslang::TProgram program;
        program.addShader(&shader);
        if (!program.link(messages))
        {
            ADD_FAILURE() << program.getInfoLog();
            return false;
        }
        glslang::GlslangToSpv(*program.getIntermediate(stage), words);
        return !words.empty();
    }

    // Only the offscreen target/descriptor bootstrap is supplied by this fixture.
    // Registration, upload, pipeline bake, Prepare and Execute use production code.
    // No Engine singleton, desktop window, surface or real swapchain is needed.
    struct OffscreenGizmoCycle
    {
        struct BorrowedGraphicPass : Renderers::RenderPasses::GraphicPass
        {
            void InitializeBindings(ZEngine::Hardwares::VulkanDevice* device)
            {
                InitializeDescriptorBindings(device);
            }
        };

        ArenaAllocator                                             Arena           = {};
        HeadlessVulkan                                             Vulkan          = {};
        ZEngine::Hardwares::DeviceSwapchain                        Swapchain       = {};
        ZEngine::Hardwares::FrameContext                           CurrentFrame    = {};
        std::unique_ptr<ZEngine::Hardwares::VulkanDevice>          Device          = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
        std::unique_ptr<ZEngine::Rendering::RenderResourceManager> Manager         = std::make_unique<ZEngine::Rendering::RenderResourceManager>();
        std::unique_ptr<Renderers::Pipelines::PSOCache>            Cache           = std::make_unique<Renderers::Pipelines::PSOCache>();
        std::unique_ptr<Renderers::RenderGraph>                    Graph           = std::make_unique<Renderers::RenderGraph>();
        ZEngine::Rendering::Shaders::Shader                        Shader          = {};
        ZEngine::Rendering::Shaders::Shader*                       DrawShader      = &Shader;
        Renderers::Pipelines::GraphicPipeline                      Pipeline        = {};
        BorrowedGraphicPass                                        Backend         = {};
        std::unique_ptr<Renderers::RenderPasses::Attachment>       Attachment      = {};
        std::unique_ptr<ZEngine::Hardwares::CommandBuffer>         Commands        = {};
        Renderers::RenderGraphResourceBuilder                      Builder         = {};
        Renderers::GizmoPass*                                      Pass            = nullptr;
        VkCommandPool                                              Pool            = VK_NULL_HANDLE;
        VkQueue                                                    Queue           = VK_NULL_HANDLE;
        VkFence                                                    Fence           = VK_NULL_HANDLE;
        VkDescriptorPool                                           Descriptors     = VK_NULL_HANDLE;
        VkDescriptorSetLayout                                      CameraLayout    = VK_NULL_HANDLE;
        VkDescriptorSetLayout                                      EmptyLayouts[2] = {};
        VkImage                                                    Image           = VK_NULL_HANDLE;
        VmaAllocation                                              ImageMemory     = nullptr;
        VkImageView                                                ImageView       = VK_NULL_HANDLE;
        ZEngine::Rendering::Buffers::FramebufferVNext              Framebuffer{Device.get()};
        BufferView                                                 Camera         = {};
        BufferView                                                 Readback       = {};
        ZEngine::Core::Containers::Array<uint32_t>                 Pixels         = {};
        uint32_t                                                   CameraStride   = 0;
        bool                                                       AllocatorReady = false;

        ~OffscreenGizmoCycle()
        {
            if (Vulkan.Device)
                vkDeviceWaitIdle(Vulkan.Device);
            Commands.reset();
            if (Pool)
                vkDestroyCommandPool(Vulkan.Device, Pool, nullptr);
            if (Fence)
                vkDestroyFence(Vulkan.Device, Fence, nullptr);
            DestroyTarget();
            Backend.Pipeline   = nullptr;
            Backend.Attachment = nullptr;
            Pipeline.Dispose();
            Cache->Shutdown();
            Attachment.reset();
            if (Descriptors)
                vkDestroyDescriptorPool(Vulkan.Device, Descriptors, nullptr);
            if (CameraLayout)
                vkDestroyDescriptorSetLayout(Vulkan.Device, CameraLayout, nullptr);
            for (const auto layout : EmptyLayouts)
                if (layout)
                    vkDestroyDescriptorSetLayout(Vulkan.Device, layout, nullptr);
            for (const auto module : DrawShader->ShaderModules)
                vkDestroyShaderModule(Vulkan.Device, module, nullptr);
            if (AllocatorReady)
            {
                Pass->Deinitialize(Device.get());
                Device->GpuMem.FreeBuffer(Camera);
                Device->GpuMem.FreeBuffer(Readback);
                Device->PendingFree.Drain(&Device->GpuMem, Vulkan.Device, UINT64_MAX);
                Device->GpuMem.Shutdown();
            }
            Vulkan.Destroy();
            EXPECT_EQ(Vulkan.ValidationErrors.load(std::memory_order_relaxed), 0u);
        }

        void InitializeLabelBindings()
        {
            auto&                                 shader = *DrawShader;
            const VkDescriptorSetLayoutCreateInfo empty  = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            for (auto& layout : EmptyLayouts)
                ASSERT_EQ(vkCreateDescriptorSetLayout(Vulkan.Device, &empty, nullptr, &layout), VK_SUCCESS);
            const VkDescriptorSetLayoutBinding bindings[] = {
                {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
                {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
                {.binding = 2,       .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
            };
            const VkDescriptorSetLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 3, .pBindings = bindings};
            ASSERT_EQ(vkCreateDescriptorSetLayout(Vulkan.Device, &layout_info, nullptr, &CameraLayout), VK_SUCCESS);
            shader.SetLayouts.init(&Arena, 3);
            shader.SetLayouts.push(EmptyLayouts[0]);
            shader.SetLayouts.push(EmptyLayouts[1]);
            shader.SetLayouts.push(CameraLayout);
            shader.PushConstants.init(&Arena, 1);
            shader.PushConstants.push({VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Renderers::GridLabelPushConstantData)});
            shader.DescriptorSetMap.init(&Arena, 4);
            shader.LayoutBindingSpecificationMap.init(&Arena, 1);
            shader.LayoutBindingSpecifications.init(&Arena, 3);
            shader.BindingsByName.init(&Arena, 4);
            const Specs::LayoutBindingSpecification specs[] = {
                {.Set = 2, .Binding = 0,   .Name = "SceneDepth", .DescriptorTypeValue = Specs::DescriptorType::SAMPLED_IMAGE, .Flags = Specs::ShaderStageFlags::FRAGMENT},
                {.Set = 2, .Binding = 1,    .Name = "FontAtlas", .DescriptorTypeValue = Specs::DescriptorType::SAMPLED_IMAGE, .Flags = Specs::ShaderStageFlags::FRAGMENT},
                {.Set = 2, .Binding = 2, .Name = "AtlasSampler",       .DescriptorTypeValue = Specs::DescriptorType::SAMPLER, .Flags = Specs::ShaderStageFlags::FRAGMENT},
            };
            auto& reflected = shader.LayoutBindingSpecificationMap[2];
            reflected.init(&Arena, 3);
            for (const auto& spec : specs)
            {
                shader.BindingsByName.insert(spec.Name, spec);
                shader.LayoutBindingSpecifications.push(spec);
                reflected.push(spec);
            }
            const VkDescriptorPoolSize sizes[] = {
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 6},
                {      VK_DESCRIPTOR_TYPE_SAMPLER, 3}
            };
            const VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 9, .poolSizeCount = 2, .pPoolSizes = sizes};
            ASSERT_EQ(vkCreateDescriptorPool(Vulkan.Device, &pool_info, nullptr, &Descriptors), VK_SUCCESS);
            for (uint32_t set = 0; set < 3; ++set)
            {
                auto& sets = shader.DescriptorSetMap[set];
                sets.init(&Arena, 3, 3);
                const VkDescriptorSetLayout       layouts[]  = {shader.SetLayouts[set], shader.SetLayouts[set], shader.SetLayouts[set]};
                const VkDescriptorSetAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = Descriptors, .descriptorSetCount = 3, .pSetLayouts = layouts};
                ASSERT_EQ(vkAllocateDescriptorSets(Vulkan.Device, &allocation, sets.data()), VK_SUCCESS);
            }
        }

        void Initialize(Renderers::GizmoPass& pass, const std::vector<uint32_t> (&binaries)[2], bool graph_execution = false, bool label_shader = false)
        {
            Pass = &pass;
            ASSERT_NE(Vulkan.Device, VK_NULL_HANDLE);
            Arena.Initialize(graph_execution ? ZMega(512) : ZMega(8), 0, "GizmoOffscreen");
            ASSERT_NE(Arena.m_memory, nullptr);
            Device->Arena                                 = &Arena;
            Device->LogicalDevice                         = Vulkan.Device;
            Device->GraphicFamilyIndex                    = Vulkan.QueueFamily;
            Device->SwapchainPtr                          = &Swapchain;
            Device->RRM                                   = Manager.get();
            Device->PipelineStateCache                    = Cache.get();
            Device->PhysicalDeviceSupportDynamicRendering = Vulkan.DynamicRenderingEnabled;
            if (Vulkan.DynamicRenderingEnabled)
                ASSERT_TRUE(RRMUploadBatchTestHelper::InitializeDynamicRendering(*Device));
            Swapchain.BufferredFrameCount = Renderers::GizmoPass::FramesInFlight;
            Swapchain.CurrentFrame        = &CurrentFrame;
            Device->GpuMem.Initialize(Vulkan.PhysicalDevice, Vulkan.Device, Vulkan.Instance, false, false);
            AllocatorReady = true;
            RRMUploadBatchTestHelper::InitializeHostWrites(*Manager, *Device);
            Cache->Initialize(Device.get());
            if (!label_shader)
                Pass->Initialize(Device.get());
            vkGetDeviceQueue(Vulkan.Device, Vulkan.QueueFamily, 0, &Queue);
            const VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = Vulkan.QueueFamily};
            ASSERT_EQ(vkCreateCommandPool(Vulkan.Device, &pool_info, nullptr, &Pool), VK_SUCCESS);
            Commands                           = std::make_unique<ZEngine::Hardwares::CommandBuffer>(Device.get(), Pool, ZEngine::Rendering::QueueType::GRAPHIC_QUEUE, true);
            const VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            ASSERT_EQ(vkCreateFence(Vulkan.Device, &fence_info, nullptr, &Fence), VK_SUCCESS);

            // Seed only the fixture-owned compiled shader/descriptor metadata.
            // The graph still constructs and bakes its actual backend passes.
            if (graph_execution)
            {
                Device->ShaderManager.Initialize(&Arena, 1);
                Device->ShaderCaches.init(&Arena, 4);
                const auto handle = Device->ShaderManager.Create();
                ASSERT_TRUE(handle);
                auto* graph_shader = Device->ShaderManager.Access(handle);
                ASSERT_NE(graph_shader, nullptr);
                DrawShader = graph_shader;
                Device->ShaderCaches.insert(label_shader ? "grid_labels" : "gizmo", handle);
            }
            auto& shader                = *DrawShader;
            shader.m_specification.Name = label_shader ? "grid_labels" : "gizmo";
            shader.ShaderModules.init(&Arena, 2);
            shader.ShaderCreateInfos.init(&Arena, 2);
            ASSERT_NE(shader.ShaderModules.data(), nullptr);
            ASSERT_NE(shader.ShaderCreateInfos.data(), nullptr);
            for (uint32_t index = 0; index < 2; ++index)
            {
                const auto&                    code        = binaries[index];
                VkShaderModule                 module      = VK_NULL_HANDLE;
                const VkShaderModuleCreateInfo module_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = code.size() * sizeof(uint32_t), .pCode = code.data()};
                ASSERT_EQ(vkCreateShaderModule(Vulkan.Device, &module_info, nullptr, &module), VK_SUCCESS);
                shader.ShaderModules.push(module);
                shader.ShaderCreateInfos.push({.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = index == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT, .module = module, .pName = "main"});
            }
            if (label_shader)
            {
                ASSERT_NO_FATAL_FAILURE(InitializeLabelBindings());
            }
            else
            {
                VkDescriptorSetLayoutBinding          camera_binding = {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT};
                const VkDescriptorSetLayoutCreateInfo layout_info    = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &camera_binding};
                ASSERT_EQ(vkCreateDescriptorSetLayout(Vulkan.Device, &layout_info, nullptr, &CameraLayout), VK_SUCCESS);
                shader.SetLayouts.init(&Arena, 1);
                shader.SetLayouts.push(CameraLayout);
                shader.PushConstants.init(&Arena, 1);
                shader.PushConstants.push({VK_SHADER_STAGE_VERTEX_BIT, 0, 2 * sizeof(float)});
                shader.DescriptorSetMap.init(&Arena, 1);
                shader.LayoutBindingSpecificationMap.init(&Arena, 1);
                shader.BindingsByName.init(&Arena, 4);
                const Specs::LayoutBindingSpecification camera_spec = {.Set = 0, .Binding = 0, .Name = "UBCamera", .DescriptorTypeValue = Specs::DescriptorType::UNIFORM_BUFFER_DYNAMIC, .Flags = Specs::ShaderStageFlags::VERTEX};
                shader.BindingsByName.insert("UBCamera", camera_spec);
                auto& bindings = shader.LayoutBindingSpecificationMap[0];
                bindings.init(&Arena, 1);
                bindings.push(camera_spec);
                auto& sets = shader.DescriptorSetMap[0];
                sets.init(&Arena, 3, 3);
                const VkDescriptorPoolSize       pool_size        = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 3};
                const VkDescriptorPoolCreateInfo descriptors_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 3, .poolSizeCount = 1, .pPoolSizes = &pool_size};
                ASSERT_EQ(vkCreateDescriptorPool(Vulkan.Device, &descriptors_info, nullptr, &Descriptors), VK_SUCCESS);
                const VkDescriptorSetLayout       layouts[3] = {CameraLayout, CameraLayout, CameraLayout};
                const VkDescriptorSetAllocateInfo sets_info  = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = Descriptors, .descriptorSetCount = 3, .pSetLayouts = layouts};
                ASSERT_EQ(vkAllocateDescriptorSets(Vulkan.Device, &sets_info, sets.data()), VK_SUCCESS);
            }
            VkPhysicalDeviceProperties properties = {};
            vkGetPhysicalDeviceProperties(Vulkan.PhysicalDevice, &properties);
            const auto alignment = properties.limits.minUniformBufferOffsetAlignment;
            CameraStride         = static_cast<uint32_t>((sizeof(Renderers::UBOCameraLayout) + alignment - 1) / alignment * alignment);
            Camera               = Device->GpuMem.AllocateBuffer(CameraStride * 2, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, GpuMemoryDomain::HostUniform, "gizmo_test_camera");
            ASSERT_TRUE(Camera);
            const Renderers::UBOCameraLayout camera = {};
            Manager->UpdateBuffer(Camera, &camera, sizeof(camera));
            Manager->UpdateBuffer(Camera, &camera, sizeof(camera), CameraStride);
            for (auto& heap : Device->FrameHeaps)
                heap.Handle = Camera.Handle;
            const uint32_t pixel_capacity = label_shader ? 1024 * 768 : 128 * 96;
            Readback                      = Device->GpuMem.AllocateBuffer(pixel_capacity * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT, GpuMemoryDomain::HostReadback, "gizmo_test_readback");
            ASSERT_TRUE(Readback);
            Pixels.init(&Arena, pixel_capacity, pixel_capacity);

            if (graph_execution)
                return;

            Specs::AttachmentSpecification attachment_spec = {};
            attachment_spec.ColorsMap.init(&Arena, 1);
            attachment_spec.ColorsMap.insert(0, {.Format = Specs::ImageFormat::R8G8B8A8_UNORM, .Load = Specs::LoadOperation::LOAD, .Store = Specs::StoreOperation::STORE, .Initial = Specs::ImageLayout::COLOR_ATTACHMENT_OPTIMAL, .Final = Specs::ImageLayout::COLOR_ATTACHMENT_OPTIMAL, .ReferenceLayout = Specs::ImageLayout::COLOR_ATTACHMENT_OPTIMAL});
            Attachment           = std::make_unique<Renderers::RenderPasses::Attachment>(Device.get(), std::move(attachment_spec));
            Pipeline.Device      = Device.get();
            Pipeline.Shader      = DrawShader;
            Pipeline.Description = Pass->BuildGraphicsPipelineDescription(&Arena);
            Pipeline.Attachment  = Attachment.get();
            Pipeline.Bake();
            ASSERT_NE(Pipeline.Handle, VK_NULL_HANDLE);
            EXPECT_EQ(Attachment->GetHandle() == VK_NULL_HANDLE, Vulkan.DynamicRenderingEnabled);
            Backend.InitializeBindings(Device.get());
            Backend.Pipeline                                    = &Pipeline;
            Backend.Attachment                                  = Attachment.get();
            // Descriptor replay owns one stable binding-name copy. Warm that
            // persistent state before measuring repeated frame preparation.
            CurrentFrame.Index                                  = 0;
            ZEngine::Rendering::Scenes::SceneData initial_scene = {};
            Pass->Prepare(Device.get(), &initial_scene, nullptr, &Backend);

            Graph->FrameArena.Initialize(ZMega(1), 0, "GizmoOffscreenGraph");
            Graph->Passes.init(&Graph->FrameArena, 1, 1);
            Graph->Resources.init(&Graph->FrameArena, 1);
            Graph->ResourceIndex.init(&Graph->FrameArena, 4);
            Graph->Passes[0].Reads.init(&Graph->FrameArena, 1);
            Graph->Passes[0].Writes.init(&Graph->FrameArena, 8);
            Builder.Initialize(Graph.get());
            Builder.CurrentPass = 0;
        }

        void DestroyTarget()
        {
            if (Framebuffer.Handle)
                vkDestroyFramebuffer(Vulkan.Device, Framebuffer.Handle, nullptr);
            if (ImageView)
                vkDestroyImageView(Vulkan.Device, ImageView, nullptr);
            if (Image)
                vmaDestroyImage(Device->GpuMem.Allocator, Image, ImageMemory);
            Framebuffer.Handle = VK_NULL_HANDLE;
            ImageView          = VK_NULL_HANDLE;
            Image              = VK_NULL_HANDLE;
            ImageMemory        = nullptr;
        }

        void Render(const Renderers::GizmoFrameData& frame, uint8_t slot, uint32_t width, uint32_t height, bool enabled)
        {
            DestroyTarget(); // Every prior submission has completed its exact fence.
            const VkImageCreateInfo image_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {width, height, 1},
                           .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
            };
            const VmaAllocationCreateInfo allocation_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
            ASSERT_EQ(vmaCreateImage(Device->GpuMem.Allocator, &image_info, &allocation_info, &Image, &ImageMemory, nullptr), VK_SUCCESS);
            const VkImageSubresourceRange range     = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            const VkImageViewCreateInfo   view_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = Image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM, .subresourceRange = range};
            ASSERT_EQ(vkCreateImageView(Vulkan.Device, &view_info, nullptr, &ImageView), VK_SUCCESS);
            if (!Vulkan.DynamicRenderingEnabled)
            {
                const VkFramebufferCreateInfo framebuffer_info = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = Attachment->GetHandle(), .attachmentCount = 1, .pAttachments = &ImageView, .width = width, .height = height, .layers = 1};
                ASSERT_EQ(vkCreateFramebuffer(Vulkan.Device, &framebuffer_info, nullptr, &Framebuffer.Handle), VK_SUCCESS);
            }
            Backend.RenderAreaWidth  = width;
            Backend.RenderAreaHeight = height;
            CurrentFrame.Index       = slot;
            Pass->SetFrame(frame);
            Graph->Passes[0].Writes.clear();
            const size_t arena_before = Arena.m_current_offset;
            ASSERT_EQ(Pass->Register(Device.get(), "Gizmo Pass", {.FrameIndex = slot}, &Builder, nullptr), enabled);
            ZEngine::Rendering::Scenes::SceneData scene = {};
            scene.CameraHeapOffset                      = (slot % 2) * CameraStride;
            if (enabled)
                Pass->Prepare(Device.get(), &scene, nullptr, &Backend);
            Commands->Begin();
            auto barrier = [&](VkImageLayout before, VkImageLayout after, VkAccessFlags source, VkAccessFlags destination, VkPipelineStageFlags from, VkPipelineStageFlags to) {
                const VkImageMemoryBarrier transition = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = source, .dstAccessMask = destination, .oldLayout = before, .newLayout = after, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = Image, .subresourceRange = range};
                vkCmdPipelineBarrier(Commands->GetHandle(), from, to, 0, 0, nullptr, 0, nullptr, 1, &transition);
            };
            barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            const VkClearColorValue black = {
                {0.f, 0.f, 0.f, 1.f}
            };
            vkCmdClearColorImage(Commands->GetHandle(), Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
            barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            if (Vulkan.DynamicRenderingEnabled)
            {
                // The graph owns the dynamic scope in production. Supply only
                // that scope here and exercise the real body-only callback.
                const VkRenderingAttachmentInfo color     = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = ImageView, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
                const VkRenderingInfo           rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {.extent = {width, height}}, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &color};
                Commands->BeginDynamicRendering(rendering);
                EXPECT_EQ(Pass->RecordDraw(Device.get(), nullptr, &scene, &Backend, &Framebuffer, Commands.get()), enabled);
                Commands->EndDynamicRendering();
                EXPECT_EQ(Framebuffer.Handle, VK_NULL_HANDLE);
            }
            else
                Pass->Execute(Device.get(), nullptr, &scene, &Backend, &Framebuffer, Commands.get());
            barrier(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            const VkBufferImageCopy copy = {
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                  .imageExtent = {width, height, 1}
            };
            vkCmdCopyImageToBuffer(Commands->GetHandle(), Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Readback.Handle, 1, &copy);
            const VkBufferMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .buffer = Readback.Handle, .offset = 0, .size = width * height * sizeof(uint32_t)};
            vkCmdPipelineBarrier(Commands->GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host, 0, nullptr);
            Commands->End();
            EXPECT_EQ(Arena.m_current_offset, arena_before); // The real pass does not grow its CPU arena.
            const auto         command    = Commands->GetHandle();
            const VkSubmitInfo submission = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command};
            ASSERT_EQ(vkQueueSubmit(Queue, 1, &submission, Fence), VK_SUCCESS);
            ASSERT_EQ(vkWaitForFences(Vulkan.Device, 1, &Fence, VK_TRUE, 10'000'000'000), VK_SUCCESS);
            ASSERT_EQ(vmaCopyAllocationToMemory(Device->GpuMem.Allocator, Readback.Allocation, 0, Pixels.data(), width * height * sizeof(uint32_t)), VK_SUCCESS);
            ASSERT_EQ(vkResetFences(Vulkan.Device, 1, &Fence), VK_SUCCESS);
            ASSERT_EQ(vkResetCommandBuffer(command, 0), VK_SUCCESS);
            Commands->SetState(ZEngine::Hardwares::CommandBufferState::Idle);
        }

        uint32_t Pixel(float x, float y) const
        {
            const auto column = static_cast<uint32_t>((x + 1.f) * 0.5f * Backend.RenderAreaWidth);
            const auto row    = static_cast<uint32_t>((y + 1.f) * 0.5f * Backend.RenderAreaHeight);
            return Pixels[row * Backend.RenderAreaWidth + column];
        }
    };
    void RunOffscreenGizmoCycles(bool dynamic_rendering)
    {
        GlslangLifetime compiler = {};
        ASSERT_TRUE(compiler.Ready);
        std::vector<uint32_t> binaries[2] = {};
        ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Vertex, sizeof(ZEngine::Tests::GizmoShaders::Vertex) - 1, EShLangVertex, binaries[0]));
        ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Fragment, sizeof(ZEngine::Tests::GizmoShaders::Fragment) - 1, EShLangFragment, binaries[1]));
        Renderers::GizmoPass pass             = {};
        auto                 frame            = std::make_unique<Renderers::GizmoFrameData>();
        constexpr uint32_t   black            = 0xff000000u;
        constexpr uint32_t   colors[3]        = {0xff0000ffu, 0xff00ff00u, 0xffff0000u};
        uint32_t             validated_cycles = 0;
        for (uint32_t cycle = 0; cycle < 3; ++cycle)
        {
            SCOPED_TRACE(cycle);
            OffscreenGizmoCycle owner = {};
            ASSERT_TRUE(owner.Vulkan.Create(true, dynamic_rendering));
            if (dynamic_rendering && !owner.Vulkan.DynamicRenderingEnabled)
                GTEST_SKIP() << "The selected Vulkan device does not support dynamic rendering";
            ASSERT_NO_FATAL_FAILURE(owner.Initialize(pass, binaries));
            validated_cycles    += owner.Vulkan.ValidationEnabled ? 1u : 0u;
            const auto pipeline  = owner.Pipeline.Handle;
            ASSERT_NO_FATAL_FAILURE(owner.Render(*frame, 0, 96, 64, false));
            EXPECT_EQ(owner.Pixel(-.55f, -.5f), black);
            for (uint32_t step = 0; step < 6; ++step)
            {
                SCOPED_TRACE(step);
                const auto color     = (step + cycle) % 3;
                frame->TriangleCount = frame->LineCount = frame->SpriteCount = 1;
                frame->Triangles[0]                                          = {
                    .A = { -.9f, -.8f, .5f},
                      .B = { -.2f, -.8f, .5f},
                      .C = {-.55f, .15f, .5f},
                      .Color = colors[color]
                };
                frame->Lines[0] = {
                    .A = { .2f, -.5f, .5f},
                      .B = {.85f, -.5f, .5f},
                      .Width = 6.f, .Color = colors[(color + 1) % 3]
                };
                frame->Sprites[0] = {
                    .Position = {.5f, .5f, .5f},
                      .Size = 12.f, .Fill = colors[(color + 2) % 3]
                };
                ASSERT_NO_FATAL_FAILURE(owner.Render(*frame, step % 3, step % 2 ? 128 : 96, step % 2 ? 96 : 64, true));
                EXPECT_EQ(owner.Pixel(-.55f, -.5f), frame->Triangles[0].Color);
                EXPECT_EQ(owner.Pixel(.5f, -.5f), frame->Lines[0].Color);
                EXPECT_EQ(owner.Pixel(.5f, .5f), frame->Sprites[0].Fill);
                EXPECT_EQ(owner.Pixel(-.9f, .9f), black);
                EXPECT_EQ(owner.Pipeline.Handle, pipeline); // Resize does not rebuild the compatible PSO.
            }
            frame->Reset();
            ASSERT_NO_FATAL_FAILURE(owner.Render(*frame, 2, 128, 96, false));
            EXPECT_EQ(owner.Pixel(-.55f, -.5f), black);
            EXPECT_EQ(owner.Pixel(.5f, -.5f), black);
            EXPECT_EQ(owner.Pixel(.5f, .5f), black);
        }
        ::testing::Test::RecordProperty("fresh_graphics_device_cycles", 3);
        ::testing::Test::RecordProperty("drawn_frames", 18);
        ::testing::Test::RecordProperty("disabled_frames", 6);
        ::testing::Test::RecordProperty("upload_slots_per_cycle", 3);
        ::testing::Test::RecordProperty("target_extent_count", 2);
        ::testing::Test::RecordProperty("validated_graphics_device_cycles", validated_cycles);
        ::testing::Test::RecordProperty("dynamic_rendering", dynamic_rendering ? 1 : 0);
    }

    void RunJoinedGizmoStroke(bool dynamic_rendering)
    {
        GlslangLifetime compiler = {};
        ASSERT_TRUE(compiler.Ready);
        std::vector<uint32_t> binaries[2] = {};
        ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Vertex, sizeof(ZEngine::Tests::GizmoShaders::Vertex) - 1, EShLangVertex, binaries[0]));
        ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Fragment, sizeof(ZEngine::Tests::GizmoShaders::Fragment) - 1, EShLangFragment, binaries[1]));
        Renderers::GizmoPass pass  = {};
        OffscreenGizmoCycle  owner = {};
        ASSERT_TRUE(owner.Vulkan.Create(true, dynamic_rendering, true));
        if (dynamic_rendering && !owner.Vulkan.DynamicRenderingEnabled)
            GTEST_SKIP() << "The selected Vulkan device does not support dynamic rendering";
        ASSERT_NO_FATAL_FAILURE(owner.Initialize(pass, binaries));
        auto               frame    = std::make_unique<Renderers::GizmoFrameData>();
        constexpr uint32_t segments = 16, rgb = 0x00e39d57u;
        uint32_t           submissions = 0;
        for (uint32_t dpi : {1u, 2u})
            for (uint32_t opacity : {255u, 51u})
            {
                const uint32_t              width = 64 * dpi, height = 48 * dpi, radius = 12 * dpi;
                ZEngine::Core::Maths::Vec3f points[segments + 1]  = {};
                Renderers::GizmoJoinNormal  normals[segments + 1] = {};
                for (uint32_t i = 0; i < segments; ++i)
                {
                    const double angle = 2.0 * 3.14159265358979323846 * i / segments;
                    const float  x = float(std::cos(angle)), y = float(std::sin(angle));
                    points[i]  = {(1.f + 2.f * radius * x) / width, (1.f + 2.f * radius * y) / height, .5f};
                    normals[i] = Renderers::GizmoJoinNormal::FromUnit(-double(y), double(x));
                }
                points[segments]  = points[0];
                normals[segments] = normals[0];
                frame->LineCount  = segments;
                for (uint32_t i = 0; i < segments; ++i)
                    frame->Lines[i] = {.A = points[i], .B = points[i + 1], .Width = 3.5f * dpi, .Color = (opacity << 24) | rgb, .RoundCaps = true, .StartJoinNormal = normals[i], .EndJoinNormal = normals[i + 1]};
                ASSERT_NO_FATAL_FAILURE(owner.Render(*frame, submissions++ % 3, width, height, true));
                const uint32_t coordinates[4][2] = {
                    {width / 2 + radius,          height / 2},
                    {         width / 2, height / 2 + radius},
                    {width / 2 - radius,          height / 2},
                    {         width / 2, height / 2 - radius}
                };
                for (const auto& coordinate : coordinates)
                {
                    const auto pixel = owner.Pixels[coordinate[1] * width + coordinate[0]];
                    for (uint32_t shift : {0u, 8u, 16u})
                        EXPECT_NEAR(float((pixel >> shift) & 255u), float((rgb >> shift) & 255u) * opacity / 255.f, 1.1f) << "dpi=" << dpi << " opacity=" << opacity << " point=" << coordinate[0] << ',' << coordinate[1];
                }
            }
        ::testing::Test::RecordProperty("joined_stroke_submissions", submissions);
        ::testing::Test::RecordProperty("validated_graphics_device_cycles", owner.Vulkan.ValidationEnabled ? 1 : 0);
        ::testing::Test::RecordProperty("synchronization_validated_graphics_device_cycles", owner.Vulkan.SynchronizationValidationEnabled ? 1 : 0);
    }
} // namespace

TEST_F(GpuAllocatorTest, GizmoPassDrawsAfterResizeAndFreshDevices)
{
    ASSERT_NE(s_allocator, nullptr);
    RunOffscreenGizmoCycles(false);
}

TEST_F(GpuAllocatorTest, DynamicGizmoPassDrawsAfterResizeAndFreshDevices)
{
    ASSERT_NE(s_allocator, nullptr);
    RunOffscreenGizmoCycles(true);
}

TEST_F(GpuAllocatorTest, GizmoJoinedStrokesHaveNoOpaqueGapsOrTranslucentOverlap)
{
    ASSERT_NE(s_allocator, nullptr);
    RunJoinedGizmoStroke(false);
}

TEST_F(GpuAllocatorTest, DynamicGizmoJoinedStrokesHaveNoOpaqueGapsOrTranslucentOverlap)
{
    ASSERT_NE(s_allocator, nullptr);
    RunJoinedGizmoStroke(true);
}

#include "GizmoGraphFixture.h"
#include "GridLabelGraphFixture.h"

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
        Device->GraphicFamilyIndex = s_vk->QueueFamily;
        Device->SwapchainPtr       = &Swapchain;
        Device->GpuMem.Allocator   = s_allocator->Allocator;

        VkQueue queue              = VK_NULL_HANDLE;
        vkGetDeviceQueue(s_vk->Device, s_vk->QueueFamily, 0, &queue);
        VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = s_vk->QueueFamily};
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
