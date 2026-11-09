#include <SPIRV/GlslangToSpv.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/ECS/SceneGridSettings.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/Logging/LoggerConfiguration.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/Graphics/GizmoPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/GridLabelPass.h>
#include <ZEngine/Rendering/Renderers/Pipelines/PSOCache.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <ZEngine/Rendering/Shaders/Shader.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <gtest/gtest.h>
#include <stb/stb_image_write.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <thread>
#include "GizmoShaderSources.h"
#include "GridLabelShaderSources.h"

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Logging;
using ZEngine::Managers::AssetManager;
using ZEngine::Rendering::Renderers::Pipelines::PSOPreparationState;
using Bank               = ZEngine::Rendering::Renderers::RenderPasses::PassDescriptorBank;
using RRM                = ZEngine::Rendering::RenderResourceManager;
using RetirementQueue    = ZEngine::Hardwares::DeferredFreeQueue;
using RetirementEntry    = ZEngine::Hardwares::DeferredFreeEntry;
using NativeResourceType = ZEngine::Rendering::DeviceResourceType;

struct GpuAllocatorTestAccess
{
    static constexpr uint32_t Capacity = GpuAllocator::AllocationCapacity;
    static uint32_t           Reserve(GpuAllocator& allocator)
    {
        return allocator.ReserveAllocation();
    }
    static void Release(GpuAllocator& allocator, uint32_t record)
    {
        allocator.ReleaseAllocation(record, nullptr);
    }
};

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
    struct NativeMetadataOwner
    {
        HeadlessVulkan                                                      Vulkan  = {};
        std::unique_ptr<ZEngine::Hardwares::VulkanDevice>                   Device  = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
        std::unique_ptr<ZEngine::Rendering::Renderers::Pipelines::PSOCache> Cache   = std::make_unique<ZEngine::Rendering::Renderers::Pipelines::PSOCache>();
        VkSampler                                                           Sampler = VK_NULL_HANDLE;
        bool                                                                Initialize()
        {
            if (!Vulkan.Create(true))
            {
                return false;
            }
            Device->LogicalDevice  = Vulkan.Device;
            Device->PhysicalDevice = Vulkan.PhysicalDevice;
            vkGetPhysicalDeviceProperties(Vulkan.PhysicalDevice, &Device->PhysicalDeviceProperties.properties);
            Cache->Initialize(Device.get());
            return true;
        }
        ~NativeMetadataOwner()
        {
            Cache->Shutdown();
            if (Sampler)
            {
                vkDestroySampler(Vulkan.Device, Sampler, nullptr);
            }
            Vulkan.Destroy();
        }
    };

    struct FramebufferAdmissionFault
    {
        static inline thread_local VkResult   Result = VK_ERROR_OUT_OF_HOST_MEMORY;
        static inline thread_local uint32_t   Calls  = 0;
        static VKAPI_ATTR VkResult VKAPI_CALL Create(VkDevice, const VkFramebufferCreateInfo*, const VkAllocationCallbacks*, VkFramebuffer*)
        {
            ++Calls;
            return Result;
        }
    };

    struct BindlessTestTargets
    {
        VkDevice              Device  = VK_NULL_HANDLE;
        VkDescriptorSetLayout Layout  = VK_NULL_HANDLE;
        VkDescriptorPool      Pool    = VK_NULL_HANDLE;
        VkDescriptorSet       Sets[2] = {};

        ~BindlessTestTargets()
        {
            if (Pool)
            {
                vkDestroyDescriptorPool(Device, Pool, nullptr);
            }
            if (Layout)
            {
                vkDestroyDescriptorSetLayout(Device, Layout, nullptr);
            }
        }

        bool Initialize(ZEngine::Hardwares::VulkanDevice& device, uint32_t textures)
        {
            Device                                            = device.LogicalDevice;
            const VkDescriptorSetLayoutBinding    binding     = {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = textures, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
            const VkDescriptorSetLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &binding};
            if (vkCreateDescriptorSetLayout(Device, &layout_info, nullptr, &Layout) != VK_SUCCESS)
            {
                return false;
            }
            const VkDescriptorPoolSize       pool_size = {.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = textures * 2};
            const VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 2, .poolSizeCount = 1, .pPoolSizes = &pool_size};
            if (vkCreateDescriptorPool(Device, &pool_info, nullptr, &Pool) != VK_SUCCESS)
            {
                return false;
            }
            const VkDescriptorSetLayout       layouts[]  = {Layout, Layout};
            const VkDescriptorSetAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = Pool, .descriptorSetCount = 2, .pSetLayouts = layouts};
            if (vkAllocateDescriptorSets(Device, &allocation, Sets) != VK_SUCCESS)
            {
                return false;
            }
            for (const auto set : Sets)
            {
                if (!device.AddBindlessTextureSlotRequest({.Binding = 0, .DstSet = set}))
                {
                    return false;
                }
            }
            return device.SealBindlessTextureTargets();
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
    uint64_t LiveAllocationBytes()
    {
        VmaTotalStatistics statistics = {};
        vmaCalculateStatistics(allocator().Allocator, &statistics);
        return statistics.total.statistics.allocationBytes;
    }
};

MemoryManager*  GpuAllocatorTest::s_manager      = nullptr;
ArenaAllocator* GpuAllocatorTest::s_logger_arena = nullptr;
HeadlessVulkan* GpuAllocatorTest::s_vk           = nullptr;
GpuAllocator*   GpuAllocatorTest::s_allocator    = nullptr;

TEST_F(GpuAllocatorTest, RuntimeImageMetadataCannotRewindOrOverwriteRenderScratch)
{
    ASSERT_NE(s_allocator, nullptr);
    ArenaAllocator arena{};
    arena.Initialize(ZMega(2), 0);
    auto                                device = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    ZEngine::Hardwares::DeviceSwapchain swapchain{};
    device->Arena            = &arena;
    device->LogicalDevice    = s_vk->Device;
    device->SwapchainPtr     = &swapchain;
    device->GpuMem.Allocator = allocator().Allocator;
    device->ImageViewCacheSlab.Init(&arena, ZKilo(64));
    device->ImageBufferManager.Initialize(&arena, 16);
    device->GlobalTextures.Initialize(&arena, 16);
    const auto                                  before = arena.m_current_offset;
    ZEngine::Rendering::Textures::TextureHandle handle{};
    {
        const auto scratch    = ZGetScratch(&arena);
        auto*      completion = ZPushStructCtor<PaddedAtomic<uint32_t>>(&arena);
        completion->value.store(42);
        const auto  scratch_end = arena.m_current_offset;
        // The main/UI thread can request a preview while the render thread owns
        // an active temporary allocation scope on Device->Arena.
        std::thread ui([&] { handle = device->CreateTexture({.Width = 4, .Height = 4, .Format = ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM}); });
        ui.join();
        ASSERT_TRUE(handle.Valid());
        EXPECT_EQ(arena.m_current_offset, scratch_end);
        EXPECT_EQ(completion->value.load(), 42u);
        ZReleaseScratch(scratch);
    }
    EXPECT_EQ(arena.m_current_offset, before);
    auto* image = device->ImageBufferManager.Access(device->GlobalTextures.Access(handle)->BufferHandle);
    ASSERT_NE(image, nullptr);
    // Grow the actual persistent subresource cache after aggressively reusing
    // the render scratch that used to own it. Validate exact view reuse too.
    auto* overwritten = arena.Allocate(4096);
    std::memset(overwritten, 0xa5, 4096);
    const VkImageSubresourceRange range = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1};
    const auto                    view  = image->GetImageViewHandle(VK_IMAGE_VIEW_TYPE_2D_ARRAY, range);
    EXPECT_NE(view, VK_NULL_HANDLE);
    EXPECT_EQ(image->GetImageViewHandle(VK_IMAGE_VIEW_TYPE_2D_ARRAY, range), view);
    image->Dispose();
    device->PendingFree.Drain(&device->GpuMem, device->LogicalDevice, UINT64_MAX);
    device->ImageViewCacheSlab.Shutdown();
    device->GpuMem.Allocator = nullptr; // Borrowed from the fixture.
}

TEST_F(GpuAllocatorTest, CheckedImageViewsRefuseBeforeNativeCreationAndPreserveExistingCacheOnRetry)
{
    struct NativeFailures
    {
        static VKAPI_ATTR VkResult VKAPI_CALL OutOfMemory(VkDevice, const VkImageViewCreateInfo* info, const VkAllocationCallbacks*, VkImageView*)
        {
            EXPECT_EQ(info->viewType, VK_IMAGE_VIEW_TYPE_2D_ARRAY);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL Unexpected(VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView*)
        {
            ADD_FAILURE() << "Native creation must not run before admission or on a cache hit";
            return VK_ERROR_UNKNOWN;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL DeviceLost(VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView*)
        {
            return VK_ERROR_DEVICE_LOST;
        }
    };
    ArenaAllocator arena = {};
    arena.Initialize(ZMega(2), 0);
    auto                                device    = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    ZEngine::Hardwares::DeviceSwapchain swapchain = {};
    device->Arena                                 = &arena;
    device->LogicalDevice                         = s_vk->Device;
    device->SwapchainPtr                          = &swapchain;
    device->GpuMem.Allocator                      = allocator().Allocator;
    {
        ZEngine::Hardwares::ImageBuffer image = {};
        image.Specification                   = {.Width = 128, .Height = 128, .ImageFormat = VK_FORMAT_R8G8B8A8_UNORM, .ImageUsage = VK_IMAGE_USAGE_SAMPLED_BIT, .MipLevelCount = 8};
        VkResult status                       = VK_SUCCESS;
        ASSERT_TRUE(image.TryConstruct(device.get(), &status));
        const auto                    outgoing = image.GetImageViewHandle();
        const VkImageSubresourceRange range    = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1};
        EXPECT_EQ(image.TryGetImageViewHandle(VK_IMAGE_VIEW_TYPE_2D_ARRAY, range, &status, NativeFailures::Unexpected), VK_NULL_HANDLE);
        EXPECT_EQ(status, VK_ERROR_OUT_OF_HOST_MEMORY);
        device->ImageViewCacheSlab.Init(&arena, ZKilo(64));
        EXPECT_EQ(image.TryGetImageViewHandle(VK_IMAGE_VIEW_TYPE_2D_ARRAY, range, &status, NativeFailures::OutOfMemory), VK_NULL_HANDLE);
        EXPECT_EQ(status, VK_ERROR_OUT_OF_DEVICE_MEMORY);
        EXPECT_FALSE(device->IsDeviceLost.load());
        EXPECT_EQ(image.GetImageViewHandle(), outgoing);
        const auto first = image.TryGetImageViewHandle(VK_IMAGE_VIEW_TYPE_2D_ARRAY, range, &status);
        ASSERT_NE(first, VK_NULL_HANDLE);
        EXPECT_EQ(status, VK_SUCCESS);
        EXPECT_EQ(image.TryGetImageViewHandle(VK_IMAGE_VIEW_TYPE_2D_ARRAY, range, &status, NativeFailures::Unexpected), first);
        auto invalid         = range;
        invalid.baseMipLevel = UINT32_MAX;
        EXPECT_EQ(image.TryGetImageViewHandle(VK_IMAGE_VIEW_TYPE_2D_ARRAY, invalid, &status, NativeFailures::Unexpected), VK_NULL_HANDLE);
        invalid            = range;
        invalid.layerCount = UINT32_MAX;
        EXPECT_EQ(image.TryGetImageViewHandle(VK_IMAGE_VIEW_TYPE_2D_ARRAY, invalid, &status, NativeFailures::Unexpected), VK_NULL_HANDLE);
        invalid            = range;
        invalid.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        EXPECT_EQ(image.TryGetImageViewHandle(VK_IMAGE_VIEW_TYPE_2D_ARRAY, invalid, &status, NativeFailures::Unexpected), VK_NULL_HANDLE);
        EXPECT_EQ(image.TryGetImageViewHandle(VK_IMAGE_VIEW_TYPE_3D, range, &status, NativeFailures::Unexpected), VK_NULL_HANDLE);
        EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED);
        auto next         = range;
        next.baseMipLevel = 1;
        EXPECT_EQ(image.TryGetImageViewHandle(VK_IMAGE_VIEW_TYPE_2D_ARRAY, next, &status, NativeFailures::DeviceLost), VK_NULL_HANDLE);
        EXPECT_EQ(status, VK_ERROR_DEVICE_LOST);
        EXPECT_TRUE(device->IsDeviceLost.load());
        EXPECT_EQ(image.TryGetImageViewHandle(VK_IMAGE_VIEW_TYPE_2D_ARRAY, range, &status, NativeFailures::Unexpected), VK_NULL_HANDLE);
        EXPECT_EQ(status, VK_ERROR_DEVICE_LOST);
        device->IsDeviceLost.store(false); // Injected loss only; the real device remains usable.
        EXPECT_NE(image.TryGetImageViewHandle(VK_IMAGE_VIEW_TYPE_2D_ARRAY, next, &status), VK_NULL_HANDLE);
        uint32_t admitted = 2;
        for (const auto type : {VK_IMAGE_VIEW_TYPE_2D_ARRAY, VK_IMAGE_VIEW_TYPE_2D})
        {
            for (uint32_t base = 0; base < 8; ++base)
            {
                for (uint32_t count = 1; count <= 8 - base; ++count)
                {
                    if ((type == VK_IMAGE_VIEW_TYPE_2D_ARRAY && count == 1 && base < 2) || (type == VK_IMAGE_VIEW_TYPE_2D && base == 0 && count == 8))
                    {
                        continue;
                    }
                    const VkImageSubresourceRange candidate = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = base, .levelCount = count, .layerCount = 1};
                    if (admitted == 4)
                    {
                        // Fill the real shared slab before the first cache grow.
                        // Refusal must preserve the existing cache, then retry
                        // successfully after unrelated metadata is released.
                        std::array<void*, 1024> pressure = {};
                        for (auto& block : pressure)
                        {
                            block = device->ImageViewCacheSlab.TryAllocate(64, alignof(uint64_t));
                            if (!block)
                            {
                                break;
                            }
                        }
                        EXPECT_EQ(image.TryGetImageViewHandle(type, candidate, &status, NativeFailures::Unexpected), VK_NULL_HANDLE);
                        EXPECT_EQ(status, VK_ERROR_OUT_OF_HOST_MEMORY);
                        EXPECT_EQ(image.TryGetImageViewHandle(VK_IMAGE_VIEW_TYPE_2D_ARRAY, range, &status, NativeFailures::Unexpected), first);
                        EXPECT_EQ(status, VK_SUCCESS);
                        EXPECT_EQ(image.GetImageViewHandle(), outgoing);
                        for (auto* block : pressure)
                        {
                            if (block)
                            {
                                device->ImageViewCacheSlab.Free(block);
                            }
                        }
                    }
                    const auto view = image.TryGetImageViewHandle(type, candidate, &status, admitted < image.MaxCachedImageViews ? vkCreateImageView : NativeFailures::Unexpected);
                    if (admitted < image.MaxCachedImageViews)
                    {
                        ASSERT_NE(view, VK_NULL_HANDLE);
                        EXPECT_EQ(status, VK_SUCCESS);
                        ++admitted;
                    }
                    else
                    {
                        EXPECT_EQ(view, VK_NULL_HANDLE);
                        EXPECT_EQ(status, VK_ERROR_TOO_MANY_OBJECTS);
                    }
                }
            }
        }
        EXPECT_EQ(admitted, image.MaxCachedImageViews);
        EXPECT_EQ(image.TryGetImageViewHandle(VK_IMAGE_VIEW_TYPE_2D_ARRAY, range, &status, NativeFailures::Unexpected), first);
        EXPECT_EQ(image.GetImageViewHandle(), outgoing);
        image.Dispose();
        device->PendingFree.Drain(&device->GpuMem, device->LogicalDevice, UINT64_MAX);
    } // Release cache metadata before shutting down its slab.
    device->ImageViewCacheSlab.Shutdown();
    device->GpuMem.Allocator = nullptr;
    device.reset();
    arena.Shutdown();
}

TEST_F(GpuAllocatorTest, RecoverableTextureCreationRollsBackMetadataRefusalAndRetriesWithoutLeaking)
{
    ASSERT_NE(s_allocator, nullptr);
    ArenaAllocator arena = {};
    arena.Initialize(ZMega(2), 0);
    auto                                device    = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    ZEngine::Hardwares::DeviceSwapchain swapchain = {};
    device->SwapchainPtr                          = &swapchain;
    device->Arena                                 = &arena;
    device->PhysicalDevice                        = s_vk->PhysicalDevice;
    device->LogicalDevice                         = s_vk->Device;
    device->GpuMem.Allocator                      = allocator().Allocator;
    device->MaxGlobalTexture                      = 8;
    device->GlobalTextures.Initialize(&arena, 8);
    device->ImageBufferManager.Initialize(&arena, 8);
    // Exhaust the real bounded image-allocation ledger without allocating
    // thousands of driver images. This is the production metadata refusal path.
    std::array<uint32_t, GpuAllocatorTestAccess::Capacity> records = {};
    for (auto& record : records)
    {
        record = GpuAllocatorTestAccess::Reserve(device->GpuMem);
        ASSERT_NE(record, UINT32_MAX);
    }
    const auto                                                     live_before = LiveAllocationBytes();
    const ZEngine::Rendering::Specifications::TextureSpecification spec        = {.Width = 4, .Height = 4, .Format = ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM};
    VkResult                                                       status      = VK_SUCCESS;
    for (uint32_t attempt = 0; attempt < 3; ++attempt)
    {
        EXPECT_FALSE(device->TryCreateTexture(spec, "refused candidate target", 0, &status));
        EXPECT_EQ(status, VK_ERROR_OUT_OF_HOST_MEMORY);
        EXPECT_EQ(device->GlobalTextures.Size(), 0u);
        EXPECT_EQ(device->ImageBufferManager.Size(), 0u);
        EXPECT_EQ(LiveAllocationBytes(), live_before);
        EXPECT_FALSE(device->IsDeviceLost.load());
    }
    for (auto record : records)
    {
        GpuAllocatorTestAccess::Release(device->GpuMem, record);
    }
    auto handle = device->TryCreateTexture(spec, "retried candidate target", 0, &status);
    ASSERT_TRUE(handle);
    EXPECT_EQ(status, VK_SUCCESS);
    auto* texture = device->GlobalTextures.Access(handle);
    ASSERT_NE(texture, nullptr);
    auto  image_handle = texture->BufferHandle;
    auto* image        = device->ImageBufferManager.Access(image_handle);
    ASSERT_NE(image, nullptr);
    EXPECT_TRUE(image->GetBuffer());
    // Refusing to construct over live storage must preserve the exact version.
    const auto previous = image->GetBuffer().Handle;
    EXPECT_FALSE(image->TryConstruct(device.get(), &status));
    EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED);
    EXPECT_EQ(image->GetBuffer().Handle, previous);
    image->Dispose();
    device->ImageBufferManager.Remove(image_handle);
    device->GlobalTextures.Remove(handle);
    device->PendingFree.Drain(&device->GpuMem, s_vk->Device, UINT64_MAX);
    EXPECT_EQ(LiveAllocationBytes(), live_before);
    device->GpuMem.Allocator = nullptr;
}

TEST_F(GpuAllocatorTest, RecoverableTextureCreationPreservesHandleTablesOnInvalidCapacityAndLostDevice)
{
    ASSERT_NE(s_allocator, nullptr);
    ArenaAllocator arena = {};
    arena.Initialize(ZMega(2), 0);
    auto                                device    = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    ZEngine::Hardwares::DeviceSwapchain swapchain = {};
    device->SwapchainPtr                          = &swapchain;
    device->Arena                                 = &arena;
    device->PhysicalDevice                        = s_vk->PhysicalDevice;
    device->LogicalDevice                         = s_vk->Device;
    device->GpuMem.Allocator                      = allocator().Allocator;
    device->MaxGlobalTexture                      = 2;
    device->GlobalTextures.Initialize(&arena, 2);
    device->ImageBufferManager.Initialize(&arena, 1);
    auto     spec    = ZEngine::Rendering::Specifications::TextureSpecification{.Width = 4, .Height = 4, .Format = ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM};
    VkResult status  = VK_SUCCESS;
    auto     invalid = spec;
    invalid.Width    = 0;
    EXPECT_FALSE(device->TryCreateTexture(invalid, nullptr, 0, &status));
    EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED);
    invalid           = spec;
    invalid.IsCubemap = true;
    EXPECT_FALSE(device->TryCreateTexture(invalid, nullptr, 0, &status));
    EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED);
    invalid       = spec;
    invalid.Width = UINT32_MAX;
    EXPECT_FALSE(device->TryCreateTexture(invalid, nullptr, 0, &status));
    EXPECT_EQ(status, VK_ERROR_FORMAT_NOT_SUPPORTED);
    EXPECT_FALSE(device->TryCreateTexture(spec, nullptr, UINT32_MAX, &status));
    EXPECT_EQ(status, VK_ERROR_TOO_MANY_OBJECTS);
    EXPECT_EQ(device->GlobalTextures.Size(), 0u);
    EXPECT_EQ(device->ImageBufferManager.Size(), 0u);
    device->IsDeviceLost.store(true);
    EXPECT_FALSE(device->TryCreateTexture(spec, nullptr, 0, &status));
    EXPECT_EQ(status, VK_ERROR_DEVICE_LOST);
    EXPECT_TRUE(device->IsDeviceLost.load());
    device->IsDeviceLost.store(false);
    auto first = device->TryCreateTexture(spec, nullptr, 0, &status);
    ASSERT_TRUE(first);
    const auto live_before = LiveAllocationBytes();
    // Global texture slot is available, but the independent image table is full.
    EXPECT_FALSE(device->TryCreateTexture(spec, nullptr, 0, &status));
    EXPECT_EQ(status, VK_ERROR_TOO_MANY_OBJECTS);
    EXPECT_EQ(device->GlobalTextures.Size(), 1u);
    EXPECT_EQ(device->ImageBufferManager.Size(), 1u);
    EXPECT_EQ(LiveAllocationBytes(), live_before);
    auto image_handle = device->GlobalTextures.Access(first)->BufferHandle;
    device->ImageBufferManager.Access(image_handle)->Dispose();
    device->ImageBufferManager.Remove(image_handle);
    device->GlobalTextures.Remove(first);
    device->PendingFree.Drain(&device->GpuMem, s_vk->Device, UINT64_MAX);
    EXPECT_EQ(device->GlobalTextures.Size(), 0u);
    EXPECT_EQ(device->ImageBufferManager.Size(), 0u);
    device->GpuMem.Allocator = nullptr;
}

TEST_F(GpuAllocatorTest, CompatibilityRenderPassAdmissionUsesActualDeviceLimitsAndRejectsUnsupportedFormats)
{
    NativeMetadataOwner owner = {};
    ASSERT_TRUE(owner.Initialize());
    ZEngine::Rendering::Renderers::Pipelines::PSOCompatibilityRenderPassKey key = {.AttachmentCount = 9, .ColorAttachmentCount = 8, .DepthAttachmentIndex = 8};
    for (uint32_t i = 0; i < 8; ++i)
    {
        key.Attachments[i] = {.Format = VK_FORMAT_R8G8B8A8_UNORM, .ReferenceLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    }
    key.Attachments[8] = {.Format = VK_FORMAT_D16_UNORM, .ReferenceLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkResult   status  = VK_SUCCESS;
    const auto full    = owner.Cache->TryGetOrCreateCompatibilityRenderPass(key, &status);
    if (owner.Device->PhysicalDeviceProperties.properties.limits.maxColorAttachments >= 8)
    {
        ASSERT_NE(full, VK_NULL_HANDLE);
        EXPECT_EQ(status, VK_SUCCESS);
        EXPECT_EQ(owner.Cache->TryGetOrCreateCompatibilityRenderPass(key, &status), full);
    }
    else
    {
        EXPECT_EQ(full, VK_NULL_HANDLE);
        EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED);
    }
    ZEngine::Rendering::Renderers::Pipelines::PSOCompatibilityRenderPassKey empty = {};
    const auto                                                              zero  = owner.Cache->TryGetOrCreateCompatibilityRenderPass(empty, &status);
    ASSERT_NE(zero, VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_SUCCESS);
    auto unsupported                 = empty;
    unsupported.AttachmentCount      = 1;
    unsupported.ColorAttachmentCount = 1;
    unsupported.Attachments[0]       = {.Format = VK_FORMAT_BC1_RGB_UNORM_BLOCK, .ReferenceLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    EXPECT_EQ(owner.Cache->TryGetOrCreateCompatibilityRenderPass(unsupported, &status), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_FORMAT_NOT_SUPPORTED);
    EXPECT_EQ(owner.Cache->TryGetOrCreateCompatibilityRenderPass(empty, &status), zero);
    // Both unsupported attempts must release their unpublished cache reservation.
    EXPECT_EQ(owner.Cache->TryGetOrCreateCompatibilityRenderPass(unsupported, &status), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_FORMAT_NOT_SUPPORTED);
    EXPECT_EQ(owner.Vulkan.ValidationErrors.load(), 0u);
}

TEST_F(GpuAllocatorTest, DescriptorLayoutAdmissionRetainsRealImmutableSamplersAndRefusesUnsupportedBindingFeatures)
{
    NativeMetadataOwner owner = {};
    ASSERT_TRUE(owner.Initialize());
    const VkSamplerCreateInfo sampler = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .maxLod = VK_LOD_CLAMP_NONE};
    ASSERT_EQ(vkCreateSampler(owner.Vulkan.Device, &sampler, nullptr, &owner.Sampler), VK_SUCCESS);
    const VkDescriptorSetLayoutBinding bindings[2] = {
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &owner.Sampler},
        {1,         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,   VK_SHADER_STAGE_VERTEX_BIT,        nullptr}
    };
    const VkDescriptorSetLayoutCreateInfo recipe   = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = bindings};
    VkResult                              status   = VK_SUCCESS;
    const auto                            outgoing = owner.Cache->TryGetOrCreateDescriptorSetLayout(recipe, &status);
    ASSERT_NE(outgoing, VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_SUCCESS);
    EXPECT_EQ(owner.Cache->TryGetOrCreateDescriptorSetLayout(recipe, &status), outgoing);
    const VkDescriptorBindingFlags                    flags[2]    = {VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, 0};
    const VkDescriptorSetLayoutBindingFlagsCreateInfo extension   = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO, .bindingCount = 2, .pBindingFlags = flags};
    auto                                              unsupported = recipe;
    unsupported.pNext                                             = &extension;
    EXPECT_EQ(owner.Cache->TryGetOrCreateDescriptorSetLayout(unsupported, &status), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_FEATURE_NOT_PRESENT);
    EXPECT_EQ(owner.Cache->TryGetOrCreateDescriptorSetLayout(recipe, &status), outgoing);
    EXPECT_EQ(status, VK_SUCCESS);
    EXPECT_EQ(owner.Vulkan.ValidationErrors.load(), 0u);
}

TEST_F(GpuAllocatorTest, CheckedRenderPassCreationPropagatesNativeFailuresAndPreservesOutgoingOnRetry)
{
    struct NativeFailures
    {
        static VKAPI_ATTR VkResult VKAPI_CALL OutOfMemory(VkDevice, const VkRenderPassCreateInfo* info, const VkAllocationCallbacks*, VkRenderPass*)
        {
            EXPECT_EQ(info->attachmentCount, 1u);
            EXPECT_EQ(info->pSubpasses[0].colorAttachmentCount, 1u);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL DeviceLost(VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass*)
        {
            return VK_ERROR_DEVICE_LOST;
        }
    };
    ArenaAllocator arena = {};
    arena.Initialize(ZKilo(16), 0);
    struct ArenaGuard
    {
        ArenaAllocator& Arena;
        ~ArenaGuard()
        {
            Arena.Shutdown();
        }
    } arena_guard{arena};
    auto                                device    = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    ZEngine::Hardwares::DeviceSwapchain swapchain = {};
    device->SwapchainPtr                          = &swapchain;
    device->LogicalDevice                         = s_vk->Device;
    struct DrainGuard
    {
        ZEngine::Hardwares::VulkanDevice& Device;
        ~DrainGuard()
        {
            Device.PendingFree.Drain(&Device.GpuMem, Device.LogicalDevice, UINT64_MAX);
        }
    } drain{*device};
    const auto specification = [&]() {
        ZEngine::Rendering::Specifications::AttachmentSpecification result = {};
        result.ColorsMap.init(&arena, 1);
        result.ColorsMap[0] = {
            .Format          = ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM,
            .Load            = ZEngine::Rendering::Specifications::LoadOperation::CLEAR,
            .Store           = ZEngine::Rendering::Specifications::StoreOperation::STORE,
            .Initial         = ZEngine::Rendering::Specifications::ImageLayout::UNDEFINED,
            .Final           = ZEngine::Rendering::Specifications::ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
            .ReferenceLayout = ZEngine::Rendering::Specifications::ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
        };
        return result;
    };
    VkResult                                                status = VK_ERROR_UNKNOWN;
    ZEngine::Rendering::Renderers::RenderPasses::Attachment outgoing(device.get(), specification(), &status);
    ASSERT_EQ(status, VK_SUCCESS);
    const auto old_handle = outgoing.GetHandle();
    ASSERT_NE(old_handle, VK_NULL_HANDLE);
    device->LogicalDevice = VK_NULL_HANDLE;
    ZEngine::Rendering::Renderers::RenderPasses::Attachment incoming(device.get(), specification(), &status);
    EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED);
    device->LogicalDevice     = s_vk->Device;
    const auto arena_position = arena.m_current_offset;
    for (uint32_t attempt = 0; attempt < 3; ++attempt)
    {
        EXPECT_FALSE(incoming.TryPrepare(&status, NativeFailures::OutOfMemory));
        EXPECT_EQ(status, VK_ERROR_OUT_OF_DEVICE_MEMORY);
        EXPECT_EQ(incoming.GetHandle(), VK_NULL_HANDLE);
        EXPECT_EQ(incoming.GetColorAttachmentCount(), 0u);
        EXPECT_EQ(outgoing.GetHandle(), old_handle);
        EXPECT_EQ(arena.m_current_offset, arena_position);
        EXPECT_FALSE(device->IsDeviceLost.load());
    }
    // No device arena/scratch storage is configured: native creation uses only
    // the retained recipe and bounded stack metadata, including the retry.
    ASSERT_TRUE(incoming.TryPrepare(&status));
    EXPECT_EQ(status, VK_SUCCESS);
    EXPECT_NE(incoming.GetHandle(), old_handle);
    EXPECT_EQ(incoming.GetColorAttachmentCount(), 1u);
    EXPECT_TRUE(outgoing.TryPrepare(&status, NativeFailures::OutOfMemory));
    EXPECT_EQ(status, VK_SUCCESS); // Idempotence must not call the failing dispatch.
    EXPECT_EQ(outgoing.GetHandle(), old_handle);
    device->LogicalDevice = VK_NULL_HANDLE;
    ZEngine::Rendering::Renderers::RenderPasses::Attachment lost(device.get(), specification(), &status);
    device->LogicalDevice = s_vk->Device;
    EXPECT_FALSE(lost.TryPrepare(&status, NativeFailures::DeviceLost));
    EXPECT_EQ(status, VK_ERROR_DEVICE_LOST);
    EXPECT_TRUE(device->IsDeviceLost.load());
    EXPECT_FALSE(lost.TryPrepare(&status));
    EXPECT_EQ(status, VK_ERROR_DEVICE_LOST);
    EXPECT_EQ(lost.GetHandle(), VK_NULL_HANDLE);
    // Only the injected loss is reset; the shared test device is still healthy.
    device->IsDeviceLost.store(false);
}

TEST_F(GpuAllocatorTest, RecoverableFramebuffersRejectInvalidAdmissionAndPreserveOutgoingOnRetry)
{
    ASSERT_NE(s_allocator, nullptr);
    auto device            = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    device->LogicalDevice  = s_vk->Device;
    device->PhysicalDevice = s_vk->PhysicalDevice;
    struct NativeResources
    {
        VkDevice      Device   = VK_NULL_HANDLE;
        VkRenderPass  Pass     = VK_NULL_HANDLE;
        VkFramebuffer Outgoing = VK_NULL_HANDLE;
        VkFramebuffer Incoming = VK_NULL_HANDLE;

        ~NativeResources()
        {
            if (Incoming)
            {
                vkDestroyFramebuffer(Device, Incoming, nullptr);
            }
            if (Outgoing)
            {
                vkDestroyFramebuffer(Device, Outgoing, nullptr);
            }
            if (Pass)
            {
                vkDestroyRenderPass(Device, Pass, nullptr);
            }
        }
    } resources{.Device = s_vk->Device};
    // A no-attachment subpass is legal and exercises admission without unrelated
    // image allocation. The graph integration tests cover real attachment views.
    const VkSubpassDescription   subpass   = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS};
    const VkRenderPassCreateInfo pass_info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .subpassCount = 1, .pSubpasses = &subpass};
    ASSERT_EQ(vkCreateRenderPass(s_vk->Device, &pass_info, nullptr, &resources.Pass), VK_SUCCESS);
    VkResult status    = VK_ERROR_UNKNOWN;
    resources.Outgoing = device->TryCreateFramebuffer({}, resources.Pass, 8, 8, 1, &status);
    ASSERT_NE(resources.Outgoing, VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_SUCCESS);

    const auto reject = [&](ZEngine::Core::Containers::ArrayView<VkImageView> views, VkRenderPass pass, uint32_t width, uint32_t height, uint32_t layers) {
        status = VK_SUCCESS;
        EXPECT_EQ(device->TryCreateFramebuffer(views, pass, width, height, layers, &status), VK_NULL_HANDLE);
        EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED);
        EXPECT_FALSE(device->IsDeviceLost.load());
    };
    reject({}, VK_NULL_HANDLE, 8, 8, 1);
    reject({}, resources.Pass, 0, 8, 1);
    reject({}, resources.Pass, 8, 0, 1);
    reject({}, resources.Pass, 8, 8, 0);
    reject({}, resources.Pass, UINT32_MAX, 8, 1);
    reject({}, resources.Pass, 8, UINT32_MAX, 1);
    reject({}, resources.Pass, 8, 8, UINT32_MAX);
    reject({nullptr, 1}, resources.Pass, 8, 8, 1);
    VkImageView null_view = VK_NULL_HANDLE;
    reject({&null_view, 1}, resources.Pass, 8, 8, 1);
    reject({&null_view, static_cast<size_t>(UINT32_MAX) + 1}, resources.Pass, 8, 8, 1);
    device->LogicalDevice = VK_NULL_HANDLE;
    reject({}, resources.Pass, 8, 8, 1);
    device->LogicalDevice = s_vk->Device;

    device->IsDeviceLost.store(true);
    status = VK_SUCCESS;
    EXPECT_EQ(device->TryCreateFramebuffer({}, resources.Pass, 8, 8, 1, &status), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_DEVICE_LOST);
    EXPECT_TRUE(device->IsDeviceLost.load());
    // This resets only a simulated preexisting loss, not a genuinely lost device.
    device->IsDeviceLost.store(false);
    resources.Incoming = device->TryCreateFramebuffer({}, resources.Pass, 8, 8, 1, &status);
    ASSERT_NE(resources.Incoming, VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_SUCCESS);
    EXPECT_NE(resources.Incoming, resources.Outgoing);
    // No status pointer is required; the compatibility wrapper uses the same
    // checked path and must continue to accept valid zero-attachment passes.
    vkDestroyFramebuffer(s_vk->Device, resources.Incoming, nullptr);
    resources.Incoming = device->CreateFramebuffer({}, resources.Pass, 8, 8);
    ASSERT_NE(resources.Incoming, VK_NULL_HANDLE);
}

TEST_F(GpuAllocatorTest, BindlessDescriptorWritesUseBoundedStorageAcrossMultipleTargets)
{
    ASSERT_NE(s_allocator, nullptr);
    ArenaAllocator arena = {};
    arena.Initialize(ZMega(2), 0);
    auto                                device    = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    ZEngine::Hardwares::DeviceSwapchain swapchain = {};
    device->Arena                                 = &arena;
    device->LogicalDevice                         = s_vk->Device;
    device->SwapchainPtr                          = &swapchain;
    device->GpuMem.Allocator                      = allocator().Allocator;
    device->MaxGlobalTexture                      = 8;
    device->GlobalTextures.Initialize(&arena, 8);
    device->ImageBufferManager.Initialize(&arena, 8);
    device->ImageViewCacheSlab.Init(&arena, ZKilo(64));

    BindlessTestTargets targets = {};
    ASSERT_TRUE(targets.Initialize(*device, 3));
    EXPECT_TRUE(device->AddBindlessTextureSlotRequest({.Binding = 0, .DstSet = targets.Sets[0]}));
    EXPECT_FALSE(device->AddBindlessTextureSlotRequest({.Binding = 1, .DstSet = targets.Sets[0]}));
    ZEngine::Rendering::Textures::TextureHandle textures[3] = {};
    for (auto& texture : textures)
    {
        texture = device->CreateTexture({.Width = 1, .Height = 1, .Format = ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM});
        ASSERT_TRUE(texture.Valid());
    }
    constexpr uint32_t slice = ZEngine::Hardwares::VulkanDevice::BindlessTextureUpdateSlice;
    using DescriptorState    = ZEngine::Rendering::SceneTextureDescriptorState;
    uint64_t publications[3] = {};
    for (uint32_t index = 0; index < 3; ++index)
    {
        const auto image    = device->GlobalTextures.Access(textures[index])->BufferHandle;
        publications[index] = 123;
        EXPECT_EQ(device->PrepareSceneTextureDescriptor(textures[index], image, &publications[index]), DescriptorState::Pending);
        EXPECT_EQ(publications[index], 123u); // Pending cannot fabricate readiness.
        EXPECT_FALSE(device->IsSceneTextureDescriptorCurrent(textures[index], image, publications[index]));
    }
    for (uint32_t index = 0; index < slice + 1; ++index)
    {
        device->RequestDescriptorUpdate(textures[index % 3]);
    }
    const auto before = arena.m_current_offset;
    device->FlushBindlessTextureUpdates();
    EXPECT_EQ(arena.m_current_offset, before);
    EXPECT_FALSE(device->TextureHandleToUpdates.empty());
    device->FlushBindlessTextureUpdates();
    EXPECT_EQ(arena.m_current_offset, before);
    EXPECT_TRUE(device->TextureHandleToUpdates.empty());
    for (uint32_t index = 0; index < 3; ++index)
    {
        const auto image = device->GlobalTextures.Access(textures[index])->BufferHandle;
        EXPECT_EQ(device->PrepareSceneTextureDescriptor(textures[index], image, &publications[index]), DescriptorState::Ready);
        EXPECT_TRUE(device->IsSceneTextureDescriptorCurrent(textures[index], image, publications[index]));
    }
    const auto wrong_image = device->GlobalTextures.Access(textures[1])->BufferHandle;
    uint64_t   untouched   = 789;
    EXPECT_EQ(device->PrepareSceneTextureDescriptor(textures[0], wrong_image, &untouched), DescriptorState::Failed);
    EXPECT_EQ(untouched, 789u);

    // Deferred publication still writes the fallback first and the real image
    // only on the next pump, keeping resource admission pending between them.
    device->FallbackDescriptorImageInfo = device->ImageBufferManager.Access(device->GlobalTextures.Access(textures[0])->BufferHandle)->GetDescriptorImageInfo();
    device->RequestDeferredDescriptorUpdate(textures[1]);
    device->FlushBindlessTextureUpdates();
    EXPECT_TRUE(device->DeferredTextureDescriptorUpdates.empty());
    EXPECT_FALSE(device->TextureHandleToUpdates.empty());
    const auto deferred_image = device->GlobalTextures.Access(textures[1])->BufferHandle;
    EXPECT_FALSE(device->IsSceneTextureDescriptorCurrent(textures[1], deferred_image, publications[1]));
    device->FlushBindlessTextureUpdates();
    EXPECT_TRUE(device->TextureHandleToUpdates.empty());
    uint64_t replacement_publication = 0;
    EXPECT_EQ(device->PrepareSceneTextureDescriptor(textures[1], deferred_image, &replacement_publication), DescriptorState::Ready);
    EXPECT_NE(replacement_publication, publications[1]);
    EXPECT_FALSE(device->IsSceneTextureDescriptorCurrent(textures[1], deferred_image, publications[1]));
    EXPECT_EQ(arena.m_current_offset, before);

    for (const auto texture : textures)
    {
        device->ImageBufferManager.Access(device->GlobalTextures.Access(texture)->BufferHandle)->Dispose();
    }
    device->PendingFree.Drain(&device->GpuMem, device->LogicalDevice, UINT64_MAX);
    device->ImageViewCacheSlab.Shutdown();
    device->GpuMem.Allocator = nullptr;
}

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

TEST_F(GpuAllocatorTest, PerFrameUploadHeapRetainsAllocationAndRetirementOwnershipAcrossResetAndShutdown)
{
    ASSERT_NE(s_allocator, nullptr);
    auto queue                                  = std::make_unique<RetirementQueue>();
    auto owner                                  = std::make_unique<GpuAllocator>(queue.get());
    owner->Allocator                            = allocator().Allocator;
    ZEngine::Hardwares::PerFrameUploadHeap heap = {};
    for (uint32_t cycle = 0; cycle < 8; ++cycle)
    {
        heap.Initialize(owner.get(), "upload_heap_lifetime_test");
        ASSERT_TRUE(heap.Backing);
        EXPECT_NE(heap.Backing.AllocationRecord, UINT32_MAX);
        EXPECT_NE(heap.Backing.RetirementReservation, 0u);
        EXPECT_EQ(queue->GetStatistics().Reserved, 1u);
        EXPECT_EQ(owner->GetAllocationStatistics().AllocationCount, 1u);
        const auto     record      = heap.Backing.AllocationRecord;
        const auto     reservation = heap.Backing.RetirementReservation;
        const uint32_t value       = cycle + 1;
        EXPECT_EQ(heap.Push(&value, sizeof(value), 256).Offset, 0u);
        EXPECT_EQ(heap.Push(&value, sizeof(value), 256).Offset, 256u);
        heap.Flush(owner.get());
        heap.Reset();
        EXPECT_EQ(heap.WritePos, 0u);
        EXPECT_EQ(heap.Backing.AllocationRecord, record);
        EXPECT_EQ(heap.Backing.RetirementReservation, reservation);
        heap.Shutdown(owner.get());
        EXPECT_FALSE(heap.Backing);
        EXPECT_EQ(heap.Backing.AllocationRecord, UINT32_MAX);
        EXPECT_EQ(heap.Backing.RetirementReservation, 0u);
        EXPECT_EQ(owner->GetAllocationStatistics().AllocationCount, 0u);
        EXPECT_EQ(owner->GetAllocationStatistics().AdmittedBytes, 0u);
        EXPECT_EQ(queue->GetStatistics().Reserved, 0u);
        heap.Shutdown(owner.get());
        EXPECT_EQ(queue->GetStatistics().Reserved, 0u);
    }
}

TEST_F(GpuAllocatorTest, RecoverableHostBuffersRejectInvalidRangesWithoutStagingOrMutation)
{
    ASSERT_NE(s_allocator, nullptr);
    GpuAllocator absent = {};
    VkResult     status = VK_SUCCESS;
    EXPECT_FALSE(absent.TryAllocateBuffer(64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, GpuMemoryDomain::HostUniform, nullptr, &status));
    EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED);
    EXPECT_FALSE(allocator().TryAllocateBuffer(0, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, GpuMemoryDomain::HostUniform));
    EXPECT_FALSE(allocator().TryAllocateBuffer(64, 0, GpuMemoryDomain::HostUniform));
    EXPECT_FALSE(allocator().TryAllocateBuffer(64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, GpuMemoryDomain::Count));
    auto buffer = allocator().TryAllocateBuffer(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, GpuMemoryDomain::HostUniform, "recoverable_host_write", &status);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(status, VK_SUCCESS);
    const std::array<uint32_t, 4> initial = {1, 2, 3, 4};
    EXPECT_TRUE(allocator().WriteHostBuffer(buffer, initial.data(), sizeof(initial), 0, &status));
    EXPECT_EQ(status, VK_SUCCESS);
    const uint32_t replacement = 7;
    EXPECT_TRUE(allocator().WriteHostBuffer(buffer, &replacement, sizeof(replacement), 4));
    EXPECT_FALSE(allocator().WriteHostBuffer(buffer, &replacement, sizeof(replacement), 13, &status));
    EXPECT_EQ(status, VK_ERROR_MEMORY_MAP_FAILED);
    EXPECT_FALSE(allocator().WriteHostBuffer(buffer, &replacement, SIZE_MAX, 4));
    EXPECT_FALSE(allocator().WriteHostBuffer(buffer, &replacement, sizeof(replacement), UINT64_MAX));
    EXPECT_FALSE(allocator().WriteHostBuffer(buffer, nullptr, 4));
    EXPECT_FALSE(allocator().WriteHostBuffer(buffer, &replacement, 0));
    auto alias           = buffer;
    alias.OwnsAllocation = false;
    EXPECT_FALSE(allocator().WriteHostBuffer(alias, &replacement, 4));
    alias            = buffer;
    alias.MappedData = nullptr;
    EXPECT_FALSE(allocator().WriteHostBuffer(alias, &replacement, 4));
    std::array<uint32_t, 4> observed = {};
    EXPECT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, buffer.Allocation, 0, observed.data(), sizeof(observed)), VK_SUCCESS);
    EXPECT_EQ(observed, (std::array<uint32_t, 4>{1, 7, 3, 4}));
    allocator().FreeBuffer(buffer);
}

TEST_F(GpuAllocatorTest, SharedImageAllocationSurvivesEitherObjectDestructionOrder)
{
    VkImageCreateInfo specification = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .flags = VK_IMAGE_CREATE_ALIAS_BIT, .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {16, 16, 1},
                      .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    };
    for (const bool backing_first : {false, true})
    {
        SCOPED_TRACE(backing_first);
        const auto baseline      = LiveAllocationBytes();
        const auto ledger_before = allocator().GetAllocationStatistics();
        VkResult   status        = VK_NOT_READY;
        auto       backing       = allocator().TryAllocateImage(specification, GpuMemoryDomain::RenderTarget, s_vk->Device, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, 1, "shared_image_backing", &status);
        ASSERT_TRUE(backing);
        EXPECT_EQ(status, VK_SUCCESS);
        auto alias = allocator().AllocateAliasingImage(backing, specification, s_vk->Device, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, 1, "shared_image_alias");
        ASSERT_TRUE(alias);
        EXPECT_EQ(alias.Allocation, backing.Allocation);
        EXPECT_EQ(alias.AllocationRecord, backing.AllocationRecord);
        EXPECT_FALSE(alias.OwnsAllocation);
        const auto allocated = LiveAllocationBytes();
        const auto charged   = allocator().GetAllocationStatistics();
        EXPECT_EQ(charged.AllocationCount, ledger_before.AllocationCount + 1);
        EXPECT_EQ(charged.AllocationBytes, ledger_before.AllocationBytes + allocator().GetAllocationSize(backing.Allocation));
        EXPECT_GT(allocated, baseline);
        allocator().FreeImage(backing_first ? backing : alias, s_vk->Device);
        EXPECT_EQ(LiveAllocationBytes(), allocated);
        EXPECT_EQ(allocator().GetAllocationStatistics().AllocationBytes, charged.AllocationBytes);
        EXPECT_GT(allocator().GetAllocationSize(backing_first ? alias.Allocation : backing.Allocation), 0u);
        allocator().FreeImage(backing_first ? alias : backing, s_vk->Device);
        EXPECT_EQ(LiveAllocationBytes(), baseline);
        EXPECT_EQ(allocator().GetAllocationStatistics().AllocationBytes, ledger_before.AllocationBytes);
    }
}

TEST_F(GpuAllocatorTest, SharedBufferAllocationSurvivesEitherObjectDestructionOrder)
{
    constexpr auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    for (const bool backing_first : {false, true})
    {
        SCOPED_TRACE(backing_first);
        const auto baseline = LiveAllocationBytes();
        const auto before   = allocator().GetAllocationStatistics();
        auto       backing  = allocator().TryAllocateBuffer(256, usage, GpuMemoryDomain::HostReadback, "shared_buffer_backing");
        ASSERT_TRUE(backing);
        auto alias = allocator().AllocateAliasingBuffer(backing, 256, usage, "shared_buffer_alias");
        ASSERT_TRUE(alias);
        EXPECT_EQ(alias.Allocation, backing.Allocation);
        EXPECT_EQ(alias.AllocationRecord, backing.AllocationRecord);
        EXPECT_FALSE(alias.OwnsAllocation);
        const auto bytes   = allocator().GetAllocationSize(backing.Allocation);
        const auto charged = allocator().GetAllocationStatistics();
        EXPECT_EQ(charged.ReservedRecords, before.ReservedRecords + 1);
        EXPECT_EQ(charged.AllocationCount, before.AllocationCount + 1);
        EXPECT_EQ(charged.AllocationBytes, before.AllocationBytes + bytes);
        EXPECT_GE(charged.PeakAllocationCount, charged.AllocationCount);
        EXPECT_GE(charged.PeakAllocationBytes, charged.AllocationBytes);
        EXPECT_FALSE(allocator().AllocateAliasingBuffer(backing, 257, usage));
        EXPECT_FALSE(allocator().AllocateAliasingBuffer(backing, 0, usage));
        EXPECT_FALSE(allocator().AllocateAliasingBuffer(backing, 256, 0));
        const auto allocated = LiveAllocationBytes();
        allocator().FreeBuffer(backing_first ? backing : alias);
        EXPECT_EQ(LiveAllocationBytes(), allocated);
        EXPECT_EQ(allocator().GetAllocationStatistics().AllocationBytes, charged.AllocationBytes);
        auto&                survivor     = backing_first ? alias : backing;
        VkMemoryRequirements requirements = {};
        vkGetBufferMemoryRequirements(s_vk->Device, survivor.Handle, &requirements);
        EXPECT_LE(requirements.size, bytes);
        // Exercise the allocation after either native buffer object is gone.
        const uint32_t expected = 0x1234abcd;
        uint32_t       observed = 0;
        EXPECT_EQ(vmaCopyMemoryToAllocation(allocator().Allocator, &expected, survivor.Allocation, 0, sizeof(expected)), VK_SUCCESS);
        EXPECT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, survivor.Allocation, 0, &observed, sizeof(observed)), VK_SUCCESS);
        EXPECT_EQ(observed, expected);
        allocator().FreeBuffer(survivor);
        allocator().FreeBuffer(survivor); // Empty cleanup is inert.
        EXPECT_EQ(survivor.AllocationRecord, UINT32_MAX);
        EXPECT_EQ(LiveAllocationBytes(), baseline);
        const auto reclaimed = allocator().GetAllocationStatistics();
        EXPECT_EQ(reclaimed.ReservedRecords, before.ReservedRecords);
        EXPECT_EQ(reclaimed.AllocationCount, before.AllocationCount);
        EXPECT_EQ(reclaimed.AllocationBytes, before.AllocationBytes);
    }
}

TEST_F(GpuAllocatorTest, ByteAdmissionChargesRequirementsBeforeBackingAndReturnsAllCreditsOnRefusal)
{
    auto queue                  = std::make_unique<ZEngine::Hardwares::DeferredFreeQueue>();
    auto local                  = std::make_unique<GpuAllocator>(queue.get());
    local->Allocator            = allocator().Allocator;
    const auto     baseline     = LiveAllocationBytes();
    constexpr auto domain       = GpuMemoryDomain::HostUniform;
    constexpr auto domain_index = static_cast<uint32_t>(domain);
    constexpr auto usage        = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    auto           limits       = local->GetAllocationLimits();
    limits.TotalBytes           = 0;
    ASSERT_TRUE(local->SetAllocationLimits(limits));
    VkResult status = VK_SUCCESS;
    EXPECT_FALSE(local->TryAllocateBuffer(64, usage, domain, nullptr, &status));
    EXPECT_EQ(status, VK_ERROR_OUT_OF_DEVICE_MEMORY);
    EXPECT_EQ(LiveAllocationBytes(), baseline);
    EXPECT_EQ(local->GetAllocationStatistics().ReservedRecords, 0u);
    EXPECT_EQ(local->GetAllocationStatistics().AdmittedBytes, 0u);
    EXPECT_EQ(queue->GetStatistics().Reserved, 0u);
    limits.TotalBytes                = ZMega(1);
    limits.DomainBytes[domain_index] = 0;
    ASSERT_TRUE(local->SetAllocationLimits(limits));
    EXPECT_FALSE(local->TryAllocateBuffer(64, usage, domain, nullptr, &status));
    EXPECT_EQ(status, VK_ERROR_OUT_OF_DEVICE_MEMORY);
    limits.DomainBytes[domain_index] = ZMega(1);
    ASSERT_TRUE(local->SetAllocationLimits(limits));
    auto buffer = local->TryAllocateBuffer(64, usage, domain);
    ASSERT_TRUE(buffer);
    VkMemoryRequirements requirements = {};
    vkGetBufferMemoryRequirements(s_vk->Device, buffer.Handle, &requirements);
    const auto live = local->GetAllocationStatistics();
    EXPECT_EQ(live.AdmittedBytes, requirements.size);
    EXPECT_EQ(live.DomainAdmittedBytes[domain_index], requirements.size);
    EXPECT_LE(live.AllocationBytes, live.AdmittedBytes);
    limits.TotalBytes = live.AdmittedBytes - 1;
    EXPECT_FALSE(local->SetAllocationLimits(limits));
    limits.TotalBytes                = live.AdmittedBytes;
    limits.DomainBytes[domain_index] = live.AdmittedBytes;
    ASSERT_TRUE(local->SetAllocationLimits(limits));
    auto alias = local->AllocateAliasingBuffer(buffer, 64, usage);
    ASSERT_TRUE(alias);
    EXPECT_EQ(local->GetAllocationStatistics().AdmittedBytes, live.AdmittedBytes);
    EXPECT_EQ(queue->GetStatistics().Reserved, 2u);
    EXPECT_FALSE(local->TryAllocateBuffer(1, usage, domain, nullptr, &status));
    EXPECT_EQ(status, VK_ERROR_OUT_OF_DEVICE_MEMORY);
    EXPECT_EQ(local->GetAllocationStatistics().ReservedRecords, 1u);
    EXPECT_EQ(queue->GetStatistics().Reserved, 2u);
    local->FreeBuffer(buffer);
    EXPECT_EQ(local->GetAllocationStatistics().AdmittedBytes, live.AdmittedBytes);
    local->FreeBuffer(alias);
    EXPECT_EQ(local->GetAllocationStatistics().ReservedRecords, 0u);
    EXPECT_EQ(local->GetAllocationStatistics().AdmittedBytes, 0u);
    EXPECT_EQ(queue->GetStatistics().Reserved, 0u);
    EXPECT_EQ(LiveAllocationBytes(), baseline);
}

TEST_F(GpuAllocatorTest, RetirementSaturationRefusesNativeCreationAndAliasesThenRecovers)
{
    auto queue            = std::make_unique<ZEngine::Hardwares::DeferredFreeQueue>();
    auto local            = std::make_unique<GpuAllocator>(queue.get());
    local->Allocator      = allocator().Allocator;
    constexpr auto usage  = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    auto           buffer = local->TryAllocateBuffer(64, usage, GpuMemoryDomain::HostUniform);
    ASSERT_TRUE(buffer);
    const auto                                                                                                     baseline = local->GetAllocationStatistics();
    std::array<ZEngine::Hardwares::DeferredFreeQueue::Lease, ZEngine::Hardwares::DeferredFreeQueue::kCapacity - 2> leases   = {};
    for (auto& lease : leases)
    {
        lease = queue->Reserve();
        ASSERT_TRUE(lease);
    }
    VkResult status = VK_SUCCESS;
    EXPECT_FALSE(local->TryAllocateBuffer(64, usage, GpuMemoryDomain::HostUniform, nullptr, &status));
    EXPECT_EQ(status, VK_ERROR_OUT_OF_HOST_MEMORY);
    EXPECT_FALSE(local->AllocateAliasingBuffer(buffer, 64, usage));
    EXPECT_EQ(local->GetAllocationStatistics().AdmittedBytes, baseline.AdmittedBytes);
    EXPECT_EQ(local->GetAllocationStatistics().AllocationCount, baseline.AllocationCount);
    leases.back() = {};
    auto alias    = local->AllocateAliasingBuffer(buffer, 64, usage);
    ASSERT_TRUE(alias);
    local->FreeBuffer(buffer);
    local->FreeBuffer(alias);
    for (auto& lease : leases)
    {
        lease = {};
    }
    EXPECT_EQ(queue->GetStatistics().Reserved, 0u);
    EXPECT_EQ(local->GetAllocationStatistics().AdmittedBytes, 0u);
}

TEST(DeferredFreeAdmissionTest, ActiveAndQueuedObjectsShareCapacityAndStaleTokensCannotClaimReusedSlots)
{
    auto                                                               queue  = std::make_unique<RetirementQueue>();
    std::array<RetirementQueue::Lease, RetirementQueue::kCapacity - 1> leases = {};
    for (auto& lease : leases)
    {
        lease = queue->Reserve();
        ASSERT_TRUE(lease);
    }
    EXPECT_FALSE(queue->Reserve());
    EXPECT_EQ(queue->GetStatistics().Reserved, leases.size());
    EXPECT_EQ(queue->GetStatistics().HighWater, leases.size());
    VkBuffer handle  = VK_NULL_HANDLE;
    uint32_t creates = 0;
    auto     create  = [&](VkBuffer* output) {
        ++creates;
        *output = reinterpret_cast<VkBuffer>(uintptr_t(42));
        return VK_SUCCESS;
    };
    EXPECT_EQ(queue->CreateNative(NativeResourceType::BUFFER, &handle, create), VK_ERROR_OUT_OF_HOST_MEMORY);
    EXPECT_EQ(creates, 0u);
    EXPECT_EQ(handle, VK_NULL_HANDLE);
    RetirementEntry entry  = {};
    entry.Reservation      = leases.back().Detach();
    const auto stale_token = entry.Reservation;
    entry.TimelineValue    = 10;
    // Metadata-only fixture: raw BUFFER entries deliberately have no native
    // destroy operation. Actual buffer/image frees are covered by Vulkan tests.
    entry.Data.Vk.Type     = NativeResourceType::BUFFER;
    ASSERT_TRUE(queue->TryEnqueue(entry));
    EXPECT_FALSE(queue->TryEnqueue(entry));
    EXPECT_EQ(queue->GetStatistics().Reserved, leases.size() - 1);
    EXPECT_EQ(queue->GetStatistics().Queued, 1u);
    EXPECT_FALSE(queue->Reserve());
    queue->Drain(nullptr, VK_NULL_HANDLE, 9);
    EXPECT_FALSE(queue->Empty());
    queue->Drain(nullptr, VK_NULL_HANDLE, 10);
    EXPECT_TRUE(queue->Empty());
    auto reused = queue->Reserve();
    ASSERT_TRUE(reused);
    entry.Reservation = stale_token;
    EXPECT_FALSE(queue->TryEnqueue(entry));
    reused = {};
    EXPECT_EQ(queue->CreateNative(NativeResourceType::BUFFER, &handle, create), VK_SUCCESS);
    EXPECT_EQ(creates, 1u);
    bool destroyed = false;
    queue->DestroyNative(NativeResourceType::BUFFER, handle, [&] { destroyed = true; });
    EXPECT_TRUE(destroyed);
    for (auto& lease : leases)
    {
        lease = {};
    }
    EXPECT_EQ(queue->GetStatistics().Reserved, 0u);
    EXPECT_EQ(queue->GetStatistics().Queued, 0u);
}

TEST(DeferredFreeAdmissionTest, ConcurrentProducersRetainEveryDisposalAndRestoreExactBaseline)
{
    auto                       queue     = std::make_unique<RetirementQueue>();
    std::array<std::thread, 4> producers = {};
    for (auto& producer : producers)
    {
        producer = std::thread([&] {
            for (uint32_t index = 0; index < 500; ++index)
            {
                auto lease = queue->Reserve();
                EXPECT_TRUE(lease);
                if (!lease)
                {
                    return;
                }
                RetirementEntry entry = {};
                entry.Reservation     = lease.Detach();
                entry.TimelineValue   = 5;
                entry.Data.Vk.Type    = ZEngine::Rendering::DeviceResourceType::BUFFER;
                EXPECT_TRUE(queue->TryEnqueue(entry));
            }
        });
    }
    for (auto& producer : producers)
    {
        producer.join();
    }
    EXPECT_EQ(queue->GetStatistics().Queued, 2000u);
    EXPECT_EQ(queue->GetStatistics().Reserved, 0u);
    queue->Drain(nullptr, VK_NULL_HANDLE, 4);
    EXPECT_EQ(queue->GetStatistics().Queued, 2000u);
    queue->Drain(nullptr, VK_NULL_HANDLE, 5);
    EXPECT_EQ(queue->GetStatistics().Queued, 0u);
    EXPECT_EQ(queue->GetStatistics().Draining, 0u);
    EXPECT_EQ(queue->GetStatistics().Reserved, 0u);
    EXPECT_TRUE(queue->Empty());
    EXPECT_TRUE(queue->Reserve());
}

TEST_F(GpuAllocatorTest, BufferAndImageAdmissionShareFiniteAllocationRecords)
{
    auto local                                                      = std::make_unique<GpuAllocator>();
    local->Allocator                                                = allocator().Allocator;
    const auto                                             baseline = LiveAllocationBytes();
    std::array<uint32_t, GpuAllocatorTestAccess::Capacity> records  = {};
    for (auto& record : records)
    {
        record = GpuAllocatorTestAccess::Reserve(*local);
        ASSERT_NE(record, UINT32_MAX);
    }
    EXPECT_EQ(local->GetAllocationStatistics().ReservedRecords, records.size());
    EXPECT_EQ(local->GetAllocationStatistics().AllocationBytes, 0u);
    VkResult       status = VK_SUCCESS;
    constexpr auto usage  = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    EXPECT_FALSE(local->TryAllocateBuffer(64, usage, GpuMemoryDomain::HostUniform, nullptr, &status));
    EXPECT_EQ(status, VK_ERROR_OUT_OF_HOST_MEMORY);
    VkImageCreateInfo specification = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {4, 4, 1},
                   .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_SAMPLED_BIT
    };
    EXPECT_FALSE(local->TryAllocateImage(specification, GpuMemoryDomain::RenderTarget, s_vk->Device, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, 1, nullptr, &status));
    EXPECT_EQ(status, VK_ERROR_OUT_OF_HOST_MEMORY);
    EXPECT_EQ(LiveAllocationBytes(), baseline);
    GpuAllocatorTestAccess::Release(*local, records.back());
    auto buffer = local->TryAllocateBuffer(64, usage, GpuMemoryDomain::HostUniform);
    ASSERT_TRUE(buffer);
    EXPECT_FALSE(local->TryAllocateImage(specification, GpuMemoryDomain::RenderTarget, s_vk->Device, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, 1, nullptr, &status));
    EXPECT_EQ(status, VK_ERROR_OUT_OF_HOST_MEMORY);
    local->FreeBuffer(buffer);
    auto image = local->TryAllocateImage(specification, GpuMemoryDomain::RenderTarget, s_vk->Device, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, 1);
    ASSERT_TRUE(image);
    local->FreeImage(image, s_vk->Device);
    for (size_t index = 0; index + 1 < records.size(); ++index)
    {
        GpuAllocatorTestAccess::Release(*local, records[index]);
    }
    EXPECT_EQ(local->GetAllocationStatistics().ReservedRecords, 0u);
    EXPECT_EQ(local->GetAllocationStatistics().AllocationCount, 0u);
    EXPECT_EQ(local->GetAllocationStatistics().AllocationBytes, 0u);
    EXPECT_EQ(LiveAllocationBytes(), baseline);
}

TEST_F(GpuAllocatorTest, SharedBuffersReclaimExactlyOnceAcrossOwnerThreads)
{
    const auto                 baseline        = allocator().GetAllocationStatistics();
    const auto                 native_baseline = LiveAllocationBytes();
    std::array<std::thread, 4> workers         = {};
    for (auto& worker : workers)
    {
        worker = std::thread([&] {
            for (uint32_t cycle = 0; cycle < 64; ++cycle)
            {
                constexpr auto usage   = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                auto           backing = allocator().TryAllocateBuffer(256, usage, GpuMemoryDomain::HostUniform);
                EXPECT_TRUE(backing);
                if (!backing)
                {
                    return;
                }
                auto alias = allocator().AllocateAliasingBuffer(backing, 256, usage);
                EXPECT_TRUE(alias);
                allocator().FreeBuffer(backing);
                if (alias)
                {
                    EXPECT_GT(allocator().GetAllocationSize(alias.Allocation), 0u);
                    allocator().FreeBuffer(alias);
                }
            }
        });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }
    // The same allocation's final references may retire on different owners.
    // Both native handles are distinct; no thread mutates the other's view.
    std::array<BufferView, 128> backings = {};
    std::array<BufferView, 128> aliases  = {};
    for (size_t index = 0; index < backings.size(); ++index)
    {
        constexpr auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        backings[index]      = allocator().TryAllocateBuffer(256, usage, GpuMemoryDomain::HostUniform);
        ASSERT_TRUE(backings[index]);
        aliases[index] = allocator().AllocateAliasingBuffer(backings[index], 256, usage);
        ASSERT_TRUE(aliases[index]);
    }
    std::atomic<uint32_t> ready  = 0;
    const auto            retire = [&](auto& buffers) {
        ready.fetch_add(1, std::memory_order_release);
        while (ready.load(std::memory_order_acquire) != 2)
        {
            std::this_thread::yield();
        }
        for (auto& buffer : buffers)
        {
            allocator().FreeBuffer(buffer);
        }
    };
    std::thread backing_owner([&] { retire(backings); });
    std::thread alias_owner([&] { retire(aliases); });
    backing_owner.join();
    alias_owner.join();
    const auto reclaimed = allocator().GetAllocationStatistics();
    EXPECT_EQ(reclaimed.ReservedRecords, baseline.ReservedRecords);
    EXPECT_EQ(reclaimed.AllocationCount, baseline.AllocationCount);
    EXPECT_EQ(reclaimed.AllocationBytes, baseline.AllocationBytes);
    EXPECT_EQ(LiveAllocationBytes(), native_baseline);
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
    static void TickScenePreparation(RRM& manager, bool failed = false)
    {
        manager.TickSceneResourcePreparations(0, failed);
    }
    static void ReleaseSceneBuffersAfterIdle(RRM& manager)
    {
        manager.ReleaseSceneResourcePreparationsAfterIdle();
    }
    static void PumpSceneBufferSlice(RRM& manager, RRM::SceneResourcePreparationId id, uint32_t& budget)
    {
        auto& record   = manager.m_scene_resources[id.Slot];
        auto  expected = RRM::SceneResourcePhase::Queued;
        if (record.Phase.compare_exchange_strong(expected, RRM::SceneResourcePhase::Preparing))
        {
            manager.BeginSceneFrameBufferPreparation(record);
        }
        manager.PrepareSceneFrameBuffers(record, budget);
    }
    static void FailNextSceneBufferWrite(RRM& manager, RRM::SceneResourcePreparationId id)
    {
        manager.m_scene_resources[id.Slot].InitialTransformBuffer.MappedData = nullptr;
    }
    static uint32_t UploadedSceneTransforms(const RRM& manager, RRM::SceneResourcePreparationId id)
    {
        return manager.m_scene_resources[id.Slot].InitialTransformUploadCursor;
    }
    static void InitializeTextureTasks(RRM& manager, ArenaAllocator& arena)
    {
        manager.m_texture_task_slab.Init(&arena, RRM::TEXTURE_TASK_SLAB_BYTES);
        manager.m_accept_texture_decodes.value.store(true);
    }

    static void DispatchTextureTasks(RRM& manager)
    {
        manager.DispatchQueuedTextureDecodes();
    }

    static uint32_t PendingTextureTasks(const RRM& manager)
    {
        return manager.m_pending_texture_decodes.value.load();
    }

    static void ShutdownTextureTasks(RRM& manager)
    {
        manager.m_accept_texture_decodes.value.store(false);
        manager.DiscardQueuedTextureDecodes();
        manager.m_texture_task_slab.Shutdown();
    }
    static void PublishSceneTextureProducer(RRM& manager, ArenaAllocator& arena, const ZEngine::Hardwares::StreamingUploadTicket& ticket)
    {
        manager.m_streaming_upload_tickets.init(&arena, 8);
        manager.PublishStreamingUploadTicket(ticket);
    }
    static bool SceneTextureReady(RRM& manager, const RRM::MaterialVersionPin& material, bool* failed)
    {
        return manager.SceneTextureProducersReady(material, failed);
    }
    static const RRM::TextureVersionSnapshot* FrameMaterialTexture(const RRM& manager, uint32_t index, uint32_t map)
    {
        return manager.m_frame_materials[index].Pin.GetTexture(map);
    }
    static void StageTextureReplacement(RRM& manager, const uuids::uuid& uuid, ZEngine::Rendering::Textures::TextureHandle expected, ZEngine::Rendering::Textures::TextureHandle candidate, ArenaAllocator& arena)
    {
        manager.m_streaming_upload_tickets.init(&arena, 8);
        auto& replacement       = manager.m_texture_replacements[0];
        replacement.UUID        = uuid;
        replacement.Expected    = expected;
        replacement.Replacement = candidate;
        manager.m_texture_replacement_count.store(1);
        EXPECT_TRUE(manager.TrackTextureDecode(candidate));
    }
    static void SubmitReplacementTicket(RRM& manager, const ZEngine::Hardwares::StreamingUploadTicket& ticket)
    {
        RRM::OnStreamingUploadSubmitted(&manager, ticket);
    }
    static void PollTextureReplacements(RRM& manager)
    {
        manager.PollTextureReplacements();
    }
    static RRM::MaterialVersionPin RetainMaterial(RRM& manager, const ZEngine::Rendering::Meshes::MeshMaterial& material)
    {
        return manager.RetainMaterialVersion(material);
    }
    static ZEngine::Rendering::SceneTextureDescriptorState PrepareMaterialDescriptors(RRM& manager, RRM::MaterialVersionPin& pin)
    {
        return manager.PrepareMaterialDescriptorVersions(pin);
    }
    static ZEngine::Rendering::SceneTextureDescriptorState PrepareFrameMaterialDescriptors(RRM& manager, uint32_t index)
    {
        return manager.PrepareMaterialDescriptorVersions(manager.m_frame_materials[index].Pin);
    }
    static void InitializeGeometry(ZEngine::Rendering::RenderResourceManager& manager, ArenaAllocator& arena)
    {
        manager.m_pool.Initialize(&arena, 64, 64, 32);
    }

    static void InitializeSceneResidency(RRM& manager, ArenaAllocator& arena, BufferView vertex, BufferView index, ZEngine::Core::VFS::AssetRegistry* registry)
    {
        InitializeGeometry(manager, arena);
        manager.m_pool.VertexBuffer = vertex;
        manager.m_pool.IndexBuffer  = index;
        manager.m_registry          = registry;
        manager.m_streaming_mgr.Initialize(manager.m_device, &manager);
    }

    static void EvictSceneGeometry(RRM& manager, ZEngine::Rendering::BufferHandle handle)
    {
        manager.m_streaming_mgr.RequestEvict(handle);
    }

    static void ReleaseSceneResidencyAfterIdle(RRM& manager)
    {
        manager.ReleaseSceneResourcePreparationsAfterIdle();
        manager.ReleaseFrameResourcesAfterIdle();
        manager.ResetGeometryBuffersInternal();
        manager.m_streaming_mgr.Deinitialize();
        manager.m_registry          = nullptr;
        manager.m_pool.VertexBuffer = {};
        manager.m_pool.IndexBuffer  = {};
    }

    static const RRM::MeshDrawMetadata* CaptureMetadata(TLSFSlab& slab, const ZEngine::Importers::AssetMesh& mesh)
    {
        return RRM::CaptureMeshDrawMetadata(slab, mesh);
    }

    static size_t AvailableMetadataBlocks(TLSFSlab& slab)
    {
        const ZEngine::Importers::AssetMesh       mesh   = {};
        std::vector<const RRM::MeshDrawMetadata*> blocks = {};
        while (const auto* block = CaptureMetadata(slab, mesh))
        {
            blocks.push_back(block);
        }
        for (const auto* block : blocks)
        {
            RRM::ReleaseMeshDrawMetadata(block);
        }
        return blocks.size();
    }

    static ZEngine::Rendering::BufferHandle InstallGeometry(ZEngine::Rendering::RenderResourceManager& manager, const RRM::MeshDrawMetadata* metadata = nullptr)
    {
        ZEngine::Rendering::RenderResourceManager::MeshSlot replacement = {};
        if (!manager.m_pool.Allocate(32, 4, replacement.Region))
        {
            return {};
        }
        replacement.Version      = manager.NextMeshGeometryVersion();
        replacement.VtxCount     = 1;
        replacement.IdxCount     = 1;
        replacement.DrawMetadata = metadata;
        if (manager.m_mesh_slot_count == 0)
        {
            manager.AllocMeshSlot();
        }
        manager.ReplaceMeshGeometry(manager.m_mesh_slots[0].Data, replacement);
        return {0, manager.m_mesh_slots[0].Generation};
    }

    static void ResetGeometry(ZEngine::Rendering::RenderResourceManager& manager)
    {
        manager.ResetGeometryBuffersInternal();
    }

    static VkDeviceSize GeometryBytes(const ZEngine::Rendering::RenderResourceManager& manager)
    {
        return manager.m_pool.VtxUsed;
    }

    static void RegisterGeometry(ZEngine::Rendering::RenderResourceManager& manager, const uuids::uuid& uuid, ZEngine::Rendering::BufferHandle mesh)
    {
        manager.m_uuid_to_buffer[0]    = {uuid, mesh};
        manager.m_uuid_to_buffer_count = 1;
    }

    static void InitializeGraphicsQueue(ZEngine::Hardwares::VulkanDevice& device, ArenaAllocator& arena, VkQueue queue)
    {
        device.m_queue_map.init(&arena, 2);
        device.m_queue_map.insert(ZEngine::Rendering::QueueType::GRAPHIC_QUEUE, queue);
    }

    static void InitializeHostWrites(ZEngine::Rendering::RenderResourceManager& manager, ZEngine::Hardwares::VulkanDevice& device)
    {
        manager.m_device = &device;
    }

    static void FlushTextureReleases(ZEngine::Rendering::RenderResourceManager& manager)
    {
        manager.FlushPendingTextureReleases();
    }

    static void InitializeTextureUploadSlots(RRM& manager, ZEngine::Hardwares::VulkanDevice& device, ZEngine::Hardwares::CommandBufferManager& commands, bool transfer)
    {
        device.CommandBufferMgr = &commands;
        commands.TotalPoolCount = 3;
        if (transfer)
        {
            // Exercise transfer-role bookkeeping on the fixture's actual shared
            // family. This is not separate-family hardware qualification.
            device.HasSeparateTransferQueue = true;
            device.TransferFamilyIndex      = device.GraphicFamilyIndex;
            device.m_queue_map.insert(ZEngine::Rendering::QueueType::TRANSFER_QUEUE, device.m_queue_map.at(ZEngine::Rendering::QueueType::GRAPHIC_QUEUE));
            commands.InstantTransferCommandBuffers.init(device.Arena, commands.InstantGraphicsCommandBuffers.size(), commands.InstantGraphicsCommandBuffers.size());
            for (uint32_t index = 0; index < commands.InstantGraphicsCommandBuffers.size(); ++index)
            {
                commands.InstantTransferCommandBuffers[index] = commands.InstantGraphicsCommandBuffers[index];
            }
        }
        manager.InitTextureTimelines();
        manager.m_async_uploads.Initialize(&device);
    }

    static void ClearTextureUploadJobs(RRM& manager)
    {
        manager.m_async_uploads.Clear();
    }

    static void SaturateTextureUploadQueue(RRM& manager)
    {
        for (uint32_t index = 0; index < ZEngine::Hardwares::AsyncUploadQueue::CAPACITY - 1; ++index)
        {
            manager.m_async_uploads.Enqueue({});
        }
    }

    static uint64_t TextureUploadSlotValue(const RRM& manager, bool transfer)
    {
        return transfer ? manager.m_tex_transfer_retire[0][0] : manager.m_tex_retire_values[0][0];
    }

    static bool TextureUploadSlotHasStaging(const RRM& manager, bool transfer)
    {
        return bool(transfer ? manager.m_tex_transfer_staging[0][0] : manager.m_tex_retire_staging[0][0]);
    }

    static bool TextureUploadPending(const RRM& manager, ZEngine::Rendering::Textures::TextureHandle texture)
    {
        const auto& producer = manager.m_texture_producer_completions[texture.Index];
        return producer.Texture.Index == texture.Index && producer.Texture.Generation == texture.Generation && producer.PendingSubmission;
    }

    static ZEngine::Rendering::QueueType TextureUploadQueue(const RRM& manager, ZEngine::Rendering::Textures::TextureHandle texture, ZEngine::Rendering::Specifications::ImageLayout layout)
    {
        return manager.TextureUploadQueue(texture, layout);
    }

    static bool RefuseUploadWithBusyTextureSlots(RRM& manager, ZEngine::Rendering::Textures::TextureHandle texture, unsigned char* pixels, size_t size, bool transfer)
    {
        // Deterministically exhaust command capacity before any recording. This
        // does not pretend that synthetic retire values represent submitted work.
        auto&                                           values   = transfer ? manager.m_tex_transfer_retire[0] : manager.m_tex_retire_values[0];
        std::array<uint64_t, RRM::GEOMETRY_UPLOAD_SLOT> previous = {};
        for (uint32_t slot = 0; slot < previous.size(); ++slot)
        {
            previous[slot] = values[slot];
            values[slot]   = 1;
        }
        const bool refused = !manager.UploadTextureBuffer(0, 0, texture, pixels, size).Valid();
        for (uint32_t slot = 0; slot < previous.size(); ++slot)
        {
            values[slot] = previous[slot];
        }
        return refused;
    }

    static void RetireTextureUploadSlots(RRM& manager)
    {
        manager.RetireTextureSlots(0, 0);
    }

    static void ShutdownTextureUploadSlots(RRM& manager)
    {
        manager.m_async_uploads.Clear();
        manager.ShutdownTextureTimelines();
    }

    static void Initialize(ZEngine::Rendering::RenderResourceManager& manager, ZEngine::Hardwares::VulkanDevice& device, ZEngine::Hardwares::CommandBufferManager& commands, ZEngine::Rendering::Primitives::Semaphore* timeline, ArenaAllocator& arena, VkQueue queue)
    {
        InitializeGraphicsQueue(device, arena, queue);
        manager.m_device                       = &device;
        manager.m_scene_descriptor_preparation = &device;
        manager.m_upload_cmd_mgr               = &commands;
        manager.m_batch_timeline               = timeline;
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

    struct ShaderAdmissionOwner
    {
        NativeMetadataOwner                  Native      = {};
        ArenaAllocator                       Arena       = {};
        ZEngine::Core::VFS::VFSContext       VFS         = {};
        ZEngine::Core::VFS::VFSMemoryBackend Backend     = {};
        ZEngine::Hardwares::DeviceSwapchain  Swapchain   = {};
        ZEngine::Rendering::Shaders::Shader  Shader      = {};
        bool                                 Initialized = false;

        bool                                 Initialize()
        {
            if (!Native.Initialize())
            {
                return false;
            }
            Arena.Initialize(ZMega(32), 0, "ShaderAdmissionTest");
            Native.Device->Arena              = &Arena;
            Native.Device->SwapchainPtr       = &Swapchain;
            Native.Device->PipelineStateCache = Native.Cache.get();
            Swapchain.BufferredFrameCount     = 2;
            VFS.Initialize(&Arena);
            Backend.Initialize(&Arena);
            if (VFS.Mount(&Backend, ZEngine::Core::VFS::VFSPath::Root(), 0).Failed())
            {
                return false;
            }
            Native.Device->ShaderManager.Initialize(&Arena, 1);
            Native.Device->ShaderCaches.init(&Arena, 16);
            Initialized = true;
            return true;
        }
        bool Write(const std::vector<uint32_t>& binary, const char* path = "/test.spv")
        {
            return Backend.WriteFile(ZEngine::Core::VFS::VFSPath::Parse(path).Value(), {reinterpret_cast<const uint8_t*>(binary.data()), binary.size() * sizeof(uint32_t)}).Succeeded();
        }
        ~ShaderAdmissionOwner()
        {
            Shader.Dispose();
            if (Initialized)
            {
                Native.Device->ShaderManager.Dispose();
                VFS.Shutdown();
            }
            Native.Device->PendingFree.Drain(&Native.Device->GpuMem, Native.Vulkan.Device, UINT64_MAX);
            Native.Device->Arena              = nullptr;
            Native.Device->SwapchainPtr       = nullptr;
            Native.Device->PipelineStateCache = nullptr;
        }
    };

    struct ShaderAdmissionFault
    {
        static inline thread_local VkResult                             Result         = VK_ERROR_OUT_OF_HOST_MEMORY;
        static inline thread_local ZEngine::Rendering::Shaders::Shader* ObservedShader = nullptr;
        static inline thread_local uint32_t                             CapturedCount  = 0;
        static VKAPI_ATTR VkResult VKAPI_CALL                           Module(VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule*)
        {
            if (ObservedShader && ObservedShader->LayoutBindingSpecificationMap.contains(0) && !ObservedShader->LayoutBindingSpecificationMap.at(0).empty())
            {
                for (const auto& binding : ObservedShader->LayoutBindingSpecificationMap.at(0))
                {
                    CapturedCount = std::max(CapturedCount, binding.Count);
                }
            }
            return Result;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL Layout(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout*)
        {
            return Result;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL Pool(VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool*)
        {
            return Result;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL Sets(VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet*)
        {
            return Result;
        }
    };

    // Only the offscreen target/descriptor bootstrap is supplied by this fixture.
    // Registration, upload, pipeline bake, Prepare and Execute use production code.
    // No Engine singleton, desktop window, surface or real swapchain is needed.
    struct PipelinePreparationWorkers
    {
        bool Owned = !ZEngine::Helpers::ThreadPoolHelper::IsInitialized();
        PipelinePreparationWorkers()
        {
            if (Owned)
            {
                ZEngine::Helpers::ThreadPoolHelper::Initialize();
            }
        }
        ~PipelinePreparationWorkers()
        {
            if (Owned)
            {
                ZEngine::Helpers::ThreadPoolHelper::Shutdown();
            }
        }
    };

    struct OffscreenGizmoCycle
    {
        struct BorrowedGraphicPass : Renderers::RenderPasses::GraphicPass
        {
            void InitializeBindings(ZEngine::Hardwares::VulkanDevice* device)
            {
                InitializeDescriptorBindings(device);
            }
            void ReleaseBindings()
            {
                ReleaseDescriptorStorage();
            }
            void ConnectPipeline()
            {
                Pipeline->ResolveDescriptorSet    = &DescriptorBoundPass::ResolveDescriptorSet;
                Pipeline->DescriptorReplayContext = static_cast<DescriptorBoundPass*>(this);
            }
            bool CheckBindings()
            {
                return VerifyDescriptorBindings();
            }
            bool ReplayBindings()
            {
                return DescriptorBoundPass::ReplayDescriptorBindings(this);
            }
        };

        ArenaAllocator                                             Arena                 = {};
        HeadlessVulkan                                             Vulkan                = {};
        ZEngine::Hardwares::DeviceSwapchain                        Swapchain             = {};
        ZEngine::Hardwares::FrameContext                           CurrentFrame          = {};
        std::unique_ptr<ZEngine::Hardwares::VulkanDevice>          Device                = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
        std::unique_ptr<ZEngine::Rendering::RenderResourceManager> Manager               = std::make_unique<ZEngine::Rendering::RenderResourceManager>();
        std::unique_ptr<Renderers::Pipelines::PSOCache>            Cache                 = std::make_unique<Renderers::Pipelines::PSOCache>();
        std::unique_ptr<Renderers::RenderGraph>                    Graph                 = std::make_unique<Renderers::RenderGraph>();
        ZEngine::Rendering::Shaders::Shader                        Shader                = {};
        ZEngine::Rendering::Shaders::Shader*                       DrawShader            = &Shader;
        Renderers::Pipelines::GraphicPipeline                      Pipeline              = {};
        BorrowedGraphicPass                                        Backend               = {};
        std::unique_ptr<Renderers::RenderPasses::Attachment>       Attachment            = {};
        std::unique_ptr<ZEngine::Hardwares::CommandBuffer>         Commands              = {};
        Renderers::RenderGraphResourceBuilder                      Builder               = {};
        Renderers::GizmoPass*                                      Pass                  = nullptr;
        VkCommandPool                                              Pool                  = VK_NULL_HANDLE;
        VkQueue                                                    Queue                 = VK_NULL_HANDLE;
        VkFence                                                    Fence                 = VK_NULL_HANDLE;
        VkDescriptorPool                                           Descriptors           = VK_NULL_HANDLE;
        VkDescriptorSetLayout                                      CameraLayout          = VK_NULL_HANDLE;
        VkDescriptorSetLayout                                      EmptyLayouts[2]       = {};
        VkShaderModule                                             AuxiliaryShaderModule = VK_NULL_HANDLE;
        VkImage                                                    Image                 = VK_NULL_HANDLE;
        VmaAllocation                                              ImageMemory           = nullptr;
        VkImageView                                                ImageView             = VK_NULL_HANDLE;
        ZEngine::Rendering::Buffers::FramebufferVNext              Framebuffer{Device.get()};
        BufferView                                                 Camera         = {};
        BufferView                                                 PreparedCamera = {};
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
            Backend.ReleaseBindings();
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
            if (AuxiliaryShaderModule)
            {
                vkDestroyShaderModule(Vulkan.Device, AuxiliaryShaderModule, nullptr);
            }
            if (AllocatorReady)
            {
                Pass->Deinitialize(Device.get());
                Device->GpuMem.FreeBuffer(Camera);
                Device->GpuMem.FreeBuffer(PreparedCamera);
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

        void Initialize(Renderers::GizmoPass& pass, const std::vector<uint32_t> (&binaries)[2], bool graph_execution = false, bool label_shader = false, bool asynchronous_pipeline = false, uint32_t shader_capacity = 1)
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
            Device->WorkerThreadCount                     = asynchronous_pipeline ? 1 : 0;
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
                Device->ShaderManager.Initialize(&Arena, shader_capacity);
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
            {
                heap.Backing.Handle = Camera.Handle;
            }
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
            if (asynchronous_pipeline)
            {
                auto state = Pipeline.PrepareAsync();
                ASSERT_EQ(state, PSOPreparationState::Pending);
                ASSERT_EQ(Pipeline.Handle, VK_NULL_HANDLE);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while ((state == PSOPreparationState::Pending || state == PSOPreparationState::Backpressure) && std::chrono::steady_clock::now() < deadline)
                {
                    Cache->FlushAsyncPipelineJobs();
                    state = Pipeline.PrepareAsync();
                    std::this_thread::yield();
                }
                ASSERT_EQ(state, PSOPreparationState::Ready);
                EXPECT_EQ(Cache->GetTelemetry().AsyncPipelineJobs, 1u);
                EXPECT_EQ(Cache->GetTelemetry().AsyncPipelinePublishes, 1u);
                EXPECT_EQ(Cache->GetTelemetry().GraphicsPipelineMisses, 0u);
            }
            else
            {
                Pipeline.Bake();
            }
            ASSERT_NE(Pipeline.Handle, VK_NULL_HANDLE);
            EXPECT_EQ(Attachment->GetHandle() == VK_NULL_HANDLE, Vulkan.DynamicRenderingEnabled);
            Backend.InitializeBindings(Device.get());
            Backend.Pipeline = &Pipeline;
            Backend.ConnectPipeline();
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
    void RunOffscreenGizmoCycles(bool dynamic_rendering, bool asynchronous_pipeline = false)
    {
        struct PreparationWorkers
        {
            bool Owned = false;
            ~PreparationWorkers()
            {
                if (Owned)
                {
                    ZEngine::Helpers::ThreadPoolHelper::Shutdown();
                }
            }
        } workers = {};
        if (asynchronous_pipeline && !ZEngine::Helpers::ThreadPoolHelper::IsInitialized())
        {
            ZEngine::Helpers::ThreadPoolHelper::Initialize();
            workers.Owned = true;
        }
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
            ASSERT_NO_FATAL_FAILURE(owner.Initialize(pass, binaries, false, false, asynchronous_pipeline));
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

TEST_F(GpuAllocatorTest, PreparedAsyncGizmoPipelineDrawsAfterResizeAndFreshDevices)
{
    ASSERT_NE(s_allocator, nullptr);
    RunOffscreenGizmoCycles(false, true);
}

TEST_F(GpuAllocatorTest, PreparedAsyncDynamicGizmoPipelineDrawsAfterResizeAndFreshDevices)
{
    ASSERT_NE(s_allocator, nullptr);
    RunOffscreenGizmoCycles(true, true);
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

#include "DelayedReadbackGpuFixture.h"
#include "GizmoGraphFixture.h"
#include "GridLabelGraphFixture.h"
#include "MaterialAlphaGpuFixture.h"

TEST_F(GpuAllocatorTest, PassDescriptorBanksIsolateShaderUsersAndRetireGenerations)
{
    GlslangLifetime compiler = {};
    ASSERT_TRUE(compiler.Ready);
    std::vector<uint32_t> binaries[2] = {};
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Vertex, sizeof(ZEngine::Tests::GizmoShaders::Vertex) - 1, EShLangVertex, binaries[0]));
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Fragment, sizeof(ZEngine::Tests::GizmoShaders::Fragment) - 1, EShLangFragment, binaries[1]));
    Renderers::GizmoPass pass  = {};
    OffscreenGizmoCycle  owner = {};
    ASSERT_TRUE(owner.Vulkan.Create(true));
    ASSERT_NO_FATAL_FAILURE(owner.Initialize(pass, binaries));
    Bank outgoing  = {};
    Bank candidate = {};
    ASSERT_TRUE(outgoing.Prepare(owner.Device.get(), owner.DrawShader));
    ASSERT_TRUE(candidate.Prepare(owner.Device.get(), owner.DrawShader));
    const auto old_generation = owner.DrawShader->Generation;
    const auto old_set        = outgoing.GetSets(0)[0];
    for (uint32_t frame = 0; frame < Bank::MaxFrames; ++frame)
    {
        ASSERT_EQ(outgoing.GetSets(0).size(), Bank::MaxFrames);
        EXPECT_NE(outgoing.GetSets(0)[frame], candidate.GetSets(0)[frame]);
        EXPECT_NE(outgoing.GetSets(0)[frame], owner.DrawShader->DescriptorSetMap.at(0)[frame]);
        EXPECT_NE(outgoing.GetSets(0)[frame], owner.Pipeline.ResolveDescriptorSet(owner.Pipeline.DescriptorReplayContext, 0, frame));
    }
    EXPECT_TRUE(outgoing.GetSets(Bank::MaxSets).empty());
    ASSERT_TRUE(outgoing.Prepare(owner.Device.get(), owner.DrawShader));
    EXPECT_EQ(outgoing.GetSets(0)[0], old_set);
    owner.Swapchain.BufferredFrameCount = Bank::MaxFrames + 1;
    EXPECT_FALSE(outgoing.Prepare(owner.Device.get(), owner.DrawShader));
    owner.Swapchain.BufferredFrameCount = Bank::MaxFrames;
    EXPECT_EQ(outgoing.GetSets(0)[0], old_set);

    ++owner.DrawShader->Generation;
    EXPECT_TRUE(outgoing.GetSets(0).empty());
    EXPECT_TRUE(candidate.GetSets(0).empty());
    auto& binding = owner.DrawShader->BindingsByName["UBCamera"];
    binding.Count = Bank::MaxBindings + 1;
    EXPECT_FALSE(outgoing.Prepare(owner.Device.get(), owner.DrawShader));
    EXPECT_TRUE(outgoing.GetSets(0).empty());
    binding.Count = 1;
    ASSERT_TRUE(outgoing.Prepare(owner.Device.get(), owner.DrawShader));
    EXPECT_NE(outgoing.GetSets(0)[0], old_set); // Old pool remains deferred, not reused.
    outgoing.Reset();
    outgoing.Reset();
    EXPECT_TRUE(outgoing.GetSets(0).empty());
    candidate.Reset();
    owner.DrawShader->Generation = old_generation;
    EXPECT_EQ(owner.Vulkan.ValidationErrors.load(std::memory_order_relaxed), 0u);
}

TEST_F(GpuAllocatorTest, UnpublishedDescriptorFramesAdoptOnlyCompleteMatchingBanks)
{
    GlslangLifetime compiler = {};
    ASSERT_TRUE(compiler.Ready);
    std::vector<uint32_t> binaries[2] = {};
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Vertex, sizeof(ZEngine::Tests::GizmoShaders::Vertex) - 1, EShLangVertex, binaries[0]));
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Fragment, sizeof(ZEngine::Tests::GizmoShaders::Fragment) - 1, EShLangFragment, binaries[1]));
    Renderers::GizmoPass pass  = {};
    OffscreenGizmoCycle  owner = {};
    ASSERT_TRUE(owner.Vulkan.Create(true));
    ASSERT_NO_FATAL_FAILURE(owner.Initialize(pass, binaries));
    owner.DrawShader->LayoutBindingSpecifications.init(&owner.Arena, 1);
    owner.DrawShader->LayoutBindingSpecifications.push(owner.DrawShader->BindingsByName.at("UBCamera"));
    Renderers::RenderPasses::PreparedDescriptorPass candidate         = {};
    const auto                                      binding_bytes     = 2 * Renderers::RenderPasses::kMaxDescriptorReplayBindings * sizeof(ZEngine::Core::Containers::OrderedHashEntry<cstring, bool>);
    std::array<std::max_align_t, 1024>              storage           = {};
    const auto                                      outgoing_pipeline = owner.Pipeline.Handle;
    const auto                                      original_position = owner.Arena.m_current_offset;
    for (const auto bytes : {size_t(1), binding_bytes, binding_bytes + 8 * sizeof(uint32_t)})
    {
        ArenaAllocator bounded = {};
        ASSERT_TRUE(bounded.InitializeBorrowed(storage.data(), bytes));
        auto* arena         = owner.Device->Arena;
        owner.Device->Arena = &bounded;
        bool graphics       = true;
        bool compute        = true;
        bool descriptors    = true;
        {
            ArenaCheckpoint                      transaction(&bounded);
            Renderers::RenderPasses::GraphicPass checked = {};
            graphics                                     = checked.TryInitializePrepared(owner.Device.get(), {}, owner.Attachment.get(), owner.DrawShader);
        }
        {
            ArenaCheckpoint                      transaction(&bounded);
            Renderers::RenderPasses::ComputePass checked = {};
            compute                                      = checked.TryInitializePrepared(owner.Device.get(), {}, owner.DrawShader);
        }
        {
            ArenaCheckpoint                                 transaction(&bounded);
            Renderers::RenderPasses::PreparedDescriptorPass checked = {};
            descriptors                                             = checked.TryInitializeForPipeline(owner.Device.get(), &owner.Pipeline);
        }
        owner.Device->Arena = arena;
        EXPECT_FALSE(graphics);
        EXPECT_FALSE(compute);
        EXPECT_EQ(descriptors, bytes >= binding_bytes);
        EXPECT_EQ(bounded.m_current_offset, 0u);
        EXPECT_EQ(owner.Arena.m_current_offset, original_position);
        EXPECT_EQ(owner.Pipeline.Handle, outgoing_pipeline);
    }
    candidate.InitializeForPipeline(owner.Device.get(), &owner.Pipeline);
    const auto outgoing          = owner.Pipeline.ResolveDescriptorSet(owner.Pipeline.DescriptorReplayContext, 0, 0);
    const auto generation        = owner.DrawShader->Generation;
    owner.Swapchain.CurrentFrame = nullptr;
    for (uint32_t invalid = 0; invalid < 3; ++invalid)
    {
        auto source = owner.Camera;
        if (invalid == 0)
        {
            source.Handle = VK_NULL_HANDLE;
        }
        else if (invalid == 1)
        {
            source.Usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        }
        else
        {
            source.Size = sizeof(Renderers::UBOCameraLayout) - 1;
        }
        candidate.BeginPreparedDescriptorPreparation(0);
        candidate.SetDynamicUniform("UBCamera", sizeof(Renderers::UBOCameraLayout), &source);
        EXPECT_FALSE(candidate.Verify());
        EXPECT_EQ(owner.Pipeline.ResolveDescriptorSet(owner.Pipeline.DescriptorReplayContext, 0, 0), outgoing);
    }
    for (uint32_t frame = 0; frame < Bank::MaxFrames; ++frame)
    {
        candidate.BeginPreparedDescriptorPreparation(frame);
        EXPECT_FALSE(candidate.Verify()); // A previous frame's writes are insufficient.
        candidate.SetDynamicUniform("UBCamera", sizeof(Renderers::UBOCameraLayout));
        ASSERT_TRUE(candidate.Verify());
        EXPECT_EQ(owner.Swapchain.CurrentFrame, nullptr);
        EXPECT_EQ(owner.Pipeline.ResolveDescriptorSet(owner.Pipeline.DescriptorReplayContext, 0, 0), outgoing);
        if (frame + 1 < Bank::MaxFrames)
        {
            EXPECT_FALSE(owner.Backend.AdoptPreparedDescriptors(candidate));
        }
    }
    ++owner.DrawShader->Generation;
    EXPECT_FALSE(owner.Backend.AdoptPreparedDescriptors(candidate));
    owner.DrawShader->Generation = generation;
    candidate.BeginPreparedDescriptorPreparation(Bank::MaxFrames);
    EXPECT_FALSE(candidate.Verify());
    EXPECT_FALSE(owner.Backend.AdoptPreparedDescriptors(candidate));
    candidate.BeginPreparedDescriptorPreparation(0);
    EXPECT_FALSE(owner.Backend.AdoptPreparedDescriptors(candidate));
    candidate.SetDynamicUniform("UBCamera", sizeof(Renderers::UBOCameraLayout));
    ASSERT_TRUE(candidate.Verify());
    const auto arena_before = owner.Arena.m_current_offset;
    ASSERT_TRUE(owner.Backend.AdoptPreparedDescriptors(candidate));
    EXPECT_EQ(owner.Arena.m_current_offset, arena_before);
    EXPECT_NE(owner.Pipeline.ResolveDescriptorSet(owner.Pipeline.DescriptorReplayContext, 0, 0), outgoing);
    EXPECT_FALSE(owner.Backend.AdoptPreparedDescriptors(candidate));
    owner.Swapchain.CurrentFrame = &owner.CurrentFrame;
    auto frame                   = std::make_unique<Renderers::GizmoFrameData>();
    frame->TriangleCount         = 1;
    frame->Triangles[0]          = {
        .A = { -.9f, -.8f, .5f},
          .B = { -.2f, -.8f, .5f},
          .C = {-.55f, .15f, .5f},
          .Color = 0xff0000ffu
    };
    for (uint32_t slot = 0; slot < Bank::MaxFrames; ++slot)
    {
        ASSERT_NO_FATAL_FAILURE(owner.Render(*frame, slot, 96, 64, true));
        EXPECT_EQ(owner.Pixel(-.55f, -.5f), frame->Triangles[0].Color);
    }
    EXPECT_EQ(owner.Vulkan.ValidationErrors.load(std::memory_order_relaxed), 0u);
}

TEST_F(GpuAllocatorTest, GraphResizePreservesPinnedImageVersionAndPublishesNewStorage)
{
    GlslangLifetime compiler = {};
    ASSERT_TRUE(compiler.Ready);
    std::vector<uint32_t> binaries[2] = {};
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Vertex, sizeof(ZEngine::Tests::GizmoShaders::Vertex) - 1, EShLangVertex, binaries[0]));
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Fragment, sizeof(ZEngine::Tests::GizmoShaders::Fragment) - 1, EShLangFragment, binaries[1]));
    Renderers::GizmoPass     pass  = {};
    OffscreenGizmoGraphCycle owner = {};
    ASSERT_TRUE(owner.Vulkan.Create(true, false, true));
    ASSERT_NO_FATAL_FAILURE(owner.InitializeGraph(pass, binaries));
    auto frame           = std::make_unique<Renderers::GizmoFrameData>();
    frame->TriangleCount = 1;
    frame->Triangles[0]  = {
        .A = { -.9f, -.8f, .5f},
          .B = { -.2f, -.8f, .5f},
          .C = {-.55f, .15f, .5f},
          .Color = 0xff0000ffu
    };
    ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 0, 64, 48, false));
    const auto previous = owner.Graph->ResourceInspector->GetRenderTarget(Renderers::RendererResourceName::FrameColorRenderTargetName);
    ASSERT_TRUE(previous.Valid());
    auto pin = owner.Manager->PinGraphTextureVersion(previous);
    ASSERT_TRUE(pin);
    const auto snapshot = *pin.Get();
    auto*      image    = owner.Device->ImageBufferManager.Access(snapshot.Image);
    ASSERT_NE(image, nullptr);
    const auto native = image->GetHandle();
    ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 1, 96, 64, false));
    const auto next = owner.Graph->ResourceInspector->GetRenderTarget(Renderers::RendererResourceName::FrameColorRenderTargetName);
    EXPECT_TRUE(next.Index != previous.Index || next.Generation != previous.Generation);
    EXPECT_EQ(pin.Get()->Version, snapshot.Version);
    EXPECT_EQ(pin.Get()->Width, 64u);
    EXPECT_EQ(pin.Get()->Height, 48u);
    EXPECT_EQ(image->GetHandle(), native);
    EXPECT_FALSE(owner.Manager->PinGraphTextureVersion(previous)); // Old version is sealed.
    EXPECT_EQ(owner.Pixel(-.55f, -.5f), frame->Triangles[0].Color);
    const auto* current = owner.Device->GlobalTextures.Access(next);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->Width, 96u);
    EXPECT_EQ(current->Height, 64u);
    pin.Reset();
    RRMUploadBatchTestHelper::FlushTextureReleases(*owner.Manager);
    EXPECT_EQ(owner.Vulkan.ValidationErrors.load(), 0u);
}

static void CheckGraphResizeRollback(bool ledger_refusal)
{
    GlslangLifetime compiler = {};
    ASSERT_TRUE(compiler.Ready);
    std::vector<uint32_t> binaries[2] = {};
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GridLabelShaders::Vertex, sizeof(ZEngine::Tests::GridLabelShaders::Vertex) - 1, EShLangVertex, binaries[0]));
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GridLabelShaders::Fragment, sizeof(ZEngine::Tests::GridLabelShaders::Fragment) - 1, EShLangFragment, binaries[1]));
    Renderers::GizmoPass    unused  = {};
    OffscreenGridLabelCycle fixture = {};
    ASSERT_TRUE(fixture.Owner.Vulkan.Create(true, false, true));
    ASSERT_NO_FATAL_FAILURE(fixture.Initialize(unused, binaries));
    ASSERT_NO_FATAL_FAILURE(fixture.Render({}, 512, 384, 0, 1.f, false, false));
    auto& owner = fixture.Owner;
    ASSERT_EQ(owner.Graph->TransientPool.Slots.size(), 2u);
    const auto                                                 previous_color = owner.Graph->ResourceInspector->GetRenderTarget(Renderers::RendererResourceName::FrameColorRenderTargetName);
    const auto                                                 previous_depth = owner.Graph->ResourceInspector->GetRenderTarget(Renderers::RendererResourceName::FrameDepthRenderTargetName);
    const uint32_t                                             live_before    = owner.Device->GlobalTextures.Size();
    std::array<ZEngine::Rendering::Textures::TextureHandle, 8> fillers        = {};
    uint32_t                                                   filler_count   = 0;
    std::vector<uint32_t>                                      ledger_fillers = {};
    // Leave one slot for the first replacement; the second must fail, exercising
    // rollback after a real image was allocated, not just an early refusal.
    if (ledger_refusal)
    {
        for (uint32_t index = 0; index < GpuAllocatorTestAccess::Capacity; ++index)
        {
            const auto record = GpuAllocatorTestAccess::Reserve(owner.Device->GpuMem);
            if (record == UINT32_MAX)
            {
                break;
            }
            ledger_fillers.push_back(record);
        }
        ASSERT_FALSE(ledger_fillers.empty());
        GpuAllocatorTestAccess::Release(owner.Device->GpuMem, ledger_fillers.back());
        ledger_fillers.pop_back();
    }
    while (!ledger_refusal && owner.Device->GlobalTextures.Size() + 1 < owner.Device->MaxGlobalTexture)
    {
        ASSERT_LT(filler_count, fillers.size());
        const auto filler = owner.Device->CreateTexture({.Width = 1, .Height = 1, .Format = Specs::ImageFormat::R8G8B8A8_UNORM}, "resize_admission_filler");
        ASSERT_TRUE(filler.Valid());
        fillers[filler_count++] = filler;
    }
    const auto                 reserved_before        = owner.Device->GlobalTextures.Size();
    Renderers::GraphicRenderer renderer               = {};
    renderer.RenderGraph                              = owner.Graph.get();
    ZEngine::Applications::AppRenderPipeline pipeline = {};
    pipeline.SceneRenderer                            = &renderer;
    ZEngine::Applications::RenderFrameState state     = {};
    state.RenderTargetW                               = 1024;
    state.RenderTargetH                               = 768;
    state.ResizeSequence                              = 7;
    uint64_t applied_sequence                         = 6;
    EXPECT_FALSE(pipeline.ApplyRenderTargetResize(state, applied_sequence));
    EXPECT_EQ(applied_sequence, 6u);
    EXPECT_EQ(owner.Device->GlobalTextures.Size(), reserved_before + 1);
    EXPECT_EQ(owner.Graph->RenderWidth, 512u);
    EXPECT_EQ(owner.Graph->RenderHeight, 384u);
    EXPECT_EQ(owner.Graph->ResourceInspector->GetRenderTarget(Renderers::RendererResourceName::FrameColorRenderTargetName), previous_color);
    EXPECT_EQ(owner.Graph->ResourceInspector->GetRenderTarget(Renderers::RendererResourceName::FrameDepthRenderTargetName), previous_depth);
    owner.DrainRetiredTextures();
    owner.Device->PendingFree.Drain(&owner.Device->GpuMem, owner.Vulkan.Device, UINT64_MAX);
    EXPECT_EQ(owner.Device->GlobalTextures.Size(), reserved_before);
    ASSERT_NO_FATAL_FAILURE(fixture.Render({}, 512, 384, 1, 1.f, false, false));
    ASSERT_NO_FATAL_FAILURE(fixture.ExpectPixels({}, 1.f, false));
    for (uint32_t index = 0; index < filler_count; ++index)
    {
        owner.Device->DestroyTexture(fillers[index]);
    }
    for (auto record : ledger_fillers)
    {
        GpuAllocatorTestAccess::Release(owner.Device->GpuMem, record);
    }
    owner.DrainRetiredTextures();
    owner.Device->PendingFree.Drain(&owner.Device->GpuMem, owner.Vulkan.Device, UINT64_MAX);
    EXPECT_EQ(owner.Device->GlobalTextures.Size(), live_before);
    // Retry the same retained mailbox request, not a new resize event.
    ASSERT_TRUE(pipeline.ApplyRenderTargetResize(state, applied_sequence));
    EXPECT_EQ(applied_sequence, 7u);
    EXPECT_TRUE(pipeline.ApplyRenderTargetResize(state, applied_sequence));
    ASSERT_NO_FATAL_FAILURE(fixture.Render({}, 1024, 768, 2, 1.f, false, false));
    ASSERT_NO_FATAL_FAILURE(fixture.ExpectPixels({}, 1.f, false));
    EXPECT_EQ(owner.Graph->RenderWidth, 1024u);
    EXPECT_EQ(owner.Graph->RenderHeight, 768u);
    EXPECT_EQ(owner.Vulkan.ValidationErrors.load(), 0u);
}

TEST_F(GpuAllocatorTest, GraphResizeRollsBackPartialImageReservationAndCanRetry)
{
    CheckGraphResizeRollback(false);
}

TEST_F(GpuAllocatorTest, GraphResizeRollsBackPartialImageAllocationRefusalAndCanRetry)
{
    CheckGraphResizeRollback(true);
}

TEST_F(GpuAllocatorTest, PassDescriptorBankBorrowsOnlyExplicitSharedSets)
{
    HeadlessVulkan vulkan = {};
    ASSERT_TRUE(vulkan.Create(true));
    ArenaAllocator arena = {};
    arena.Initialize(ZMega(1), 0, "SharedDescriptorBankTest");
    auto                                device    = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    ZEngine::Hardwares::DeviceSwapchain swapchain = {};
    ZEngine::Rendering::Shaders::Shader shader    = {};
    device->LogicalDevice                         = vulkan.Device;
    device->Arena                                 = &arena;
    device->SwapchainPtr                          = &swapchain;
    swapchain.BufferredFrameCount                 = 2;
    BindlessTestTargets targets                   = {};
    ASSERT_TRUE(targets.Initialize(*device, 1));
    device->ShaderReservedDescriptorSetMap.init(&arena, 1);
    device->ShaderReservedDescriptorSetLayoutMap.init(&arena, 1);
    device->ShaderReservedDescriptorSetLayoutMap[0] = targets.Layout;
    auto& shared                                    = device->ShaderReservedDescriptorSetMap[0];
    shared.init(&arena, 2);
    shared.push(targets.Sets[0]);
    shared.push(targets.Sets[1]);
    shader.SetLayouts.init(&arena, 1);
    shader.SetLayouts.push(targets.Layout);
    Bank first  = {};
    Bank second = {};
    ASSERT_TRUE(first.Prepare(device.get(), &shader));
    ASSERT_TRUE(second.Prepare(device.get(), &shader));
    EXPECT_EQ(first.GetSets(0)[0], targets.Sets[0]);
    EXPECT_EQ(second.GetSets(0)[1], targets.Sets[1]);
    first.Reset();
    second.Reset();
    // Reset must not retire or destroy the device-owned shared pool.
    EXPECT_EQ(device->PendingFree.Head, device->PendingFree.Tail);
    VkDescriptorSetLayout                 empty      = VK_NULL_HANDLE;
    const VkDescriptorSetLayoutCreateInfo empty_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ASSERT_EQ(vkCreateDescriptorSetLayout(vulkan.Device, &empty_info, nullptr, &empty), VK_SUCCESS);
    shader.SetLayouts[0] = empty;
    ASSERT_TRUE(first.Prepare(device.get(), &shader));
    EXPECT_NE(first.GetSets(0)[0], targets.Sets[0]);
    EXPECT_NE(first.GetSets(0)[1], targets.Sets[1]);
    first.Reset();
    device->PendingFree.Drain(&device->GpuMem, vulkan.Device, UINT64_MAX);
    vkDestroyDescriptorSetLayout(vulkan.Device, empty, nullptr);
    EXPECT_EQ(vulkan.ValidationErrors.load(std::memory_order_relaxed), 0u);
}

TEST_F(DelayedReadbackGpuFixture, PassDescriptorPoolRetiresAfterBlockedAcceptedSubmission)
{
    // No shader work is needed: a recorded descriptor-set bind references its
    // pool even while the accepted submission is blocked at the real GPU gate.
    struct LayoutOwner
    {
        ZEngine::Hardwares::VulkanDevice* Device   = nullptr;
        VkDescriptorSetLayout             Set      = VK_NULL_HANDLE;
        VkPipelineLayout                  Pipeline = VK_NULL_HANDLE;
        ~LayoutOwner()
        {
            if (Pipeline)
            {
                ZEngine::Hardwares::DeferredFreeEntry entry = {};
                entry.EntryKind                             = ZEngine::Hardwares::DeferredFreeEntry::Kind::VkHandle;
                entry.Data.Vk                               = {Pipeline, ZEngine::Rendering::DeviceResourceType::PIPELINE_LAYOUT, nullptr};
                Device->DeferFree(entry);
            }
            if (Set)
            {
                ZEngine::Hardwares::DeferredFreeEntry entry = {};
                entry.EntryKind                             = ZEngine::Hardwares::DeferredFreeEntry::Kind::VkHandle;
                entry.Data.Vk                               = {Set, ZEngine::Rendering::DeviceResourceType::DESCRIPTORSETLAYOUT, nullptr};
                Device->DeferFree(entry);
            }
        }
    } layouts                                      = {.Device = Device.get()};
    const VkDescriptorSetLayoutCreateInfo set_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ASSERT_EQ(vkCreateDescriptorSetLayout(Vulkan.Device, &set_info, nullptr, &layouts.Set), VK_SUCCESS);
    const VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &layouts.Set};
    ASSERT_EQ(vkCreatePipelineLayout(Vulkan.Device, &layout_info, nullptr, &layouts.Pipeline), VK_SUCCESS);
    ZEngine::Rendering::Shaders::Shader shader = {};
    shader.SetLayouts.init(Device->Arena, 1);
    shader.SetLayouts.push(layouts.Set);
    Swapchain.BufferredFrameCount     = 1;
    Swapchain.RenderTimelineNextValue = 3;
    Bank bank                         = {};
    ASSERT_TRUE(bank.Prepare(Device.get(), &shader));
    const uint32_t index = Ring.Acquire(sizeof(Pixel));
    ASSERT_NE(index, UINT32_MAX);
    SubmitBlockedCopy(index, layouts.Pipeline, bank.GetSets(0)[0]);
    ASSERT_FALSE(HasFatalFailure());
    uint64_t completed = UINT64_MAX;
    ASSERT_EQ(vkGetSemaphoreCounterValue(Vulkan.Device, Completion->GetHandle(), &completed), VK_SUCCESS);
    ASSERT_EQ(completed, 0u);
    bank.Reset();
    EXPECT_TRUE(bank.GetSets(0).empty());
    EXPECT_NE(Device->PendingFree.Head, Device->PendingFree.Tail);
    Device->PendingFree.Drain(&Device->GpuMem, Vulkan.Device, completed);
    EXPECT_NE(Device->PendingFree.Head, Device->PendingFree.Tail);
    EXPECT_EQ(Vulkan.ValidationErrors.load(), 0u);
    ReleaseGate();
    WaitForCopy();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(vkGetSemaphoreCounterValue(Vulkan.Device, Completion->GetHandle(), &completed), VK_SUCCESS);
    ASSERT_EQ(completed, 3u);
    Device->PendingFree.Drain(&Device->GpuMem, Vulkan.Device, completed);
    EXPECT_EQ(Device->PendingFree.Head, Device->PendingFree.Tail);
    Ring.Poll();
    EXPECT_EQ(Deliveries, 1u);
    EXPECT_EQ(Pixel, ExpectedPixel);
}

TEST_F(DelayedReadbackGpuFixture, CandidateValidatesImmutableSharedSamplerWhileItsSetIsPending)
{
    struct Resources
    {
        DelayedReadbackGpuFixture* Fixture  = nullptr;
        VkSampler                  Sampler  = VK_NULL_HANDLE;
        VkSampler                  Other    = VK_NULL_HANDLE;
        VkDescriptorSetLayout      Set      = VK_NULL_HANDLE;
        VkPipelineLayout           Pipeline = VK_NULL_HANDLE;
        ~Resources()
        {
            Fixture->ReleaseGate();
            EXPECT_EQ(vkDeviceWaitIdle(Fixture->Vulkan.Device), VK_SUCCESS);
            Fixture->Device->PendingFree.Drain(&Fixture->Device->GpuMem, Fixture->Vulkan.Device, UINT64_MAX);
            if (Pipeline)
            {
                vkDestroyPipelineLayout(Fixture->Vulkan.Device, Pipeline, nullptr);
            }
            if (Set)
            {
                vkDestroyDescriptorSetLayout(Fixture->Vulkan.Device, Set, nullptr);
            }
            if (Other)
            {
                vkDestroySampler(Fixture->Vulkan.Device, Other, nullptr);
            }
            if (Sampler)
            {
                vkDestroySampler(Fixture->Vulkan.Device, Sampler, nullptr);
            }
        }
    } resources                            = {.Fixture = this};
    const VkSamplerCreateInfo sampler_info = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .maxLod = VK_LOD_CLAMP_NONE};
    ASSERT_EQ(vkCreateSampler(Vulkan.Device, &sampler_info, nullptr, &resources.Sampler), VK_SUCCESS);
    ASSERT_EQ(vkCreateSampler(Vulkan.Device, &sampler_info, nullptr, &resources.Other), VK_SUCCESS);
    Device->GlobalLinearWrapSampler                       = resources.Sampler;
    const Specs::LayoutBindingSpecification specification = {.Set = 0, .Binding = 0, .Name = "LinearWrapSampler", .DescriptorTypeValue = Specs::DescriptorType::SAMPLER, .Flags = Specs::ShaderStageFlags::COMPUTE};
    Device->ShaderReservedLayoutBindingSpecificationMap.init(Device->Arena, 1);
    auto& specifications = Device->ShaderReservedLayoutBindingSpecificationMap[0];
    specifications.init(Device->Arena, 1);
    specifications.push(specification);
    EXPECT_EQ(Device->ResolveReservedImmutableSampler(0, 0), &Device->GlobalLinearWrapSampler);
    EXPECT_EQ(Device->ResolveReservedImmutableSampler(0, 1), nullptr);
    EXPECT_EQ(Device->ResolveReservedImmutableSampler(1, 0), nullptr);
    const VkDescriptorSetLayoutBinding    binding  = {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .pImmutableSamplers = Device->ResolveReservedImmutableSampler(0, 0)};
    const VkDescriptorSetLayoutCreateInfo set_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &binding};
    ASSERT_EQ(vkCreateDescriptorSetLayout(Vulkan.Device, &set_info, nullptr, &resources.Set), VK_SUCCESS);
    const VkPipelineLayoutCreateInfo pipeline_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &resources.Set};
    ASSERT_EQ(vkCreatePipelineLayout(Vulkan.Device, &pipeline_info, nullptr, &resources.Pipeline), VK_SUCCESS);
    ZEngine::Rendering::Shaders::Shader shader = {};
    shader.SetLayouts.init(Device->Arena, 1);
    shader.SetLayouts.push(resources.Set);
    shader.BindingsByName.init(Device->Arena, 4);
    shader.BindingsByName.insert(specification.Name, specification);
    shader.LayoutBindingSpecifications.init(Device->Arena, 1);
    shader.LayoutBindingSpecifications.push(specification);
    Swapchain.BufferredFrameCount     = 1;
    Swapchain.RenderTimelineNextValue = 3;
    Bank shared                       = {};
    ASSERT_TRUE(shared.Prepare(Device.get(), &shader));
    Device->ShaderReservedDescriptorSetLayoutMap.init(Device->Arena, 1);
    Device->ShaderReservedDescriptorSetLayoutMap.insert(0, resources.Set);
    Device->ShaderReservedDescriptorSetMap.init(Device->Arena, 1);
    auto& sets = Device->ShaderReservedDescriptorSetMap[0];
    sets.init(Device->Arena, 1);
    sets.push(shared.GetSets(0)[0]);
    Renderers::Pipelines::ComputePipeline pipeline            = {};
    pipeline.Device                                           = Device.get();
    pipeline.Shader                                           = &shader;
    Renderers::RenderPasses::PreparedDescriptorPass candidate = {};
    candidate.InitializeForPipeline(Device.get(), &pipeline);
    const auto index = Ring.Acquire(sizeof(Pixel));
    ASSERT_NE(index, UINT32_MAX);
    SubmitBlockedCopy(index, resources.Pipeline, shared.GetSets(0)[0]);
    ASSERT_FALSE(HasFatalFailure());
    candidate.BeginPreparedDescriptorPreparation(0);
    candidate.SetSampler(specification.Name, {.sampler = resources.Sampler});
    EXPECT_TRUE(candidate.Verify());
    candidate.SetSampler(specification.Name, {.sampler = resources.Other});
    EXPECT_FALSE(candidate.Verify()); // Shared ABI cannot be silently replaced.
    candidate.BeginPreparedDescriptorPreparation(0);
    candidate.SetSampler(specification.Name, {.sampler = resources.Sampler});
    EXPECT_TRUE(candidate.Verify());
    uint64_t completed = UINT64_MAX;
    ASSERT_EQ(vkGetSemaphoreCounterValue(Vulkan.Device, Completion->GetHandle(), &completed), VK_SUCCESS);
    EXPECT_EQ(completed, 0u); // Preparation neither waited nor released the gate.
    EXPECT_EQ(Vulkan.ValidationErrors.load(), 0u);
    ReleaseGate();
    WaitForCopy();
    ASSERT_FALSE(HasFatalFailure());
    Ring.Poll();
    EXPECT_EQ(Deliveries, 1u);
    EXPECT_EQ(Pixel, ExpectedPixel);
}

TEST_F(GpuAllocatorTest, RejectedPipelineOrDescriptorsDoNotRecordOrAcknowledgeGraphWork)
{
    ASSERT_NE(s_allocator, nullptr);
    GlslangLifetime compiler = {};
    ASSERT_TRUE(compiler.Ready);
    std::vector<uint32_t> binaries[2] = {};
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Vertex, sizeof(ZEngine::Tests::GizmoShaders::Vertex) - 1, EShLangVertex, binaries[0]));
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Fragment, sizeof(ZEngine::Tests::GizmoShaders::Fragment) - 1, EShLangFragment, binaries[1]));
    for (const bool dynamic_rendering : {false, true})
    {
        SCOPED_TRACE(dynamic_rendering);
        Renderers::GizmoPass     pass  = {};
        OffscreenGizmoGraphCycle owner = {};
        ASSERT_TRUE(owner.Vulkan.Create(true, dynamic_rendering, true));
        if (dynamic_rendering && !owner.Vulkan.DynamicRenderingEnabled)
        {
            continue;
        }
        ASSERT_NO_FATAL_FAILURE(owner.InitializeGraph(pass, binaries));
        auto frame           = std::make_unique<Renderers::GizmoFrameData>();
        frame->TriangleCount = 1;
        frame->Triangles[0]  = {
            .A = { -.9f, -.8f, .5f},
              .B = { -.2f, -.8f, .5f},
              .C = {-.55f, .15f, .5f},
              .Color = 0xff0000ffu
        };
        ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 0, 96, 64, false));
        auto* backend = static_cast<Renderers::RenderPasses::GraphicPass*>(owner.Graph->GetPass("Gizmo Pass")->Handle);
        ASSERT_NE(backend, nullptr);
        auto* clear_backend = static_cast<Renderers::RenderPasses::GraphicPass*>(owner.Graph->GetPass("Fixture clear")->Handle);
        ASSERT_NE(clear_backend, nullptr);
        ASSERT_EQ(clear_backend->Pipeline->Shader, backend->Pipeline->Shader);
        const auto resolve = backend->Pipeline->ResolveDescriptorSet;
        ASSERT_NE(resolve, nullptr);
        EXPECT_NE(resolve(backend->Pipeline->DescriptorReplayContext, 0, 0), resolve(clear_backend->Pipeline->DescriptorReplayContext, 0, 0));
        for (uint32_t failure = 0; failure < 5; ++failure)
        {
            SCOPED_TRACE(failure);
            const auto draws = owner.Probe.BodyCalls + owner.Probe.PrimaryCalls;
            const auto sinks = owner.Sink.Executions;
            if (failure == 0)
            {
                backend->Pipeline->DescriptorBindingsValid = false;
            }
            else if (failure == 1)
            {
                ASSERT_TRUE(owner.DrawShader->LayoutBindingSpecifications.empty());
                owner.DrawShader->LayoutBindingSpecifications.init(&owner.Arena, 1);
                owner.DrawShader->LayoutBindingSpecifications.push({.Set = 0, .Binding = 1, .Name = "RequiredUnsetBinding", .DescriptorTypeValue = Specs::DescriptorType::UNIFORM_BUFFER_DYNAMIC, .Flags = Specs::ShaderStageFlags::VERTEX});
            }
            else
            {
                // UBCamera was bound by the preceding successful frame. A stale
                // name must not hide omission or a failed attempt in this one.
                owner.DrawShader->LayoutBindingSpecifications.push(owner.DrawShader->BindingsByName.at("UBCamera"));
                owner.Probe.PreparationFault = static_cast<GizmoGraphRecordingProbe::DescriptorFault>(failure - 1);
            }
            owner.Commands->Begin();
            EXPECT_EQ(owner.Graph->Execute(owner.Commands.get()), nullptr);
            EXPECT_FALSE(owner.Graph->m_compile_valid);
            EXPECT_EQ(owner.Probe.BodyCalls + owner.Probe.PrimaryCalls, draws);
            EXPECT_EQ(owner.Sink.Executions, sinks);
            owner.Commands->End();
            ASSERT_EQ(vkResetCommandBuffer(owner.Commands->GetHandle(), 0), VK_SUCCESS);
            owner.Commands->SetState(ZEngine::Hardwares::CommandBufferState::Idle);
            backend->Pipeline->DescriptorBindingsValid = true;
            owner.Probe.PreparationFault               = GizmoGraphRecordingProbe::DescriptorFault::None;
            owner.DrawShader->LayoutBindingSpecifications.clear();
            ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 0, 96, 64, false));
            EXPECT_EQ(owner.Pixel(-.55f, -.5f), frame->Triangles[0].Color);
        }
        EXPECT_EQ(owner.Vulkan.ValidationErrors.load(), 0u);
    }
}

TEST_F(GpuAllocatorTest, RequiredImageRecipesPrepareIsolatedBanksAndRetainTheirSources)
{
    PipelinePreparationWorkers workers  = {};
    GlslangLifetime            compiler = {};
    ASSERT_TRUE(compiler.Ready);
    std::vector<uint32_t> binaries[2] = {};
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Vertex, sizeof(ZEngine::Tests::GizmoShaders::Vertex) - 1, EShLangVertex, binaries[0]));
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Fragment, sizeof(ZEngine::Tests::GizmoShaders::Fragment) - 1, EShLangFragment, binaries[1]));
    using Profile = Renderers::RGSceneDescriptorProfile;
    using Target  = Renderers::SceneRenderTarget;
    using Type    = Specs::DescriptorType;
    struct Input
    {
        const char* Name = nullptr;
        Type        Kind = Type::SAMPLED_IMAGE;
    };
    struct Recipe
    {
        Profile            Kind       = Profile::None;
        uint32_t           Targets    = 0;
        bool               Composited = false;
        std::vector<Input> Inputs     = {};
    };
    const Recipe recipes[] = {
        {       Profile::GridLabels,                                                                                                                                                                                                                 Renderers::SceneTargetMask(Target::Depth), false,                                                                                                                                                                                                                      {{"SceneDepth"}, {"FontAtlas"}, {"AtlasSampler", Type::SAMPLER}}},
        { Profile::SelectionOutline,                                                                                                                                                                                                              Renderers::SceneTargetMask(Target::ObjectId), false,                                                                                                                                                                                    {{"ObjectId"}, {"SelectionTokenStates", Type::STORAGE_BUFFER}, {"ObjectIdSampler", Type::SAMPLER}}},
        {         Profile::Lighting,                                                     Renderers::SceneTargetMask(Target::Depth) | Renderers::SceneTargetMask(Target::AlbedoAO) | Renderers::SceneTargetMask(Target::NormalRoughness) | Renderers::SceneTargetMask(Target::MetallicEmissive), false, {{"GBufferAlbedoAO"}, {"GBufferNormalRoughness"}, {"GBufferMetallicEmissive"}, {"GBufferDepth"}, {"DiffuseIrradiance"}, {"SpecularEnvironment"}, {"BrdfIntegrationLut"}, {"LightSB", Type::STORAGE_BUFFER}, {"GBufferSampler", Type::SAMPLER}, {"EnvironmentSampler", Type::SAMPLER}}},
        {      Profile::ToneMapping,                                                                                                                                                                                                              Renderers::SceneTargetMask(Target::HdrColor), false,                                                                                                                                                                                                                         {{"SceneColor"}, {"LinearClampToEdgeSampler", Type::SAMPLER}}},
        {      Profile::ToneMapping,                                                                                                                                                                                                         Renderers::SceneTargetMask(Target::HdrComposited),  true,                                                                                                                                                                                                                         {{"SceneColor"}, {"LinearClampToEdgeSampler", Type::SAMPLER}}},
        {       Profile::SkyViewLut,                                                                                                                                                                                                            Renderers::SceneTargetMask(Target::SkyViewLut), false,                                                                                                                                                           {{"Transmittance"}, {"Multiscattering"}, {"Destination", Type::STORAGE_IMAGE}, {"LinearClampToEdgeSampler", Type::SAMPLER}}},
        {Profile::AerialPerspective,                                                                                                                                             Renderers::SceneTargetMask(Target::AerialInscatter) | Renderers::SceneTargetMask(Target::AerialTransmittance), false,                                                                                               {{"Transmittance"}, {"Multiscattering"}, {"InscatterDestination", Type::STORAGE_IMAGE}, {"TransmittanceDestination", Type::STORAGE_IMAGE}, {"LinearClampToEdgeSampler", Type::SAMPLER}}},
        {     Profile::SkyComposite, Renderers::SceneTargetMask(Target::Depth) | Renderers::SceneTargetMask(Target::HdrColor) | Renderers::SceneTargetMask(Target::SkyViewLut) | Renderers::SceneTargetMask(Target::AerialInscatter) | Renderers::SceneTargetMask(Target::AerialTransmittance),  true,                                                                     {{"OpaqueSceneColor"}, {"SceneDepth"}, {"SkyViewLut"}, {"AerialInscatter"}, {"AerialTransmittance"}, {"AtmosphereTransmittance"}, {"LinearClampToEdgeSampler", Type::SAMPLER}, {"SkyViewSampler", Type::SAMPLER}}},
        {             Profile::Grid,                                                                                                                                                                                                                 Renderers::SceneTargetMask(Target::Depth), false,                                                                                                                                                                                                                                     {{"SceneDepth"}, {"DepthSampler", Type::SAMPLER}}},
    };
    for (const auto& recipe : recipes)
    {
        SCOPED_TRACE(static_cast<uint32_t>(recipe.Kind));
        SCOPED_TRACE(recipe.Composited);
        Renderers::GizmoPass     pass  = {};
        OffscreenGizmoGraphCycle owner = {};
        ASSERT_TRUE(owner.Vulkan.Create(true));
        ASSERT_NO_FATAL_FAILURE(owner.InitializeGraph(pass, binaries, {.AsynchronousPipeline = true}));
        // The fixture shader keeps its real camera interface. These additional
        // reflected bindings have matching real Vulkan layouts, but are not
        // consumed by its draw: this tests preparation/leases, not pass pixels.
        owner.Device->PhysicalDevice = owner.Vulkan.PhysicalDevice;
        owner.Device->DefaultDepthFormats.init(&owner.Arena, 3);
        owner.Device->DefaultDepthFormats.push(VK_FORMAT_D32_SFLOAT);
        owner.Device->DefaultDepthFormats.push(VK_FORMAT_D32_SFLOAT_S8_UINT);
        owner.Device->DefaultDepthFormats.push(VK_FORMAT_D24_UNORM_S8_UINT);
        auto&                                     shader = *owner.DrawShader;
        std::vector<VkDescriptorSetLayoutBinding> layout = {
            {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT},
        };
        shader.LayoutBindingSpecifications.init(&owner.Arena, recipe.Inputs.size() + 1);
        shader.LayoutBindingSpecifications.push(shader.BindingsByName.at("UBCamera"));
        shader.BindingsByName.init(&owner.Arena, 32);
        shader.BindingsByName.insert("UBCamera", shader.LayoutBindingSpecifications[0]);
        auto& reflected = shader.LayoutBindingSpecificationMap[0];
        reflected.init(&owner.Arena, recipe.Inputs.size() + 1);
        reflected.push(shader.LayoutBindingSpecifications[0]);
        for (const auto& input : recipe.Inputs)
        {
            const uint32_t                          index   = static_cast<uint32_t>(layout.size());
            const Specs::LayoutBindingSpecification binding = {.Set = 0, .Binding = index, .Name = input.Name, .DescriptorTypeValue = input.Kind, .Flags = Specs::ShaderStageFlags::FRAGMENT};
            shader.BindingsByName.insert(binding.Name, binding);
            shader.LayoutBindingSpecifications.push(binding);
            reflected.push(binding);
            layout.push_back({.binding = index, .descriptorType = Specs::DescriptorTypeMap[static_cast<uint32_t>(input.Kind)], .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT});
        }
        vkDestroyDescriptorSetLayout(owner.Vulkan.Device, owner.CameraLayout, nullptr);
        owner.CameraLayout                                = VK_NULL_HANDLE;
        const VkDescriptorSetLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = static_cast<uint32_t>(layout.size()), .pBindings = layout.data()};
        ASSERT_EQ(vkCreateDescriptorSetLayout(owner.Vulkan.Device, &layout_info, nullptr, &owner.CameraLayout), VK_SUCCESS);
        shader.SetLayouts[0] = owner.CameraLayout;
        struct SamplerOwner
        {
            VkDevice  Device = VK_NULL_HANDLE;
            VkSampler Handle = VK_NULL_HANDLE;
            ~SamplerOwner()
            {
                if (Handle)
                {
                    vkDestroySampler(Device, Handle, nullptr);
                }
            }
        } sampler                              = {.Device = owner.Vulkan.Device};
        const VkSamplerCreateInfo sampler_info = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .maxLod = VK_LOD_CLAMP_NONE};
        ASSERT_EQ(vkCreateSampler(owner.Vulkan.Device, &sampler_info, nullptr, &sampler.Handle), VK_SUCCESS);
        owner.Device->GlobalLinearWrapSamplerImageInfo              = {.sampler = sampler.Handle};
        owner.Device->GlobalLinearClampToEdgeSamplerImageInfo       = {.sampler = sampler.Handle};
        owner.Device->GlobalLinearWrapUClampToEdgeVSamplerImageInfo = {.sampler = sampler.Handle};
        owner.Device->GlobalNearestClampToEdgeSamplerImageInfo      = {.sampler = sampler.Handle};
        struct CandidateResources
        {
            OffscreenGizmoGraphCycle*                   Owner  = nullptr;
            ZEngine::Rendering::Textures::TextureHandle Source = {};
            BufferView                                  Lights = {};
            ~CandidateResources()
            {
                Owner->Graph->EndScenePipelinePreparation();
                if (Source.Valid())
                {
                    Owner->Device->DestroyTexture(Source);
                }
                if (Lights)
                {
                    Owner->Device->GpuMem.FreeBuffer(Lights);
                }
            }
        } resources                                   = {.Owner = &owner};
        const Specs::TextureSpecification source_spec = {.Width = 2, .Height = 2, .Format = Specs::ImageFormat::R8G8B8A8_UNORM};
        resources.Source                              = owner.Device->TryCreateTexture(source_spec, "recipe_environment");
        const auto source                             = resources.Source;
        ASSERT_TRUE(source.Valid());
        auto source_pin = owner.Manager->PinTextureVersion(source);
        ASSERT_TRUE(source_pin);
        const std::array<BufferView, Bank::MaxFrames> inputs = {owner.Camera, owner.Camera, owner.Camera};
        resources.Lights                                     = owner.Device->CreateBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, GpuMemoryDomain::DeviceGeometry, "recipe_lights");
        auto& lights                                         = resources.Lights;
        ASSERT_TRUE(lights);
        const std::array<BufferView, Bank::MaxFrames>  selections  = {lights, lights, lights};
        const Renderers::SceneDescriptorEnvironment    environment = {&source_pin, &source_pin, &source_pin, &source_pin, &source_pin, &source_pin, recipe.Composited, &source_pin, selections, recipe.Kind == Profile::GridLabels};
        ZEngine::Rendering::Scenes::PreparedSceneFrame initial     = {.TransformBuffer = &owner.Camera, .LightBuffer = &lights, .MaterialBuffer = &owner.Camera, .DrawBuffer = &owner.Camera, .CullingInputBuffers = inputs, .CulledIndirectBuffers = inputs};
        initial.View                                               = {{}, 96, 64, 7, true, true};
        initial.CameraBuffer                                       = &owner.Camera;
        auto& required                                             = owner.Graph->PersistentPasses[1];
        required.ScenePipelineProfile                              = {.Usage = Renderers::RGScenePipelineUsage::Core, .Formats = {Specs::ImageFormat::R8G8B8A8_UNORM}, .AttachmentCount = 1, .Descriptors = recipe.Kind, .Targets = recipe.Targets | Renderers::SceneTargetMask(Target::Color), .AttachmentTargets = {Target::Color}};
        ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
        auto       pipeline_state = owner.Graph->PollScenePipelinePreparation();
        const auto deadline       = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (pipeline_state == Renderers::RGScenePipelinePreparationState::Preparing && std::chrono::steady_clock::now() < deadline)
        {
            owner.Cache->FlushAsyncPipelineJobs();
            std::this_thread::yield();
            pipeline_state = owner.Graph->PollScenePipelinePreparation();
        }
        ASSERT_EQ(pipeline_state, Renderers::RGScenePipelinePreparationState::Ready);
        auto state = Renderers::RGScenePipelinePreparationState::Preparing;
        for (uint32_t pump = 0; pump < 32 && state == Renderers::RGScenePipelinePreparationState::Preparing; ++pump)
        {
            state = owner.Graph->PollSceneDescriptorPreparation(initial, 1, environment);
        }
        ASSERT_EQ(state, Renderers::RGScenePipelinePreparationState::Ready);
        EXPECT_EQ(required.PreparedSceneDescriptorFrame, Bank::MaxFrames);
        EXPECT_EQ(required.Handle, nullptr);
        EXPECT_TRUE(required.PreparedSceneDescriptors->Verify());
        EXPECT_FALSE(required.SceneDescriptorsAdopted);
        source_pin.Reset();
        const bool uses_environment = recipe.Kind != Profile::ToneMapping && recipe.Kind != Profile::Grid && recipe.Kind != Profile::SelectionOutline;
        EXPECT_EQ(owner.Manager->IsTextureVersionPinned(source), uses_environment);
        owner.Device->DestroyTexture(source);
        resources.Source = {};
        EXPECT_NE(owner.Device->GlobalTextures.Access(source), nullptr);
        owner.Graph->EndScenePipelinePreparation();
        EXPECT_FALSE(owner.Manager->IsTextureVersionPinned(source));
        owner.Manager->PollFrameResources();
        RRMUploadBatchTestHelper::FlushTextureReleases(*owner.Manager);
        owner.DrainRetiredTextures();
        EXPECT_EQ(owner.Device->GlobalTextures.Access(source), nullptr);
        if (recipe.Kind == Profile::GridLabels || recipe.Kind == Profile::SelectionOutline)
        {
            // Required overlay inputs cannot silently bind another frame's
            // font/token resources. Disabled labels alone may omit their bank.
            const Renderers::SceneDescriptorEnvironment missing = {.GridLabelsEnabled = recipe.Kind == Profile::GridLabels};
            ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
            ASSERT_EQ(owner.Graph->PollScenePipelinePreparation(), Renderers::RGScenePipelinePreparationState::Ready);
            state = Renderers::RGScenePipelinePreparationState::Preparing;
            for (uint32_t pump = 0; pump < 32 && state == Renderers::RGScenePipelinePreparationState::Preparing; ++pump)
            {
                state = owner.Graph->PollSceneDescriptorPreparation(initial, 1, missing);
            }
            EXPECT_EQ(state, Renderers::RGScenePipelinePreparationState::Failed);
            EXPECT_FALSE(owner.Graph->ActivateSceneDescriptors());
            owner.Graph->EndScenePipelinePreparation();
            if (recipe.Kind == Profile::GridLabels)
            {
                // Drain cancelled images before the next attempt in this
                // intentionally eight-texture fixture.
                owner.Manager->PollFrameResources();
                RRMUploadBatchTestHelper::FlushTextureReleases(*owner.Manager);
                owner.DrainRetiredTextures();
                ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
                ASSERT_EQ(owner.Graph->PollScenePipelinePreparation(), Renderers::RGScenePipelinePreparationState::Ready);
                state = Renderers::RGScenePipelinePreparationState::Preparing;
                for (uint32_t pump = 0; pump < 32 && state == Renderers::RGScenePipelinePreparationState::Preparing; ++pump)
                {
                    state = owner.Graph->PollSceneDescriptorPreparation(initial);
                }
                EXPECT_EQ(state, Renderers::RGScenePipelinePreparationState::Ready);
                EXPECT_EQ(required.PreparedSceneDescriptorFrame, 0u);
                owner.Graph->EndScenePipelinePreparation();
            }
        }
        EXPECT_EQ(owner.Vulkan.ValidationErrors.load(), 0u);
    }
}

TEST_F(GpuAllocatorTest, ComputeOnlySceneBackendAdoptsWithoutAnImagePlanAndDispatches)
{
    PipelinePreparationWorkers workers  = {};
    GlslangLifetime            compiler = {};
    ASSERT_TRUE(compiler.Ready);
    std::vector<uint32_t> binaries[2] = {};
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Vertex, sizeof(ZEngine::Tests::GizmoShaders::Vertex) - 1, EShLangVertex, binaries[0]));
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Fragment, sizeof(ZEngine::Tests::GizmoShaders::Fragment) - 1, EShLangFragment, binaries[1]));
    struct ComputeCallback final : Renderers::IRenderGraphCallbackPass
    {
        bool Register(ZEngine::Hardwares::VulkanDevice*, cstring, const Renderers::RenderGraphFrameContext&, Renderers::RenderGraphResourceBuilder*, Renderers::RenderGraphResourceInspector*) override
        {
            return true;
        }
        Specs::RenderPassType GetPipelineType() const override
        {
            return Specs::RenderPassType::COMPUTE;
        }
        cstring GetComputeShaderName() const override
        {
            return "candidate_noop";
        }
        void Execute(ZEngine::Hardwares::VulkanDevice*, Renderers::RenderGraphResourceInspector*, ZEngine::Rendering::Scenes::SceneData*, Renderers::RenderPasses::RenderPass* backend, ZEngine::Rendering::Buffers::FramebufferVNext*, ZEngine::Hardwares::CommandBuffer* commands) override
        {
            const auto* compute = static_cast<Renderers::RenderPasses::ComputePass*>(backend);
            vkCmdBindPipeline(commands->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, compute->Pipeline->Handle);
            vkCmdDispatch(commands->GetHandle(), 1, 1, 1);
        }
    } callback;
    Renderers::GizmoPass     pass  = {};
    OffscreenGizmoGraphCycle owner = {};
    ASSERT_TRUE(owner.Vulkan.Create(true));
    ASSERT_NO_FATAL_FAILURE(owner.InitializeGraph(pass, binaries, {.AsynchronousPipeline = true, .ShaderCapacity = 2}));
    const char            source[] = "#version 450\nlayout(local_size_x=1) in; void main() {}";
    std::vector<uint32_t> words    = {};
    ASSERT_TRUE(CompileGizmoStage(source, sizeof(source) - 1, EShLangCompute, words));
    const VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = words.size() * sizeof(uint32_t), .pCode = words.data()};
    // The fixture retires this module only after its pipeline workers join,
    // including assertion failure during asynchronous preparation.
    ASSERT_EQ(vkCreateShaderModule(owner.Vulkan.Device, &info, nullptr, &owner.AuxiliaryShaderModule), VK_SUCCESS);
    const auto shader_handle = owner.Device->ShaderManager.Create();
    ASSERT_TRUE(shader_handle);
    auto* shader = owner.Device->ShaderManager.Access(shader_handle);
    ASSERT_NE(shader, nullptr);
    shader->m_specification.Name = callback.GetComputeShaderName();
    shader->ShaderCreateInfos.init(&owner.Arena, 1);
    shader->ShaderCreateInfos.push({.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = owner.AuxiliaryShaderModule, .pName = "main"});
    owner.Device->ShaderCaches.insert(callback.GetComputeShaderName(), shader_handle);
    owner.Graph->AddCallbackPass("candidate compute", &callback, {.Usage = Renderers::RGScenePipelineUsage::Core});
    auto& required = owner.Graph->PersistentPasses[3];
    ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
    auto       state    = owner.Graph->PollScenePipelinePreparation();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (state == Renderers::RGScenePipelinePreparationState::Preparing && std::chrono::steady_clock::now() < deadline)
    {
        owner.Cache->FlushAsyncPipelineJobs();
        std::this_thread::yield();
        state = owner.Graph->PollScenePipelinePreparation();
    }
    ASSERT_EQ(state, Renderers::RGScenePipelinePreparationState::Ready);
    const std::array<BufferView, Bank::MaxFrames>        buffers = {owner.Camera, owner.Camera, owner.Camera};
    const ZEngine::Rendering::Scenes::PreparedSceneFrame initial = {.TransformBuffer = &owner.Camera, .MaterialBuffer = &owner.Camera, .DrawBuffer = &owner.Camera, .CullingInputBuffers = buffers, .CulledIndirectBuffers = buffers};
    state                                                        = Renderers::RGScenePipelinePreparationState::Preparing;
    for (uint32_t pump = 0; pump < 16 && state == Renderers::RGScenePipelinePreparationState::Preparing; ++pump)
    {
        state = owner.Graph->PollSceneDescriptorPreparation(initial);
    }
    ASSERT_EQ(state, Renderers::RGScenePipelinePreparationState::Ready);
    auto* backend = required.PreparedSceneBackend;
    ASSERT_NE(backend, nullptr);
    EXPECT_EQ(required.Handle, nullptr);
    EXPECT_EQ(required.PreparedFramebuffer, nullptr);
    ASSERT_TRUE(owner.Graph->ActivateSceneDescriptors());
    EXPECT_EQ(required.Handle, backend);
    ASSERT_TRUE(owner.Graph->ActivateSceneDescriptors());
    EXPECT_EQ(required.Handle, backend);
    owner.Commands->Begin();
    callback.Execute(owner.Device.get(), nullptr, nullptr, backend, nullptr, owner.Commands.get());
    owner.Commands->End();
    const auto         command    = owner.Commands->GetHandle();
    const VkSubmitInfo submission = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command};
    ASSERT_EQ(vkQueueSubmit(owner.Queue, 1, &submission, owner.Fence), VK_SUCCESS);
    ASSERT_EQ(vkWaitForFences(owner.Vulkan.Device, 1, &owner.Fence, VK_TRUE, 10'000'000'000), VK_SUCCESS);
    owner.Graph->EndScenePipelinePreparation();
    EXPECT_FALSE(shader->HasGenerationLeases());
    EXPECT_EQ(owner.Vulkan.ValidationErrors.load(), 0u);
}

TEST_F(GpuAllocatorTest, CandidateDescriptionRefusalRollsBackMetadataAndFailedShaderLookupCanRetryWithoutReplacingOutgoing)
{
    PipelinePreparationWorkers workers  = {};
    GlslangLifetime            compiler = {};
    ASSERT_TRUE(compiler.Ready);
    std::vector<uint32_t> binaries[2] = {};
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Vertex, sizeof(ZEngine::Tests::GizmoShaders::Vertex) - 1, EShLangVertex, binaries[0]));
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Fragment, sizeof(ZEngine::Tests::GizmoShaders::Fragment) - 1, EShLangFragment, binaries[1]));
    for (const bool dynamic : {false, true})
    {
        SCOPED_TRACE(dynamic);
        Renderers::GizmoPass     pass  = {};
        OffscreenGizmoGraphCycle owner = {};
        ASSERT_TRUE(owner.Vulkan.Create(true, dynamic, true));
        if (dynamic && !owner.Vulkan.DynamicRenderingEnabled)
        {
            continue;
        }
        ASSERT_NO_FATAL_FAILURE(owner.InitializeGraph(pass, binaries));
        auto frame           = std::make_unique<Renderers::GizmoFrameData>();
        frame->TriangleCount = 1;
        frame->Triangles[0]  = {
            .A = { -.9f, -.8f, .5f},
              .B = { -.2f, -.8f, .5f},
              .C = {-.55f, .15f, .5f},
              .Color = 0xff0000ffu
        };
        ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 0, 96, 64, false));
        auto& required = owner.Graph->PersistentPasses[1];
        auto* outgoing = static_cast<Renderers::RenderPasses::GraphicPass*>(required.Handle);
        ASSERT_NE(outgoing, nullptr);
        const auto outgoing_pipeline = outgoing->Pipeline->Handle;
        ASSERT_NE(outgoing_pipeline, VK_NULL_HANDLE);
        required.ScenePipelineProfile             = {.Usage = Renderers::RGScenePipelineUsage::Core, .Formats = {Specs::ImageFormat::R8G8B8A8_UNORM}, .AttachmentCount = 1};
        Specs::AttachmentSpecification attachment = {.BindPoint = Specs::PipelineBindPoint::GRAPHIC};
        attachment.ColorsMap.init(&owner.Arena, 16);
        attachment.ColorsMap[0]                        = {.Format = Specs::ImageFormat::R8G8B8A8_UNORM, .Load = Specs::LoadOperation::CLEAR, .Store = Specs::StoreOperation::STORE, .Initial = Specs::ImageLayout::UNDEFINED, .Final = Specs::ImageLayout::COLOR_ATTACHMENT_OPTIMAL, .ReferenceLayout = Specs::ImageLayout::COLOR_ATTACHMENT_OPTIMAL};
        required.PreparedSceneAttachment               = ZPushStructCtorArgs<Renderers::RenderPasses::Attachment>(&owner.Arena, owner.Device.get(), std::move(attachment));
        const size_t                     binding_bytes = sizeof(Specs::VertexInputBindingSpecification);
        const size_t                     recipe_bytes  = ZEngine::Helpers::memory_align_size_t(binding_bytes, ZAlignof<Specs::VertexInputAttributeSpecification>()) + 6 * sizeof(Specs::VertexInputAttributeSpecification);
        std::array<std::max_align_t, 64> storage       = {};
        for (const auto bytes : {size_t(1), binding_bytes, recipe_bytes})
        {
            ArenaAllocator bounded = {};
            ASSERT_TRUE(bounded.InitializeBorrowed(storage.data(), bytes));
            ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
            auto* saved         = owner.Device->Arena;
            owner.Device->Arena = &bounded;
            const auto state    = owner.Graph->PollScenePipelinePreparation();
            owner.Device->Arena = saved;
            EXPECT_EQ(state, Renderers::RGScenePipelinePreparationState::Failed);
            EXPECT_EQ(bounded.m_current_offset, 0u);
            EXPECT_EQ(required.PreparedScenePipeline, nullptr);
            EXPECT_EQ(required.Handle, outgoing);
            EXPECT_EQ(outgoing->Pipeline->Handle, outgoing_pipeline);
            owner.Graph->EndScenePipelinePreparation();
        }
        const auto shader_handle = owner.Device->ShaderCaches.at("gizmo");
        owner.Device->ShaderCaches.remove("gizmo");
        ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
        const auto missing = owner.Graph->PollScenePipelinePreparation();
        owner.Device->ShaderCaches.insert("gizmo", shader_handle);
        ASSERT_EQ(missing, Renderers::RGScenePipelinePreparationState::Failed);
        auto* retained = static_cast<Renderers::Pipelines::GraphicPipeline*>(required.PreparedScenePipeline);
        ASSERT_NE(retained, nullptr);
        EXPECT_EQ(retained->Shader, nullptr);
        auto*      bindings        = retained->Description.VertexInputBindingSpecifications.data();
        const auto retained_cursor = owner.Arena.m_current_offset;
        owner.Graph->EndScenePipelinePreparation();
        ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
        auto       state    = owner.Graph->PollScenePipelinePreparation();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (state == Renderers::RGScenePipelinePreparationState::Preparing && std::chrono::steady_clock::now() < deadline)
        {
            owner.Cache->FlushAsyncPipelineJobs();
            std::this_thread::yield();
            state = owner.Graph->PollScenePipelinePreparation();
        }
        ASSERT_EQ(state, Renderers::RGScenePipelinePreparationState::Ready);
        EXPECT_EQ(required.PreparedScenePipeline, retained);
        EXPECT_EQ(retained->Description.VertexInputBindingSpecifications.data(), bindings);
        EXPECT_EQ(retained->Shader, owner.DrawShader);
        EXPECT_EQ(owner.Arena.m_current_offset, retained_cursor);
        EXPECT_EQ(required.Handle, outgoing);
        EXPECT_EQ(outgoing->Pipeline->Handle, outgoing_pipeline);
        owner.Graph->EndScenePipelinePreparation();
        ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 1, 96, 64, false));
        EXPECT_EQ(owner.Vulkan.ValidationErrors.load(), 0u);
    }
}

TEST_F(GpuAllocatorTest, ColdSceneBackendIsPreparedPrivatelyAndFirstRecordingCannotCreateAFallback)
{
    PipelinePreparationWorkers workers  = {};
    GlslangLifetime            compiler = {};
    ASSERT_TRUE(compiler.Ready);
    std::vector<uint32_t> binaries[2] = {};
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Vertex, sizeof(ZEngine::Tests::GizmoShaders::Vertex) - 1, EShLangVertex, binaries[0]));
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Fragment, sizeof(ZEngine::Tests::GizmoShaders::Fragment) - 1, EShLangFragment, binaries[1]));
    for (const bool dynamic : {false, true})
    {
        SCOPED_TRACE(dynamic);
        Renderers::GizmoPass     pass  = {};
        OffscreenGizmoGraphCycle owner = {};
        ASSERT_TRUE(owner.Vulkan.Create(true, dynamic, true));
        if (dynamic && !owner.Vulkan.DynamicRenderingEnabled)
        {
            continue;
        }
        ASSERT_NO_FATAL_FAILURE(owner.InitializeGraph(pass, binaries, {.AsynchronousPipeline = true}));
        owner.DrawShader->LayoutBindingSpecifications.init(&owner.Arena, 1);
        owner.DrawShader->LayoutBindingSpecifications.push(owner.DrawShader->BindingsByName.at("UBCamera"));
        auto& required                                     = owner.Graph->PersistentPasses[1];
        required.ScenePipelineProfile                      = {.Usage = Renderers::RGScenePipelineUsage::Core, .Formats = {Specs::ImageFormat::R8G8B8A8_UNORM}, .AttachmentCount = 1, .Descriptors = Renderers::RGSceneDescriptorProfile::Camera, .AttachmentTargets = {Renderers::SceneRenderTarget::Color}};
        // Refuse both the recipe table and attachment-object reservation without
        // leaving partial arena metadata or constructing a pipeline/backend.
        const auto                        recipe_bytes     = 16 * sizeof(ZEngine::Core::Containers::HashEntry<uint32_t, Specs::ColorAttachment>);
        std::array<std::max_align_t, 128> metadata_storage = {};
        for (const auto bytes : {size_t(1), recipe_bytes})
        {
            ArenaAllocator bounded = {};
            ASSERT_TRUE(bounded.InitializeBorrowed(metadata_storage.data(), bytes));
            ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
            auto* saved_arena   = owner.Device->Arena;
            owner.Device->Arena = &bounded;
            const auto refused  = owner.Graph->PollScenePipelinePreparation();
            owner.Device->Arena = saved_arena;
            EXPECT_EQ(refused, Renderers::RGScenePipelinePreparationState::Failed);
            EXPECT_EQ(bounded.m_current_offset, 0u);
            EXPECT_EQ(required.PreparedSceneAttachment, nullptr);
            EXPECT_EQ(required.PreparedScenePipeline, nullptr);
            EXPECT_EQ(required.Handle, nullptr);
            owner.Graph->EndScenePipelinePreparation();
        }
        if (!dynamic)
        {
            ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
            const auto logical          = owner.Device->LogicalDevice;
            owner.Device->LogicalDevice = VK_NULL_HANDLE;
            const auto refused          = owner.Graph->PollScenePipelinePreparation();
            owner.Device->LogicalDevice = logical;
            EXPECT_EQ(refused, Renderers::RGScenePipelinePreparationState::Failed);
            ASSERT_NE(required.PreparedSceneAttachment, nullptr);
            EXPECT_EQ(required.PreparedSceneAttachment->GetHandle(), VK_NULL_HANDLE);
            EXPECT_EQ(required.PreparedScenePipeline, nullptr);
            auto* retained = required.PreparedSceneAttachment;
            owner.Graph->EndScenePipelinePreparation();
            ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
            (void) owner.Graph->PollScenePipelinePreparation();
            EXPECT_EQ(required.PreparedSceneAttachment, retained);
            EXPECT_NE(retained->GetHandle(), VK_NULL_HANDLE);
            owner.Graph->EndScenePipelinePreparation();
        }
        ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
        auto       state    = owner.Graph->PollScenePipelinePreparation();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (state == Renderers::RGScenePipelinePreparationState::Preparing && std::chrono::steady_clock::now() < deadline)
        {
            owner.Cache->FlushAsyncPipelineJobs();
            std::this_thread::yield();
            state = owner.Graph->PollScenePipelinePreparation();
        }
        ASSERT_EQ(state, Renderers::RGScenePipelinePreparationState::Ready);
        const std::array<BufferView, Bank::MaxFrames>  buffers = {owner.Camera, owner.Camera, owner.Camera};
        ZEngine::Rendering::Scenes::PreparedSceneFrame initial = {.TransformBuffer = &owner.Camera, .MaterialBuffer = &owner.Camera, .DrawBuffer = &owner.Camera, .CullingInputBuffers = buffers, .CulledIndirectBuffers = buffers};
        initial.View                                           = {{}, 96, 64, 1, true, true};
        owner.PreparedCamera                                   = owner.Device->GpuMem.TryAllocateBuffer(sizeof(Renderers::UBOCameraLayout), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, GpuMemoryDomain::HostUniform, "PreparedCameraTest");
        ASSERT_TRUE(owner.PreparedCamera);
        Renderers::UBOCameraLayout candidate_camera = {};
        candidate_camera.View(0, 3)                 = .35f;
        ASSERT_TRUE(owner.Device->GpuMem.WriteHostBuffer(owner.PreparedCamera, &candidate_camera, sizeof(candidate_camera)));
        initial.CameraBuffer = &owner.PreparedCamera;
        for (auto& heap : owner.Device->FrameHeaps)
        {
            heap.Backing.Handle = VK_NULL_HANDLE; // No shared camera heap is available.
        }
        state = Renderers::RGScenePipelinePreparationState::Preparing;
        for (uint32_t pump = 0; pump < 16 && state == Renderers::RGScenePipelinePreparationState::Preparing; ++pump)
        {
            state = owner.Graph->PollSceneDescriptorPreparation(initial);
        }
        ASSERT_EQ(state, Renderers::RGScenePipelinePreparationState::Ready);
        ASSERT_TRUE(required.SceneBackendReady);
        ASSERT_EQ(required.Handle, nullptr);
        auto* prepared = required.PreparedSceneBackend;
        ASSERT_NE(prepared, nullptr);
        auto* graphics = static_cast<Renderers::RenderPasses::GraphicPass*>(prepared);
        EXPECT_EQ(graphics->GetAttachment(), required.PreparedSceneAttachment);
        EXPECT_EQ(graphics->Pipeline->Handle, required.PreparedScenePipeline->Handle);
        ASSERT_TRUE(owner.Graph->ActivateSceneDescriptors());
        EXPECT_EQ(required.Handle, prepared);
        EXPECT_EQ(required.PreparedSceneBackend, nullptr);
        ASSERT_TRUE(owner.Graph->ActivateSceneDescriptors());
        EXPECT_EQ(required.Handle, prepared); // Repeated activation cannot swap back.
        auto frame           = std::make_unique<Renderers::GizmoFrameData>();
        frame->TriangleCount = 1;
        frame->Triangles[0]  = {
            .A = { -.9f, -.8f, .5f},
              .B = { -.2f, -.8f, .5f},
              .C = {-.55f, .15f, .5f},
              .Color = 0xff0000ffu
        };
        pass.SetFrame(*frame);
        // Removing the installed backend simulates an invariant breach, not an
        // allocation failure. First compilation must not reconstruct it.
        required.Handle = nullptr;
        owner.Commands->Begin();
        EXPECT_EQ(owner.Graph->Execute(owner.Commands.get()), nullptr);
        EXPECT_FALSE(owner.Graph->m_compile_valid);
        EXPECT_EQ(required.Handle, nullptr);
        owner.Commands->End();
        ASSERT_EQ(vkResetCommandBuffer(owner.Commands->GetHandle(), 0), VK_SUCCESS);
        owner.Commands->SetState(ZEngine::Hardwares::CommandBufferState::Idle);
        required.Handle                = prepared;
        owner.Scene.ActiveCameraBuffer = &owner.PreparedCamera;
        ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 0, 96, 64, false));
        EXPECT_EQ(required.Handle, prepared);
        EXPECT_EQ(owner.Pixel(-.2f, -.5f), frame->Triangles[0].Color);
        EXPECT_EQ(owner.Pixel(-.75f, -.5f), 0xff000000u);
        owner.Scene.ActiveCameraBuffer = nullptr;
        for (auto& heap : owner.Device->FrameHeaps)
        {
            heap.Backing.Handle = owner.Camera.Handle;
        }
        ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 0, 96, 64, false));
        EXPECT_EQ(owner.Pixel(-.55f, -.5f), frame->Triangles[0].Color);
        owner.Graph->EndScenePipelinePreparation();
        const auto color       = owner.Graph->ResourceInspector->GetRenderTarget(Renderers::RendererResourceName::FrameColorRenderTargetName);
        const auto render_pass = required.PreparedSceneAttachment->GetHandle();
        prepared->Dispose();
        EXPECT_NE(owner.Device->GlobalTextures.Access(color), nullptr);
        EXPECT_EQ(required.PreparedSceneAttachment->GetHandle(), render_pass);
        EXPECT_EQ(owner.Vulkan.ValidationErrors.load(), 0u);
    }
}

TEST_F(GpuAllocatorTest, RequiredSceneVariantPreparesBeforeFirstGraphDrawAndRejectsAttachmentDrift)
{
    PipelinePreparationWorkers workers = {};
    ASSERT_NE(s_allocator, nullptr);
    GlslangLifetime compiler = {};
    ASSERT_TRUE(compiler.Ready);
    std::vector<uint32_t> binaries[2] = {};
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Vertex, sizeof(ZEngine::Tests::GizmoShaders::Vertex) - 1, EShLangVertex, binaries[0]));
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Fragment, sizeof(ZEngine::Tests::GizmoShaders::Fragment) - 1, EShLangFragment, binaries[1]));
    for (const bool dynamic_rendering : {false, true})
    {
        SCOPED_TRACE(dynamic_rendering);
        Renderers::GizmoPass     pass  = {};
        OffscreenGizmoGraphCycle owner = {};
        ASSERT_TRUE(owner.Vulkan.Create(true, dynamic_rendering, true));
        if (dynamic_rendering && !owner.Vulkan.DynamicRenderingEnabled)
        {
            continue;
        }
        ASSERT_NO_FATAL_FAILURE(owner.InitializeGraph(pass, binaries, {.AsynchronousPipeline = true}));
        // The bootstrap pipeline is an outgoing version. The candidate must
        // asynchronously prepare a genuinely cold, new generation.
        owner.Pipeline.ReleasePreparationGeneration();
        ++owner.DrawShader->Generation;
        // Unlike the minimal shader fixture, real reflected shaders enumerate
        // their required names. Initial async bake must not mistake unbound
        // first-use descriptors for an incompatible hot-reload replay.
        ASSERT_TRUE(owner.DrawShader->LayoutBindingSpecifications.empty());
        owner.DrawShader->LayoutBindingSpecifications.init(&owner.Arena, 1);
        owner.DrawShader->LayoutBindingSpecifications.push(owner.DrawShader->BindingsByName.at("UBCamera"));
        auto& required                = owner.Graph->PersistentPasses[1];
        required.ScenePipelineProfile = {.Usage = Renderers::RGScenePipelineUsage::Core, .Formats = {Specs::ImageFormat::R8G8B8A8_UNORM}, .AttachmentCount = 1, .Descriptors = Renderers::RGSceneDescriptorProfile::Geometry, .Targets = Renderers::SceneTargetMask(Renderers::SceneRenderTarget::Color), .AttachmentTargets = {Renderers::SceneRenderTarget::Color}};
        const auto jobs               = owner.Cache->GetTelemetry().AsyncPipelineJobs;
        const auto misses             = owner.Cache->GetTelemetry().GraphicsPipelineMisses;
        ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
        auto state = owner.Graph->PollScenePipelinePreparation();
        EXPECT_EQ(state, Renderers::RGScenePipelinePreparationState::Preparing);
        EXPECT_TRUE(owner.DrawShader->HasGenerationLeases());
        EXPECT_EQ(required.Handle, nullptr); // No active graph/backend was needed.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (state == Renderers::RGScenePipelinePreparationState::Preparing && std::chrono::steady_clock::now() < deadline)
        {
            owner.Cache->FlushAsyncPipelineJobs();
            state = owner.Graph->PollScenePipelinePreparation();
            std::this_thread::yield();
        }
        ASSERT_EQ(state, Renderers::RGScenePipelinePreparationState::Ready);
        EXPECT_EQ(owner.Cache->GetTelemetry().AsyncPipelineJobs, jobs + 1);
        EXPECT_EQ(owner.Cache->GetTelemetry().GraphicsPipelineMisses, misses);
        const auto prepared = required.PreparedScenePipeline->Handle;
        ASSERT_NE(prepared, VK_NULL_HANDLE);
        // This fixture reflects only UBCamera; scene storage is nevertheless
        // supplied through real allocator-owned buffers, not fake Vulkan handles.
        const std::array<BufferView, Bank::MaxFrames>  inputs  = {owner.Camera, owner.Camera, owner.Camera};
        ZEngine::Rendering::Scenes::PreparedSceneFrame initial = {.TransformBuffer = &owner.Camera, .MaterialBuffer = &owner.Camera, .DrawBuffer = &owner.Camera, .CullingInputBuffers = inputs, .CulledIndirectBuffers = inputs};
        initial.View                                           = {{}, 96, 64, 7, true, true};
        initial.CameraBuffer                                   = &owner.Camera;
        EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(initial, 0), Renderers::RGScenePipelinePreparationState::Preparing);
        EXPECT_FALSE(owner.Graph->ActivateSceneDescriptors());
        EXPECT_EQ(owner.Graph->GetPreparedSceneTarget(Renderers::SceneRenderTarget::Color), nullptr);
        EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(initial), Renderers::RGScenePipelinePreparationState::Preparing);
        const auto* target_pin = owner.Graph->GetPreparedSceneTarget(Renderers::SceneRenderTarget::Color);
        ASSERT_NE(target_pin, nullptr);
        const auto prepared_target = target_pin->Get()->Handle;
        EXPECT_EQ(required.PreparedSceneDescriptorFrame, 0u);
        EXPECT_FALSE(owner.Graph->HasPreparedSceneTargets(96, 64)); // Banks not ready yet.
        EXPECT_FALSE(required.PreparedSceneAttachments.Ready);
        EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(initial), Renderers::RGScenePipelinePreparationState::Preparing);
        EXPECT_TRUE(required.PreparedSceneAttachments.Ready);
        EXPECT_EQ(required.PreparedSceneDescriptorFrame, 0u); // Attachment work owns this slice.
        const auto prepared_framebuffer = required.PreparedFramebuffer ? required.PreparedFramebuffer->Handle : VK_NULL_HANDLE;
        EXPECT_EQ(prepared_framebuffer == VK_NULL_HANDLE, dynamic_rendering);
        EXPECT_FALSE(required.SceneBackendReady);
        EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(initial), Renderers::RGScenePipelinePreparationState::Preparing);
        EXPECT_TRUE(required.SceneBackendReady);
        EXPECT_EQ(required.PreparedSceneDescriptorFrame, 0u); // Backend construction owns its own slice.
        auto* const prepared_backend = required.PreparedSceneBackend;
        ASSERT_NE(prepared_backend, nullptr);
        EXPECT_EQ(required.Handle, nullptr);
        EXPECT_EQ(static_cast<Renderers::RenderPasses::GraphicPass*>(prepared_backend)->GetAttachment(), required.PreparedSceneAttachment);
        for (uint32_t index = 0; index < Bank::MaxFrames; ++index)
        {
            EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(initial), index + 1 < Bank::MaxFrames ? Renderers::RGScenePipelinePreparationState::Preparing : Renderers::RGScenePipelinePreparationState::Ready);
            EXPECT_EQ(required.PreparedSceneDescriptorFrame, index + 1);
            EXPECT_FALSE(required.SceneDescriptorsAdopted);
        }
        auto frame           = std::make_unique<Renderers::GizmoFrameData>();
        frame->TriangleCount = 1;
        frame->Triangles[0]  = {
            .A = { -.9f, -.8f, .5f},
              .B = { -.2f, -.8f, .5f},
              .C = {-.55f, .15f, .5f},
              .Color = 0xff0000ffu
        };
        ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 0, 96, 64, false));
        EXPECT_EQ(owner.Pixel(-.55f, -.5f), frame->Triangles[0].Color);
        EXPECT_NE(owner.Graph->ResourceInspector->GetRenderTarget(Renderers::RendererResourceName::FrameColorRenderTargetName).Index, prepared_target.Index);
        EXPECT_TRUE(owner.Graph->HasPreparedSceneTargets(96, 64));
        EXPECT_FALSE(owner.Graph->HasPreparedSceneTargets(128, 64));
        ASSERT_NE(required.Handle, nullptr);
        EXPECT_EQ(static_cast<Renderers::RenderPasses::GraphicPass*>(required.Handle)->Pipeline->Handle, prepared);
        EXPECT_FALSE(required.SceneDescriptorsAdopted);
        auto*       active           = static_cast<Renderers::RenderPasses::GraphicPass*>(required.Handle)->Pipeline;
        const auto  old_set          = active->ResolveDescriptorSet(active->DescriptorReplayContext, 0, 0);
        auto* const outgoing_backend = required.Handle;
        ASSERT_TRUE(owner.Graph->ActivateSceneDescriptors());
        EXPECT_EQ(required.Handle, prepared_backend);
        EXPECT_EQ(required.PreparedSceneBackend, outgoing_backend);
        active = static_cast<Renderers::RenderPasses::GraphicPass*>(required.Handle)->Pipeline;
        EXPECT_TRUE(required.SceneAttachmentsAdopted);
        EXPECT_EQ(required.Framebuffer ? required.Framebuffer->Handle : VK_NULL_HANDLE, prepared_framebuffer);
        EXPECT_FALSE(owner.Graph->HasPreparedSceneTargets(96, 64));
        // A first-use mismatch must refuse the frame, not allocate a fallback
        // framebuffer/view after publication. Restoring the immutable metadata
        // allows the same candidate to record successfully.
        for (const bool operation_drift : {false, true})
        {
            const auto expected_view = required.PreparedSceneAttachments.Views[0];
            if (operation_drift)
            {
                required.ScenePipelineProfile.AttachmentTargets[0].Load = Specs::LoadOperation::CLEAR;
            }
            else
            {
                required.PreparedSceneAttachments.Views[0] = VK_NULL_HANDLE;
            }
            owner.Commands->Begin();
            EXPECT_EQ(owner.Graph->Execute(owner.Commands.get()), nullptr);
            EXPECT_FALSE(owner.Graph->m_compile_valid);
            owner.Commands->End();
            ASSERT_EQ(vkResetCommandBuffer(owner.Commands->GetHandle(), 0), VK_SUCCESS);
            owner.Commands->SetState(ZEngine::Hardwares::CommandBufferState::Idle);
            EXPECT_EQ(required.Framebuffer ? required.Framebuffer->Handle : VK_NULL_HANDLE, prepared_framebuffer);
            required.PreparedSceneAttachments.Views[0]              = expected_view;
            required.ScenePipelineProfile.AttachmentTargets[0].Load = Specs::LoadOperation::LOAD;
        }
        ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 1, 96, 64, false));
        EXPECT_EQ(required.Framebuffer ? required.Framebuffer->Handle : VK_NULL_HANDLE, prepared_framebuffer);
        EXPECT_EQ(required.FramebufferRenderPass, static_cast<Renderers::RenderPasses::GraphicPass*>(required.Handle)->GetAttachment()->GetHandle());
        EXPECT_EQ(owner.Pixel(-.55f, -.5f), frame->Triangles[0].Color);
        const auto activated_target = owner.Graph->ResourceInspector->GetRenderTarget(Renderers::RendererResourceName::FrameColorRenderTargetName);
        EXPECT_EQ(activated_target.Index, prepared_target.Index);
        EXPECT_EQ(activated_target.Generation, prepared_target.Generation);
        EXPECT_TRUE(required.SceneDescriptorsAdopted);
        EXPECT_NE(active->ResolveDescriptorSet(active->DescriptorReplayContext, 0, 0), old_set);
        EXPECT_EQ(owner.Cache->GetTelemetry().GraphicsPipelineMisses, misses);
        owner.Graph->EndScenePipelinePreparation();
        EXPECT_FALSE(owner.DrawShader->HasGenerationLeases());
        // Cancel a fully prepared plan twice: the outgoing framebuffer remains
        // usable, and the bounded spare wrapper is reused rather than growing
        // the persistent arena on every replacement.
        ZEngine::Rendering::Buffers::FramebufferVNext* spare         = nullptr;
        auto* const                                    backend_spare = required.PreparedSceneBackend;
        for (uint32_t attempt = 0; attempt < 2; ++attempt)
        {
            const auto outgoing = required.Framebuffer ? required.Framebuffer->Handle : VK_NULL_HANDLE;
            ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
            ASSERT_EQ(owner.Graph->PollScenePipelinePreparation(), Renderers::RGScenePipelinePreparationState::Ready);
            auto preparation = Renderers::RGScenePipelinePreparationState::Preparing;
            for (uint32_t pump = 0; pump < 16 && preparation == Renderers::RGScenePipelinePreparationState::Preparing; ++pump)
            {
                preparation = owner.Graph->PollSceneDescriptorPreparation(initial);
            }
            ASSERT_EQ(preparation, Renderers::RGScenePipelinePreparationState::Ready);
            EXPECT_TRUE(required.PreparedSceneAttachments.Ready);
            EXPECT_EQ(required.PreparedSceneBackend, backend_spare);
            EXPECT_NE(required.PreparedSceneBackend, required.Handle);
            EXPECT_EQ(required.Framebuffer ? required.Framebuffer->Handle : VK_NULL_HANDLE, outgoing);
            if (attempt)
            {
                EXPECT_EQ(required.PreparedFramebuffer, spare);
            }
            spare = required.PreparedFramebuffer;
            if (attempt)
            {
                // A completed CPU plan is not permission to publish after the
                // host has observed device loss, including dynamic rendering.
                owner.Device->IsDeviceLost.store(true);
                EXPECT_FALSE(owner.Graph->ActivateSceneDescriptors());
                EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(initial), Renderers::RGScenePipelinePreparationState::Failed);
                owner.Device->IsDeviceLost.store(false); // Simulated, not actual GPU loss.
            }
            owner.Graph->EndScenePipelinePreparation();
            EXPECT_FALSE(required.PreparedSceneAttachments.Ready);
            EXPECT_TRUE(!spare || !spare->Handle);
            owner.Manager->PollFrameResources();
            RRMUploadBatchTestHelper::FlushTextureReleases(*owner.Manager);
            owner.DrainRetiredTextures();
            owner.Device->PendingFree.Drain(&owner.Device->GpuMem, owner.Vulkan.Device, UINT64_MAX);
            ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 0, 96, 64, false));
            EXPECT_EQ(owner.Pixel(-.55f, -.5f), frame->Triangles[0].Color);
            EXPECT_EQ(required.Framebuffer ? required.Framebuffer->Handle : VK_NULL_HANDLE, outgoing);
        }
        // Missing attachment recipes and extent-admission failure are both
        // terminal before publication, preserving the outgoing scene.
        for (const bool missing_recipe : {false, true})
        {
            ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
            ASSERT_EQ(owner.Graph->PollScenePipelinePreparation(), Renderers::RGScenePipelinePreparationState::Ready);
            EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(initial), Renderers::RGScenePipelinePreparationState::Preparing);
            const auto original_target = required.ScenePipelineProfile.AttachmentTargets[0];
            const auto original_limit  = owner.Device->PhysicalDeviceProperties.properties.limits.maxFramebufferWidth;
            if (missing_recipe)
            {
                required.ScenePipelineProfile.AttachmentTargets[0] = {};
            }
            else
            {
                owner.Device->PhysicalDeviceProperties.properties.limits.maxFramebufferWidth = 1;
            }
            EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(initial), Renderers::RGScenePipelinePreparationState::Failed);
            EXPECT_FALSE(owner.Graph->ActivateSceneDescriptors());
            required.ScenePipelineProfile.AttachmentTargets[0]                           = original_target;
            owner.Device->PhysicalDeviceProperties.properties.limits.maxFramebufferWidth = original_limit;
            owner.Graph->EndScenePipelinePreparation();
            owner.Manager->PollFrameResources();
            RRMUploadBatchTestHelper::FlushTextureReleases(*owner.Manager);
            owner.DrainRetiredTextures();
            owner.Device->PendingFree.Drain(&owner.Device->GpuMem, owner.Vulkan.Device, UINT64_MAX);
            ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 0, 96, 64, false));
            EXPECT_EQ(owner.Pixel(-.55f, -.5f), frame->Triangles[0].Color);
        }
        // Both cancellation and a later allocation refusal must leave the
        // outgoing pool/extent untouched, including a partially prepared plan.
        for (const bool refuse_second_target : {false, true})
        {
            SCOPED_TRACE(refuse_second_target);
            required.ScenePipelineProfile.Targets |= Renderers::SceneTargetMask(Renderers::SceneRenderTarget::HdrColor);
            ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
            ASSERT_EQ(owner.Graph->PollScenePipelinePreparation(), Renderers::RGScenePipelinePreparationState::Ready);
            auto candidate                = initial;
            candidate.View.Width          = 320;
            candidate.View.Height         = 240;
            candidate.View.ResizeSequence = 8;
            const auto live_before        = owner.Device->GlobalTextures.Size();
            EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(candidate), Renderers::RGScenePipelinePreparationState::Preparing);
            ASSERT_NE(owner.Graph->GetPreparedSceneTarget(Renderers::SceneRenderTarget::HdrColor), nullptr);
            EXPECT_EQ(owner.Graph->GetPreparedSceneTarget(Renderers::SceneRenderTarget::Color), nullptr);
            EXPECT_EQ(owner.Graph->RenderWidth, 96u);
            EXPECT_EQ(owner.Graph->RenderHeight, 64u);
            std::vector<uint32_t> reservations = {};
            if (refuse_second_target)
            {
                for (uint32_t index = 0; index < GpuAllocatorTestAccess::Capacity; ++index)
                {
                    const auto record = GpuAllocatorTestAccess::Reserve(owner.Device->GpuMem);
                    if (record == UINT32_MAX)
                    {
                        break;
                    }
                    reservations.push_back(record);
                }
                EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(candidate), Renderers::RGScenePipelinePreparationState::Failed);
            }
            EXPECT_FALSE(owner.Graph->ActivateSceneDescriptors());
            owner.Graph->EndScenePipelinePreparation();
            for (auto record : reservations)
            {
                GpuAllocatorTestAccess::Release(owner.Device->GpuMem, record);
            }
            owner.Manager->PollFrameResources();
            RRMUploadBatchTestHelper::FlushTextureReleases(*owner.Manager);
            owner.DrainRetiredTextures();
            owner.Device->PendingFree.Drain(&owner.Device->GpuMem, owner.Vulkan.Device, UINT64_MAX);
            EXPECT_EQ(owner.Device->GlobalTextures.Size(), live_before);
            EXPECT_EQ(owner.Graph->ResourceInspector->GetRenderTarget(Renderers::RendererResourceName::FrameColorRenderTargetName).Index, prepared_target.Index);
            ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 0, 96, 64, false));
            EXPECT_EQ(owner.Pixel(-.55f, -.5f), frame->Triangles[0].Color);
            required.ScenePipelineProfile.Targets = Renderers::SceneTargetMask(Renderers::SceneRenderTarget::Color);
        }
        // Atmosphere storage targets use fixed 2D/3D shapes, independent of
        // the incoming viewport, and participate in the same isolated plan.
        const Renderers::SceneRenderTarget atmosphere_targets[] = {
            Renderers::SceneRenderTarget::SkyViewLut,
            Renderers::SceneRenderTarget::AerialInscatter,
            Renderers::SceneRenderTarget::AerialTransmittance,
            Renderers::SceneRenderTarget::HdrComposited,
        };
        for (const auto target : atmosphere_targets)
        {
            required.ScenePipelineProfile.Targets |= Renderers::SceneTargetMask(target);
        }
        ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
        ASSERT_EQ(owner.Graph->PollScenePipelinePreparation(), Renderers::RGScenePipelinePreparationState::Ready);
        for (uint32_t index = 0; index < 5; ++index)
        {
            EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(initial), Renderers::RGScenePipelinePreparationState::Preparing);
            EXPECT_EQ(required.PreparedSceneDescriptorFrame, 0u);
        }
        EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(initial), Renderers::RGScenePipelinePreparationState::Preparing); // Framebuffer/views before banks.
        EXPECT_TRUE(required.PreparedSceneAttachments.Ready);
        EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(initial), Renderers::RGScenePipelinePreparationState::Preparing); // Private backend before banks.
        for (const auto target : atmosphere_targets)
        {
            const auto* pin = owner.Graph->GetPreparedSceneTarget(target);
            ASSERT_NE(pin, nullptr);
            const auto* texture = owner.Device->GlobalTextures.Access(pin->Get()->Handle);
            ASSERT_NE(texture, nullptr);
            const auto expected = Renderers::DescribeSceneTarget(target, 96, 64).Spec;
            EXPECT_EQ(texture->Specification.Width, expected.Width);
            EXPECT_EQ(texture->Specification.Height, expected.Height);
            EXPECT_EQ(texture->Specification.Depth, expected.Depth);
            EXPECT_EQ(texture->Specification.Is3D, expected.Is3D);
            EXPECT_EQ(texture->Specification.IsUsageStorage, expected.IsUsageStorage);
        }
        for (uint32_t index = 0; index < Bank::MaxFrames; ++index)
        {
            EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(initial), index + 1 < Bank::MaxFrames ? Renderers::RGScenePipelinePreparationState::Preparing : Renderers::RGScenePipelinePreparationState::Ready);
        }
        const auto incoming_color = owner.Graph->GetPreparedSceneTarget(Renderers::SceneRenderTarget::Color)->Get()->Handle;
        ASSERT_TRUE(owner.Graph->ActivateSceneDescriptors());
        ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 1, 96, 64, false));
        const auto active_color = owner.Graph->ResourceInspector->GetRenderTarget(Renderers::RendererResourceName::FrameColorRenderTargetName);
        EXPECT_EQ(active_color.Index, incoming_color.Index);
        EXPECT_EQ(active_color.Generation, incoming_color.Generation);
        EXPECT_EQ(owner.Pixel(-.55f, -.5f), frame->Triangles[0].Color);
        owner.Graph->EndScenePipelinePreparation();
        // RenderGraphFrame waits its real readback fence. Complete the normal
        // retirement service before starting more candidates in this deliberately
        // eight-texture fixture; do not mask admission failures by enlarging it.
        owner.Manager->PollFrameResources();
        RRMUploadBatchTestHelper::FlushTextureReleases(*owner.Manager);
        owner.DrainRetiredTextures();
        owner.Device->PendingFree.Drain(&owner.Device->GpuMem, owner.Vulkan.Device, UINT64_MAX);
        required.ScenePipelineProfile.Targets = Renderers::SceneTargetMask(Renderers::SceneRenderTarget::Color);
        const auto arena_before               = owner.Arena.m_current_offset;
        ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
        EXPECT_EQ(owner.Graph->PollScenePipelinePreparation(), Renderers::RGScenePipelinePreparationState::Ready);
        EXPECT_EQ(owner.Arena.m_current_offset, arena_before);
        owner.Graph->EndScenePipelinePreparation();
        required.ScenePipelineProfile.Formats[0] = Specs::ImageFormat::R16G16B16A16_SFLOAT;
        const auto draws                         = owner.Probe.BodyCalls + owner.Probe.PrimaryCalls;
        const auto sinks                         = owner.Sink.Executions;
        owner.Commands->Begin();
        EXPECT_EQ(owner.Graph->Execute(owner.Commands.get()), nullptr);
        EXPECT_EQ(owner.Probe.BodyCalls + owner.Probe.PrimaryCalls, draws);
        EXPECT_EQ(owner.Sink.Executions, sinks);
        owner.Commands->End();
        ASSERT_EQ(vkResetCommandBuffer(owner.Commands->GetHandle(), 0), VK_SUCCESS);
        owner.Commands->SetState(ZEngine::Hardwares::CommandBufferState::Idle);
        required.ScenePipelineProfile.Formats[0] = Specs::ImageFormat::R8G8B8A8_UNORM;
        ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 0, 128, 96, false));
        EXPECT_EQ(owner.Pixel(-.55f, -.5f), frame->Triangles[0].Color);
        owner.DrawShader->LayoutBindingSpecifications.init(&owner.Arena, 2);
        for (const auto name : {"DiffuseIrradiance", "SpecularEnvironment", "BrdfIntegrationLut", "EnvMap"})
        {
            SCOPED_TRACE(name);
            owner.DrawShader->LayoutBindingSpecifications.clear();
            owner.DrawShader->LayoutBindingSpecifications.push(owner.DrawShader->BindingsByName.at("UBCamera"));
            owner.DrawShader->LayoutBindingSpecifications.push({.Set = 0, .Binding = 1, .Name = name, .DescriptorTypeValue = Specs::DescriptorType::SAMPLED_IMAGE, .Flags = Specs::ShaderStageFlags::FRAGMENT});
            required.ScenePipelineProfile.Descriptors = std::strcmp(name, "EnvMap") == 0 ? Renderers::RGSceneDescriptorProfile::EnvironmentBackground : Renderers::RGSceneDescriptorProfile::Transparent;
            ASSERT_TRUE(owner.Graph->BeginScenePipelinePreparation(Renderers::RGScenePipelineUsage::Core));
            ASSERT_EQ(owner.Graph->PollScenePipelinePreparation(), Renderers::RGScenePipelinePreparationState::Ready);
            // Missing retained environment input is a terminal refusal, not a
            // raw-handle lookup or fallback to the outgoing scene's lighting.
            EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(initial), Renderers::RGScenePipelinePreparationState::Preparing); // Target image first.
            EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(initial), Renderers::RGScenePipelinePreparationState::Preparing); // Then framebuffer/views.
            EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(initial), Renderers::RGScenePipelinePreparationState::Preparing); // Then the private backend.
            EXPECT_EQ(owner.Graph->PollSceneDescriptorPreparation(initial), Renderers::RGScenePipelinePreparationState::Failed);
            EXPECT_FALSE(owner.Graph->ActivateSceneDescriptors());
            EXPECT_FALSE(required.SceneDescriptorsAdopted);
            owner.Graph->EndScenePipelinePreparation();
            owner.Manager->PollFrameResources();
            RRMUploadBatchTestHelper::FlushTextureReleases(*owner.Manager);
            owner.DrainRetiredTextures();
            owner.Device->PendingFree.Drain(&owner.Device->GpuMem, owner.Vulkan.Device, UINT64_MAX);
        }
        owner.DrawShader->LayoutBindingSpecifications.clear();
        owner.DrawShader->LayoutBindingSpecifications.push(owner.DrawShader->BindingsByName.at("UBCamera"));
        EXPECT_EQ(owner.Vulkan.ValidationErrors.load(), 0u);
    }
}

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
    bool                                                              Initialized                   = false;
    bool                                                              ResourceTexturesInitialized   = false;
    bool                                                              TextureUploadSlotsInitialized = false;

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
            Manager->FinishResourceFrame();
            Manager->BeginFrame(0); // Retire all submitted staging allocations.
            RRMUploadBatchTestHelper::ReleaseSceneBuffersAfterIdle(*Manager);
            allocator().FreeBuffer(Target);
        }
        if (TextureUploadSlotsInitialized)
        {
            RRMUploadBatchTestHelper::ShutdownTextureUploadSlots(*Manager);
        }
        for (auto& command : Commands)
            command.reset();
        if (ResourceTexturesInitialized)
        {
            Device->ImageBufferManager.Dispose();
            Device->GlobalTextures.Dispose();
            Device->ImageViewCacheSlab.Shutdown();
            Device->RRM = nullptr;
        }
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
    ZEngine::Rendering::Textures::TextureHandle CreateProducerTestTexture(cstring name)
    {
        Device->MaxGlobalTexture = 8;
        Device->GlobalTextures.Initialize(&Memory.MainArena, 8);
        Device->ImageBufferManager.Initialize(&Memory.MainArena, 8);
        Device->ImageViewCacheSlab.Init(&Memory.MainArena, 32 * 1024);
        Device->RRM                                                            = Manager.get();
        ResourceTexturesInitialized                                            = true;
        ZEngine::Rendering::Specifications::TextureSpecification specification = {};
        specification.Width = specification.Height = 1;
        specification.Format                       = ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM;
        return Device->CreateTexture(specification, name);
    }

    void CheckRejectedTextureUploadReclamation(bool transfer)
    {
        const auto texture = CreateProducerTestTexture("rejected-texture-upload");
        ASSERT_TRUE(texture.Valid());
        RRMUploadBatchTestHelper::InitializeTextureUploadSlots(*Manager, *Device, UploadCommands, transfer);
        TextureUploadSlotsInitialized = true;
        VmaTotalStatistics before     = {};
        vmaCalculateStatistics(allocator().Allocator, &before);
        uint8_t  pixels[4]      = {17, 53, 127, 255};
        uint64_t previous_value = 0;
        for (uint32_t cycle = 0; cycle < 32; ++cycle)
        {
            SCOPED_TRACE(cycle);
            ASSERT_TRUE(Manager->UploadTextureBuffer(0, 0, texture, pixels, sizeof(pixels)).Valid());
            const uint64_t value = RRMUploadBatchTestHelper::TextureUploadSlotValue(*Manager, transfer);
            EXPECT_GT(value, previous_value);
            previous_value = value;
            EXPECT_TRUE(RRMUploadBatchTestHelper::TextureUploadSlotHasStaging(*Manager, transfer));
            EXPECT_EQ(Commands[0]->GetState(), ZEngine::Hardwares::CommandBufferState::Executable);
            RRMUploadBatchTestHelper::ClearTextureUploadJobs(*Manager);
            EXPECT_EQ(RRMUploadBatchTestHelper::TextureUploadSlotValue(*Manager, transfer), 0u);
            EXPECT_FALSE(RRMUploadBatchTestHelper::TextureUploadSlotHasStaging(*Manager, transfer));
            EXPECT_EQ(Commands[0]->GetState(), ZEngine::Hardwares::CommandBufferState::Idle);
            EXPECT_EQ(Manager->GetTextureDecodeState(texture), RRM::TextureDecodeState::Failed);
            EXPECT_FALSE(Manager->PinTextureVersion(texture));
            EXPECT_EQ(Manager->FindStreamingUploadTicket(texture), nullptr);
            VmaTotalStatistics after = {};
            vmaCalculateStatistics(allocator().Allocator, &after);
            EXPECT_EQ(after.total.statistics.allocationCount, before.total.statistics.allocationCount);
            EXPECT_EQ(after.total.statistics.allocationBytes, before.total.statistics.allocationBytes);
        }
        // Queue-capacity rejection happens inline, before UploadTextureBuffer
        // returns. It must not restore a layout belonging to discarded commands.
        RRMUploadBatchTestHelper::SaturateTextureUploadQueue(*Manager);
        ASSERT_TRUE(Manager->UploadTextureBuffer(0, 0, texture, pixels, sizeof(pixels)).Valid());
        EXPECT_EQ(RRMUploadBatchTestHelper::TextureUploadSlotValue(*Manager, transfer), 0u);
        EXPECT_FALSE(RRMUploadBatchTestHelper::TextureUploadSlotHasStaging(*Manager, transfer));
        const auto* resource = Device->GlobalTextures.Access(texture);
        ASSERT_NE(resource, nullptr);
        EXPECT_EQ(Device->ImageBufferManager.Access(resource->BufferHandle)->Layout, ZEngine::Rendering::Specifications::ImageLayout::UNDEFINED);
        RRMUploadBatchTestHelper::ClearTextureUploadJobs(*Manager);

        // A new valid generation/request reuses the exact slot after more
        // cancellations than the entire per-pool command budget.
        const auto next = Device->CreateTexture(resource->Specification, "valid-after-rejected-texture");
        ASSERT_TRUE(next.Valid());
        ASSERT_TRUE(Manager->UploadTextureBuffer(0, 0, next, pixels, sizeof(pixels)).Valid());
        Manager->SubmitAsyncUploads();
        const auto* ticket = Manager->FindStreamingUploadTicket(next);
        ASSERT_NE(ticket, nullptr); // Submitted jobs do not wait on a host gate.
        ticket->CompletionTimeline->Wait(ticket->CompletionValue);
        RRMUploadBatchTestHelper::RetireTextureUploadSlots(*Manager);
        EXPECT_EQ(RRMUploadBatchTestHelper::TextureUploadSlotValue(*Manager, transfer), 0u);
        EXPECT_FALSE(RRMUploadBatchTestHelper::TextureUploadSlotHasStaging(*Manager, transfer));
        EXPECT_EQ(Manager->GetTextureDecodeState(next), RRM::TextureDecodeState::Succeeded);
        EXPECT_TRUE(Manager->PinTextureVersion(next));
        const auto accepted = *ticket;
        Manager->ClearAsyncUploads();
        EXPECT_NE(Manager->FindStreamingUploadTicket(next), nullptr);
        Swapchain.BufferredFrameCount = 3;
        Manager->ResetTextureTimelines();
        const auto* retained = Manager->FindStreamingUploadTicket(next);
        ASSERT_NE(retained, nullptr);
        EXPECT_EQ(retained->CompletionTimeline, accepted.CompletionTimeline);
        EXPECT_EQ(retained->CompletionValue, accepted.CompletionValue);
        EXPECT_EQ(retained->PreReleaseLayout, accepted.PreReleaseLayout);
        EXPECT_EQ(retained->PostReleaseLayout, accepted.PostReleaseLayout);
    }

    void CheckQueuedTextureUploadLifetime(bool transfer, bool submit)
    {
        const auto texture = CreateProducerTestTexture("queued-texture-upload-lifetime");
        ASSERT_TRUE(texture.Valid());
        RRMUploadBatchTestHelper::InitializeTextureUploadSlots(*Manager, *Device, UploadCommands, transfer);
        TextureUploadSlotsInitialized = true;
        uint8_t pixels[4]             = {17, 53, 127, 255};
        // Invalid input must not leave a hold which would strand destruction.
        EXPECT_FALSE(Manager->UploadTextureBuffer(0, 0, texture, pixels, 5).Valid());
        EXPECT_FALSE(RRMUploadBatchTestHelper::TextureUploadPending(*Manager, texture));
        EXPECT_TRUE(Manager->CanWriteTextureVersion(texture));
        EXPECT_TRUE(RRMUploadBatchTestHelper::RefuseUploadWithBusyTextureSlots(*Manager, texture, pixels, sizeof(pixels), transfer));
        EXPECT_FALSE(RRMUploadBatchTestHelper::TextureUploadPending(*Manager, texture));
        EXPECT_TRUE(Manager->CanWriteTextureVersion(texture));
        ASSERT_TRUE(Manager->UploadTextureBuffer(0, 0, texture, pixels, sizeof(pixels)).Valid());
        ASSERT_TRUE(RRMUploadBatchTestHelper::TextureUploadPending(*Manager, texture));
        const uint64_t value = RRMUploadBatchTestHelper::TextureUploadSlotValue(*Manager, transfer);
        EXPECT_NE(value, 0u);
        EXPECT_EQ(Manager->FindStreamingUploadTicket(texture), nullptr); // Not submitted yet.
        EXPECT_FALSE(Manager->CanWriteTextureVersion(texture));
        EXPECT_FALSE(Manager->BeginTextureVersionWrite(texture));
        EXPECT_FALSE(Manager->PinTextureVersion(texture));
        VmaTotalStatistics before = {};
        vmaCalculateStatistics(allocator().Allocator, &before);
        EXPECT_FALSE(Manager->UploadTextureBuffer(0, 0, texture, pixels, sizeof(pixels)).Valid());
        EXPECT_FALSE(Device->WriteTextureData(Commands[0].get(), texture, pixels, nullptr, false, sizeof(pixels)));
        VmaTotalStatistics after = {};
        vmaCalculateStatistics(allocator().Allocator, &after);
        EXPECT_EQ(after.total.statistics.allocationCount, before.total.statistics.allocationCount);
        EXPECT_EQ(after.total.statistics.allocationBytes, before.total.statistics.allocationBytes);
        EXPECT_EQ(RRMUploadBatchTestHelper::TextureUploadSlotValue(*Manager, transfer), value);

        std::thread release_owner([&] {
            Device->DestroyTexture(texture);
            Device->DestroyTexture(texture);
        });
        release_owner.join();
        EXPECT_TRUE(Device->TextureHandleToDispose.empty());
        RRMUploadBatchTestHelper::FlushTextureReleases(*Manager);
        EXPECT_TRUE(Device->TextureHandleToDispose.empty());
        EXPECT_TRUE(Device->GlobalTextures.IsLive(texture));
        if (submit)
        {
            // Destruction seals new work, not the already recorded producer.
            Manager->SubmitAsyncUploads();
            const auto* ticket = Manager->FindStreamingUploadTicket(texture);
            ASSERT_NE(ticket, nullptr);
            EXPECT_FALSE(RRMUploadBatchTestHelper::TextureUploadPending(*Manager, texture));
            ticket->CompletionTimeline->Wait(ticket->CompletionValue);
            RRMUploadBatchTestHelper::RetireTextureUploadSlots(*Manager);
        }
        else
        {
            RRMUploadBatchTestHelper::ClearTextureUploadJobs(*Manager);
            EXPECT_EQ(Manager->GetTextureDecodeState(texture), RRM::TextureDecodeState::Failed);
        }
        EXPECT_FALSE(RRMUploadBatchTestHelper::TextureUploadPending(*Manager, texture));
        EXPECT_EQ(RRMUploadBatchTestHelper::TextureUploadSlotValue(*Manager, transfer), 0u);
        EXPECT_FALSE(RRMUploadBatchTestHelper::TextureUploadSlotHasStaging(*Manager, transfer));
        RRMUploadBatchTestHelper::FlushTextureReleases(*Manager);
        ZEngine::Hardwares::TextureDisposeEntry disposal = {};
        EXPECT_TRUE(Device->TextureHandleToDispose.pop(disposal));
        EXPECT_EQ(disposal.Handle.Index, texture.Index);
        EXPECT_EQ(disposal.Handle.Generation, texture.Generation);
        EXPECT_FALSE(Manager->CanWriteTextureVersion(texture));
        RRMUploadBatchTestHelper::FlushTextureReleases(*Manager);
        EXPECT_TRUE(Device->TextureHandleToDispose.empty());
    }

    void CheckAcknowledgedProducerRetirement(bool direct_device)
    {
        const auto texture = CreateProducerTestTexture("acknowledged-producer-retirement");
        ASSERT_TRUE(texture.Valid());
        Append(0, 42);
        auto gate                         = std::make_unique<ZEngine::Rendering::Primitives::Semaphore>(Device.get(), true);
        Swapchain.RenderTimeline          = gate.get();
        Swapchain.RenderTimelineNextValue = 1;
        Manager->EndFrame();
        // No fatal assertions may strand the accepted submission after this point.
        const ZEngine::Hardwares::StreamingUploadTicket ticket = {.Texture = texture, .CompletionTimeline = Timeline.get(), .CompletionValue = 1, .ProducerQueueFamily = Device->GraphicFamilyIndex};
        RRMUploadBatchTestHelper::PublishSceneTextureProducer(*Manager, Memory.MainArena, ticket);
        Manager->AcknowledgeStreamingUploadTicket(ticket);
        if (direct_device)
        {
            std::thread release_owner([&] {
                Device->DestroyTexture(texture);
                Device->DestroyTexture(texture);
            });
            release_owner.join();
            EXPECT_TRUE(Device->TextureHandleToDispose.empty());
            EXPECT_FALSE(Manager->PinTextureVersion(texture));
            EXPECT_FALSE(Manager->CanWriteTextureVersion(texture));
        }
        else
        {
            Manager->ReleaseTexture(texture);
            Manager->ReleaseTexture(texture);
        }
        RRMUploadBatchTestHelper::FlushTextureReleases(*Manager);
        EXPECT_EQ(Manager->FindStreamingUploadTicket(texture), nullptr);
        EXPECT_TRUE(Device->TextureHandleToDispose.empty());
        EXPECT_TRUE(Device->GlobalTextures.IsLive(texture));

        gate->Signal(1);
        EXPECT_EQ(Read(1), 42u);
        RRMUploadBatchTestHelper::FlushTextureReleases(*Manager);
        ZEngine::Hardwares::TextureDisposeEntry disposal = {};
        EXPECT_TRUE(Device->TextureHandleToDispose.pop(disposal));
        EXPECT_EQ(disposal.Handle.Index, texture.Index);
        EXPECT_EQ(disposal.Handle.Generation, texture.Generation);
        RRMUploadBatchTestHelper::FlushTextureReleases(*Manager);
        EXPECT_TRUE(Device->TextureHandleToDispose.empty()); // Exactly one retirement admission.
        Swapchain.RenderTimeline          = nullptr;
        Swapchain.RenderTimelineNextValue = 0;
    }

    RRM::SceneResourcePreparationResult FinishScenePreparation(RRM::SceneResourcePreparationId id)
    {
        RRM::SceneResourcePreparationResult result = {};
        for (uint32_t tick = 0; tick < 1000; ++tick)
        {
            Manager->PollSceneResourcePreparations();
            if (!Manager->PollSceneResourcePreparation(id, &result) || result.State != RRM::SceneResourcePreparationState::Preparing)
            {
                return result;
            }
        }
        ADD_FAILURE() << "Scene preparation exceeded its deterministic slice bound";
        return result;
    }
};

TEST_F(RenderResourceManagerBatchTest, BufferAliasExecutesAfterBackingRetiresAndKeepsChargeUntilCompletion)
{
    const auto before = allocator().GetAllocationStatistics();
    auto       alias  = allocator().AllocateAliasingBuffer(Target, Target.Size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "surviving_buffer_alias");
    ASSERT_TRUE(alias);
    allocator().FreeBuffer(Target);
    const auto retained = allocator().GetAllocationStatistics();
    EXPECT_EQ(retained.AllocationCount, before.AllocationCount);
    EXPECT_EQ(retained.AllocationBytes, before.AllocationBytes);

    const auto                     command = Commands[0]->GetHandle();
    const VkCommandBufferBeginInfo begin   = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    ASSERT_EQ(vkBeginCommandBuffer(command, &begin), VK_SUCCESS);
    constexpr uint32_t expected = 0xabcdef12;
    vkCmdFillBuffer(command, alias.Handle, 0, alias.Size, expected);
    const VkBufferMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .buffer = alias.Handle, .size = VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    ASSERT_EQ(vkEndCommandBuffer(command), VK_SUCCESS);
    const auto                          timeline      = Timeline->GetHandle();
    const uint64_t                      signal        = 1;
    const VkTimelineSemaphoreSubmitInfo timeline_info = {.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, .signalSemaphoreValueCount = 1, .pSignalSemaphoreValues = &signal};
    const VkSubmitInfo                  submit        = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = &timeline_info, .commandBufferCount = 1, .pCommandBuffers = &command, .signalSemaphoreCount = 1, .pSignalSemaphores = &timeline};
    const auto                          queue         = Device->GetQueue(ZEngine::Rendering::QueueType::GRAPHIC_QUEUE).Handle;
    ASSERT_EQ(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), VK_SUCCESS);
    const VkSemaphoreWaitInfo wait = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, .semaphoreCount = 1, .pSemaphores = &timeline, .pValues = &signal};
    ASSERT_EQ(vkWaitSemaphores(s_vk->Device, &wait, 5'000'000'000ULL), VK_SUCCESS);
    uint32_t observed = 0;
    EXPECT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, alias.Allocation, 0, &observed, sizeof(observed)), VK_SUCCESS);
    EXPECT_EQ(observed, expected);

    // Queued retirement is not a credit: only the eligible drain releases it.
    auto                                  retired = std::make_unique<ZEngine::Hardwares::DeferredFreeQueue>();
    ZEngine::Hardwares::DeferredFreeEntry entry   = {};
    entry.EntryKind                               = ZEngine::Hardwares::DeferredFreeEntry::Kind::Buffer;
    entry.TimelineValue                           = signal;
    entry.Data.Buffer                             = alias;
    retired->Enqueue(entry);
    alias = {};
    retired->Drain(&allocator(), s_vk->Device, 0);
    EXPECT_EQ(allocator().GetAllocationStatistics().AllocationBytes, retained.AllocationBytes);
    retired->Drain(&allocator(), s_vk->Device, signal);
    EXPECT_EQ(allocator().GetAllocationStatistics().AllocationCount, before.AllocationCount - 1);
    EXPECT_LT(allocator().GetAllocationStatistics().AllocationBytes, before.AllocationBytes);
}

TEST_F(RenderResourceManagerBatchTest, InitialBufferBundleIsPreparedBeforeReadyAndRetiresBehindEveryAcceptedQueue)
{
    RRM::SceneInitialFrameInput input  = {};
    input.View.Captured                = true;
    input.View.Width                   = 640;
    input.View.Height                  = 480;
    input.View.Camera.View(0, 3)       = -3.f;
    input.View.Camera.Position         = {3.f, 2.f, 1.f};
    input.Lights.PointCount            = 1;
    RRM::SceneResourcePreparationId id = {};
    ASSERT_TRUE(Manager->PrepareSceneResources({1, 1}, 1, {}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    input.Lights.PointCount      = 0;
    input.View.Camera.View(0, 3) = 99.f;
    input.View.Camera.Position   = {};
    auto result                  = FinishScenePreparation(id);
    ASSERT_TRUE(Manager->PollSceneResourcePreparation(id, &result));
    ASSERT_EQ(result.State, RRM::SceneResourcePreparationState::ResourcesRetained);
    EXPECT_TRUE(result.InitialBuffersPrepared);
    EXPECT_FALSE(RRMUploadBatchTestHelper::IsOpen(*Manager));
    EXPECT_EQ(RRMUploadBatchTestHelper::StagingCount(*Manager, 0), 0u);
    ZEngine::Rendering::Scenes::PreparedSceneFrame frame = {};
    ASSERT_TRUE(Manager->GetFramePreparedSceneFrame(id, &frame));
    ASSERT_NE(frame.TransformBuffer, nullptr);
    ASSERT_NE(frame.LightBuffer, nullptr);
    ASSERT_NE(frame.CameraBuffer, nullptr);
    EXPECT_EQ(frame.CameraBuffer->Size, sizeof(Renderers::UBOCameraLayout));
    EXPECT_EQ(frame.CameraBuffer->Usage, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    Renderers::UBOCameraLayout prepared_camera = {};
    ASSERT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, frame.CameraBuffer->Allocation, 0, &prepared_camera, sizeof(prepared_camera)), VK_SUCCESS);
    EXPECT_FLOAT_EQ(prepared_camera.View(0, 3), -3.f);
    EXPECT_FLOAT_EQ(prepared_camera.InvViewProj(0, 3), 3.f);
    EXPECT_FLOAT_EQ(prepared_camera.Position.x, 3.f);
    EXPECT_FLOAT_EQ(prepared_camera.Position.y, 2.f);
    EXPECT_FLOAT_EQ(prepared_camera.Position.z, 1.f);
    EXPECT_EQ(frame.TransformBuffer->Size, sizeof(ZEngine::Core::Maths::Mat4f));
    EXPECT_EQ(frame.LightBuffer->Size, sizeof(input.Lights));
    ZEngine::Rendering::Scenes::LightArrayUBO lights = {};
    EXPECT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, frame.LightBuffer->Allocation, 0, &lights, sizeof(lights)), VK_SUCCESS);
    EXPECT_EQ(lights.PointCount, 1u);
    ZEngine::Core::Maths::Mat4f transform = {};
    EXPECT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, frame.TransformBuffer->Allocation, 0, &transform, sizeof(transform)), VK_SUCCESS);
    for (uint32_t row = 0; row < 4; ++row)
    {
        for (uint32_t column = 0; column < 4; ++column)
        {
            EXPECT_FLOAT_EQ(transform(row, column), row == column ? 1.f : 0.f);
        }
    }
    const auto allocation_count = LiveAllocationBytes();
    ASSERT_TRUE(Manager->ReleaseSceneResourcePreparation(id));
    Manager->PollSceneResourcePreparations();
    EXPECT_TRUE(Manager->PollSceneResourcePreparation(id, &result)); // recording, not submitted yet
    auto first  = std::make_unique<ZEngine::Rendering::Primitives::Semaphore>(Device.get(), true);
    auto second = std::make_unique<ZEngine::Rendering::Primitives::Semaphore>(Device.get(), true);
    Manager->NotifyResourceSubmission(0, first.get(), 4);
    Manager->NotifyResourceSubmission(1, second.get(), 7);
    Manager->FinishResourceFrame();
    Manager->PollSceneResourcePreparations();
    EXPECT_TRUE(Manager->PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(LiveAllocationBytes(), allocation_count);
    RRMUploadBatchTestHelper::TickScenePreparation(*Manager, true);
    EXPECT_TRUE(Manager->PollSceneResourcePreparation(id, &result)); // upload/device failure is not consumer completion
    EXPECT_EQ(LiveAllocationBytes(), allocation_count);
    EXPECT_FALSE(Manager->ForgetResourceTimeline(first.get()));
    first->Signal(4);
    Manager->BeginFrame(0);
    Manager->PollSceneResourcePreparations();
    EXPECT_TRUE(Manager->PollSceneResourcePreparation(id, &result));
    EXPECT_TRUE(Manager->ForgetResourceTimeline(first.get()));
    EXPECT_FALSE(Manager->ForgetResourceTimeline(second.get()));
    second->Signal(7);
    Manager->BeginFrame(0);
    Manager->PollSceneResourcePreparations();
    EXPECT_FALSE(Manager->PollSceneResourcePreparation(id, &result));
    EXPECT_LT(LiveAllocationBytes(), allocation_count);
    EXPECT_TRUE(Manager->ForgetResourceTimeline(second.get()));
}

TEST_F(RenderResourceManagerBatchTest, InitialBufferBundleSurvivesMailboxCancellationAndUnsubmittedRecordingReclaimsWithoutAGpuWait)
{
    const auto                      baseline = LiveAllocationBytes();
    RRM::SceneInitialFrameInput     input    = {};
    RRM::SceneResourcePreparationId id       = {};
    ASSERT_TRUE(Manager->PrepareSceneResources({1, 1}, 1, {}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    ASSERT_EQ(FinishScenePreparation(id).State, RRM::SceneResourcePreparationState::ResourcesRetained);
    auto lease = Manager->AcquireSceneResourceLease(id, 2);
    ASSERT_TRUE(lease);
    ASSERT_TRUE(Manager->ReleaseSceneResourcePreparation(id));
    Manager->PollSceneResourcePreparations();
    ZEngine::Rendering::Scenes::PreparedSceneFrame frame = {};
    ASSERT_TRUE(Manager->GetFramePreparedSceneFrame(id, &frame));
    ASSERT_NE(frame.TransformBuffer, nullptr);
    Manager->FinishResourceFrame(); // no accepted submissions
    Manager->PollSceneResourcePreparations();
    RRM::SceneResourcePreparationResult result = {};
    EXPECT_TRUE(Manager->PollSceneResourcePreparation(id, &result)); // mailbox is still a reader
    EXPECT_GT(LiveAllocationBytes(), baseline);
    lease.Reset();
    Manager->PollSceneResourcePreparations();
    EXPECT_FALSE(Manager->PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(LiveAllocationBytes(), baseline);
    EXPECT_EQ(RRMUploadBatchTestHelper::LastSignal(*Manager, 0), 0u);
}

TEST_F(RenderResourceManagerBatchTest, InitialBufferUploadsAreSlicedAndUseCopiedInputsBeforePublication)
{
    ASSERT_EQ(AssetManager::Instance(), nullptr);
    MemoryManager asset_memory = {};
    asset_memory.Initialize(ZMega(512), {});
    AssetManager::Initialize(&asset_memory.MainArena, nullptr, "/project");
    struct AssetGuard
    {
        RRM& Manager;
        ~AssetGuard()
        {
            RRMUploadBatchTestHelper::ReleaseSceneBuffersAfterIdle(Manager);
            Manager.FinishResourceFrame();
            RRMUploadBatchTestHelper::ResetGeometry(Manager);
            AssetManager::Shutdown();
        }
    } asset_guard{*Manager};
    const auto                               mesh_id     = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    const auto                               material_id = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");
    const auto                               blend_id    = *uuids::uuid::from_string("33333333-3333-4333-8333-333333333333");
    ZEngine::Rendering::Meshes::MeshMaterial unrelated   = {};
    unrelated.AlbedoColor.x                              = .9f;
    uint32_t                     unrelated_index         = 0;
    RRM::MaterialVersionSnapshot unrelated_snapshot      = {};
    ASSERT_TRUE(Manager->GetFrameMaterialVersion(unrelated, &unrelated_index, &unrelated_snapshot));
    ZEngine::Importers::AssetMaterial material = {};
    material.MaterialUUID                      = material_id;
    material.AlbedoColor[3]                    = 1.f;
    material.AlphaMode                         = ZEngine::Rendering::Materials::AlphaMode::Opaque;
    AssetManager::IngestMaterial(std::move(material));
    ZEngine::Importers::AssetMaterial blended = {};
    blended.MaterialUUID                      = blend_id;
    blended.AlbedoColor[3]                    = .5f;
    blended.AlphaMode                         = ZEngine::Rendering::Materials::AlphaMode::Blend;
    AssetManager::IngestMaterial(std::move(blended));
    ZEngine::Importers::AssetMesh source = {};
    source.Vertices.init(&asset_memory.MainArena, 8, 8);
    source.Indices.init(&asset_memory.MainArena, 1, 1);
    source.Indices[0] = 0;
    source.SubMeshes.init(&asset_memory.MainArena, 2);
    source.SubMeshes.push({.MaterialUUID = material_id, .VertexCount = 1, .IndexCount = 1});
    source.SubMeshes.push({.MaterialUUID = blend_id, .VertexCount = 1, .IndexCount = 1});
    const auto* metadata = RRMUploadBatchTestHelper::CaptureMetadata(AssetManager::Instance()->ContainerSlab, source);
    ASSERT_NE(metadata, nullptr);
    RRMUploadBatchTestHelper::InitializeGeometry(*Manager, Memory.MainArena);
    const auto mesh = RRMUploadBatchTestHelper::InstallGeometry(*Manager, metadata);
    ASSERT_TRUE(mesh.IsValid());
    RRMUploadBatchTestHelper::RegisterGeometry(*Manager, mesh_id, mesh);
    constexpr uint32_t                                    instance_count = 2000;
    std::vector<ZEngine::Rendering::Scenes::MeshInstance> instances(instance_count);
    for (uint32_t index = 0; index < instance_count; ++index)
    {
        instances[index].MeshUUID        = mesh_id;
        instances[index].Entity          = {index + 1, 1};
        instances[index].Transform       = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        instances[index].Transform(0, 3) = static_cast<float>(index);
        instances[index].Transform(2, 3) = -static_cast<float>(index / 2);
    }
    RRM::SceneInitialFrameInput input  = {.Instances = instances};
    input.View.Captured                = true;
    input.View.Width                   = 640;
    input.View.Height                  = 480;
    input.View.Camera.View             = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
    RRM::SceneResourcePreparationId id = {};
    ASSERT_TRUE(Manager->PrepareSceneResources({1, 1}, 1, std::array{mesh_id}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    instances.back().Transform(0, 3)                            = -900.f;
    RRM::SceneResourcePreparationResult result                  = {};
    bool                                observed_partial_upload = false;
    uint32_t                            previous_uploaded       = 0;
    for (uint32_t tick = 0; tick < 600; ++tick)
    {
        Manager->PollSceneResourcePreparations();
        ASSERT_TRUE(Manager->PollSceneResourcePreparation(id, &result));
        const auto uploaded = RRMUploadBatchTestHelper::UploadedSceneTransforms(*Manager, id);
        EXPECT_LE(uploaded - previous_uploaded, 64u * RRM::SceneResourceSliceOperations);
        previous_uploaded = uploaded;
        if (uploaded && uploaded < instance_count)
        {
            observed_partial_upload = true;
            EXPECT_EQ(result.State, RRM::SceneResourcePreparationState::Preparing);
            ZEngine::Rendering::Scenes::PreparedSceneFrame partial = {};
            EXPECT_FALSE(Manager->GetPreparedSceneFrame(id, &partial));
        }
        if (result.State != RRM::SceneResourcePreparationState::Preparing)
        {
            break;
        }
    }
    ASSERT_EQ(result.State, RRM::SceneResourcePreparationState::ResourcesRetained);
    EXPECT_TRUE(observed_partial_upload);
    EXPECT_TRUE(result.InitialBuffersPrepared);
    ZEngine::Rendering::Scenes::PreparedSceneFrame frame = {};
    ASSERT_TRUE(Manager->GetPreparedSceneFrame(id, &frame));
    EXPECT_TRUE(frame.PresentationPrepared);
    EXPECT_EQ(frame.OpaqueDrawCount, instance_count);
    ASSERT_NE(frame.TransformBuffer, nullptr);
    EXPECT_EQ(frame.TransformBuffer->Size, instance_count * sizeof(ZEngine::Core::Maths::Mat4f));
    ZEngine::Core::Maths::Mat4f last = {};
    EXPECT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, frame.TransformBuffer->Allocation, (instance_count - 1) * sizeof(last), &last, sizeof(last)), VK_SUCCESS);
    EXPECT_FLOAT_EQ(last(0, 3), static_cast<float>(instance_count - 1));
    ASSERT_NE(frame.MaterialBuffer, nullptr);
    ASSERT_NE(frame.DrawBuffer, nullptr);
    ASSERT_EQ(frame.Draws.size(), 2u * instance_count);
    EXPECT_EQ(frame.DrawBuffer->Size, frame.Draws.size() * sizeof(ZEngine::Rendering::Meshes::SubMeshAllocation));
    std::vector<ZEngine::Rendering::Meshes::SubMeshAllocation> draws(frame.Draws.size());
    ASSERT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, frame.DrawBuffer->Allocation, 0, draws.data(), draws.size() * sizeof(draws[0])), VK_SUCCESS);
    for (uint32_t index = 0; index < draws.size(); ++index)
    {
        EXPECT_EQ(draws[index].PickToken, index + 1);
        EXPECT_EQ(draws[index].TransformId, index / 2);
        EXPECT_EQ(draws[index].MaterialId, frame.Draws[index].Allocation.MaterialId);
        EXPECT_EQ(draws[index].IndexCount, 1u);
    }
    const auto opaque_index = draws[0].MaterialId;
    const auto blend_index  = draws[1].MaterialId;
    EXPECT_NE(opaque_index, unrelated_index);
    EXPECT_NE(blend_index, unrelated_index);
    std::vector<ZEngine::Rendering::Meshes::MeshMaterial> materials(frame.MaterialBuffer->Size / sizeof(unrelated));
    ASSERT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, frame.MaterialBuffer->Allocation, 0, materials.data(), frame.MaterialBuffer->Size), VK_SUCCESS);
    EXPECT_EQ(materials[opaque_index].AlphaMode, ZEngine::Rendering::Materials::AlphaMode::Opaque);
    EXPECT_EQ(materials[blend_index].AlphaMode, ZEngine::Rendering::Materials::AlphaMode::Blend);
    EXPECT_FLOAT_EQ(materials[blend_index].AlbedoColor.w, .5f);
    EXPECT_FLOAT_EQ(materials[unrelated_index].AlbedoColor.x, 1.f); // A hole, not another owner's unretained entry.
    ASSERT_EQ(frame.CullingInputBuffers.size(), ZEngine::Rendering::Scenes::SceneData::MAX_FRAMES_IN_FLIGHT);
    ASSERT_EQ(frame.CulledIndirectBuffers.size(), frame.CullingInputBuffers.size());
    ASSERT_EQ(frame.SelectionTokenBuffers.size(), frame.CullingInputBuffers.size());
    for (uint32_t slot = 0; slot < frame.CullingInputBuffers.size(); ++slot)
    {
        const auto& selection = frame.SelectionTokenBuffers[slot];
        ASSERT_EQ(selection.Size, (frame.Draws.size() + 1) * sizeof(uint32_t));
        std::vector<uint32_t> tokens(frame.Draws.size() + 1, UINT32_MAX);
        ASSERT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, selection.Allocation, 0, tokens.data(), selection.Size), VK_SUCCESS);
        EXPECT_TRUE(std::all_of(tokens.begin(), tokens.end(), [](uint32_t token) { return token == 0; }));
        std::vector<ZEngine::Rendering::Scenes::FrustumCullingInput> commands(frame.Draws.size());
        const auto&                                                  buffer = frame.CullingInputBuffers[slot];
        ASSERT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, buffer.Allocation, 0, commands.data(), commands.size() * sizeof(commands[0])), VK_SUCCESS);
        for (uint32_t index = 0; index < instance_count; ++index)
        {
            EXPECT_EQ(commands[index].Command.firstInstance, 2 * index);
            // Far-to-near, retaining original draw order at equal depth.
            const auto transparent_instance = instance_count - 2 - 2 * (index / 2) + index % 2;
            EXPECT_EQ(commands[instance_count + index].Command.firstInstance, 2 * transparent_instance + 1);
            EXPECT_EQ(commands[index].Command.vertexCount, 1u);
        }
        EXPECT_EQ(frame.CulledIndirectBuffers[slot].Size, frame.Draws.size() * sizeof(VkDrawIndirectCommand));
        EXPECT_EQ(frame.CulledIndirectBuffers[slot].Domain, GpuMemoryDomain::DeviceGeometry);
        EXPECT_EQ(frame.CulledIndirectBuffers[slot].Usage, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        for (uint32_t prior = 0; prior < slot; ++prior)
        {
            EXPECT_NE(buffer.Handle, frame.CullingInputBuffers[prior].Handle);
            EXPECT_NE(frame.CulledIndirectBuffers[slot].Handle, frame.CulledIndirectBuffers[prior].Handle);
            EXPECT_NE(selection.Handle, frame.SelectionTokenBuffers[prior].Handle);
        }
    }
    const uint32_t selected_token = 2;
    ASSERT_TRUE(allocator().WriteHostBuffer(frame.SelectionTokenBuffers[0], &selected_token, sizeof(selected_token), sizeof(uint32_t)));
    uint32_t untouched_token = UINT32_MAX;
    ASSERT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, frame.SelectionTokenBuffers[1].Allocation, sizeof(uint32_t), &untouched_token, sizeof(untouched_token)), VK_SUCCESS);
    EXPECT_EQ(untouched_token, 0u);
    ZEngine::Rendering::Scenes::FrustumCullingInput replacement = {};
    replacement.Command.vertexCount                             = 17;
    EXPECT_TRUE(allocator().WriteHostBuffer(frame.CullingInputBuffers[0], &replacement, sizeof(replacement)));
    ZEngine::Rendering::Scenes::FrustumCullingInput untouched = {};
    EXPECT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, frame.CullingInputBuffers[1].Allocation, 0, &untouched, sizeof(untouched)), VK_SUCCESS);
    EXPECT_EQ(untouched.Command.vertexCount, 1u);
    ZEngine::Rendering::Scenes::SceneData selected = {};
    selected.CulledIndirectBuffers[0]              = frame.CulledIndirectBuffers[0];
    selected.ActiveCulledIndirectBuffer            = &frame.CulledIndirectBuffers[1];
    EXPECT_EQ(selected.GetCulledIndirectBuffer(0).Handle, frame.CulledIndirectBuffers[1].Handle);
    selected.ActiveCulledIndirectBuffer = nullptr;
    EXPECT_EQ(selected.GetCulledIndirectBuffer(0).Handle, frame.CulledIndirectBuffers[0].Handle);
    EXPECT_FALSE(RRMUploadBatchTestHelper::IsOpen(*Manager));
    EXPECT_EQ(RRMUploadBatchTestHelper::StagingCount(*Manager, 0), 0u);
}

TEST_F(RenderResourceManagerBatchTest, PartialInitialBufferPreparationIsSlicedAndCancellationReclaimsIt)
{
    for (uint32_t operations : {1u, 3u, 5u, 7u, 9u, 12u, 16u, 18u, 20u, 22u})
    {
        RRM::SceneInitialFrameInput     input    = {};
        RRM::SceneResourcePreparationId id       = {};
        const auto                      baseline = LiveAllocationBytes();
        ASSERT_TRUE(Manager->PrepareSceneResources({1, operations}, 1, {}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
        uint32_t budget = operations;
        RRMUploadBatchTestHelper::PumpSceneBufferSlice(*Manager, id, budget);
        EXPECT_EQ(budget, 0u);
        RRM::SceneResourcePreparationResult result = {};
        ASSERT_TRUE(Manager->PollSceneResourcePreparation(id, &result));
        EXPECT_EQ(result.State, RRM::SceneResourcePreparationState::Preparing);
        EXPECT_FALSE(result.InitialBuffersPrepared);
        ZEngine::Rendering::Scenes::PreparedSceneFrame frame = {};
        EXPECT_FALSE(Manager->GetPreparedSceneFrame(id, &frame));
        EXPECT_GT(LiveAllocationBytes(), baseline);
        ASSERT_TRUE(Manager->ReleaseSceneResourcePreparation(id));
        Manager->PollSceneResourcePreparations();
        EXPECT_FALSE(Manager->PollSceneResourcePreparation(id, &result));
        EXPECT_EQ(LiveAllocationBytes(), baseline);
    }
}

TEST_F(RenderResourceManagerBatchTest, InitialBufferAllocationAndWriteFailureDoNotPublishAndPermitRetry)
{
    RRM::SceneInitialFrameInput input = {};
    for (uint32_t attempt = 1; attempt <= 3; ++attempt)
    {
        const auto                      baseline = LiveAllocationBytes();
        RRM::SceneResourcePreparationId id       = {};
        ASSERT_TRUE(Manager->PrepareSceneResources({1, attempt}, 1, {}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
        if (attempt == 1)
        {
            const auto allocator_handle = Device->GpuMem.Allocator;
            Device->GpuMem.Allocator    = nullptr;
            RRMUploadBatchTestHelper::TickScenePreparation(*Manager);
            Device->GpuMem.Allocator = allocator_handle;
        }
        else if (attempt == 2)
        {
            uint32_t budget = 1;
            RRMUploadBatchTestHelper::PumpSceneBufferSlice(*Manager, id, budget);
            RRMUploadBatchTestHelper::FailNextSceneBufferWrite(*Manager, id);
            RRMUploadBatchTestHelper::TickScenePreparation(*Manager);
        }
        else
        {
            EXPECT_EQ(FinishScenePreparation(id).State, RRM::SceneResourcePreparationState::ResourcesRetained);
        }
        RRM::SceneResourcePreparationResult result = {};
        ASSERT_TRUE(Manager->PollSceneResourcePreparation(id, &result));
        EXPECT_EQ(result.State, attempt == 3 ? RRM::SceneResourcePreparationState::ResourcesRetained : RRM::SceneResourcePreparationState::Failed);
        if (attempt != 3)
        {
            EXPECT_EQ(result.Error, attempt == 1 ? RRM::SceneResourcePreparationError::InitialBufferAllocation : RRM::SceneResourcePreparationError::InitialBufferWrite);
            ZEngine::Rendering::Scenes::PreparedSceneFrame frame = {};
            EXPECT_FALSE(Manager->GetPreparedSceneFrame(id, &frame));
        }
        ASSERT_TRUE(Manager->ReleaseSceneResourcePreparation(id));
        RRMUploadBatchTestHelper::TickScenePreparation(*Manager);
        EXPECT_FALSE(Manager->PollSceneResourcePreparation(id, &result));
        EXPECT_EQ(LiveAllocationBytes(), baseline);
    }
}

TEST_F(RenderResourceManagerBatchTest, EveryInitialBufferBoundaryFailsClosedAndReclaimsBeforeRetry)
{
    const auto                  baseline = LiveAllocationBytes();
    RRM::SceneInitialFrameInput input    = {};
    for (uint32_t operations = 0; operations < 28; ++operations)
    {
        // The captured empty view adds three bounded ordering steps before
        // allocations and two camera steps after the original buffer bundle.
        input.View.Captured                = true;
        input.View.Width                   = 640;
        input.View.Height                  = 480;
        RRM::SceneResourcePreparationId id = {};
        ASSERT_TRUE(Manager->PrepareSceneResources({1, operations + 1}, 1, {}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
        uint32_t budget = operations;
        RRMUploadBatchTestHelper::PumpSceneBufferSlice(*Manager, id, budget);
        EXPECT_EQ(budget, 0u);
        const auto allocator_handle = Device->GpuMem.Allocator;
        Device->GpuMem.Allocator    = nullptr; // Deterministic refusal at the next allocation/write boundary.
        RRMUploadBatchTestHelper::TickScenePreparation(*Manager);
        Device->GpuMem.Allocator                   = allocator_handle;
        RRM::SceneResourcePreparationResult result = {};
        ASSERT_TRUE(Manager->PollSceneResourcePreparation(id, &result));
        EXPECT_EQ(result.State, RRM::SceneResourcePreparationState::Failed);
        const auto boundary       = operations < 3 ? 0u : operations - 3;
        const bool write_boundary = (boundary < 14 && (boundary % 2) != 0) || (boundary >= 17 && (boundary % 2) == 0);
        EXPECT_EQ(result.Error, write_boundary ? RRM::SceneResourcePreparationError::InitialBufferWrite : RRM::SceneResourcePreparationError::InitialBufferAllocation);
        EXPECT_FALSE(result.InitialBuffersPrepared);
        ZEngine::Rendering::Scenes::PreparedSceneFrame frame = {};
        EXPECT_FALSE(Manager->GetPreparedSceneFrame(id, &frame));
        ASSERT_TRUE(Manager->ReleaseSceneResourcePreparation(id));
        RRMUploadBatchTestHelper::TickScenePreparation(*Manager);
        EXPECT_FALSE(Manager->PollSceneResourcePreparation(id, &result));
        EXPECT_EQ(LiveAllocationBytes(), baseline);
    }
    RRM::SceneResourcePreparationId retry = {};
    ASSERT_TRUE(Manager->PrepareSceneResources({1, 29}, 1, {}, &retry, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    EXPECT_TRUE(FinishScenePreparation(retry).InitialBuffersPrepared);
    ASSERT_TRUE(Manager->ReleaseSceneResourcePreparation(retry));
    Manager->PollSceneResourcePreparations();
    EXPECT_EQ(LiveAllocationBytes(), baseline);
}

TEST_F(RenderResourceManagerBatchTest, InvalidCapturedCameraRefusesPublicationAndReclaimsBeforeRetry)
{
    const auto baseline = LiveAllocationBytes();
    for (uint32_t invalid = 0; invalid < 3; ++invalid)
    {
        SCOPED_TRACE(invalid);
        RRM::SceneInitialFrameInput input = {};
        input.View.Captured               = true;
        input.View.Width                  = 640;
        input.View.Height                 = 480;
        if (invalid == 0)
        {
            input.View.Camera.Projection = {}; // Singular, not an assertion path.
        }
        else if (invalid == 1)
        {
            input.View.Camera.Position.x = std::numeric_limits<float>::quiet_NaN();
        }
        else
        {
            input.View.Camera.View(0, 0) = std::numeric_limits<float>::infinity();
        }
        RRM::SceneResourcePreparationId id = {};
        ASSERT_TRUE(Manager->PrepareSceneResources({1, invalid + 1}, 1, {}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
        const auto result = FinishScenePreparation(id);
        EXPECT_EQ(result.State, RRM::SceneResourcePreparationState::Failed);
        EXPECT_EQ(result.Error, RRM::SceneResourcePreparationError::InvalidInitialFrame);
        ZEngine::Rendering::Scenes::PreparedSceneFrame frame = {};
        EXPECT_FALSE(Manager->GetPreparedSceneFrame(id, &frame));
        ASSERT_TRUE(Manager->ReleaseSceneResourcePreparation(id));
        Manager->PollSceneResourcePreparations();
        EXPECT_EQ(LiveAllocationBytes(), baseline);
    }
    RRM::SceneInitialFrameInput input = {};
    input.View.Captured               = true;
    input.View.Width                  = 640;
    input.View.Height                 = 480;
    for (const uint32_t operations : {26u, 27u})
    {
        RRM::SceneResourcePreparationId id = {};
        ASSERT_TRUE(Manager->PrepareSceneResources({2, operations}, 1, {}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
        uint32_t budget = operations;
        RRMUploadBatchTestHelper::PumpSceneBufferSlice(*Manager, id, budget);
        EXPECT_EQ(budget, 0u);
        ASSERT_TRUE(Manager->ReleaseSceneResourcePreparation(id));
        Manager->PollSceneResourcePreparations();
        EXPECT_EQ(LiveAllocationBytes(), baseline);
    }
    RRM::SceneResourcePreparationId retry = {};
    ASSERT_TRUE(Manager->PrepareSceneResources({3, 1}, 1, {}, &retry, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    EXPECT_TRUE(FinishScenePreparation(retry).InitialBuffersPrepared);
    ASSERT_TRUE(Manager->ReleaseSceneResourcePreparation(retry));
    Manager->PollSceneResourcePreparations();
    EXPECT_EQ(LiveAllocationBytes(), baseline);
}

TEST_F(RenderResourceManagerBatchTest, InitialMaterialTextureTracksWorkerRejectionInsteadOfBecomingReady)
{
    ASSERT_EQ(AssetManager::Instance(), nullptr);
    MemoryManager asset_memory = {};
    asset_memory.Initialize(ZMega(512), {});
    const auto directory = std::filesystem::temp_directory_path().string();
    const auto filename  = std::filesystem::path(directory) / ("zengine-scene-initial-texture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
    struct FileGuard
    {
        std::filesystem::path Path = {};
        ~FileGuard()
        {
            std::error_code error = {};
            std::filesystem::remove(Path, error);
        }
    } file_guard{filename};
    const uint8_t pixels[4] = {12, 34, 56, 255};
    ASSERT_NE(stbi_write_png(filename.string().c_str(), 1, 1, 4, pixels, 4), 0);
    Device->MaxGlobalTexture = 8;
    Device->GlobalTextures.Initialize(&Memory.MainArena, 8);
    Device->ImageBufferManager.Initialize(&Memory.MainArena, 8);
    Device->ImageViewCacheSlab.Init(&Memory.MainArena, 32 * 1024);
    Device->RRM                 = Manager.get();
    ResourceTexturesInitialized = true;
    AssetManager::Initialize(&asset_memory.MainArena, Device.get(), directory.c_str());
    RRMUploadBatchTestHelper::InitializeTextureTasks(*Manager, asset_memory.MainArena);
    struct AssetGuard
    {
        RRM& Manager;
        ~AssetGuard()
        {
            RRMUploadBatchTestHelper::ShutdownTextureTasks(Manager);
            AssetManager::Shutdown();
        }
    } asset_guard{*Manager};
    struct UnavailablePool
    {
        decltype(ZEngine::Helpers::ThreadPoolHelper::Pool) Previous = std::move(ZEngine::Helpers::ThreadPoolHelper::Pool);
        ~UnavailablePool()
        {
            ZEngine::Helpers::ThreadPoolHelper::Pool = std::move(Previous);
        }
    } unavailable_pool                            = {};
    const auto                        texture_id  = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    const auto                        material_id = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");
    ZEngine::Core::Containers::String relative    = {};
    relative.init(&asset_memory.MainArena, filename.filename().string().c_str());
    const auto texture = AssetManager::IngestTexture(texture_id, relative);
    ASSERT_TRUE(texture.Valid());
    EXPECT_EQ(Manager->GetTextureDecodeState(texture), RRM::TextureDecodeState::Pending);
    ZEngine::Importers::AssetMaterial material = {};
    material.MaterialUUID                      = material_id;
    material.AlbedoTexUUID                     = texture_id;
    AssetManager::IngestMaterial(std::move(material));
    RRM::MaterialVersionError error = {};
    EXPECT_FALSE(Manager->PinMaterialVersion(material_id, RRM::MaterialVersionPolicy::RequiredSceneBindings, &error));
    EXPECT_EQ(error, RRM::MaterialVersionError::ResourcesUnavailable);
    RRMUploadBatchTestHelper::DispatchTextureTasks(*Manager);
    EXPECT_EQ(RRMUploadBatchTestHelper::PendingTextureTasks(*Manager), 0u);
    EXPECT_EQ(Manager->GetTextureDecodeState(texture), RRM::TextureDecodeState::Failed);
    EXPECT_FALSE(Manager->PinMaterialVersion(material_id, RRM::MaterialVersionPolicy::RequiredSceneBindings, &error));
    EXPECT_EQ(error, RRM::MaterialVersionError::TextureLoadFailed);
    Manager->ForgetTextureDecode(texture);
}

TEST_F(RenderResourceManagerBatchTest, ScenePreparationRestoresCachedAndEvictedGeometryWithoutAViewportDraw)
{
    ASSERT_EQ(AssetManager::Instance(), nullptr);
    MemoryManager asset_memory = {};
    asset_memory.Initialize(ZMega(512), {});
    AssetManager::Initialize(&asset_memory.MainArena, nullptr, "/project");
    // The residency guard destroys this through the device's ledger. Sharing
    // its VMA handle with the suite allocator does not share allocation records.
    BufferView indices = Device->GpuMem.AllocateBuffer(64, VK_BUFFER_USAGE_TRANSFER_DST_BIT, GpuMemoryDomain::HostReadback, "scene_residency_indices");
    struct ResidencyGuard
    {
        RRM&                              Manager;
        ZEngine::Hardwares::VulkanDevice& Device;
        BufferView&                       Indices;
        ~ResidencyGuard()
        {
            Manager.EndFrame();
            vkDeviceWaitIdle(Device.LogicalDevice);
            RRMUploadBatchTestHelper::ReleaseSceneResidencyAfterIdle(Manager);
            Device.GpuMem.FreeBuffer(Indices);
            AssetManager::Shutdown();
        }
    } guard{*Manager, *Device, indices};
    ASSERT_TRUE(indices);
    auto*                             assets      = AssetManager::Instance();
    const auto                        mesh_id     = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    const auto                        material_id = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");
    ZEngine::Importers::AssetMaterial material    = {};
    material.MaterialUUID                         = material_id;
    AssetManager::IngestMaterial(std::move(material));
    auto& mesh    = assets->Meshes.push_use({});
    mesh.MeshUUID = mesh_id;
    mesh.Vertices.init(&asset_memory.MainArena, 8, 8);
    std::fill(mesh.Vertices.begin(), mesh.Vertices.end(), 0.f);
    mesh.Vertices[0] = 42.f;
    mesh.Indices.init(&asset_memory.MainArena, 1, 1);
    mesh.Indices[0] = 0;
    mesh.SubMeshes.init(&asset_memory.MainArena, 1);
    mesh.SubMeshes.push({.MaterialUUID = material_id, .VertexCount = 1, .IndexCount = 1});
    ZEngine::Core::VFS::MetaFileData meta = {};
    meta.AssetUUID                        = mesh_id;
    // CPU mesh slot zero is valid; only HasLoadedSlot distinguishes it from a scanner placeholder.
    ASSERT_TRUE(assets->Registry->RegisterLoaded(mesh_id, ZEngine::Managers::AssetType::MESH, {}, meta, 0).IsOk());
    RRMUploadBatchTestHelper::InitializeSceneResidency(*Manager, Memory.MainArena, Target, indices, assets->Registry);
    const std::array meshes{mesh_id};
    for (uint64_t cycle = 1; cycle <= 2; ++cycle)
    {
        RRM::SceneResourcePreparationId id = {};
        ASSERT_TRUE(Manager->PrepareSceneResources({1, cycle}, 1, meshes, &id, RRM::ScenePreparationContent::MeshMaterials));
        Manager->PollSceneResourcePreparations();
        RRM::SceneResourcePreparationResult result = {};
        ASSERT_TRUE(Manager->PollSceneResourcePreparation(id, &result));
        EXPECT_EQ(result.State, RRM::SceneResourcePreparationState::Preparing);
        EXPECT_FALSE(Manager->IsMeshResident(Manager->FindMeshBuffer(mesh_id)));
        Manager->BeginFrame(0);
        ASSERT_TRUE(Manager->PollSceneResourcePreparation(id, &result));
        EXPECT_EQ(result.State, RRM::SceneResourcePreparationState::Preparing); // Recorded is not completed.
        Manager->EndFrame();
        const auto signal = RRMUploadBatchTestHelper::LastSignal(*Manager, 0);
        ASSERT_GT(signal, 0u);
        Timeline->Wait(signal);
        Manager->BeginFrame(1);
        ASSERT_TRUE(Manager->PollSceneResourcePreparation(id, &result));
        ASSERT_EQ(result.State, RRM::SceneResourcePreparationState::ResourcesRetained);
        RRM::MeshGeometrySnapshot geometry = {};
        ASSERT_TRUE(Manager->GetPreparedSceneGeometry(id, mesh_id, &geometry));
        ASSERT_NE(geometry.DrawMetadata, nullptr);
        ASSERT_EQ(geometry.DrawMetadata->SubMeshes.size(), 1u);
        EXPECT_EQ(geometry.DrawMetadata->SubMeshes[0].MaterialUUID, material_id);
        float vertex = 0;
        EXPECT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, Target.Allocation, geometry.Region.VtxByteOffset, &vertex, sizeof(vertex)), VK_SUCCESS);
        EXPECT_EQ(vertex, 42.f);
        ASSERT_TRUE(Manager->ReleaseSceneResourcePreparation(id));
        Manager->PollSceneResourcePreparations();
        const auto handle = Manager->FindMeshBuffer(mesh_id);
        RRMUploadBatchTestHelper::EvictSceneGeometry(*Manager, handle);
        EXPECT_FALSE(Manager->IsMeshResident(handle));
    }
}

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

TEST_F(RenderResourceManagerBatchTest, PinnedVersionRetainsUploadedBytesAcrossReplacementAndReset)
{
    RRMUploadBatchTestHelper::InitializeGeometry(*Manager, Memory.MainArena);
    const auto original = RRMUploadBatchTestHelper::InstallGeometry(*Manager);
    ASSERT_TRUE(original.IsValid());
    auto old_pin = Manager->PinMeshGeometry(original);
    ASSERT_TRUE(old_pin);
    const auto old_offset  = old_pin.Get()->Region.VtxByteOffset;
    const auto old_version = old_pin.Get()->Version;
    Append(0, 42, old_offset);
    Manager->EndFrame();
    EXPECT_EQ(Read(1, old_offset), 42u);

    const auto replacement = RRMUploadBatchTestHelper::InstallGeometry(*Manager);
    ASSERT_TRUE(replacement.IsValid());
    auto new_pin = Manager->PinMeshGeometry(replacement);
    ASSERT_TRUE(new_pin);
    const auto new_offset = new_pin.Get()->Region.VtxByteOffset;
    EXPECT_NE(new_pin.Get()->Version, old_version);
    ASSERT_NE(new_offset, old_offset);
    Append(1, 99, new_offset);
    Manager->EndFrame();
    EXPECT_EQ(Read(2, old_offset), 42u);
    EXPECT_EQ(Read(2, new_offset), 99u);

    // The waits above prove those submitted consumers have completed before we
    // relinquish their pins. Reset alone must not reclaim the older pinned range.
    new_pin.Reset();
    RRMUploadBatchTestHelper::ResetGeometry(*Manager);
    const auto after_reset = RRMUploadBatchTestHelper::InstallGeometry(*Manager);
    ASSERT_TRUE(after_reset.IsValid());
    auto after_reset_pin = Manager->PinMeshGeometry(after_reset);
    ASSERT_TRUE(after_reset_pin);
    EXPECT_EQ(after_reset_pin.Get()->Region.VtxByteOffset, new_offset);
    Append(2, 77, after_reset_pin.Get()->Region.VtxByteOffset);
    Manager->EndFrame();
    EXPECT_EQ(Read(3, old_offset), 42u);
    EXPECT_EQ(Read(3, new_offset), 77u);
    EXPECT_EQ(old_pin.Get()->Version, old_version);

    old_pin.Reset();
    const auto reused = RRMUploadBatchTestHelper::InstallGeometry(*Manager);
    ASSERT_TRUE(reused.IsValid());
    auto reused_pin = Manager->PinMeshGeometry(reused);
    ASSERT_TRUE(reused_pin);
    EXPECT_EQ(reused_pin.Get()->Region.VtxByteOffset, old_offset);
    Append(0, 123, reused_pin.Get()->Region.VtxByteOffset);
    Manager->EndFrame();
    EXPECT_EQ(Read(4, old_offset), 123u);
    EXPECT_EQ(Read(4, new_offset), 77u);
    after_reset_pin.Reset();
    reused_pin.Reset();
}

TEST_F(RenderResourceManagerBatchTest, FrameGeometryRetiresOnlyAfterAcceptedGpuSignalCompletes)
{
    RRMUploadBatchTestHelper::InitializeGeometry(*Manager, Memory.MainArena);
    const auto mesh = RRMUploadBatchTestHelper::InstallGeometry(*Manager);
    ASSERT_TRUE(mesh.IsValid());
    ZEngine::Rendering::RenderResourceManager::MeshGeometrySnapshot snapshot = {};
    ASSERT_TRUE(Manager->GetFrameMeshGeometry(mesh, &snapshot));
    auto gate                         = std::make_unique<ZEngine::Rendering::Primitives::Semaphore>(Device.get(), true);
    Swapchain.RenderTimeline          = gate.get();
    Swapchain.RenderTimelineNextValue = 1;
    Append(0, 42, snapshot.Region.VtxByteOffset);
    Manager->EndFrame(); // accepted copy waits on the deliberately unsignalled gate
    Manager->NotifyResourceSubmission(static_cast<uint32_t>(ZEngine::Rendering::QueueType::GRAPHIC_QUEUE), Timeline.get(), 1);
    Manager->FinishResourceFrame(); // final presentation aborted; graph gate survives
    Manager->Release(mesh);
    Manager->PollFrameResources();
    EXPECT_EQ(RRMUploadBatchTestHelper::GeometryBytes(*Manager), 32u);
    EXPECT_FALSE(Manager->ForgetResourceTimeline(Timeline.get()));

    // Always unblock before any fatal assertion or fixture shutdown waits for idle.
    gate->Signal(1);
    EXPECT_EQ(Read(1, snapshot.Region.VtxByteOffset), 42u);
    Manager->PollFrameResources();
    EXPECT_EQ(RRMUploadBatchTestHelper::GeometryBytes(*Manager), 0u);
    EXPECT_TRUE(Manager->ForgetResourceTimeline(Timeline.get()));
    Swapchain.RenderTimeline          = nullptr;
    Swapchain.RenderTimelineNextValue = 0;
}

class RetainedMaterialImageTest : public RenderResourceManagerBatchTest, public ::testing::WithParamInterface<uint32_t>
{
};

TEST_F(RenderResourceManagerBatchTest, FrameMaterialUploadBatchesUseExactRetainedTableIndices)
{
    auto destination = allocator().AllocateBuffer(RRM::FrameMaterialCapacity * sizeof(ZEngine::Rendering::Meshes::MeshMaterial), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, GpuMemoryDomain::HostUniform, "frame-material-batch-test");
    ASSERT_TRUE(destination);
    std::array<uint32_t, 40> indices = {};
    for (uint32_t version = 0; version < indices.size(); ++version)
    {
        ZEngine::Rendering::Meshes::MeshMaterial material = {};
        material.Factors.x                                = static_cast<float>(version);
        material.AlbedoColor.w                            = version / 40.f;
        RRM::MaterialVersionSnapshot snapshot             = {};
        EXPECT_TRUE(Manager->GetFrameMaterialVersion(material, &indices[version], &snapshot));
    }
    Manager->UploadFrameMaterials(destination); // two full 16-entry batches plus a tail
    for (uint32_t version = 0; version < indices.size(); ++version)
    {
        ZEngine::Rendering::Meshes::MeshMaterial uploaded = {};
        EXPECT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, destination.Allocation, indices[version] * sizeof(uploaded), &uploaded, sizeof(uploaded)), VK_SUCCESS);
        EXPECT_FLOAT_EQ(uploaded.Factors.x, static_cast<float>(version));
        EXPECT_FLOAT_EQ(uploaded.AlbedoColor.w, version / 40.f);
        EXPECT_EQ(uploaded.AlbedoMap, INVALID_MAP_HANDLE);
    }
    allocator().FreeBuffer(destination);
}

TEST_P(RetainedMaterialImageTest, MaterialTextureLeasePreservesRealImageUntilDelayedConsumerRetires)
{
    const bool storage_image = GetParam() >= 8;
    const bool image_array   = GetParam() >= 9;
    const bool graph_alias   = GetParam() == 7 || storage_image;
    const bool prepared_bank = GetParam() == 6 || GetParam() == 10;
    const bool source_scope  = GetParam() == 5 || GetParam() == 11;
    Device->MaxGlobalTexture = 8;
    Device->GlobalTextures.Initialize(&Memory.MainArena, 8);
    Device->ImageBufferManager.Initialize(&Memory.MainArena, 8);
    Device->ImageViewCacheSlab.Init(&Memory.MainArena, 32 * 1024);
    Device->RRM                                                            = Manager.get();
    ResourceTexturesInitialized                                            = true;
    ZEngine::Rendering::Specifications::TextureSpecification specification = {};
    specification.Width = specification.Height = 1;
    specification.Format                       = ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM;
    specification.IsUsageTransferSource        = true;
    specification.IsAliasable                  = graph_alias;
    specification.IsUsageStorage               = storage_image;
    const auto backing                         = Device->CreateTexture(specification, "retained-material-test");
    ASSERT_TRUE(backing.Valid());
    const auto texture = graph_alias ? Device->CreateAliasingTexture(specification, backing, "retained-graph-alias") : backing;
    ASSERT_TRUE(texture.Valid());
    BindlessTestTargets targets = {};
    ASSERT_TRUE(targets.Initialize(*Device, Device->MaxGlobalTexture));
    Device->RequestDescriptorUpdate(texture);
    Device->FlushBindlessTextureUpdates();
    const auto* resource = Manager->GetTexture(texture);
    ASSERT_NE(resource, nullptr);
    auto* image = Device->ImageBufferManager.Access(resource->BufferHandle);
    ASSERT_NE(image, nullptr);
    const auto original_image         = image->GetHandle();
    uint64_t   descriptor_publication = 0;
    using DescriptorState             = ZEngine::Rendering::SceneTextureDescriptorState;
    ASSERT_EQ(Device->PrepareSceneTextureDescriptor(texture, resource->BufferHandle, &descriptor_publication), DescriptorState::Ready);

    const auto               command = Commands[0]->GetHandle();
    VkCommandBufferBeginInfo begin   = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    ASSERT_EQ(vkBeginCommandBuffer(command, &begin), VK_SUCCESS);
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = original_image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    const VkClearColorValue red = {
        .float32 = {1.f, 0.f, 0.f, 1.f}
    };
    vkCmdClearColorImage(command, original_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &red, 1, &barrier.subresourceRange);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    ASSERT_EQ(vkEndCommandBuffer(command), VK_SUCCESS);
    const auto                    timeline      = Timeline->GetHandle();
    uint64_t                      signal        = 1;
    VkTimelineSemaphoreSubmitInfo timeline_info = {.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, .signalSemaphoreValueCount = 1, .pSignalSemaphoreValues = &signal};
    VkSubmitInfo                  submit        = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = &timeline_info, .commandBufferCount = 1, .pCommandBuffers = &command, .signalSemaphoreCount = 1, .pSignalSemaphores = &timeline};
    const auto                    queue         = Device->GetQueue(ZEngine::Rendering::QueueType::GRAPHIC_QUEUE).Handle;
    ASSERT_EQ(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), VK_SUCCESS);
    Timeline->Wait(1); // Initial upload completes before immutable retention.
    ASSERT_EQ(vkResetCommandBuffer(command, 0), VK_SUCCESS);
    if (graph_alias)
    {
        EXPECT_FALSE(Manager->PinTextureVersion(texture));
        Device->DestroyTexture(backing);
        ZEngine::Hardwares::TextureDisposeEntry retired = {};
        ASSERT_TRUE(Device->TextureHandleToDispose.pop(retired));
        ASSERT_EQ(retired.Handle.Index, backing.Index);
        auto* old = Device->GlobalTextures.Access(backing);
        ASSERT_NE(old, nullptr);
        auto* old_image = Device->ImageBufferManager.Access(old->BufferHandle);
        ASSERT_NE(old_image, nullptr);
        old_image->Dispose();
        Device->ImageBufferManager.Remove(old->BufferHandle);
        auto stale = backing;
        Device->GlobalTextures.Remove(stale);
        Device->PendingFree.Drain(&Device->GpuMem, Device->LogicalDevice, UINT64_MAX);
        EXPECT_GT(Device->GpuMem.GetAllocationSize(image->GetBuffer().Allocation), 0u);
    }
    ASSERT_EQ(vkBeginCommandBuffer(command, &begin), VK_SUCCESS);
    VkBufferImageCopy copy = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
          .imageExtent = {1, 1, 1}
    };
    vkCmdCopyImageToBuffer(command, original_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Target.Handle, 1, &copy);
    ASSERT_EQ(vkEndCommandBuffer(command), VK_SUCCESS);

    ZEngine::Rendering::Meshes::MeshMaterial material = {};
    material.AlbedoMap                                = static_cast<uint32_t>(texture.Index);
    material.OpacityMap                               = material.AlbedoMap;
    material.AlbedoColor.w                            = .25f;
    RRM::MaterialVersionPin      retained             = {};
    RRM::MaterialVersionSnapshot snapshot             = {};
    RRM::TextureVersionSnapshot  texture_snapshot     = {};
    uint32_t                     material_index       = 0;
    if (GetParam() >= 3)
    {
        auto pin = graph_alias ? Manager->PinGraphTextureVersion(texture) : Manager->PinTextureVersion(texture);
        ASSERT_TRUE(pin);
        if (GetParam() >= 4)
        {
            struct FrameSelection
            {
                ZEngine::Hardwares::DeviceSwapchain& Swapchain;
                ZEngine::Hardwares::FrameContext*    Previous = nullptr;
                ~FrameSelection()
                {
                    Swapchain.CurrentFrame = Previous;
                }
            } selection                            = {Swapchain, Swapchain.CurrentFrame};
            ZEngine::Hardwares::FrameContext frame = {.Index = 0};
            Swapchain.CurrentFrame                 = &frame;
            Swapchain.BufferredFrameCount          = 3;
            struct LayoutOwner
            {
                ZEngine::Hardwares::VulkanDevice* Device = nullptr;
                VkDescriptorSetLayout             Layout = VK_NULL_HANDLE;
                ~LayoutOwner()
                {
                    if (Layout)
                    {
                        ZEngine::Hardwares::DeferredFreeEntry entry = {};
                        entry.Data.Vk                               = {Layout, ZEngine::Rendering::DeviceResourceType::DESCRIPTORSETLAYOUT, nullptr};
                        Device->DeferFree(entry);
                    }
                }
            } layout                                          = {.Device = Device.get()};
            const VkDescriptorSetLayoutBinding    binding     = {.binding = 0, .descriptorType = storage_image ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = image_array ? 2u : 1u, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
            const VkDescriptorSetLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &binding};
            ASSERT_EQ(vkCreateDescriptorSetLayout(Device->LogicalDevice, &layout_info, nullptr, &layout.Layout), VK_SUCCESS);
            ZEngine::Rendering::Shaders::Shader shader = {};
            shader.SetLayouts.init(&Memory.MainArena, 1);
            shader.SetLayouts.push(layout.Layout);
            shader.BindingsByName.init(&Memory.MainArena, 4);
            const Specs::LayoutBindingSpecification reflected = {.Set = 0, .Binding = 0, .Count = image_array ? 2u : 1u, .Name = "Source", .DescriptorTypeValue = storage_image ? Specs::DescriptorType::STORAGE_IMAGE : Specs::DescriptorType::SAMPLED_IMAGE, .Flags = Specs::ShaderStageFlags::FRAGMENT};
            shader.BindingsByName.insert("Source", reflected);
            shader.LayoutBindingSpecifications.init(&Memory.MainArena, 1);
            shader.LayoutBindingSpecifications.push(reflected);
            Renderers::Pipelines::GraphicPipeline pipeline   = {};
            pipeline.Shader                                  = &shader;
            OffscreenGizmoCycle::BorrowedGraphicPass backend = {};
            backend.InitializeBindings(Device.get());
            backend.Pipeline = &pipeline;
            backend.ConnectPipeline();
            const VkImageSubresourceRange ranges[2] = {
                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            };
            const auto bind_image = [&](Renderers::RenderPasses::DescriptorBoundPass& pass, bool retained_image) {
                if (image_array)
                {
                    if (retained_image)
                    {
                        pass.SetRetainedStorageImageArray("Source", pin, ranges, 2);
                    }
                    else
                    {
                        pass.SetStorageImageArray("Source", texture, ranges, 2);
                    }
                }
                else if (storage_image)
                {
                    if (retained_image)
                    {
                        pass.SetRetainedStorageImage("Source", pin);
                    }
                    else
                    {
                        pass.SetStorageImage("Source", texture);
                    }
                }
                else if (retained_image)
                {
                    pass.SetRetainedTexture("Source", pin, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
                else
                {
                    pass.SetTexture("Source", texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
            };
            if (prepared_bank)
            {
                Renderers::RenderPasses::PreparedDescriptorPass candidate = {};
                candidate.InitializeForPipeline(Device.get(), &pipeline);
                // Candidate writes retain images but must not masquerade as
                // accepted recording references while no frame is acquired.
                Swapchain.CurrentFrame = nullptr;
                for (uint32_t index = 0; index < Bank::MaxFrames; ++index)
                {
                    candidate.BeginPreparedDescriptorPreparation(index);
                    bind_image(candidate, true);
                    ASSERT_TRUE(candidate.Verify());
                }
                Device->DestroyTexture(texture);
                EXPECT_FALSE(Manager->PinTextureVersion(texture));
                ASSERT_TRUE(backend.AdoptPreparedDescriptors(candidate));
                candidate.ReleaseDescriptorStorage();
                Swapchain.CurrentFrame = &frame;
            }
            backend.BeginDescriptorPreparation();
            if (source_scope)
            {
                Device->DestroyTexture(texture); // No new admission after activation.
                EXPECT_FALSE(Manager->PinTextureVersion(texture));
                Renderers::RenderPasses::DescriptorBoundPass::TextureSourceScope sources{
                    &backend, {&pin, 1}
                };
                bind_image(backend, false); // Unchanged production callback.
            }
            else
            {
                bind_image(backend, true);
            }
            EXPECT_TRUE(backend.CheckBindings());
            backend.BeginDescriptorPreparation();
            bind_image(backend, false);
            EXPECT_TRUE(backend.CheckBindings());
            Device->DestroyTexture(texture);
            EXPECT_FALSE(Manager->PinTextureVersion(texture));
            texture_snapshot = *pin.Get();
            pin.Reset();
            if (source_scope)
            {
                auto graph = std::make_unique<Renderers::RenderGraph>();
                graph->PersistentPasses.init(&Memory.MainArena, 1);
                graph->PersistentPasses.push({.Handle = &backend});
                graph->Passes.init(&Memory.MainArena, 1);
                graph->Passes.push({.Handle = &backend});
                graph->ReleaseDescriptorStorageAfterReadersJoined();
                graph->ReleaseDescriptorStorageAfterReadersJoined();
            }
            else
            {
                backend.ReleaseBindings();
            }
            // Only the accepted-frame hold remains, including shutdown drain.
            backend.Pipeline = nullptr;
        }
        else
        {
            Device->DestroyTexture(texture);
            EXPECT_FALSE(Manager->PinTextureVersion(texture));
            EXPECT_TRUE(Manager->GetFrameTextureVersion(pin, &texture_snapshot));
            RRM::TextureVersionSnapshot repeated = {};
            EXPECT_TRUE(Manager->GetFrameTextureVersion(pin, &repeated));
            EXPECT_EQ(repeated.Version, texture_snapshot.Version);
            pin.Reset();
        }
        EXPECT_TRUE(Manager->IsTextureVersionPinned(texture));
    }
    else if (GetParam())
    {
        if (GetParam() == 2)
        {
            retained = RRMUploadBatchTestHelper::RetainMaterial(*Manager, material);
            ASSERT_TRUE(retained);
            ASSERT_EQ(RRMUploadBatchTestHelper::PrepareMaterialDescriptors(*Manager, retained), DescriptorState::Ready);
            Device->DestroyTexture(texture); // Seal new admission before consumer handoff.
            EXPECT_FALSE(Manager->PinTextureVersion(texture));
            ASSERT_TRUE(Manager->GetFrameMaterialVersion(retained, &material_index, &snapshot));
            retained.Reset(); // The frame alone now owns this image version.
        }
        else
        {
            ASSERT_TRUE(Manager->GetFrameMaterialVersion(material, &material_index, &snapshot));
            ASSERT_EQ(RRMUploadBatchTestHelper::PrepareFrameMaterialDescriptors(*Manager, material_index), DescriptorState::Ready);
        }
        auto gpu_materials = allocator().AllocateBuffer(RRM::FrameMaterialCapacity * sizeof(material), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, GpuMemoryDomain::HostUniform, "immutable-frame-material-test");
        ASSERT_TRUE(gpu_materials);
        material.AlbedoColor.w = .75f; // uploading must use the retained copy
        Manager->UploadFrameMaterials(gpu_materials);
        ZEngine::Rendering::Meshes::MeshMaterial uploaded = {};
        EXPECT_EQ(vmaCopyAllocationToMemory(allocator().Allocator, gpu_materials.Allocation, material_index * sizeof(material), &uploaded, sizeof(uploaded)), VK_SUCCESS);
        EXPECT_FLOAT_EQ(uploaded.AlbedoColor.w, .25f);
        EXPECT_EQ(uploaded.AlbedoMap, texture.Index);
        allocator().FreeBuffer(gpu_materials);
    }
    else
    {
        retained = RRMUploadBatchTestHelper::RetainMaterial(*Manager, material);
        ASSERT_TRUE(retained);
        ASSERT_EQ(RRMUploadBatchTestHelper::PrepareMaterialDescriptors(*Manager, retained), DescriptorState::Ready);
    }
    const auto fallback = Device->CreateTexture(specification, "descriptor-fallback-test");
    ASSERT_TRUE(fallback.Valid());
    Device->FallbackDescriptorImageInfo = Device->ImageBufferManager.Access(Device->GlobalTextures.Access(fallback)->BufferHandle)->GetDescriptorImageInfo();
    ASSERT_TRUE(Device->RequestDeferredDescriptorUpdate(texture));
    Device->FlushBindlessTextureUpdates();
    EXPECT_TRUE(Device->IsSceneTextureDescriptorCurrent(texture, resource->BufferHandle, descriptor_publication));
    Device->FlushBindlessTextureUpdates();
    EXPECT_TRUE(Device->IsSceneTextureDescriptorCurrent(texture, resource->BufferHandle, descriptor_publication));
    const auto                 get_texture = [&](uint32_t map) { return GetParam() >= 3 ? &texture_snapshot : GetParam() ? RRMUploadBatchTestHelper::FrameMaterialTexture(*Manager, material_index, map) : retained.GetTexture(map); };
    auto                       gate        = std::make_unique<ZEngine::Rendering::Primitives::Semaphore>(Device.get(), true);
    const auto                 wait        = gate->GetHandle();
    const uint64_t             wait_value  = 1;
    const VkPipelineStageFlags wait_stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
    signal                                 = 2;
    timeline_info.waitSemaphoreValueCount  = 1;
    timeline_info.pWaitSemaphoreValues     = &wait_value;
    submit.waitSemaphoreCount              = 1;
    submit.pWaitSemaphores                 = &wait;
    submit.pWaitDstStageMask               = &wait_stage;
    ASSERT_EQ(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), VK_SUCCESS);
    // No fatal assertion may strand this deliberately blocked GPU consumer.
    if (GetParam())
    {
        Manager->NotifyResourceSubmission(0, Timeline.get(), 2);
        Manager->FinishResourceFrame(); // presentation abort cannot discard this gate
        EXPECT_FALSE(Manager->ForgetResourceTimeline(Timeline.get()));
    }
    specification.Width = 2;
    EXPECT_FALSE(Device->ReconstructTexture(texture, specification));
    Manager->ReleaseTexture(texture);
    Manager->BeginFrame(0);
    Device->DestroyTexture(texture);
    EXPECT_TRUE(Device->TextureHandleToDispose.empty());
    EXPECT_EQ(image->GetHandle(), original_image);
    EXPECT_EQ(get_texture(0)->Handle.Generation, texture.Generation);
    EXPECT_EQ(get_texture(0)->Version, get_texture(3)->Version);
    gate->Signal(1);
    EXPECT_EQ(Read(2), 0xff0000ffu);
    retained.Reset();
    Manager->BeginFrame(0);
    ZEngine::Hardwares::TextureDisposeEntry retired = {};
    EXPECT_TRUE(Device->TextureHandleToDispose.pop(retired));
    EXPECT_EQ(retired.Handle.Generation, texture.Generation);
    EXPECT_FALSE(Manager->PinTextureVersion(texture));
    EXPECT_TRUE(Manager->ForgetResourceTimeline(Timeline.get()));
}

INSTANTIATE_TEST_SUITE_P(ExplicitAutomaticAndTransferred, RetainedMaterialImageTest, ::testing::Values(0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u));

TEST_F(RenderResourceManagerBatchTest, StorageImageArrayRetainsEachElementAndRefusesPartialReplacement)
{
    Device->MaxGlobalTexture = 8;
    Device->GlobalTextures.Initialize(&Memory.MainArena, 8);
    Device->ImageBufferManager.Initialize(&Memory.MainArena, 8);
    Device->ImageViewCacheSlab.Init(&Memory.MainArena, 32 * 1024);
    Device->RRM                              = Manager.get();
    ResourceTexturesInitialized              = true;
    const Specs::TextureSpecification spec   = {.IsUsageStorage = true, .IsAliasable = true, .Width = 2, .Height = 2, .MipLevelCount = 2, .Format = Specs::ImageFormat::R8G8B8A8_UNORM};
    const auto                        first  = Device->CreateTexture(spec, "storage_array_first");
    const auto                        second = Device->CreateTexture(spec, "storage_array_second");
    ASSERT_TRUE(first.Valid());
    ASSERT_TRUE(second.Valid());
    auto first_pin  = Manager->PinGraphTextureVersion(first);
    auto second_pin = Manager->PinGraphTextureVersion(second);
    ASSERT_TRUE(first_pin);
    ASSERT_TRUE(second_pin);
    struct LayoutOwner
    {
        ZEngine::Hardwares::VulkanDevice* Device = nullptr;
        VkDescriptorSetLayout             Layout = VK_NULL_HANDLE;
        ~LayoutOwner()
        {
            if (Layout)
            {
                ZEngine::Hardwares::DeferredFreeEntry entry = {};
                entry.Data.Vk                               = {Layout, ZEngine::Rendering::DeviceResourceType::DESCRIPTORSETLAYOUT, nullptr};
                Device->DeferFree(entry);
            }
        }
    } layout                                          = {.Device = Device.get()};
    const VkDescriptorSetLayoutBinding    binding     = {.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 2, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    const VkDescriptorSetLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &binding};
    ASSERT_EQ(vkCreateDescriptorSetLayout(Device->LogicalDevice, &layout_info, nullptr, &layout.Layout), VK_SUCCESS);
    ZEngine::Rendering::Shaders::Shader shader = {};
    shader.SetLayouts.init(&Memory.MainArena, 1);
    shader.SetLayouts.push(layout.Layout);
    shader.BindingsByName.init(&Memory.MainArena, 4);
    const Specs::LayoutBindingSpecification reflected = {.Set = 0, .Binding = 0, .Count = 2, .Name = "Images", .DescriptorTypeValue = Specs::DescriptorType::STORAGE_IMAGE, .Flags = Specs::ShaderStageFlags::FRAGMENT};
    shader.BindingsByName.insert("Images", reflected);
    shader.LayoutBindingSpecifications.init(&Memory.MainArena, 1);
    shader.LayoutBindingSpecifications.push(reflected);
    Renderers::Pipelines::GraphicPipeline pipeline = {};
    pipeline.Shader                                = &shader;
    ZEngine::Hardwares::FrameContext frame         = {.Index = 0};
    struct FrameSelection
    {
        ZEngine::Hardwares::DeviceSwapchain& Swapchain;
        ~FrameSelection()
        {
            Swapchain.CurrentFrame = nullptr;
        }
    } selection                                      = {Swapchain};
    Swapchain.CurrentFrame                           = &frame;
    Swapchain.BufferredFrameCount                    = 3;
    OffscreenGizmoCycle::BorrowedGraphicPass backend = {};
    backend.InitializeBindings(Device.get());
    backend.Pipeline = &pipeline;
    backend.ConnectPipeline();
    const VkImageSubresourceRange ranges[2] = {
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        {VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 0, 1}
    };
    backend.BeginDescriptorPreparation();
    backend.SetRetainedStorageImage("Images", first_pin, ranges[0], 0);
    EXPECT_FALSE(backend.CheckBindings()); // Every array element needs a fresh write.
    backend.SetRetainedStorageImage("Images", second_pin, ranges[1], 1);
    EXPECT_TRUE(backend.CheckBindings());
    auto replacement_source = Manager->RetainTextureVersion(first_pin);
    EXPECT_TRUE(replacement_source);
    first_pin.Reset();
    second_pin.Reset();
    Device->DestroyTexture(first);
    Device->DestroyTexture(second);
    Manager->FinishResourceFrame();
    EXPECT_TRUE(Manager->IsTextureVersionPinned(first));
    EXPECT_TRUE(Manager->IsTextureVersionPinned(second));
    backend.BeginDescriptorPreparation();
    VkImageSubresourceRange invalid[2] = {ranges[0], ranges[1]};
    invalid[1].baseMipLevel            = 2;
    backend.SetStorageImageArray("Images", first, invalid, 2);
    EXPECT_FALSE(backend.CheckBindings());
    EXPECT_TRUE(Manager->IsTextureVersionPinned(second)); // Failed write is transactional.
    backend.BeginDescriptorPreparation();
    backend.SetStorageImage("Images", first, ranges[0], 0);
    backend.SetStorageImage("Images", second, ranges[1], 1);
    EXPECT_TRUE(backend.CheckBindings());
    EXPECT_TRUE(backend.ReplayBindings()); // Replay must restore each element's lease.
    EXPECT_TRUE(backend.CheckBindings());
    Manager->FinishResourceFrame();
    backend.BeginDescriptorPreparation();
    // A successful replacement releases only the old element's bank lease.
    // Both descriptors now use the already-retained first image.
    EXPECT_FALSE(Manager->PinGraphTextureVersion(first)); // No new admission.
    backend.SetRetainedStorageImageArray("Images", replacement_source, ranges, 2);
    EXPECT_TRUE(backend.CheckBindings());
    replacement_source.Reset();
    Manager->FinishResourceFrame();
    EXPECT_TRUE(Manager->IsTextureVersionPinned(first));
    EXPECT_FALSE(Manager->IsTextureVersionPinned(second));
    EXPECT_TRUE(backend.ReplayBindings());
    EXPECT_TRUE(backend.CheckBindings());
    backend.ReleaseBindings();
    Manager->FinishResourceFrame();
    EXPECT_FALSE(Manager->IsTextureVersionPinned(first));
    EXPECT_FALSE(Manager->IsTextureVersionPinned(second));
    backend.Pipeline = nullptr;
}

TEST(FramebufferAdmissionTest, MetadataAndNativeRefusalsPreserveOutputAndOutgoingHandleThenReuseEmptyWrapper)
{
    NativeMetadataOwner owner = {};
    ASSERT_TRUE(owner.Initialize());
    ArenaAllocator arena = {};
    arena.Initialize(ZKilo(64), 0, "FramebufferAdmission");
    ZEngine::Hardwares::DeviceSwapchain swapchain = {};
    owner.Device->Arena                           = &arena;
    owner.Device->SwapchainPtr                    = &swapchain;
    struct Resources
    {
        NativeMetadataOwner*                           Owner     = nullptr;
        VkRenderPass                                   Pass      = VK_NULL_HANDLE;
        ZEngine::Rendering::Buffers::FramebufferVNext* Outgoing  = nullptr;
        ZEngine::Rendering::Buffers::FramebufferVNext* Candidate = nullptr;
        void                                           Drain()
        {
            if (Outgoing)
            {
                Outgoing->Dispose();
            }
            if (Candidate)
            {
                Candidate->Dispose();
            }
            Owner->Device->PendingFree.Drain(&Owner->Device->GpuMem, Owner->Vulkan.Device, UINT64_MAX);
        }
        ~Resources()
        {
            Drain();
            if (Pass)
            {
                vkDestroyRenderPass(Owner->Vulkan.Device, Pass, nullptr);
            }
            Owner->Device->Arena        = nullptr;
            Owner->Device->SwapchainPtr = nullptr;
        }
    } resources                          = {.Owner = &owner};
    const VkSubpassDescription   subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS};
    const VkRenderPassCreateInfo recipe  = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .subpassCount = 1, .pSubpasses = &subpass};
    ASSERT_EQ(vkCreateRenderPass(owner.Vulkan.Device, &recipe, nullptr, &resources.Pass), VK_SUCCESS);
    VkResult status = VK_ERROR_UNKNOWN;
    ASSERT_TRUE(ZEngine::Rendering::Buffers::FramebufferVNext::TryPrepare(owner.Device.get(), {}, resources.Pass, 1, 1, 1, &resources.Outgoing, &status));
    const auto outgoing = resources.Outgoing->Handle;
    ASSERT_NE(outgoing, VK_NULL_HANDLE);
    const auto retained                      = arena.m_current_offset;
    FramebufferAdmissionFault::Calls         = 0;
    std::array<std::max_align_t, 64> storage = {};
    ArenaAllocator                   tiny    = {};
    ASSERT_TRUE(tiny.InitializeBorrowed(storage.data(), sizeof(ZEngine::Rendering::Buffers::FramebufferVNext) - 1));
    owner.Device->Arena         = &tiny;
    const auto metadata_refused = ZEngine::Rendering::Buffers::FramebufferVNext::TryPrepare(owner.Device.get(), {}, resources.Pass, 1, 1, 1, &resources.Candidate, &status, &FramebufferAdmissionFault::Create);
    owner.Device->Arena         = &arena;
    EXPECT_FALSE(metadata_refused);
    EXPECT_EQ(status, VK_ERROR_OUT_OF_HOST_MEMORY);
    EXPECT_EQ(tiny.m_current_offset, 0u);
    EXPECT_EQ(FramebufferAdmissionFault::Calls, 0u);
    EXPECT_EQ(resources.Candidate, nullptr);
    for (const auto failure : {VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_OUT_OF_DEVICE_MEMORY})
    {
        FramebufferAdmissionFault::Result = failure;
        for (uint32_t retry = 0; retry < 16; ++retry)
        {
            EXPECT_FALSE(ZEngine::Rendering::Buffers::FramebufferVNext::TryPrepare(owner.Device.get(), {}, resources.Pass, 1, 1, 1, &resources.Candidate, &status, &FramebufferAdmissionFault::Create));
            EXPECT_EQ(status, failure);
            EXPECT_EQ(resources.Candidate, nullptr);
            EXPECT_EQ(arena.m_current_offset, retained);
            EXPECT_EQ(resources.Outgoing->Handle, outgoing);
            EXPECT_EQ(owner.Device->PendingFree.Head, owner.Device->PendingFree.Tail);
        }
    }
    ASSERT_TRUE(ZEngine::Rendering::Buffers::FramebufferVNext::TryPrepare(owner.Device.get(), {}, resources.Pass, 2, 3, 1, &resources.Candidate, &status));
    const auto candidate        = resources.Candidate;
    const auto candidate_cursor = arena.m_current_offset;
    EXPECT_FALSE(ZEngine::Rendering::Buffers::FramebufferVNext::TryPrepare(owner.Device.get(), {}, resources.Pass, 4, 4, 1, &resources.Candidate, &status));
    EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED);
    EXPECT_EQ(resources.Candidate, candidate);
    EXPECT_EQ(candidate->GetWidth(), 2u);
    EXPECT_EQ(candidate->GetHeight(), 3u);
    candidate->Dispose();
    owner.Device->PendingFree.Drain(&owner.Device->GpuMem, owner.Vulkan.Device, UINT64_MAX);
    FramebufferAdmissionFault::Result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    EXPECT_FALSE(ZEngine::Rendering::Buffers::FramebufferVNext::TryPrepare(owner.Device.get(), {}, resources.Pass, 4, 4, 1, &resources.Candidate, &status, &FramebufferAdmissionFault::Create));
    EXPECT_EQ(resources.Candidate, candidate);
    EXPECT_EQ(arena.m_current_offset, candidate_cursor);
    ASSERT_TRUE(ZEngine::Rendering::Buffers::FramebufferVNext::TryPrepare(owner.Device.get(), {}, resources.Pass, 4, 4, 1, &resources.Candidate, &status));
    EXPECT_EQ(resources.Candidate, candidate);
    EXPECT_EQ(arena.m_current_offset, candidate_cursor);
    EXPECT_EQ(resources.Outgoing->Handle, outgoing);
    resources.Drain();
    EXPECT_EQ(owner.Vulkan.ValidationErrors.load(), 0u);
    FramebufferAdmissionFault::Result = VK_ERROR_DEVICE_LOST;
    EXPECT_FALSE(ZEngine::Rendering::Buffers::FramebufferVNext::TryPrepare(owner.Device.get(), {}, resources.Pass, 4, 4, 1, &resources.Candidate, &status, &FramebufferAdmissionFault::Create));
    EXPECT_EQ(status, VK_ERROR_DEVICE_LOST);
    EXPECT_TRUE(owner.Device->IsDeviceLost.load());
    EXPECT_EQ(arena.m_current_offset, candidate_cursor);
    const auto calls = FramebufferAdmissionFault::Calls;
    EXPECT_FALSE(ZEngine::Rendering::Buffers::FramebufferVNext::TryPrepare(owner.Device.get(), {}, resources.Pass, 4, 4, 1, &resources.Candidate, &status, &FramebufferAdmissionFault::Create));
    EXPECT_EQ(status, VK_ERROR_DEVICE_LOST);
    EXPECT_EQ(FramebufferAdmissionFault::Calls, calls);
}

TEST(ShaderAdmissionTest, NativeRefusalsReuseOneStorageSlotWithoutRetirementGrowthThenRetry)
{
    GlslangLifetime compiler = {};
    ASSERT_TRUE(compiler.Ready);
    constexpr char        source[] = R"(#version 450
layout(local_size_x=1) in;
layout(set=0, binding=1, std430) buffer Result { uint Value; } Output;
void main() { Output.Value = 1; }
)";
    std::vector<uint32_t> binary   = {};
    ASSERT_TRUE(CompileGizmoStage(source, sizeof(source) - 1, EShLangCompute, binary));
    ShaderAdmissionOwner owner = {};
    ASSERT_TRUE(owner.Initialize());
    ASSERT_TRUE(owner.Write(binary));
    auto&                            device        = *owner.Native.Device;
    const Specs::ShaderSpecification specification = {.ComputeFilename = "/test.spv", .Name = "ShaderAdmission"};
    const auto                       initial       = owner.Arena.m_current_offset;
    size_t                           retained      = 0;
    ShaderAdmissionFault::Result                   = VK_ERROR_OUT_OF_HOST_MEMORY;
    for (uint32_t phase = 0; phase < 4; ++phase)
    {
        ZEngine::Rendering::Shaders::ShaderInitializationServices services = {.VFS = &owner.VFS};
        if (phase == 0)
        {
            services.CreateModule = &ShaderAdmissionFault::Module;
        }
        if (phase == 1)
        {
            services.CreateLayout = &ShaderAdmissionFault::Layout;
        }
        if (phase == 2)
        {
            services.CreatePool = &ShaderAdmissionFault::Pool;
        }
        if (phase == 3)
        {
            services.AllocateSets = &ShaderAdmissionFault::Sets;
        }
        for (uint32_t retry = 0; retry < 16; ++retry)
        {
            VkResult status = VK_SUCCESS;
            ASSERT_FALSE(owner.Shader.TryInitialize(&device, specification, &status, services));
            EXPECT_EQ(status, VK_ERROR_OUT_OF_HOST_MEMORY);
            if (!retained)
            {
                retained = owner.Arena.m_current_offset;
            }
            EXPECT_EQ(owner.Arena.m_current_offset, retained);
            EXPECT_GE(retained - initial, owner.Shader.StorageBytes);
            EXPECT_EQ(owner.Shader.LocalArena.m_current_offset, 0u);
            EXPECT_TRUE(owner.Shader.ShaderModules.empty());
            EXPECT_TRUE(owner.Shader.ShaderCreateInfos.empty());
            EXPECT_TRUE(owner.Shader.DescriptorSetMap.empty());
            EXPECT_EQ(owner.Shader.m_descriptor_pool, VK_NULL_HANDLE);
            EXPECT_EQ(device.PendingFree.Head, device.PendingFree.Tail);
        }
    }
    VkResult status = VK_ERROR_UNKNOWN;
    ASSERT_TRUE(owner.Shader.TryInitialize(&device, specification, &status, {.VFS = &owner.VFS}));
    EXPECT_EQ(status, VK_SUCCESS);
    EXPECT_EQ(owner.Arena.m_current_offset, retained);
    EXPECT_TRUE(owner.Shader.HasStages(VK_SHADER_STAGE_COMPUTE_BIT));
    ASSERT_EQ(owner.Shader.DescriptorSetMap.at(0).size(), 2u);
    EXPECT_NE(owner.Shader.DescriptorSetMap.at(0)[0], VK_NULL_HANDLE);
    EXPECT_EQ(owner.Native.Vulkan.ValidationErrors.load(), 0u);
    owner.Shader.Dispose();
    owner.Shader.Dispose(); // Reset must be safe even after the metadata arena is rewound.
}

TEST(ShaderAdmissionTest, FactoryRefusalDoesNotCacheHandleOrMutateRecipeAndReusesFailedSlot)
{
    GlslangLifetime compiler = {};
    ASSERT_TRUE(compiler.Ready);
    constexpr char        source[] = R"(#version 450
layout(local_size_x=1) in;
layout(set=0, binding=1, std430) buffer Result { uint Value; } Output;
void main() { Output.Value = 1; }
)";
    std::vector<uint32_t> binary   = {};
    ASSERT_TRUE(CompileGizmoStage(source, sizeof(source) - 1, EShLangCompute, binary));
    ShaderAdmissionOwner owner = {};
    ASSERT_TRUE(owner.Initialize());
    ASSERT_TRUE(owner.Write(binary));
    auto&                      device                                       = *owner.Native.Device;
    Specs::ShaderSpecification specification                                = {.ComputeFilename = "/test.spv", .Name = "ShaderAdmission"};
    const auto                 original                                     = specification;
    ShaderAdmissionFault::Result                                            = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    const ZEngine::Rendering::Shaders::ShaderInitializationServices refused = {.VFS = &owner.VFS, .CreatePool = &ShaderAdmissionFault::Pool};
    EXPECT_FALSE(device.CompileShader(specification, refused));
    const auto retained = owner.Arena.m_current_offset;
    for (uint32_t retry = 0; retry < 32; ++retry)
    {
        EXPECT_FALSE(device.CompileShader(specification, refused));
        EXPECT_TRUE(device.ShaderCaches.empty());
        EXPECT_EQ(owner.Arena.m_current_offset, retained);
        EXPECT_EQ(specification.Name, original.Name);
        EXPECT_EQ(specification.ComputeFilename, original.ComputeFilename);
        EXPECT_EQ(device.PendingFree.Head, device.PendingFree.Tail);
    }
    const auto handle = device.CompileShader(specification, {.VFS = &owner.VFS});
    ASSERT_TRUE(handle);
    EXPECT_EQ(handle.Index, 0u);
    EXPECT_EQ(owner.Arena.m_current_offset, retained);
    ASSERT_EQ(device.ShaderCaches.size(), 1u);
    auto* shader = device.ShaderManager.Access(handle);
    ASSERT_NE(shader, nullptr);
    EXPECT_EQ(specification.Name, shader->m_specification.Name);
    const auto module = shader->ShaderModules[0];
    auto       lease  = shader->RetainGeneration();
    ASSERT_TRUE(lease);
    const auto hit = device.CompileShader(specification, refused);
    EXPECT_EQ(hit.Index, handle.Index);
    EXPECT_EQ(hit.Generation, handle.Generation);
    EXPECT_EQ(shader->ShaderModules[0], module);
    EXPECT_EQ(owner.Native.Vulkan.ValidationErrors.load(), 0u);
}

TEST(ShaderAdmissionTest, GraphicsReflectionAndLeasedReloadReuseOwnedPathsAndStorage)
{
    GlslangLifetime compiler = {};
    ASSERT_TRUE(compiler.Ready);
    std::vector<uint32_t> vertex = {}, fragment = {};
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Vertex, sizeof(ZEngine::Tests::GizmoShaders::Vertex) - 1, EShLangVertex, vertex));
    ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Fragment, sizeof(ZEngine::Tests::GizmoShaders::Fragment) - 1, EShLangFragment, fragment));
    ShaderAdmissionOwner owner = {};
    ASSERT_TRUE(owner.Initialize());
    ASSERT_TRUE(owner.Write(vertex, "/vertex.spv"));
    ASSERT_TRUE(owner.Write(fragment, "/fragment.spv"));
    char                             name[] = "OwnedGraphics";
    char                             path[] = "/vertex.spv";
    const Specs::ShaderSpecification spec   = {.VertexFilename = path, .FragmentFilename = "/fragment.spv", .Name = name};
    ASSERT_TRUE(owner.Shader.TryInitialize(owner.Native.Device.get(), spec, nullptr, {.VFS = &owner.VFS}));
    name[0] = 'X';
    path[0] = 'X';
    EXPECT_STREQ(owner.Shader.m_specification.Name, "OwnedGraphics");
    EXPECT_STREQ(owner.Shader.m_specification.VertexFilename, "/vertex.spv");
    ASSERT_TRUE(owner.Shader.HasStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT));
    EXPECT_EQ(owner.Shader.DynamicDescriptorCount(), 1u);
    ASSERT_EQ(owner.Shader.PushConstants.size(), 1u);
    EXPECT_EQ(owner.Shader.PushConstants[0].size, 8u);
    EXPECT_EQ(owner.Shader.PushConstants[0].stageFlags, VK_SHADER_STAGE_VERTEX_BIT);
    const auto parent_cursor = owner.Arena.m_current_offset;
    const auto old_module    = owner.Shader.ShaderModules[0];
    auto       lease         = owner.Shader.RetainGeneration();
    ASSERT_TRUE(lease);
    owner.Shader.Reload();
    EXPECT_TRUE(owner.Shader.HasDeferredReload());
    EXPECT_EQ(owner.Shader.ShaderModules[0], old_module);
    EXPECT_FALSE(owner.Shader.FlushDeferredReload());
    lease.Reset();
    ASSERT_TRUE(owner.Shader.FlushDeferredReload());
    EXPECT_EQ(owner.Shader.Generation, 1u);
    EXPECT_EQ(owner.Arena.m_current_offset, parent_cursor);
    for (uint32_t reload = 0; reload < 16; ++reload)
    {
        owner.Native.Device->PendingFree.Drain(&owner.Native.Device->GpuMem, owner.Native.Vulkan.Device, UINT64_MAX);
        owner.Shader.Reload();
        EXPECT_EQ(owner.Arena.m_current_offset, parent_cursor);
        EXPECT_TRUE(owner.Shader.HasStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT));
    }
    EXPECT_EQ(owner.Native.Vulkan.ValidationErrors.load(), 0u);
}

TEST(ShaderAdmissionTest, ReflectionRetainsExactArrayCountsAndRejectsUnownedRuntimeArraysBeforeNativeCreation)
{
    GlslangLifetime compiler = {};
    ASSERT_TRUE(compiler.Ready);
    constexpr char       fixed_source[]   = R"(#version 450
layout(local_size_x=1) in;
layout(set=0, binding=1) uniform sampler2D Textures[300];
layout(set=0, binding=2, std430) buffer Result { vec4 Value; } Output;
void main() { Output.Value = textureLod(Textures[299], vec2(0), 0); }
)";
    constexpr char       runtime_source[] = R"(#version 450
#extension GL_EXT_nonuniform_qualifier : require
layout(local_size_x=1) in;
layout(set=0, binding=1) uniform sampler2D Textures[];
layout(set=0, binding=2, std430) buffer Result { vec4 Value; } Output;
void main() { Output.Value = textureLod(Textures[nonuniformEXT(gl_GlobalInvocationID.x)], vec2(0), 0); }
)";
    ShaderAdmissionOwner owner            = {};
    ASSERT_TRUE(owner.Initialize());
    const Specs::ShaderSpecification spec   = {.ComputeFilename = "/test.spv", .Name = "ArrayReflection"};
    std::vector<uint32_t>            binary = {};
    ASSERT_TRUE(CompileGizmoStage(fixed_source, sizeof(fixed_source) - 1, EShLangCompute, binary));
    ASSERT_TRUE(owner.Write(binary));
    ShaderAdmissionFault::Result         = VK_ERROR_OUT_OF_HOST_MEMORY;
    ShaderAdmissionFault::ObservedShader = &owner.Shader;
    ShaderAdmissionFault::CapturedCount  = 0;
    VkResult   status                    = VK_SUCCESS;
    const bool admitted                  = owner.Shader.TryInitialize(owner.Native.Device.get(), spec, &status, {.VFS = &owner.VFS, .CreateModule = &ShaderAdmissionFault::Module});
    ShaderAdmissionFault::ObservedShader = nullptr;
    EXPECT_FALSE(admitted);
    EXPECT_EQ(status, VK_ERROR_OUT_OF_HOST_MEMORY);
    // Do not silently turn a declared 300-element array into a 256-element layout.
    EXPECT_EQ(ShaderAdmissionFault::CapturedCount, 300u);
    binary.clear();
    ASSERT_TRUE(CompileGizmoStage(runtime_source, sizeof(runtime_source) - 1, EShLangCompute, binary));
    ASSERT_TRUE(owner.Write(binary));
    ShaderAdmissionFault::CapturedCount = 0;
    EXPECT_FALSE(owner.Shader.TryInitialize(owner.Native.Device.get(), spec, &status, {.VFS = &owner.VFS, .CreateModule = &ShaderAdmissionFault::Module}));
    EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED);
    EXPECT_EQ(ShaderAdmissionFault::CapturedCount, 0u);
    EXPECT_TRUE(owner.Shader.ShaderModules.empty());
}

TEST(ShaderAdmissionTest, WrongEntryStageRefusesBeforeNativeCreationAndDescriptorFreeRetryNeedsNoGapSet)
{
    GlslangLifetime compiler = {};
    ASSERT_TRUE(compiler.Ready);
    constexpr char        vertex_source[]  = "#version 450\nvoid main() { gl_Position = vec4(0); }";
    constexpr char        compute_source[] = "#version 450\nlayout(local_size_x=1) in; void main() {}";
    std::vector<uint32_t> binary           = {};
    ASSERT_TRUE(CompileGizmoStage(vertex_source, sizeof(vertex_source) - 1, EShLangVertex, binary));
    ShaderAdmissionOwner owner = {};
    ASSERT_TRUE(owner.Initialize());
    ASSERT_TRUE(owner.Write(binary));
    const Specs::ShaderSpecification spec = {.ComputeFilename = "/test.spv", .Name = "EmptyProgram"};
    ShaderAdmissionFault::Result          = VK_ERROR_OUT_OF_HOST_MEMORY;
    VkResult status                       = VK_SUCCESS;
    EXPECT_FALSE(owner.Shader.TryInitialize(owner.Native.Device.get(), spec, &status, {.VFS = &owner.VFS, .CreateModule = &ShaderAdmissionFault::Module}));
    // The injected OOM must not be reached for a mismatched entry-point stage.
    EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED);
    EXPECT_TRUE(owner.Shader.ShaderModules.empty());
    binary.clear();
    ASSERT_TRUE(CompileGizmoStage(compute_source, sizeof(compute_source) - 1, EShLangCompute, binary));
    ASSERT_TRUE(owner.Write(binary));
    const auto retained = owner.Arena.m_current_offset;
    ASSERT_TRUE(owner.Shader.TryInitialize(owner.Native.Device.get(), spec, &status, {.VFS = &owner.VFS}));
    EXPECT_EQ(status, VK_SUCCESS);
    EXPECT_EQ(owner.Arena.m_current_offset, retained);
    EXPECT_TRUE(owner.Shader.HasStages(VK_SHADER_STAGE_COMPUTE_BIT));
    EXPECT_TRUE(owner.Shader.SetLayouts.empty());
    EXPECT_TRUE(owner.Shader.DescriptorSetMap.empty());
    EXPECT_EQ(owner.Shader.m_descriptor_pool, VK_NULL_HANDLE);
    EXPECT_EQ(owner.Native.Vulkan.ValidationErrors.load(), 0u);
}

TEST(ShaderAdmissionTest, DeviceLossIsReportedAndPreventsFurtherNativeAdmission)
{
    GlslangLifetime compiler = {};
    ASSERT_TRUE(compiler.Ready);
    constexpr char        source[] = R"(#version 450
layout(local_size_x=1) in;
layout(set=0, binding=1, std430) buffer Result { uint Value; } Output;
void main() { Output.Value = 1; }
)";
    std::vector<uint32_t> binary   = {};
    ASSERT_TRUE(CompileGizmoStage(source, sizeof(source) - 1, EShLangCompute, binary));
    ShaderAdmissionOwner owner = {};
    ASSERT_TRUE(owner.Initialize());
    ASSERT_TRUE(owner.Write(binary));
    const Specs::ShaderSpecification specification = {.ComputeFilename = "/test.spv", .Name = "LostShader"};
    ShaderAdmissionFault::Result                   = VK_ERROR_DEVICE_LOST;
    VkResult status                                = VK_SUCCESS;
    ASSERT_FALSE(owner.Shader.TryInitialize(owner.Native.Device.get(), specification, &status, {.VFS = &owner.VFS, .AllocateSets = &ShaderAdmissionFault::Sets}));
    EXPECT_EQ(status, VK_ERROR_DEVICE_LOST);
    EXPECT_TRUE(owner.Native.Device->IsDeviceLost.load());
    const auto retained = owner.Arena.m_current_offset;
    EXPECT_FALSE(owner.Shader.TryInitialize(owner.Native.Device.get(), specification, &status, {.VFS = &owner.VFS}));
    EXPECT_EQ(status, VK_ERROR_DEVICE_LOST);
    EXPECT_EQ(owner.Arena.m_current_offset, retained);
    EXPECT_EQ(owner.Native.Device->PendingFree.Head, owner.Native.Device->PendingFree.Tail);
}

TEST_F(DelayedReadbackGpuFixture, PreparedStorageImageArraySurvivesBackingAndBankReleaseUntilComputeCompletes)
{
    GlslangLifetime compiler = {};
    ASSERT_TRUE(compiler.Ready);
    constexpr char        source[] = R"(#version 460
layout(local_size_x=1) in;
layout(set=0, binding=0, rgba8) uniform readonly image2D Images[2];
layout(set=0, binding=1, std430) buffer Result { uint Values[2]; } Output;
void main() {
    Output.Values[0] = packUnorm4x8(imageLoad(Images[0], ivec2(0)));
    Output.Values[1] = packUnorm4x8(imageLoad(Images[1], ivec2(0)));
})";
    std::vector<uint32_t> words    = {};
    ASSERT_TRUE(CompileGizmoStage(source, sizeof(source) - 1, EShLangCompute, words));
    auto manager = std::make_unique<RRM>();
    RRMUploadBatchTestHelper::InitializeHostWrites(*manager, *Device);
    Device->RRM = manager.get();
    struct Resources
    {
        DelayedReadbackGpuFixture* Fixture     = nullptr;
        RRM*                       Manager     = nullptr;
        VkDescriptorSetLayout      Set         = VK_NULL_HANDLE;
        VkPipelineLayout           Layout      = VK_NULL_HANDLE;
        VkShaderModule             Module      = VK_NULL_HANDLE;
        VkPipeline                 Compute     = VK_NULL_HANDLE;
        BufferView                 Output      = {};
        bool                       ImagesReady = false;
        ~Resources()
        {
            Fixture->ReleaseGate();
            EXPECT_EQ(vkDeviceWaitIdle(Fixture->Vulkan.Device), VK_SUCCESS);
            Manager->FinishResourceFrame();
            Manager->PollFrameResources();
            EXPECT_TRUE(Manager->ForgetResourceTimeline(Fixture->Completion.get()));
            RRMUploadBatchTestHelper::FlushTextureReleases(*Manager);
            auto& device = *Fixture->Device;
            device.PendingFree.Drain(&device.GpuMem, Fixture->Vulkan.Device, UINT64_MAX);
            if (ImagesReady)
            {
                device.ImageBufferManager.Dispose();
                device.GlobalTextures.Dispose();
                device.ImageViewCacheSlab.Shutdown();
            }
            if (Output)
            {
                device.GpuMem.FreeBuffer(Output);
            }
            device.PendingFree.Drain(&device.GpuMem, Fixture->Vulkan.Device, UINT64_MAX);
            if (Compute)
            {
                vkDestroyPipeline(Fixture->Vulkan.Device, Compute, nullptr);
            }
            if (Module)
            {
                vkDestroyShaderModule(Fixture->Vulkan.Device, Module, nullptr);
            }
            if (Layout)
            {
                vkDestroyPipelineLayout(Fixture->Vulkan.Device, Layout, nullptr);
            }
            if (Set)
            {
                vkDestroyDescriptorSetLayout(Fixture->Vulkan.Device, Set, nullptr);
            }
            device.RRM                        = nullptr;
            Fixture->Swapchain.CurrentFrame   = nullptr;
            Fixture->Swapchain.RenderTimeline = nullptr;
        }
    } resources              = {.Fixture = this, .Manager = manager.get()};
    Device->MaxGlobalTexture = 8;
    Device->GlobalTextures.Initialize(Device->Arena, 8);
    Device->ImageBufferManager.Initialize(Device->Arena, 8);
    Device->ImageViewCacheSlab.Init(Device->Arena, 32 * 1024);
    resources.ImagesReady                                  = true;
    const Specs::TextureSpecification           spec       = {.IsUsageStorage = true, .IsAliasable = true, .Width = 1, .Height = 1, .Format = Specs::ImageFormat::R8G8B8A8_UNORM};
    ZEngine::Rendering::Textures::TextureHandle aliases[2] = {};
    RRM::TextureVersionPin                      pins[2]    = {};
    VkImage                                     images[2]  = {};
    for (uint32_t index = 0; index < 2; ++index)
    {
        const auto backing = Device->CreateTexture(spec, "compute_array_backing");
        ASSERT_TRUE(backing.Valid());
        aliases[index] = Device->CreateAliasingTexture(spec, backing, "compute_array_alias");
        ASSERT_TRUE(aliases[index].Valid());
        pins[index] = manager->PinGraphTextureVersion(aliases[index]);
        ASSERT_TRUE(pins[index]);
        images[index] = Device->ImageBufferManager.Access(pins[index].Get()->Image)->GetHandle();
        // No submitted or recorded command references the backing image itself.
        // The alias object must independently keep its VMA allocation alive.
        Device->DestroyTexture(backing);
        ZEngine::Hardwares::TextureDisposeEntry retired = {};
        ASSERT_TRUE(Device->TextureHandleToDispose.pop(retired));
        auto* texture = Device->GlobalTextures.Access(retired.Handle);
        ASSERT_NE(texture, nullptr);
        Device->ImageBufferManager.Access(texture->BufferHandle)->Dispose();
        Device->ImageBufferManager.Remove(texture->BufferHandle);
        Device->GlobalTextures.Remove(retired.Handle);
        Device->PendingFree.Drain(&Device->GpuMem, Vulkan.Device, UINT64_MAX);
    }
    resources.Output = Device->GpuMem.AllocateBuffer(8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, GpuMemoryDomain::HostReadback, "compute_array_result");
    ASSERT_TRUE(resources.Output);
    const VkDescriptorSetLayoutBinding bindings[2] = {
        {.binding = 0,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 2, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT}
    };
    const VkDescriptorSetLayoutCreateInfo set_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = bindings};
    ASSERT_EQ(vkCreateDescriptorSetLayout(Vulkan.Device, &set_info, nullptr, &resources.Set), VK_SUCCESS);
    const VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &resources.Set};
    ASSERT_EQ(vkCreatePipelineLayout(Vulkan.Device, &layout_info, nullptr, &resources.Layout), VK_SUCCESS);
    const VkShaderModuleCreateInfo module_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = words.size() * sizeof(uint32_t), .pCode = words.data()};
    ASSERT_EQ(vkCreateShaderModule(Vulkan.Device, &module_info, nullptr, &resources.Module), VK_SUCCESS);
    const VkComputePipelineCreateInfo compute_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = resources.Module, .pName = "main"},
             .layout = resources.Layout
    };
    ASSERT_EQ(vkCreateComputePipelines(Vulkan.Device, VK_NULL_HANDLE, 1, &compute_info, nullptr, &resources.Compute), VK_SUCCESS);
    ZEngine::Rendering::Shaders::Shader shader = {};
    shader.SetLayouts.init(Device->Arena, 1);
    shader.SetLayouts.push(resources.Set);
    shader.BindingsByName.init(Device->Arena, 4);
    shader.LayoutBindingSpecifications.init(Device->Arena, 2);
    const Specs::LayoutBindingSpecification reflected[2] = {
        {.Set = 0, .Binding = 0, .Count = 2, .Name = "Images", .DescriptorTypeValue = Specs::DescriptorType::STORAGE_IMAGE, .Flags = Specs::ShaderStageFlags::COMPUTE},
        {.Set = 0, .Binding = 1, .Name = "Output", .DescriptorTypeValue = Specs::DescriptorType::STORAGE_BUFFER, .Flags = Specs::ShaderStageFlags::COMPUTE}
    };
    for (const auto& binding : reflected)
    {
        shader.BindingsByName.insert(binding.Name, binding);
        shader.LayoutBindingSpecifications.push(binding);
    }
    Renderers::Pipelines::GraphicPipeline pipeline   = {};
    pipeline.Shader                                  = &shader;
    OffscreenGizmoCycle::BorrowedGraphicPass backend = {};
    backend.InitializeBindings(Device.get());
    backend.Pipeline = &pipeline;
    backend.ConnectPipeline();
    Renderers::RenderPasses::PreparedDescriptorPass candidate = {};
    candidate.InitializeForPipeline(Device.get(), &pipeline);
    Swapchain.BufferredFrameCount = 3;
    for (uint32_t index = 0; index < 3; ++index)
    {
        candidate.BeginPreparedDescriptorPreparation(index);
        candidate.SetRetainedStorageImage("Images", pins[0], {}, 0);
        candidate.SetRetainedStorageImage("Images", pins[1], {}, 1);
        candidate.SetStorageBuffer("Output", &resources.Output);
        ASSERT_TRUE(candidate.Verify());
    }
    ASSERT_TRUE(backend.AdoptPreparedDescriptors(candidate));
    candidate.ReleaseDescriptorStorage();
    ZEngine::Hardwares::FrameContext frame = {.Index = 0};
    Swapchain.CurrentFrame                 = &frame;
    backend.BeginDescriptorPreparation();
    backend.SetStorageImage("Images", aliases[0], {}, 0);
    backend.SetStorageImage("Images", aliases[1], {}, 1);
    backend.SetStorageBuffer("Output", &resources.Output);
    ASSERT_TRUE(backend.CheckBindings());
    const auto                        descriptors = pipeline.ResolveDescriptorSet(pipeline.DescriptorReplayContext, 0, 0);
    VkCommandBuffer                   command     = VK_NULL_HANDLE;
    const VkCommandBufferAllocateInfo allocate    = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = Pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    ASSERT_EQ(vkAllocateCommandBuffers(Vulkan.Device, &allocate, &command), VK_SUCCESS);
    const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    ASSERT_EQ(vkBeginCommandBuffer(command, &begin), VK_SUCCESS);
    for (uint32_t index = 0; index < 2; ++index)
    {
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = images[index], .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
        };
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkClearColorValue color = {
            .float32 = {0.f, 0.f, 0.f, 1.f}
        };
        color.float32[index] = 1.f;
        vkCmdClearColorImage(command, images[index], VK_IMAGE_LAYOUT_GENERAL, &color, 1, &barrier.subresourceRange);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, resources.Compute);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, resources.Layout, 0, 1, &descriptors, 0, nullptr);
    vkCmdDispatch(command, 1, 1, 1);
    const VkBufferMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .buffer = resources.Output.Handle, .offset = 0, .size = 8};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host, 0, nullptr);
    ASSERT_EQ(vkEndCommandBuffer(command), VK_SUCCESS);
    const VkSemaphoreSubmitInfo     wait     = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = Gate->GetHandle(), .value = 1, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    const VkSemaphoreSubmitInfo     signal   = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = Completion->GetHandle(), .value = 3, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    const VkCommandBufferSubmitInfo commands = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = command};
    const VkSubmitInfo2             submit   = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .waitSemaphoreInfoCount = 1, .pWaitSemaphoreInfos = &wait, .commandBufferInfoCount = 1, .pCommandBufferInfos = &commands, .signalSemaphoreInfoCount = 1, .pSignalSemaphoreInfos = &signal};
    ASSERT_EQ(vkQueueSubmit2(Queue, 1, &submit, VK_NULL_HANDLE), VK_SUCCESS);
    Submitted = true; // Every later assertion can safely release the gate in teardown.
    manager->NotifyResourceSubmission(0, Completion.get(), 3);
    manager->FinishResourceFrame();
    Swapchain.RenderTimeline          = Completion.get();
    Swapchain.RenderTimelineNextValue = 3;
    backend.ReleaseBindings();
    backend.Pipeline = nullptr;
    for (uint32_t index = 0; index < 2; ++index)
    {
        Device->DestroyTexture(aliases[index]);
        pins[index].Reset();
        EXPECT_TRUE(manager->IsTextureVersionPinned(aliases[index]));
        EXPECT_FALSE(manager->PinGraphTextureVersion(aliases[index]));
    }
    RRMUploadBatchTestHelper::FlushTextureReleases(*manager);
    EXPECT_TRUE(Device->TextureHandleToDispose.empty());
    Device->PendingFree.Drain(&Device->GpuMem, Vulkan.Device, 0);
    EXPECT_FALSE(manager->ForgetResourceTimeline(Completion.get()));
    uint64_t completed = UINT64_MAX;
    EXPECT_EQ(vkGetSemaphoreCounterValue(Vulkan.Device, Completion->GetHandle(), &completed), VK_SUCCESS);
    EXPECT_EQ(completed, 0u);
    ReleaseGate();
    WaitForCopy();
    uint32_t result[2] = {};
    EXPECT_EQ(vmaCopyAllocationToMemory(Device->GpuMem.Allocator, resources.Output.Allocation, 0, result, sizeof(result)), VK_SUCCESS);
    EXPECT_EQ(result[0], 0xff0000ffu);
    EXPECT_EQ(result[1], 0xff00ff00u);
    manager->PollFrameResources();
    RRMUploadBatchTestHelper::FlushTextureReleases(*manager);
    EXPECT_FALSE(manager->IsTextureVersionPinned(aliases[0]));
    EXPECT_FALSE(manager->IsTextureVersionPinned(aliases[1]));
    EXPECT_EQ(Vulkan.ValidationErrors.load(), 0u);
}

TEST_F(RenderResourceManagerBatchTest, TextureReplacementWaitsForGpuAndDescriptorPublicationWithoutLosingOwnershipTicket)
{
    ASSERT_EQ(AssetManager::Instance(), nullptr);
    MemoryManager asset_memory = {};
    asset_memory.Initialize(ZMega(512), {});
    AssetManager::Initialize(&asset_memory.MainArena, nullptr, "/project");
    struct AssetGuard
    {
        ~AssetGuard()
        {
            AssetManager::Shutdown();
        }
    } asset_guard            = {};
    Device->MaxGlobalTexture = 8;
    Device->GlobalTextures.Initialize(&Memory.MainArena, 8);
    Device->ImageBufferManager.Initialize(&Memory.MainArena, 8);
    Device->ImageViewCacheSlab.Init(&Memory.MainArena, 32 * 1024);
    Device->RRM                                                            = Manager.get();
    ResourceTexturesInitialized                                            = true;
    ZEngine::Rendering::Specifications::TextureSpecification specification = {};
    specification.Width = specification.Height = 1;
    specification.Format                       = ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM;
    specification.IsUsageTransferSource        = true;
    const auto previous                        = Device->CreateTexture(specification, "reload-previous");
    const auto candidate                       = Device->CreateTexture(specification, "reload-candidate");
    ASSERT_TRUE(previous.Valid());
    ASSERT_TRUE(candidate.Valid());
    const auto uuid        = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    auto*      assets      = AssetManager::Instance();
    auto&      descriptor  = assets->Textures.push_use({});
    descriptor.TextureUUID = uuid;
    descriptor.Handle      = previous;
    assets->UUIDToTextureHandle.insert(uuid, previous);
    auto retained = Manager->PinTextureVersion(previous);
    ASSERT_TRUE(retained);
    const auto  old_image = retained.Get()->Image;
    const auto* resource  = Manager->GetTexture(candidate);
    ASSERT_NE(resource, nullptr);
    const auto               image   = Device->ImageBufferManager.Access(resource->BufferHandle)->GetHandle();
    const auto               command = Commands[0]->GetHandle();
    VkCommandBufferBeginInfo begin   = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    ASSERT_EQ(vkBeginCommandBuffer(command, &begin), VK_SUCCESS);
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    const VkClearColorValue green = {
        .float32 = {0.f, 1.f, 0.f, 1.f}
    };
    vkCmdClearColorImage(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &green, 1, &barrier.subresourceRange);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
          .imageExtent = {1, 1, 1}
    };
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Target.Handle, 1, &copy);
    ASSERT_EQ(vkEndCommandBuffer(command), VK_SUCCESS);
    RRMUploadBatchTestHelper::StageTextureReplacement(*Manager, uuid, previous, candidate, Memory.MainArena);
    auto                          gate          = std::make_unique<ZEngine::Rendering::Primitives::Semaphore>(Device.get(), true);
    const auto                    wait          = gate->GetHandle();
    const auto                    timeline      = Timeline->GetHandle();
    const uint64_t                value         = 1;
    const VkPipelineStageFlags    stage         = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkTimelineSemaphoreSubmitInfo timeline_info = {.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, .waitSemaphoreValueCount = 1, .pWaitSemaphoreValues = &value, .signalSemaphoreValueCount = 1, .pSignalSemaphoreValues = &value};
    VkSubmitInfo                  submit        = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = &timeline_info, .waitSemaphoreCount = 1, .pWaitSemaphores = &wait, .pWaitDstStageMask = &stage, .commandBufferCount = 1, .pCommandBuffers = &command, .signalSemaphoreCount = 1, .pSignalSemaphores = &timeline};
    ASSERT_EQ(vkQueueSubmit(Device->GetQueue(ZEngine::Rendering::QueueType::GRAPHIC_QUEUE).Handle, 1, &submit, VK_NULL_HANDLE), VK_SUCCESS);
    // No fatal assertion may strand this deliberately delayed accepted producer.
    RRMUploadBatchTestHelper::SubmitReplacementTicket(*Manager, {.Texture = candidate, .CompletionTimeline = Timeline.get(), .CompletionValue = 1, .PostReleaseLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .ProducerQueueFamily = Device->GraphicFamilyIndex});
    RRMUploadBatchTestHelper::PollTextureReplacements(*Manager);
    EXPECT_EQ(AssetManager::FindTextureHandle(uuid).Index, previous.Index);
    gate->Signal(1);
    EXPECT_EQ(Read(1), 0xff00ff00u);
    EXPECT_TRUE(Device->TextureHandleToUpdates.push(candidate));
    RRMUploadBatchTestHelper::PollTextureReplacements(*Manager);
    EXPECT_EQ(AssetManager::FindTextureHandle(uuid).Index, previous.Index);
    ZEngine::Rendering::Textures::TextureHandle update = {};
    EXPECT_TRUE(Device->TextureHandleToUpdates.pop(update));
    RRMUploadBatchTestHelper::PollTextureReplacements(*Manager);
    EXPECT_EQ(AssetManager::FindTextureHandle(uuid).Index, candidate.Index);
    EXPECT_NE(Manager->FindStreamingUploadTicket(candidate), nullptr);
    EXPECT_EQ(retained.Get()->Image.Index, old_image.Index);
    EXPECT_EQ(retained.Get()->Image.Generation, old_image.Generation);
    EXPECT_TRUE(Manager->GetTexture(previous));
    EXPECT_FALSE(Manager->CanWriteTextureVersion(previous));
    retained.Reset();
}

TEST_F(RenderResourceManagerBatchTest, SceneTexturePreparationObservesProducerCompletionWithoutConsumingItsOwnershipTicket)
{
    const auto texture = CreateProducerTestTexture("scene-producer-readiness");
    ASSERT_TRUE(texture.Valid());
    ZEngine::Rendering::Meshes::MeshMaterial material = {};
    material.AlbedoMap                                = static_cast<uint32_t>(texture.Index);
    auto retained                                     = RRMUploadBatchTestHelper::RetainMaterial(*Manager, material);
    ASSERT_TRUE(retained);
    Append(0, 42);
    auto gate                         = std::make_unique<ZEngine::Rendering::Primitives::Semaphore>(Device.get(), true);
    Swapchain.RenderTimeline          = gate.get();
    Swapchain.RenderTimelineNextValue = 1;
    Manager->EndFrame(); // Accepted producer work remains blocked on the host gate.
    RRMUploadBatchTestHelper::PublishSceneTextureProducer(*Manager, Memory.MainArena, {.Texture = texture, .CompletionTimeline = Timeline.get(), .CompletionValue = 1, .ProducerQueueFamily = Device->GraphicFamilyIndex});
    // No fatal assertions may leave this accepted submission stranded.
    bool failed = false;
    EXPECT_FALSE(RRMUploadBatchTestHelper::SceneTextureReady(*Manager, retained, &failed));
    EXPECT_FALSE(failed);
    // Accepted consumer work can wait on this still-blocked producer. Removing
    // its acquire ticket must not turn producer completion into a false success.
    const auto ticket = *Manager->FindStreamingUploadTicket(texture);
    Manager->AcknowledgeStreamingUploadTicket(ticket);
    EXPECT_EQ(Manager->FindStreamingUploadTicket(texture), nullptr);
    EXPECT_FALSE(RRMUploadBatchTestHelper::SceneTextureReady(*Manager, retained, &failed));
    EXPECT_FALSE(failed);
    gate->Signal(1);
    EXPECT_EQ(Read(1), 42u);
    EXPECT_TRUE(RRMUploadBatchTestHelper::SceneTextureReady(*Manager, retained, &failed));
    EXPECT_FALSE(failed);
    EXPECT_EQ(Manager->FindStreamingUploadTicket(texture), nullptr);
    Swapchain.RenderTimeline          = nullptr;
    Swapchain.RenderTimelineNextValue = 0;
    retained.Reset();
}

TEST_F(RenderResourceManagerBatchTest, TextureReleaseRetainsAcknowledgedProducerUntilGpuCompletion)
{
    CheckAcknowledgedProducerRetirement(false);
}

TEST_F(RenderResourceManagerBatchTest, DirectTextureDestroyRetainsAcknowledgedProducerAcrossReleaseOwners)
{
    CheckAcknowledgedProducerRetirement(true);
}

TEST_F(RenderResourceManagerBatchTest, RejectedGraphicsTextureJobsReclaimSlotsAndStagingBeforeRetry)
{
    CheckRejectedTextureUploadReclamation(false);
}

TEST_F(RenderResourceManagerBatchTest, RejectedTransferTextureJobsReclaimSlotsAndStagingBeforeRetry)
{
    CheckRejectedTextureUploadReclamation(true);
}

TEST_F(RenderResourceManagerBatchTest, QueuedGraphicsTextureUploadRetainsImageUntilRejection)
{
    CheckQueuedTextureUploadLifetime(false, false);
}

TEST_F(RenderResourceManagerBatchTest, QueuedTransferTextureUploadRetainsImageUntilRejection)
{
    CheckQueuedTextureUploadLifetime(true, false);
}

TEST_F(RenderResourceManagerBatchTest, QueuedGraphicsTextureUploadTransfersLifetimeOnSubmission)
{
    CheckQueuedTextureUploadLifetime(false, true);
}

TEST_F(RenderResourceManagerBatchTest, QueuedTransferTextureUploadTransfersLifetimeOnSubmission)
{
    CheckQueuedTextureUploadLifetime(true, true);
}

TEST_F(RenderResourceManagerBatchTest, AcquiredTextureReuploadsStayOnGraphicsAfterRejectedRecording)
{
    const auto texture = CreateProducerTestTexture("graphics-owned-reupload");
    ASSERT_TRUE(texture.Valid());
    RRMUploadBatchTestHelper::InitializeTextureUploadSlots(*Manager, *Device, UploadCommands, true);
    TextureUploadSlotsInitialized = true;
    uint8_t pixels[4]             = {17, 53, 127, 255};
    ASSERT_TRUE(Manager->UploadTextureBuffer(0, 0, texture, pixels, sizeof(pixels)).Valid());
    EXPECT_NE(RRMUploadBatchTestHelper::TextureUploadSlotValue(*Manager, true), 0u);
    EXPECT_EQ(RRMUploadBatchTestHelper::TextureUploadSlotValue(*Manager, false), 0u);
    Manager->SubmitAsyncUploads();
    const auto* ticket = Manager->FindStreamingUploadTicket(texture);
    ASSERT_NE(ticket, nullptr);
    const auto accepted = *ticket;
    accepted.CompletionTimeline->Wait(accepted.CompletionValue);
    RRMUploadBatchTestHelper::RetireTextureUploadSlots(*Manager);
    // Even completed producer work cannot bypass its outstanding acquire.
    EXPECT_FALSE(Manager->BeginTextureVersionWrite(texture));
    EXPECT_FALSE(Manager->CanWriteTextureVersion(texture));
    EXPECT_FALSE(Device->ReconstructTexture(texture, Device->GlobalTextures.Access(texture)->Specification));
    EXPECT_FALSE(Manager->UploadTextureBuffer(0, 0, texture, pixels, sizeof(pixels)).Valid());
    Manager->AcknowledgeStreamingUploadTicket(accepted);
    EXPECT_TRUE(Manager->BeginTextureVersionWrite(texture));
    ASSERT_TRUE(Manager->UploadTextureBuffer(0, 0, texture, pixels, sizeof(pixels)).Valid());
    EXPECT_EQ(RRMUploadBatchTestHelper::TextureUploadSlotValue(*Manager, true), 0u);
    EXPECT_NE(RRMUploadBatchTestHelper::TextureUploadSlotValue(*Manager, false), 0u);
    Manager->SubmitAsyncUploads();
    ticket = Manager->FindStreamingUploadTicket(texture);
    ASSERT_NE(ticket, nullptr);
    const auto graphics_upload = *ticket;
    graphics_upload.CompletionTimeline->Wait(graphics_upload.CompletionValue);
    RRMUploadBatchTestHelper::RetireTextureUploadSlots(*Manager);
    EXPECT_EQ(graphics_upload.ProducerQueueFamily, Device->GraphicFamilyIndex);
    Manager->AcknowledgeStreamingUploadTicket(graphics_upload);
    ASSERT_TRUE(Manager->UploadTextureBuffer(0, 0, texture, pixels, sizeof(pixels)).Valid());
    RRMUploadBatchTestHelper::ClearTextureUploadJobs(*Manager);
    const auto* resource = Device->GlobalTextures.Access(texture);
    ASSERT_NE(resource, nullptr);
    const auto layout = Device->ImageBufferManager.Access(resource->BufferHandle)->Layout;
    EXPECT_EQ(layout, ZEngine::Rendering::Specifications::ImageLayout::UNDEFINED);
    // Invalidating a host layout does not return GPU ownership to transfer.
    EXPECT_EQ(RRMUploadBatchTestHelper::TextureUploadQueue(*Manager, texture, layout), ZEngine::Rendering::QueueType::GRAPHIC_QUEUE);
    ASSERT_TRUE(Manager->UploadTextureBuffer(0, 0, texture, pixels, sizeof(pixels)).Valid());
    EXPECT_NE(RRMUploadBatchTestHelper::TextureUploadSlotValue(*Manager, false), 0u);
    RRMUploadBatchTestHelper::ClearTextureUploadJobs(*Manager);
    const auto fresh = Device->CreateTexture(resource->Specification, "fresh-transfer-eligible");
    ASSERT_TRUE(fresh.Valid());
    EXPECT_EQ(RRMUploadBatchTestHelper::TextureUploadQueue(*Manager, fresh, layout), ZEngine::Rendering::QueueType::TRANSFER_QUEUE);
    ASSERT_TRUE(Manager->UploadTextureBuffer(0, 0, fresh, pixels, sizeof(pixels)).Valid());
    EXPECT_NE(RRMUploadBatchTestHelper::TextureUploadSlotValue(*Manager, true), 0u);
    RRMUploadBatchTestHelper::ClearTextureUploadJobs(*Manager);
}

TEST_F(RenderResourceManagerBatchTest, AcknowledgedButIncompleteProducerBlocksOrdinaryImageMutation)
{
    const auto texture = CreateProducerTestTexture("incomplete-image-mutation");
    ASSERT_TRUE(texture.Valid());
    const auto image = Device->GlobalTextures.Access(texture)->BufferHandle;
    Append(0, 42);
    auto gate                         = std::make_unique<ZEngine::Rendering::Primitives::Semaphore>(Device.get(), true);
    Swapchain.RenderTimeline          = gate.get();
    Swapchain.RenderTimelineNextValue = 1;
    Manager->EndFrame();
    // No fatal assertions may strand this host-gated submission.
    const ZEngine::Hardwares::StreamingUploadTicket ticket = {.Texture = texture, .CompletionTimeline = Timeline.get(), .CompletionValue = 1, .ProducerQueueFamily = Device->GraphicFamilyIndex};
    RRMUploadBatchTestHelper::PublishSceneTextureProducer(*Manager, Memory.MainArena, ticket);
    Manager->AcknowledgeStreamingUploadTicket(ticket);
    EXPECT_FALSE(Manager->CanWriteTextureVersion(texture));
    EXPECT_FALSE(Manager->BeginTextureVersionWrite(texture));
    const auto specification = Device->GlobalTextures.Access(texture)->Specification;
    EXPECT_FALSE(Device->ReconstructTexture(texture, specification));
    EXPECT_FALSE(Device->ReconstructAliasingTexture(texture, specification, texture));
    uint8_t pixels[4] = {};
    EXPECT_FALSE(Device->WriteTextureData(Commands[0].get(), texture, pixels, nullptr, false, sizeof(pixels)));
    EXPECT_FALSE(Manager->UploadTextureBuffer(0, 0, texture, pixels, sizeof(pixels)).Valid());
    EXPECT_EQ(Device->GlobalTextures.Access(texture)->BufferHandle.Index, image.Index);
    EXPECT_EQ(Device->GlobalTextures.Access(texture)->BufferHandle.Generation, image.Generation);
    gate->Signal(1);
    EXPECT_EQ(Read(1), 42u);
    EXPECT_TRUE(Manager->BeginTextureVersionWrite(texture));
    EXPECT_TRUE(Manager->CanWriteTextureVersion(texture));
    Swapchain.RenderTimeline          = nullptr;
    Swapchain.RenderTimelineNextValue = 0;
}

TEST_F(RenderResourceManagerBatchTest, SceneResourcePreparationWaitsForAnAcceptedDelayedUpload)
{
    RRMUploadBatchTestHelper::InitializeGeometry(*Manager, Memory.MainArena);
    const auto mesh = RRMUploadBatchTestHelper::InstallGeometry(*Manager);
    ASSERT_TRUE(mesh.IsValid());
    const auto uuid = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    RRMUploadBatchTestHelper::RegisterGeometry(*Manager, uuid, mesh);
    const std::array                meshes{uuid};
    RRM::SceneResourcePreparationId id = {};
    Append(0, 42);
    ASSERT_TRUE(Manager->PrepareSceneResources({123, 1}, 7, meshes, &id));
    Manager->PollSceneResourcePreparations(); // pin while its upload batch is still open
    RRM::SceneResourcePreparationResult result = {};
    ASSERT_TRUE(Manager->PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(result.State, RRM::SceneResourcePreparationState::Preparing);
    auto gate                         = std::make_unique<ZEngine::Rendering::Primitives::Semaphore>(Device.get(), true);
    Swapchain.RenderTimeline          = gate.get();
    Swapchain.RenderTimelineNextValue = 1;
    Manager->EndFrame(); // Vulkan accepted the copy; GPU waits on the unsignalled gate
    EXPECT_TRUE(Manager->PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(result.State, RRM::SceneResourcePreparationState::Preparing);
    RRM::MeshGeometrySnapshot snapshot = {};
    EXPECT_FALSE(Manager->GetPreparedSceneGeometry(id, uuid, &snapshot));
    // No fatal assertion may strand the deliberately blocked accepted submission.
    gate->Signal(1);
    EXPECT_EQ(Read(1), 42u);
    Manager->PollSceneResourcePreparations();
    EXPECT_TRUE(Manager->PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(result.State, RRM::SceneResourcePreparationState::GeometryReady);
    EXPECT_EQ(result.ExpectedGeneration, 7u);
    EXPECT_TRUE(Manager->GetPreparedSceneGeometry(id, uuid, &snapshot));
    EXPECT_TRUE(Manager->ReleaseSceneResourcePreparation(id));
    Manager->PollSceneResourcePreparations();
    EXPECT_FALSE(Manager->PollSceneResourcePreparation(id, &result));
    Swapchain.RenderTimeline          = nullptr;
    Swapchain.RenderTimelineNextValue = 0;
}

TEST_F(RenderResourceManagerBatchTest, CancelledScenePreparationRetainsGeometryUntilDelayedUploadRetires)
{
    RRMUploadBatchTestHelper::InitializeGeometry(*Manager, Memory.MainArena);
    TLSFSlab metadata_slab = {};
    metadata_slab.Init(&Memory.MainArena, 16 * 1024);
    const auto                    initial_capacity = RRMUploadBatchTestHelper::AvailableMetadataBlocks(metadata_slab);
    ZEngine::Importers::AssetMesh source           = {};
    source.BoundsCenter                            = {1.f, 2.f, 3.f};
    source.BoundsRadius                            = 4.f;
    const auto* metadata                           = RRMUploadBatchTestHelper::CaptureMetadata(metadata_slab, source);
    ASSERT_NE(metadata, nullptr);
    const auto mesh = RRMUploadBatchTestHelper::InstallGeometry(*Manager, metadata);
    ASSERT_TRUE(mesh.IsValid());
    const auto uuid = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    RRMUploadBatchTestHelper::RegisterGeometry(*Manager, uuid, mesh);
    const std::array                meshes{uuid};
    RRM::SceneResourcePreparationId id = {};
    Append(0, 42);
    ASSERT_TRUE(Manager->PrepareSceneResources({123, 1}, 7, meshes, &id));
    Manager->PollSceneResourcePreparations();
    auto gate                         = std::make_unique<ZEngine::Rendering::Primitives::Semaphore>(Device.get(), true);
    Swapchain.RenderTimeline          = gate.get();
    Swapchain.RenderTimelineNextValue = 1;
    Manager->EndFrame();
    EXPECT_TRUE(Manager->ReleaseSceneResourcePreparation(id));
    Manager->Release(mesh);
    Manager->PollSceneResourcePreparations();
    EXPECT_EQ(RRMUploadBatchTestHelper::GeometryBytes(*Manager), 32u);
    RRM::SceneResourcePreparationResult result = {};
    EXPECT_TRUE(Manager->PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(result.State, RRM::SceneResourcePreparationState::Releasing);
    EXPECT_LT(RRMUploadBatchTestHelper::AvailableMetadataBlocks(metadata_slab), initial_capacity);
    EXPECT_FLOAT_EQ(metadata->BoundsCenter.x, 1.f);
    EXPECT_FLOAT_EQ(metadata->BoundsRadius, 4.f);
    gate->Signal(1);
    EXPECT_EQ(Read(1), 42u);
    Manager->PollSceneResourcePreparations();
    EXPECT_EQ(RRMUploadBatchTestHelper::GeometryBytes(*Manager), 0u);
    EXPECT_FALSE(Manager->PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(RRMUploadBatchTestHelper::AvailableMetadataBlocks(metadata_slab), initial_capacity);
    Swapchain.RenderTimeline          = nullptr;
    Swapchain.RenderTimelineNextValue = 0;
    metadata_slab.Shutdown();
}
