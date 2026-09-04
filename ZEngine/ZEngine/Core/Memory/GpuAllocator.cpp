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

        // Segregated pools isolate each domain's allocation churn/fragmentation from the
        // others. A pool that fails to create is left null — every call site below falls
        // back to the default allocator, so this is an optimization, never a hard
        // dependency for engine startup.
        auto create_buffer_pool = [&](GpuMemoryDomain domain, VkBufferUsageFlags usage, VmaAllocationCreateInfo alloc_info, VkDeviceSize block_size, size_t max_block_count) {
            VkBufferCreateInfo rep_buffer_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            rep_buffer_info.size               = 1;
            rep_buffer_info.usage              = usage;

            uint32_t memory_type_index         = 0;
            if (vmaFindMemoryTypeIndexForBufferInfo(Allocator, &rep_buffer_info, &alloc_info, &memory_type_index) != VK_SUCCESS)
            {
                ZENGINE_CORE_WARN("[GPU] Failed to find memory type index for domain {} pool — falling back to default pool", static_cast<int>(domain))
                return;
            }

            VmaPoolCreateInfo pool_info = {};
            pool_info.memoryTypeIndex   = memory_type_index;
            pool_info.blockSize         = block_size;
            pool_info.maxBlockCount     = max_block_count;
            if (vmaCreatePool(Allocator, &pool_info, &Pools[static_cast<uint8_t>(domain)]) != VK_SUCCESS)
            {
                ZENGINE_CORE_WARN("[GPU] Failed to create pool for domain {} — falling back to default pool", static_cast<int>(domain))
                Pools[static_cast<uint8_t>(domain)] = nullptr;
            }
        };

        // DeviceGeometry — vertex/index/storage buffers. Fixed block size is fine here:
        // geometry buffers don't vary in size the way textures do.
        create_buffer_pool(GpuMemoryDomain::DeviceGeometry, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VmaAllocationCreateInfo{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE}, GeometryBytes, 2);

        // HostUniform — per-frame UBOs/SSBOs on the BAR window.
        create_buffer_pool(GpuMemoryDomain::HostUniform, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VmaAllocationCreateInfo{.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO}, UniformBytes, 1);

        // HostStaging — built from the staging ring's OWN flags (AUTO_PREFER_DEVICE), not
        // AllocateBuffer's generic HostStaging branch (plain AUTO): those can resolve to
        // different memory type indices on a discrete GPU with a BAR window, and the ring
        // is this domain's dominant consumer. The generic one-off staging buffers
        // (AllocateBuffer with domain=HostStaging) share this same pool.
        //
        // blockSize=0 (auto-sized), not a fixed StagingBytes block: a block needs some
        // alignment/bookkeeping headroom beyond its raw byte count, so a fixed block
        // exactly equal to the ring's own StagingBytes allocation can't actually fit it
        // (confirmed: fails with VK_ERROR_OUT_OF_DEVICE_MEMORY). Same underlying reason as
        // DeviceTexture's blockSize=0 below, just triggered by exact-equality instead of
        // exceeding the block.
        create_buffer_pool(GpuMemoryDomain::HostStaging, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VmaAllocationCreateInfo{.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE}, 0, 1);

        // DeviceTexture — images only (never buffers), so this MUST use the image-info
        // query, not the buffer-info one: VMA's own docs warn against building a pool from
        // buffer info and then allocating images into it. blockSize=0 (auto-sized) is
        // deliberate, not the fixed-size pattern above — a fixed block disables VMA's
        // dedicated-allocation fallback entirely and hard-fails on any single texture
        // larger than the block (an 8K RGBA8 texture is already 256 MB with no
        // compression/mips in the current import path).
        {
            VkImageCreateInfo rep_image_info          = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            rep_image_info.imageType                  = VK_IMAGE_TYPE_2D;
            rep_image_info.format                     = VK_FORMAT_R8G8B8A8_UNORM;
            rep_image_info.extent                     = {1, 1, 1};
            rep_image_info.mipLevels                  = 1;
            rep_image_info.arrayLayers                = 1;
            rep_image_info.samples                    = VK_SAMPLE_COUNT_1_BIT;
            rep_image_info.tiling                     = VK_IMAGE_TILING_OPTIMAL;
            rep_image_info.usage                      = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            rep_image_info.sharingMode                = VK_SHARING_MODE_EXCLUSIVE;
            rep_image_info.initialLayout              = VK_IMAGE_LAYOUT_UNDEFINED;

            VmaAllocationCreateInfo rep_alloc_info    = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
            uint32_t                memory_type_index = 0;
            if (vmaFindMemoryTypeIndexForImageInfo(Allocator, &rep_image_info, &rep_alloc_info, &memory_type_index) == VK_SUCCESS)
            {
                VmaPoolCreateInfo pool_info = {};
                pool_info.memoryTypeIndex   = memory_type_index;
                pool_info.blockSize         = 0; // auto-sized — keeps dedicated-allocation fallback
                pool_info.maxBlockCount     = 0; // unlimited
                if (vmaCreatePool(Allocator, &pool_info, &Pools[static_cast<uint8_t>(GpuMemoryDomain::DeviceTexture)]) != VK_SUCCESS)
                {
                    ZENGINE_CORE_WARN("[GPU] Failed to create DeviceTexture pool — falling back to default pool")
                    Pools[static_cast<uint8_t>(GpuMemoryDomain::DeviceTexture)] = nullptr;
                }
            }
            else
            {
                ZENGINE_CORE_WARN("[GPU] Failed to find memory type index for DeviceTexture pool — falling back to default pool")
            }
        }

        // RenderTarget intentionally has no pool — few, large images that individually
        // benefit from VMA's automatic dedicated-allocation promotion; pooling would work
        // against that.

        Ring.Initialize(Allocator, Pools[static_cast<uint8_t>(GpuMemoryDomain::HostStaging)]);
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

        for (uint8_t i = 0; i < static_cast<uint8_t>(GpuMemoryDomain::Count); ++i)
        {
            if (Pools[i] != nullptr)
            {
                vmaDestroyPool(Allocator, Pools[i]);
                Pools[i] = nullptr;
            }
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

        if (Pools[static_cast<uint8_t>(domain)] != nullptr)
        {
            allocation_create_info.pool = Pools[static_cast<uint8_t>(domain)];
        }

        BufferView buffer_view = {.DebugName = debug_name, .Domain = domain};
        VkResult   result      = vmaCreateBuffer(Allocator, &buffer_create_info, &allocation_create_info, &buffer_view.Handle, &buffer_view.Allocation, nullptr);
        // VMA signals VmaPool exhaustion via VK_ERROR_OUT_OF_DEVICE_MEMORY, not
        // VK_ERROR_OUT_OF_POOL_MEMORY (that's for VkDescriptorPool, unrelated).
        if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY && allocation_create_info.pool != VK_NULL_HANDLE)
        {
            ZENGINE_CORE_WARN("[GPU] Pool for domain {} exhausted — falling back to default pool", static_cast<int>(domain))
            allocation_create_info.pool = VK_NULL_HANDLE;
            result                      = vmaCreateBuffer(Allocator, &buffer_create_info, &allocation_create_info, &buffer_view.Handle, &buffer_view.Allocation, nullptr);
        }
        ZENGINE_VALIDATE_ASSERT(result == VK_SUCCESS, "Failed to allocate buffer");
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

        if (Pools[static_cast<uint8_t>(domain)] != nullptr)
        {
            allocation_create_info.pool = Pools[static_cast<uint8_t>(domain)];
        }

        BufferImage buffer_image = {.DebugName = debug_name, .Domain = domain};
        VkResult    result       = vmaCreateImage(Allocator, &image_info, &allocation_create_info, &buffer_image.Handle, &buffer_image.Allocation, nullptr);
        // VMA signals VmaPool exhaustion via VK_ERROR_OUT_OF_DEVICE_MEMORY, not
        // VK_ERROR_OUT_OF_POOL_MEMORY (that's for VkDescriptorPool, unrelated).
        if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY && allocation_create_info.pool != VK_NULL_HANDLE)
        {
            ZENGINE_CORE_WARN("[GPU] Pool for domain {} exhausted — falling back to default pool", static_cast<int>(domain))
            allocation_create_info.pool = VK_NULL_HANDLE;
            result                      = vmaCreateImage(Allocator, &image_info, &allocation_create_info, &buffer_image.Handle, &buffer_image.Allocation, nullptr);
        }
        ZENGINE_VALIDATE_ASSERT(result == VK_SUCCESS, "Failed to allocate image");
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

    void StagingRingBuffer::Initialize(VmaAllocator alloc, VmaPool pool)
    {
        VkBufferCreateInfo buffer_create_info          = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer_create_info.size                        = kCapacity;
        buffer_create_info.usage                       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_create_info.sharingMode                 = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocation_create_info = {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT};
        allocation_create_info.usage                   = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        allocation_create_info.pool                    = pool; // shares the GpuAllocator-owned HostStaging pool when one exists
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