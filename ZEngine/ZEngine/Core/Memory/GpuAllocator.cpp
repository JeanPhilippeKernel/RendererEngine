#define VMA_IMPLEMENTATION
#define VMA_VULKAN_VERSION           1003000 // Vulkan 1.3
#define VMA_STATIC_VULKAN_FUNCTIONS  1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0

#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/ZEngineDef.h>

#ifdef VMA_DEBUG_DETECT_CORRUPTION
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#define VMA_DEBUG_LOG_FORMAT(format, ...)                                          \
    do                                                                             \
    {                                                                              \
        char __vma_buf[512];                                                       \
        snprintf(__vma_buf, sizeof(__vma_buf), "[VMA] " format "\n", __VA_ARGS__); \
        OutputDebugStringA(__vma_buf);                                             \
        fputs(__vma_buf, stderr);                                                  \
    } while (0)
#else
#define VMA_DEBUG_LOG_FORMAT(format, ...) fprintf(stderr, "[VMA] " format "\n", __VA_ARGS__)
#endif
#endif

namespace ZEngine::Core::Memory
{
    void GpuAllocator::Initialize(VkPhysicalDevice physical_device, VkDevice device, VkInstance instance, bool has_memory_budget_ext, bool has_buffer_device_address_ext)
    {
        VmaAllocatorCreateInfo allocator_info = {.flags = VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT};
        allocator_info.physicalDevice         = physical_device;
        allocator_info.device                 = device;
        allocator_info.instance               = instance;
        allocator_info.vulkanApiVersion       = VK_API_VERSION_1_3;

        VmaAllocatorCreateFlags flags         = 0;
        if (has_buffer_device_address_ext)
        {
            flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        }
        if (has_memory_budget_ext)
        {
            flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
        }
        allocator_info.flags |= flags;

        ZENGINE_VALIDATE_ASSERT(vmaCreateAllocator(&allocator_info, &Allocator) == VK_SUCCESS, "Failed to create VMA Allocator");

        HasBudgetExt = has_memory_budget_ext;

        if (HasBudgetExt)
        {
            const VkPhysicalDeviceMemoryProperties* memory_properties = nullptr;
            vmaGetHeapBudgets(Allocator, HeapBudgets);
            vmaGetMemoryProperties(Allocator, &memory_properties);
            HeapCount = memory_properties->memoryHeapCount;
        }

        Ring.Initialize(Allocator);
    }

    void GpuAllocator::Shutdown()
    {
        Ring.Shutdown(Allocator);

        VmaTotalStatistics stats = {};
        vmaCalculateStatistics(Allocator, &stats);
        if (stats.total.statistics.allocationCount > 0)
        {
            ZENGINE_CORE_WARN("[GPU] VMA shutdown: {} allocation(s) still live ({} MB)", stats.total.statistics.allocationCount, stats.total.statistics.allocationBytes >> 20);
        }

        vmaDestroyAllocator(Allocator);
    }

    BufferView GpuAllocator::AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, GpuMemoryDomain domain, const char* debug_name)
    {
        VkBufferCreateInfo buffer_create_info          = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer_create_info.size                        = size;
        buffer_create_info.usage                       = usage;
        buffer_create_info.sharingMode                 = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocation_create_info = {.flags = 0};
        if (domain == GpuMemoryDomain::DeviceGeometry || domain == GpuMemoryDomain::DeviceTexture || domain == GpuMemoryDomain::RenderTarget)
        {
            allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        }

        if (domain == GpuMemoryDomain::HostUniform)
        {
            // VMA_MEMORY_USAGE_AUTO (not PREFER_DEVICE) + HOST_ACCESS_SEQUENTIAL_WRITE_BIT:
            // This guarantees HOST_VISIBLE on ALL GPU classes:
            //   Apple Silicon / UMA    → HOST_VISIBLE (same physical RAM as GPU)
            //   Discrete + ReBAR       → DEVICE_LOCAL | HOST_VISIBLE (256 MB BAR window)
            //   Discrete without ReBAR → HOST_VISIBLE system RAM (no DEVICE_LOCAL)
            //
            // We intentionally do NOT use ALLOW_TRANSFER_INSTEAD_BIT: on discrete GPUs
            // without ReBAR, VMA may choose DEVICE_LOCAL-only memory which would require
            // a staging copy path in UpdateBuffer — but that path requires a valid
            // CurrentFrameCmd (not available during initialization).  By using AUTO
            // without ALLOW_TRANSFER_INSTEAD, VMA is required to satisfy the
            // HOST_VISIBLE constraint or fail allocation.
            allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO;
            allocation_create_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        if (domain == GpuMemoryDomain::HostStaging)
        {
            allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO;
            allocation_create_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        BufferView buffer_view = {.DebugName = debug_name, .Domain = domain};
        ZENGINE_VALIDATE_ASSERT(vmaCreateBuffer(Allocator, &buffer_create_info, &allocation_create_info, &buffer_view.Handle, &buffer_view.Allocation, nullptr) == VK_SUCCESS, "Failed to allocate buffer");
        vmaSetAllocationName(Allocator, buffer_view.Allocation, debug_name);
        return buffer_view;
    }

    BufferImage GpuAllocator::AllocateImage(VkImageCreateInfo& image_info, GpuMemoryDomain domain, VkDevice device, VkImageAspectFlagBits aspect, VkImageViewType view_type, uint32_t layer_count, const char* debug_name)
    {
        VmaAllocationCreateInfo allocation_create_info = {.flags = 0};
        if (domain == GpuMemoryDomain::DeviceGeometry || domain == GpuMemoryDomain::DeviceTexture || domain == GpuMemoryDomain::RenderTarget || domain == GpuMemoryDomain::HostUniform || domain == GpuMemoryDomain::HostStaging)
        {
            allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        }

        BufferImage buffer_image = {.DebugName = debug_name, .Domain = domain};
        ZENGINE_VALIDATE_ASSERT(vmaCreateImage(Allocator, &image_info, &allocation_create_info, &buffer_image.Handle, &buffer_image.Allocation, nullptr) == VK_SUCCESS, "Failed to allocate image");
        vmaSetAllocationName(Allocator, buffer_image.Allocation, debug_name);

        VkImageViewCreateInfo view_create_info           = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_create_info.image                           = buffer_image.Handle;
        view_create_info.viewType                        = view_type;
        view_create_info.format                          = image_info.format;
        view_create_info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.subresourceRange.aspectMask     = aspect;
        view_create_info.subresourceRange.baseMipLevel   = 0;
        view_create_info.subresourceRange.levelCount     = 1;
        view_create_info.subresourceRange.baseArrayLayer = 0;
        view_create_info.subresourceRange.layerCount     = layer_count;
        ZENGINE_VALIDATE_ASSERT(vkCreateImageView(device, &view_create_info, nullptr, &buffer_image.ViewHandle) == VK_SUCCESS, "Failed to create image view");

        return buffer_image;
    }

    void GpuAllocator::FreeBuffer(BufferView& buffer)
    {
        vmaDestroyBuffer(Allocator, buffer.Handle, buffer.Allocation);
        buffer.Handle     = VK_NULL_HANDLE;
        buffer.Allocation = nullptr;
    }

    void GpuAllocator::FreeImage(BufferImage& image, VkDevice device)
    {
        if (image.ViewHandle != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, image.ViewHandle, nullptr);
            image.ViewHandle = VK_NULL_HANDLE;
        }
        vmaDestroyImage(Allocator, image.Handle, image.Allocation);
        image.Handle     = VK_NULL_HANDLE;
        image.Allocation = nullptr;
    }

    void GpuAllocator::SampleBudgets()
    {
        if (HasBudgetExt)
        {
            vmaGetHeapBudgets(Allocator, HeapBudgets);
        }
        else
        {
            VmaTotalStatistics                      stats             = {};
            const VkPhysicalDeviceMemoryProperties* memory_properties = nullptr;
            vmaCalculateStatistics(Allocator, &stats);
            vmaGetMemoryProperties(Allocator, &memory_properties);
            HeapCount = memory_properties->memoryHeapCount;

            for (uint32_t i = 0; i < HeapCount; ++i)
            {
                HeapBudgets[i].statistics = stats.memoryHeap[i].statistics;
                HeapBudgets[i].usage      = stats.memoryHeap[i].statistics.blockBytes;
                HeapBudgets[i].budget     = memory_properties->memoryHeaps[i].size;
            }
        }

        for (uint32_t i = 0; i < HeapCount; ++i)
        {
            float p = (float) HeapBudgets[i].usage / (float) HeapBudgets[i].budget;
            if (p > WarnPressure)
            {
                ZENGINE_LOG_ENGINE_WARN("[GPU] Heap %u at %.0f%% (%zu / %zu MB)", i, p * 100.0f, HeapBudgets[i].usage >> 20, HeapBudgets[i].budget >> 20);
            }
        }
    }

    float GpuAllocator::HeapPressure(uint32_t heap_index) const
    {
        if (heap_index >= HeapCount)
        {
            return 0.0f;
        }
        return (float) HeapBudgets[heap_index].usage / (float) HeapBudgets[heap_index].budget;
    }

    void StagingRingBuffer::Initialize(VmaAllocator alloc)
    {
        VkBufferCreateInfo buffer_create_info          = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer_create_info.size                        = kCapacity;
        buffer_create_info.usage                       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_create_info.sharingMode                 = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocation_create_info = {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT};
        allocation_create_info.usage                   = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VmaAllocationInfo result                       = {};
        ZENGINE_VALIDATE_ASSERT(vmaCreateBuffer(alloc, &buffer_create_info, &allocation_create_info, &Buffer, &Allocation, &result) == VK_SUCCESS, "Failed to allocate buffer");
        vmaSetAllocationName(alloc, Allocation, DebugName);

        MappedPtr = result.pMappedData;
    }

    void StagingRingBuffer::Shutdown(VmaAllocator alloc)
    {
        vmaDestroyBuffer(alloc, Buffer, Allocation);
    }

    void* StagingRingBuffer::Allocate(uint32_t size, uint32_t alignment, uint32_t* out_vk_offset)
    {
        if (!MappedPtr)
        {
            return nullptr;
        }

        uint32_t offset = static_cast<uint32_t>(Helpers::memory_align_size_t(WritePos, alignment));

        if (WritePos >= ReadPos)
        {
            if ((offset + size) < static_cast<uint32_t>(kCapacity))
            {
                *out_vk_offset = offset;
                WritePos       = offset + size;
                return reinterpret_cast<uint8_t*>(MappedPtr) + offset;
            }

            // this causes a buffer wrapping
            if (size < ReadPos)
            {
                *out_vk_offset = 0;
                WritePos       = size;
                return MappedPtr;
            }
            return nullptr;
        }
        else
        {
            if ((offset + size) < ReadPos)
            {
                *out_vk_offset = offset;
                WritePos       = offset + size;
                return reinterpret_cast<uint8_t*>(MappedPtr) + offset;
            }
            return nullptr;
        }
    }

    void StagingRingBuffer::Submit(uint32_t vk_offset, uint32_t size, uint64_t timeline_value)
    {
        Chunks[ChunkTail] = {vk_offset, size, timeline_value};
        ChunkTail         = (ChunkTail + 1) % kMaxChunks;
    }

    void StagingRingBuffer::Drain(uint64_t completed_value)
    {
        while (ChunkHead != ChunkTail && Chunks[ChunkHead].TimelineValue <= completed_value)
        {
            ReadPos   = Chunks[ChunkHead].Offset + Chunks[ChunkHead].Size;
            ChunkHead = (ChunkHead + 1) % kMaxChunks;
        }
    }

} // namespace ZEngine::Core::Memory