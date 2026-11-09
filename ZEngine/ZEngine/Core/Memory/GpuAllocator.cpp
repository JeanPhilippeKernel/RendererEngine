#define VMA_IMPLEMENTATION
#define VMA_VULKAN_VERSION           1003000 // Vulkan 1.3
#define VMA_STATIC_VULKAN_FUNCTIONS  1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0

#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Hardwares/DeferredFreeQueue.h>
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
        m_allocations_initialized             = false;
        m_free_allocation                     = UINT32_MAX;
        m_allocation_statistics               = {.Capacity = AllocationCapacity};
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

        // DeviceGeometry — vertex/index/storage buffers. blockSize=0 (auto-sized),
        // maxBlockCount=0 (unlimited): RRM's global vertex/index buffers are each exactly
        // 512 MB, and a fixed block exactly equal to an allocation's size can't actually
        // fit it (same bookkeeping-headroom issue as HostStaging below — confirmed by
        // [GPU] Pool for domain 0 exhausted firing on every single geometry allocation,
        // not just after a block-count cap was reached).
        create_buffer_pool(GpuMemoryDomain::DeviceGeometry, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VmaAllocationCreateInfo{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE}, 0, 0);

        // HostUniform — per-frame UBOs/SSBOs on the BAR window. blockSize=0, maxBlockCount=0
        // for the same reason as DeviceGeometry above: PerFrameUploadHeap::kCapacity alone
        // is exactly UniformBytes, and GraphicRenderer's Transform/RenderData/Material/Light
        // buffers plus ZUI's per-frame vertex/index buffers all share this one domain —
        // a single fixed 1-block pool was never going to fit all of that.
        create_buffer_pool(GpuMemoryDomain::HostUniform, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VmaAllocationCreateInfo{.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO}, 0, 0);

        // HostStaging — built from the staging ring's OWN flags (AUTO_PREFER_DEVICE), not
        // AllocateBuffer's generic HostStaging branch (plain AUTO): those can resolve to
        // different memory type indices on a discrete GPU with a BAR window, and the ring
        // is this domain's dominant consumer. The generic one-off staging buffers
        // (AllocateBuffer with domain=HostStaging) share this same pool.
        //
        // blockSize=0 and maxBlockCount=0 let VMA add a block for one-off staging
        // uploads after the ring's block is full. A one-block pool made every geometry
        // staging allocation fall back to VMA's default allocator on discrete GPUs.
        create_buffer_pool(GpuMemoryDomain::HostStaging, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VmaAllocationCreateInfo{.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE}, 0, 0);

        // HostReadback has the inverse host-access contract of HostStaging. It is
        // deliberately a separate pool because readback allocations can stay leased
        // for several frames while waiting for their exact submission timeline.
        create_buffer_pool(GpuMemoryDomain::HostReadback, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VmaAllocationCreateInfo{.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO}, 0, 0);

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

        Ring.Initialize(*this);
    }

    void GpuAllocator::Shutdown()
    {
        Ring.Shutdown(*this);

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
        auto buffer = TryAllocateBuffer(size, usage, domain, debug_name);
        ZENGINE_VALIDATE_ASSERT(buffer, "Failed to allocate buffer");
        return buffer;
    }

    BufferView GpuAllocator::TryAllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, GpuMemoryDomain domain, const char* debug_name, VkResult* status)
    {
        return TryAllocateBufferWithPolicy(size, usage, domain, debug_name, status, false);
    }

    BufferView GpuAllocator::TryAllocateBufferWithPolicy(VkDeviceSize size, VkBufferUsageFlags usage, GpuMemoryDomain domain, const char* debug_name, VkResult* status, bool prefer_device)
    {
        if (status)
        {
            *status = VK_ERROR_INITIALIZATION_FAILED;
        }
        if (!Allocator || !size || !usage || domain >= GpuMemoryDomain::Count)
        {
            return {};
        }
        auto retirement = RetirementQueue ? RetirementQueue->Reserve() : Hardwares::DeferredFreeQueue::Lease{};
        if (RetirementQueue && !retirement)
        {
            if (status)
            {
                *status = VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            return {};
        }
        const auto record = ReserveAllocation();
        if (record == UINT32_MAX)
        {
            if (status)
            {
                *status = VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            return {};
        }
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
            allocation_create_info.usage = prefer_device ? VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE : VMA_MEMORY_USAGE_AUTO;
            allocation_create_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        if (domain == GpuMemoryDomain::HostReadback)
        {
            allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO;
            allocation_create_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        // Every allocation may become the backing allocation of a transient graph
        // alias. This only permits aliasing; it never aliases unless requested.
        allocation_create_info.flags |= VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;

        if (Pools[static_cast<uint8_t>(domain)] != nullptr)
        {
            allocation_create_info.pool = Pools[static_cast<uint8_t>(domain)];
        }

        BufferView        buffer_view     = {.DebugName = debug_name, .Domain = domain, .Size = size, .Usage = usage, .AllocationRecord = record};
        VmaAllocationInfo allocation_info = {};
        VmaAllocatorInfo  allocator_info  = {};
        vmaGetAllocatorInfo(Allocator, &allocator_info);
        VkResult result = vkCreateBuffer(allocator_info.device, &buffer_create_info, nullptr, &buffer_view.Handle);
        if (result != VK_SUCCESS)
        {
            ReleaseAllocation(record, nullptr);
            if (status)
            {
                *status = result;
            }
            return {};
        }
        if (result == VK_SUCCESS)
        {
            VkMemoryRequirements requirements = {};
            vkGetBufferMemoryRequirements(allocator_info.device, buffer_view.Handle, &requirements);
            if (!AdmitAllocationBytes(record, requirements.size, domain))
            {
                result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
            }
        }
        // Preserve VMA's AUTO memory-type policy, then allocate the already-created
        // object with that exact type. AUTO is not legal in AllocateMemoryForBuffer.
        // Pool allocation needs no selection; fallback reruns the original policy.
        const auto allocate_memory = [&] {
            auto selected = allocation_create_info;
            if (!selected.pool)
            {
                uint32_t   memory_type = 0;
                const auto selection   = vmaFindMemoryTypeIndexForBufferInfo(Allocator, &buffer_create_info, &selected, &memory_type);
                if (selection != VK_SUCCESS)
                {
                    return selection;
                }
                selected.memoryTypeBits = uint32_t{1} << memory_type;
            }
            selected.usage = VMA_MEMORY_USAGE_UNKNOWN;
            return vmaAllocateMemoryForBuffer(Allocator, buffer_view.Handle, &selected, &buffer_view.Allocation, &allocation_info);
        };
        const bool admitted = result == VK_SUCCESS;
        if (admitted)
        {
            result = allocate_memory();
        }
        // VMA signals VmaPool exhaustion via VK_ERROR_OUT_OF_DEVICE_MEMORY, not
        // VK_ERROR_OUT_OF_POOL_MEMORY (that's for VkDescriptorPool, unrelated).
        if (admitted && result == VK_ERROR_OUT_OF_DEVICE_MEMORY && allocation_create_info.pool != VK_NULL_HANDLE)
        {
            ZENGINE_CORE_WARN("[GPU] Pool for domain {} exhausted — falling back to default pool", static_cast<int>(domain))
            allocation_create_info.pool = VK_NULL_HANDLE;
            result                      = allocate_memory();
        }
        if (result == VK_SUCCESS)
        {
            result = vmaBindBufferMemory(Allocator, buffer_view.Allocation, buffer_view.Handle);
        }
        if (status)
        {
            *status = result;
        }
        if (result != VK_SUCCESS)
        {
            vmaDestroyBuffer(Allocator, buffer_view.Handle, buffer_view.Allocation);
            ReleaseAllocation(record, nullptr);
            return {};
        }
        CommitAllocation(record, buffer_view.Allocation);
        vmaSetAllocationName(Allocator, buffer_view.Allocation, debug_name);
        buffer_view.MappedData            = allocation_info.pMappedData;
        buffer_view.RetirementReservation = retirement.Detach();
        return buffer_view;
    }

    bool GpuAllocator::WriteHostBuffer(const BufferView& buffer, const void* data, size_t size, VkDeviceSize offset, VkResult* status)
    {
        if (status)
        {
            *status = VK_ERROR_MEMORY_MAP_FAILED;
        }
        if (!Allocator || !buffer || !buffer.Allocation || !buffer.OwnsAllocation || !buffer.MappedData || !data || !size || offset > buffer.Size || size > buffer.Size - offset || offset > SIZE_MAX || size > SIZE_MAX - static_cast<size_t>(offset))
        {
            return false;
        }
        if (Helpers::secure_memcpy(static_cast<uint8_t*>(buffer.MappedData) + static_cast<size_t>(offset), size, data, size) != Helpers::MEMORY_OP_SUCCESS)
        {
            return false;
        }
        const auto result = vmaFlushAllocation(Allocator, buffer.Allocation, offset, size);
        if (status)
        {
            *status = result;
        }
        return result == VK_SUCCESS;
    }

    BufferView GpuAllocator::AllocateAliasingBuffer(const BufferView& backing, VkDeviceSize size, VkBufferUsageFlags usage, const char* debug_name)
    {
        auto retirement = RetirementQueue ? RetirementQueue->Reserve() : Hardwares::DeferredFreeQueue::Lease{};
        if (RetirementQueue && !retirement)
        {
            return {};
        }
        if (backing.Handle == VK_NULL_HANDLE || backing.Allocation == nullptr || !backing.OwnsAllocation || size == 0 || size > backing.Size || usage == 0 || !RetainAllocation(backing.AllocationRecord, backing.Allocation))
        {
            return {};
        }

        VkBufferCreateInfo buffer_create_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer_create_info.size               = size;
        buffer_create_info.usage              = usage;
        buffer_create_info.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        BufferView alias                      = {.DebugName = debug_name, .Domain = backing.Domain, .Allocation = backing.Allocation, .Size = size, .Usage = usage, .OwnsAllocation = false, .AllocationRecord = backing.AllocationRecord};
        if (vmaCreateAliasingBuffer(Allocator, backing.Allocation, &buffer_create_info, &alias.Handle) != VK_SUCCESS)
        {
            ReleaseAllocation(backing.AllocationRecord, backing.Allocation);
            return {};
        }
        alias.RetirementReservation = retirement.Detach();
        return alias;
    }

    static VkResult CreateDefaultImageView(VkDevice device, const VkImageCreateInfo& image_info, VkImage image, VkImageAspectFlagBits aspect, VkImageViewType view_type, uint32_t layer_count, VkImageView* output)
    {
        VkImageViewCreateInfo view_create_info           = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_create_info.image                           = image;
        view_create_info.viewType                        = view_type;
        view_create_info.format                          = image_info.format;
        view_create_info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.subresourceRange.aspectMask     = aspect;
        view_create_info.subresourceRange.baseMipLevel   = 0;
        view_create_info.subresourceRange.levelCount     = image_info.mipLevels;
        view_create_info.subresourceRange.baseArrayLayer = 0;
        view_create_info.subresourceRange.layerCount     = layer_count;
        return vkCreateImageView(device, &view_create_info, nullptr, output);
    }

    BufferImage GpuAllocator::AllocateImage(VkImageCreateInfo& image_info, GpuMemoryDomain domain, VkDevice device, VkImageAspectFlagBits aspect, VkImageViewType view_type, uint32_t layer_count, const char* debug_name)
    {
        auto image = TryAllocateImage(image_info, domain, device, aspect, view_type, layer_count, debug_name);
        ZENGINE_VALIDATE_ASSERT(image, "Failed to allocate image or image view")
        return image;
    }

    uint32_t GpuAllocator::ReserveAllocation()
    {
        std::lock_guard lock(m_allocation_mutex);
        if (!m_allocations_initialized)
        {
            for (uint32_t slot = 0; slot < AllocationCapacity; ++slot)
            {
                m_allocations[slot] = {.NextFree = slot + 1 < AllocationCapacity ? slot + 1 : UINT32_MAX};
            }
            m_free_allocation         = 0;
            m_allocations_initialized = true;
        }
        const auto index = m_free_allocation;
        if (index != UINT32_MAX)
        {
            m_free_allocation    = m_allocations[index].NextFree;
            m_allocations[index] = {.References = 1};
            ++m_allocation_statistics.ReservedRecords;
        }
        return index;
    }

    void GpuAllocator::CommitAllocation(uint32_t index, VmaAllocation allocation)
    {
        const auto      bytes = GetAllocationSize(allocation);
        std::lock_guard lock(m_allocation_mutex);
        ZENGINE_VALIDATE_ASSERT(index < AllocationCapacity && allocation, "Invalid GPU allocation commit")
        auto& record = m_allocations[index];
        ZENGINE_VALIDATE_ASSERT(record.References == 1 && !record.Allocation, "GPU allocation already committed")
        record.Allocation = allocation;
        ZENGINE_VALIDATE_ASSERT(bytes <= record.Bytes, "GPU allocation exceeds admitted native requirements")
        record.LiveBytes = bytes;
        ++m_allocation_statistics.AllocationCount;
        m_allocation_statistics.AllocationBytes += bytes;
        if (m_allocation_statistics.AllocationCount > m_allocation_statistics.PeakAllocationCount)
        {
            m_allocation_statistics.PeakAllocationCount = m_allocation_statistics.AllocationCount;
        }
        if (m_allocation_statistics.AllocationBytes > m_allocation_statistics.PeakAllocationBytes)
        {
            m_allocation_statistics.PeakAllocationBytes = m_allocation_statistics.AllocationBytes;
        }
    }

    bool GpuAllocator::RetainAllocation(uint32_t index, VmaAllocation allocation)
    {
        if (index >= AllocationCapacity || !allocation)
        {
            return false;
        }
        std::lock_guard lock(m_allocation_mutex);
        auto&           record = m_allocations[index];
        if (!record.References || record.Allocation != allocation || record.References == UINT32_MAX)
        {
            return false;
        }
        ++record.References;
        return true;
    }

    GpuAllocationStatistics GpuAllocator::GetAllocationStatistics() const
    {
        std::lock_guard lock(m_allocation_mutex);
        return m_allocation_statistics;
    }

    GpuAllocationLimits GpuAllocator::GetAllocationLimits() const
    {
        std::lock_guard lock(m_allocation_mutex);
        return m_allocation_limits;
    }

    bool GpuAllocator::SetAllocationLimits(const GpuAllocationLimits& limits)
    {
        std::lock_guard lock(m_allocation_mutex);
        if (limits.TotalBytes < m_allocation_statistics.AdmittedBytes)
        {
            return false;
        }
        for (uint32_t domain = 0; domain < static_cast<uint32_t>(GpuMemoryDomain::Count); ++domain)
        {
            if (limits.DomainBytes[domain] < m_allocation_statistics.DomainAdmittedBytes[domain])
            {
                return false;
            }
        }
        m_allocation_limits = limits;
        return true;
    }

    bool GpuAllocator::AdmitAllocationBytes(uint32_t index, VkDeviceSize bytes, GpuMemoryDomain domain)
    {
        std::lock_guard lock(m_allocation_mutex);
        ZENGINE_VALIDATE_ASSERT(index < AllocationCapacity && domain < GpuMemoryDomain::Count, "Invalid GPU memory admission")
        auto& record = m_allocations[index];
        ZENGINE_VALIDATE_ASSERT(record.References == 1 && !record.Allocation && !record.Bytes, "GPU allocation already charged")
        const auto domain_index = static_cast<uint32_t>(domain);
        auto&      domain_bytes = m_allocation_statistics.DomainAdmittedBytes[domain_index];
        if (!bytes || bytes > m_allocation_limits.TotalBytes - m_allocation_statistics.AdmittedBytes || bytes > m_allocation_limits.DomainBytes[domain_index] - domain_bytes)
        {
            return false;
        }
        record.Bytes                           = bytes;
        record.Domain                          = domain;
        m_allocation_statistics.AdmittedBytes += bytes;
        domain_bytes                          += bytes;
        if (m_allocation_statistics.AdmittedBytes > m_allocation_statistics.PeakAdmittedBytes)
        {
            m_allocation_statistics.PeakAdmittedBytes = m_allocation_statistics.AdmittedBytes;
        }
        return true;
    }

    void GpuAllocator::ReleaseAllocation(uint32_t index, VmaAllocation allocation)
    {
        std::lock_guard lock(m_allocation_mutex);
        ZENGINE_VALIDATE_ASSERT(index < AllocationCapacity, "Invalid shared GPU allocation record")
        auto& record = m_allocations[index];
        ZENGINE_VALIDATE_ASSERT(record.References && record.Allocation == allocation, "Invalid shared GPU allocation release")
        if (--record.References == 0)
        {
            if (allocation)
            {
                vmaFreeMemory(Allocator, allocation);
                --m_allocation_statistics.AllocationCount;
                m_allocation_statistics.AllocationBytes -= record.LiveBytes;
            }
            m_allocation_statistics.AdmittedBytes -= record.Bytes;
            if (record.Bytes)
            {
                m_allocation_statistics.DomainAdmittedBytes[static_cast<uint32_t>(record.Domain)] -= record.Bytes;
            }
            --m_allocation_statistics.ReservedRecords;
            record            = {.NextFree = m_free_allocation};
            m_free_allocation = index;
        }
    }

    BufferImage GpuAllocator::TryAllocateImage(VkImageCreateInfo& image_info, GpuMemoryDomain domain, VkDevice device, VkImageAspectFlagBits aspect, VkImageViewType view_type, uint32_t layer_count, const char* debug_name, VkResult* status)
    {
        if (status)
        {
            *status = VK_ERROR_INITIALIZATION_FAILED;
        }
        if (!Allocator || !device || domain >= GpuMemoryDomain::Count || !image_info.extent.width || !image_info.extent.height || !image_info.extent.depth || !image_info.mipLevels || !image_info.arrayLayers || !layer_count)
        {
            return {};
        }
        auto retirement = RetirementQueue ? RetirementQueue->Reserve() : Hardwares::DeferredFreeQueue::Lease{};
        if (RetirementQueue && !retirement)
        {
            if (status)
            {
                *status = VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            return {};
        }
        const auto record = ReserveAllocation();
        if (record == UINT32_MAX)
        {
            if (status)
            {
                *status = VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            return {};
        }
        VmaAllocationCreateInfo allocation_create_info = {.flags = VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT};
        if (domain == GpuMemoryDomain::DeviceGeometry || domain == GpuMemoryDomain::DeviceTexture || domain == GpuMemoryDomain::RenderTarget || domain == GpuMemoryDomain::HostUniform || domain == GpuMemoryDomain::HostStaging)
        {
            allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        }

        if (Pools[static_cast<uint8_t>(domain)] != nullptr)
        {
            allocation_create_info.pool = Pools[static_cast<uint8_t>(domain)];
        }

        BufferImage buffer_image = {.DebugName = debug_name, .Domain = domain, .AllocationRecord = record};
        VkResult    result       = vkCreateImage(device, &image_info, nullptr, &buffer_image.Handle);
        if (result != VK_SUCCESS)
        {
            ReleaseAllocation(record, nullptr);
            if (status)
            {
                *status = result;
            }
            return {};
        }
        if (result == VK_SUCCESS)
        {
            VkMemoryRequirements requirements = {};
            vkGetImageMemoryRequirements(device, buffer_image.Handle, &requirements);
            if (!AdmitAllocationBytes(record, requirements.size, domain))
            {
                result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
            }
        }
        const auto allocate_memory = [&] {
            auto selected = allocation_create_info;
            if (!selected.pool)
            {
                uint32_t   memory_type = 0;
                const auto selection   = vmaFindMemoryTypeIndexForImageInfo(Allocator, &image_info, &selected, &memory_type);
                if (selection != VK_SUCCESS)
                {
                    return selection;
                }
                selected.memoryTypeBits = uint32_t{1} << memory_type;
            }
            selected.usage = VMA_MEMORY_USAGE_UNKNOWN;
            return vmaAllocateMemoryForImage(Allocator, buffer_image.Handle, &selected, &buffer_image.Allocation, nullptr);
        };
        const bool admitted = result == VK_SUCCESS;
        if (admitted)
        {
            result = allocate_memory();
        }
        // VMA signals VmaPool exhaustion via VK_ERROR_OUT_OF_DEVICE_MEMORY, not
        // VK_ERROR_OUT_OF_POOL_MEMORY (that's for VkDescriptorPool, unrelated).
        if (admitted && result == VK_ERROR_OUT_OF_DEVICE_MEMORY && allocation_create_info.pool != VK_NULL_HANDLE)
        {
            ZENGINE_CORE_WARN("[GPU] Pool for domain {} exhausted — falling back to default pool", static_cast<int>(domain))
            allocation_create_info.pool = VK_NULL_HANDLE;
            result                      = allocate_memory();
        }
        if (result == VK_SUCCESS)
        {
            result = vmaBindImageMemory(Allocator, buffer_image.Allocation, buffer_image.Handle);
        }
        if (result != VK_SUCCESS)
        {
            vmaDestroyImage(Allocator, buffer_image.Handle, buffer_image.Allocation);
            ReleaseAllocation(record, nullptr);
            if (status)
            {
                *status = result;
            }
            return {};
        }
        CommitAllocation(record, buffer_image.Allocation);
        vmaSetAllocationName(Allocator, buffer_image.Allocation, debug_name);

        result = CreateDefaultImageView(device, image_info, buffer_image.Handle, aspect, view_type, layer_count, &buffer_image.ViewHandle);
        if (status)
        {
            *status = result;
        }
        if (result != VK_SUCCESS)
        {
            vmaDestroyImage(Allocator, buffer_image.Handle, nullptr);
            ReleaseAllocation(record, buffer_image.Allocation);
            return {};
        }

        buffer_image.RetirementReservation = retirement.Detach();
        return buffer_image;
    }

    BufferImage GpuAllocator::AllocateAliasingImage(const BufferImage& backing, const VkImageCreateInfo& image_info, VkDevice device, VkImageAspectFlagBits aspect, VkImageViewType view_type, uint32_t layer_count, const char* debug_name, VkResult* status)
    {
        if (status)
        {
            *status = VK_ERROR_INITIALIZATION_FAILED;
        }
        auto retirement = RetirementQueue ? RetirementQueue->Reserve() : Hardwares::DeferredFreeQueue::Lease{};
        if (RetirementQueue && !retirement)
        {
            if (status)
            {
                *status = VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            return {};
        }
        if (backing.Handle == VK_NULL_HANDLE || backing.Allocation == nullptr || !backing.OwnsAllocation || !RetainAllocation(backing.AllocationRecord, backing.Allocation))
        {
            return {};
        }

        BufferImage alias  = {.DebugName = debug_name, .Domain = backing.Domain, .Allocation = backing.Allocation, .OwnsAllocation = false, .AllocationRecord = backing.AllocationRecord};
        VkResult    result = vmaCreateAliasingImage(Allocator, backing.Allocation, &image_info, &alias.Handle);
        if (status)
        {
            *status = result;
        }
        if (result != VK_SUCCESS)
        {
            ReleaseAllocation(backing.AllocationRecord, backing.Allocation);
            return {};
        }
        result = CreateDefaultImageView(device, image_info, alias.Handle, aspect, view_type, layer_count, &alias.ViewHandle);
        if (status)
        {
            *status = result;
        }
        if (result != VK_SUCCESS)
        {
            vmaDestroyImage(Allocator, alias.Handle, nullptr);
            ReleaseAllocation(backing.AllocationRecord, backing.Allocation);
            return {};
        }
        alias.RetirementReservation = retirement.Detach();
        return alias;
    }

    void GpuAllocator::FreeBuffer(BufferView& buffer)
    {
        if (!buffer)
        {
            return;
        }
        vmaDestroyBuffer(Allocator, buffer.Handle, nullptr);
        ReleaseAllocation(buffer.AllocationRecord, buffer.Allocation);
        if (RetirementQueue)
        {
            RetirementQueue->CancelReservation(buffer.RetirementReservation);
        }
        buffer.Handle                = VK_NULL_HANDLE;
        buffer.Allocation            = nullptr;
        buffer.Size                  = 0;
        buffer.Usage                 = 0;
        buffer.MappedData            = nullptr;
        buffer.OwnsAllocation        = true;
        buffer.AllocationRecord      = UINT32_MAX;
        buffer.RetirementReservation = 0;
    }

    void GpuAllocator::FreeImage(BufferImage& image, VkDevice device)
    {
        if (!image)
        {
            return;
        }
        if (image.ViewHandle != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, image.ViewHandle, nullptr);
            image.ViewHandle = VK_NULL_HANDLE;
        }
        vmaDestroyImage(Allocator, image.Handle, nullptr);
        ReleaseAllocation(image.AllocationRecord, image.Allocation);
        if (RetirementQueue)
        {
            RetirementQueue->CancelReservation(image.RetirementReservation);
        }
        image.Handle                = VK_NULL_HANDLE;
        image.Allocation            = nullptr;
        image.OwnsAllocation        = true;
        image.AllocationRecord      = UINT32_MAX;
        image.RetirementReservation = 0;
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
            const float pressure = HeapBudgets[i].budget == 0 ? 0.0f : static_cast<float>(HeapBudgets[i].usage) / static_cast<float>(HeapBudgets[i].budget);
            if (pressure > WarnPressure)
            {
                if (!HeapPressureReported[i])
                {
                    ZENGINE_LOG_ENGINE_WARN("[GPU] Heap {} at {:.0f}% ({} / {} MB)", i, pressure * 100.0f, HeapBudgets[i].usage >> 20, HeapBudgets[i].budget >> 20);
                    HeapPressureReported[i] = true;
                }
                continue;
            }

            HeapPressureReported[i] = false;
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

    GpuMemoryStatistics GpuAllocator::GetMemoryStatistics() const
    {
        GpuMemoryStatistics result = {
            .HeapCount                 = HeapCount,
            .UsesDriverBudgetTelemetry = HasBudgetExt,
        };

        const auto add_saturating = [](uint64_t* total, uint64_t value) {
            if (value > std::numeric_limits<uint64_t>::max() - *total)
                *total = std::numeric_limits<uint64_t>::max();
            else
                *total += value;
        };

        for (uint32_t heap_index = 0; heap_index < HeapCount; ++heap_index)
        {
            const VmaStatistics& heap = HeapBudgets[heap_index].statistics;
            add_saturating(&result.AllocationBytes, static_cast<uint64_t>(heap.allocationBytes));
            add_saturating(&result.BlockBytes, static_cast<uint64_t>(heap.blockBytes));
            add_saturating(&result.HeapUsageBytes, static_cast<uint64_t>(HeapBudgets[heap_index].usage));
            add_saturating(&result.HeapBudgetBytes, static_cast<uint64_t>(HeapBudgets[heap_index].budget));
        }
        return result;
    }

    VkDeviceSize GpuAllocator::GetAllocationSize(VmaAllocation allocation) const
    {
        if (Allocator == nullptr || allocation == nullptr)
            return 0;

        VmaAllocationInfo info = {};
        vmaGetAllocationInfo(Allocator, allocation, &info);
        return info.size;
    }

    bool GpuAllocator::InvalidateAllocation(VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size) const
    {
        if (!Allocator || !allocation)
            return false;
        return vmaInvalidateAllocation(Allocator, allocation, offset, size) == VK_SUCCESS;
    }

    void StagingRingBuffer::Initialize(GpuAllocator& allocator)
    {
        // Retain the ring's device-preferred policy on discrete GPUs while using
        // the same pre-allocation admission and final-release accounting as uploads.
        Backing = allocator.TryAllocateBufferWithPolicy(kCapacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, GpuMemoryDomain::HostStaging, DebugName, nullptr, true);
        ZENGINE_VALIDATE_ASSERT(Backing, "Failed to allocate staging ring")
        Buffer     = Backing.Handle;
        Allocation = Backing.Allocation;
        MappedPtr  = Backing.MappedData;
    }

    void StagingRingBuffer::Shutdown(GpuAllocator& allocator)
    {
        allocator.FreeBuffer(Backing);
        Buffer     = VK_NULL_HANDLE;
        Allocation = nullptr;
        MappedPtr  = nullptr;
        WritePos   = 0;
        ReadPos    = 0;
        ChunkHead  = 0;
        ChunkTail  = 0;
        ChunkCount = 0;
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
            // <= (not <): a perfect-fit allocation landing exactly at kCapacity is valid —
            // no need to force an unnecessary wrap attempt.
            if ((offset + size) <= static_cast<uint32_t>(kCapacity))
            {
                if (!ReserveChunk(offset, size))
                    return nullptr;
                *out_vk_offset = offset;
                WritePos       = offset + size;
                return reinterpret_cast<uint8_t*>(MappedPtr) + offset;
            }

            // this causes a buffer wrapping
            if (size < ReadPos)
            {
                if (!ReserveChunk(0, size))
                    return nullptr;
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
                if (!ReserveChunk(offset, size))
                    return nullptr;
                *out_vk_offset = offset;
                WritePos       = offset + size;
                return reinterpret_cast<uint8_t*>(MappedPtr) + offset;
            }
            return nullptr;
        }
    }

    bool StagingRingBuffer::ReserveChunk(uint32_t vk_offset, uint32_t size)
    {
        if (ChunkCount == kMaxChunks)
            return false;

        Chunks[ChunkTail] = {vk_offset, size, kPendingTimelineValue};
        ChunkTail         = (ChunkTail + 1) % kMaxChunks;
        ++ChunkCount;
        return true;
    }

    void StagingRingBuffer::Submit(uint32_t vk_offset, uint32_t size, uint64_t timeline_value)
    {
        ZENGINE_VALIDATE_ASSERT(timeline_value != kPendingTimelineValue, "StagingRingBuffer::Submit: pending sentinel is not a valid completion value")
        for (uint32_t offset = 0; offset < ChunkCount; ++offset)
        {
            Chunk& chunk = Chunks[(ChunkHead + offset) % kMaxChunks];
            if (chunk.Offset == vk_offset && chunk.Size == size && chunk.TimelineValue == kPendingTimelineValue)
            {
                chunk.TimelineValue = timeline_value;
                return;
            }
        }

        ZENGINE_VALIDATE_ASSERT(false, "StagingRingBuffer::Submit: no matching outstanding reservation")
    }

    void StagingRingBuffer::Drain(uint64_t completed_value)
    {
        while (ChunkCount > 0 && Chunks[ChunkHead].TimelineValue != kPendingTimelineValue && Chunks[ChunkHead].TimelineValue <= completed_value)
        {
            ReadPos           = Chunks[ChunkHead].Offset + Chunks[ChunkHead].Size;
            Chunks[ChunkHead] = {};
            ChunkHead         = (ChunkHead + 1) % kMaxChunks;
            --ChunkCount;
        }
    }

} // namespace ZEngine::Core::Memory
