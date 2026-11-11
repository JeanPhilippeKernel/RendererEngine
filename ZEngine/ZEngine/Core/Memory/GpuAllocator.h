#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <limits>
#include <mutex>

struct GpuAllocatorTestAccess;

namespace ZEngine::Hardwares
{
    struct DeferredFreeQueue;
}

namespace ZEngine::Core::Memory
{
    constexpr uint64_t GeometryBytes     = 512ULL << 20; // 512 MB vertex/index/storage
    constexpr uint64_t TextureBytes      = 512ULL << 20; // 512 MB BC7/BC5 atlas
    constexpr uint64_t RenderTargetBytes = 172ULL << 20; // 172 MB shadow maps + post-process + thumbnails
    constexpr uint64_t UniformBytes      = 64ULL << 20;  // per-frame UBOs + bone matrices
    constexpr uint64_t StagingBytes      = 64ULL << 20;  // staging ring capacity
    constexpr float    WarnPressure      = 0.90f;

    enum class GpuMemoryDomain : uint8_t
    {
        DeviceGeometry = 0, // DEVICE_LOCAL - Geometry buffers, vertex/index buffers, etc.
        DeviceTexture  = 1, // DEVICE_LOCAL - Texture buffers, image buffers, etc.
        RenderTarget   = 2, // DEVICE_LOCAL - RT, depth, shadow maps
        HostUniform    = 3, // BAR window - uniforms, frenquently updated data
        HostStaging    = 4, // HOST_VISIBLE | HOST_COHERENT - Staging buffers for uploading data to the GPU
        // HOST_VISIBLE read-mostly allocations used by deferred GPU-to-CPU readback.
        // This is intentionally distinct from HostStaging: VMA's host-access hint is
        // directional and readback memory is consumed by the CPU after GPU writes it.
        HostReadback   = 5,
        Count          = 6
    };

    enum BufferType : uint8_t
    {
        UNKNOWN  = 0,
        VERTEX   = 1,
        INDEX    = 2,
        UNIFORM  = 3,
        STORAGE  = 4,
        INDIRECT = 5
    };

    // Snapshot of the allocator's current GPU-memory accounting. VMA allocation
    // and block bytes describe engine allocations; heap usage/budget comes from
    // VK_EXT_memory_budget when available and otherwise is a VMA-derived fallback.
    // Neither quantity is part of a CPU arena capacity.
    struct GpuMemoryStatistics
    {
        uint64_t AllocationBytes           = 0;
        uint64_t BlockBytes                = 0;
        uint64_t HeapUsageBytes            = 0;
        uint64_t HeapBudgetBytes           = 0;
        uint32_t HeapCount                 = 0;
        bool     UsesDriverBudgetTelemetry = false;
    };

    struct BufferView
    {
        const char*        DebugName             = nullptr;
        uint8_t            FrameIndex            = std::numeric_limits<uint8_t>::max();
        BufferType         Type                  = BufferType::UNKNOWN;
        GpuMemoryDomain    Domain                = GpuMemoryDomain::DeviceGeometry;
        VkBuffer           Handle                = VK_NULL_HANDLE;
        VmaAllocation      Allocation            = nullptr;
        VkDeviceSize       Size                  = 0;
        VkBufferUsageFlags Usage                 = 0;
        void*              MappedData            = nullptr;
        bool               OwnsAllocation        = true;
        // Each native buffer, including aliases, retains its backing allocation.
        uint32_t           AllocationRecord      = UINT32_MAX;
        uint64_t           RetirementReservation = 0;
        // clang-format off
        operator bool() const
        {
            return (Handle != VK_NULL_HANDLE);
        }
        // clang-format on
    };

    struct BufferImage
    {
        const char*     DebugName             = nullptr;
        uint8_t         FrameIndex            = std::numeric_limits<uint8_t>::max();
        GpuMemoryDomain Domain                = GpuMemoryDomain::DeviceTexture;
        VkImage         Handle                = VK_NULL_HANDLE;
        VkImageView     ViewHandle            = VK_NULL_HANDLE;
        VkSampler       Sampler               = VK_NULL_HANDLE;
        VmaAllocation   Allocation            = nullptr;
        bool            OwnsAllocation        = true;
        // Every Vulkan image sharing this allocation holds one record reference.
        // The original image can retire before an alias without freeing its memory.
        uint32_t        AllocationRecord      = UINT32_MAX;
        uint64_t        RetirementReservation = 0;
        // clang-format off
        operator bool() const
        {
            return (Handle != VK_NULL_HANDLE);
        }
        // clang-format on
    };

    struct GpuAllocator;

    struct StagingRingBuffer
    {
        static constexpr uint64_t kCapacity             = StagingBytes;
        static constexpr uint64_t kMaxChunks            = 256;
        static constexpr uint64_t kPendingTimelineValue = std::numeric_limits<uint64_t>::max();

        struct Chunk
        {
            uint32_t Offset        = 0;
            uint32_t Size          = 0;
            uint64_t TimelineValue = kPendingTimelineValue; // Timeline semaphore completion value when this chunk is safe to reuse
        };

        const char*   DebugName          = "ZStagingBuffer";
        VmaAllocation Allocation         = nullptr;
        VkBuffer      Buffer             = VK_NULL_HANDLE;
        void*         MappedPtr          = nullptr;
        uint32_t      WritePos           = 0;
        uint32_t      ReadPos            = 0;
        Chunk         Chunks[kMaxChunks] = {};
        uint32_t      ChunkHead          = 0;
        uint32_t      ChunkTail          = 0;
        uint32_t      ChunkCount         = 0;

        void          Initialize(GpuAllocator& allocator);
        void          Shutdown(GpuAllocator& allocator);

        // Returns mapped pointer + VkBuffer byte offset. Returns nullptr when the ring is
        // full or all retirement records are in flight; callers then fall back to a
        // one-shot staging buffer.
        // alignment has possible value as follow:
        void*         Allocate(uint32_t size, uint32_t alignment, uint32_t* out_vk_offset);

        // Assign the completion timeline to the reservation returned by Allocate().
        void          Submit(uint32_t vk_offset, uint32_t size, uint64_t timeline_value);

        // Advance ReadPos past all chunks whose TimelineValue <= completed. O(drained_count).
        void          Drain(uint64_t completed_value);

    private:
        BufferView Backing = {};
        bool       ReserveChunk(uint32_t vk_offset, uint32_t size);
    };

    // Finite live-residency admission, including pending allocations and retirement.
    // Defaults are safety ceilings, not a calibrated supported-scene-size claim.
    struct GpuAllocationLimits
    {
        uint64_t TotalBytes                                                = 4ULL << 30;
        uint64_t DomainBytes[static_cast<uint8_t>(GpuMemoryDomain::Count)] = {4ULL << 30, 4ULL << 30, 4ULL << 30, 4ULL << 30, 512ULL << 20, 256ULL << 20};
    };

    // Synchronized live ledger, not sampled driver telemetry. Aliases share one
    // charge, which remains live until the last native object is destroyed.
    // Includes the staging ring and one-shot staging allocations.
    struct GpuAllocationStatistics
    {
        uint32_t Capacity                                                          = 0;
        uint32_t ReservedRecords                                                   = 0;
        uint32_t AllocationCount                                                   = 0;
        uint32_t PeakAllocationCount                                               = 0;
        uint64_t AllocationBytes                                                   = 0;
        uint64_t PeakAllocationBytes                                               = 0;
        uint64_t AdmittedBytes                                                     = 0;
        uint64_t PeakAdmittedBytes                                                 = 0;
        uint64_t DomainAdmittedBytes[static_cast<uint8_t>(GpuMemoryDomain::Count)] = {};
    };

    struct GpuAllocator
    {
        GpuAllocator() = default;
        explicit GpuAllocator(Hardwares::DeferredFreeQueue* retirement_queue) : RetirementQueue(retirement_queue) {}
        friend struct ::GpuAllocatorTestAccess;
        friend struct StagingRingBuffer;
        Hardwares::DeferredFreeQueue*         RetirementQueue                                     = nullptr;
        VmaAllocator                          Allocator                                           = nullptr;
        VmaPool                               Pools[static_cast<uint8_t>(GpuMemoryDomain::Count)] = {nullptr};
        StagingRingBuffer                     Ring                                                = {};
        VmaBudget                             HeapBudgets[VK_MAX_MEMORY_HEAPS]                    = {};
        bool                                  HeapPressureReported[VK_MAX_MEMORY_HEAPS]           = {};
        uint32_t                              HeapCount                                           = 0;
        bool                                  HasBudgetExt                                        = false;

        void                                  Initialize(VkPhysicalDevice physical_device, VkDevice device, VkInstance instance, bool has_memory_budget_ext, bool has_buffer_device_address_ext);
        void                                  Shutdown();

        BufferView                            AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, GpuMemoryDomain domain, const char* debug_name = nullptr);
        /// Recoverable allocation for unpublished work; never asserts on refusal.
        BufferView                            TryAllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, GpuMemoryDomain domain, const char* debug_name = nullptr, VkResult* status = nullptr);
        /// Caller owns an unpublished/idle mapped allocation. No staging or GPU wait.
        bool                                  WriteHostBuffer(const BufferView& buffer, const void* data, size_t size, VkDeviceSize offset = 0, VkResult* status = nullptr);
        /// @brief Creates a distinct buffer object backed by an existing allocation.
        BufferView                            AllocateAliasingBuffer(const BufferView& backing, VkDeviceSize size, VkBufferUsageFlags usage, const char* debug_name = nullptr);
        BufferImage                           AllocateImage(VkImageCreateInfo& image_info, GpuMemoryDomain domain, VkDevice device, VkImageAspectFlagBits aspect, VkImageViewType view_type, uint32_t layer_count, const char* debug_name = nullptr);
        BufferImage                           TryAllocateImage(VkImageCreateInfo& image_info, GpuMemoryDomain domain, VkDevice device, VkImageAspectFlagBits aspect, VkImageViewType view_type, uint32_t layer_count, const char* debug_name = nullptr, VkResult* status = nullptr);
        /// @brief Creates a distinct image object backed by an existing allocation.
        BufferImage                           AllocateAliasingImage(const BufferImage& backing, const VkImageCreateInfo& image_info, VkDevice device, VkImageAspectFlagBits aspect, VkImageViewType view_type, uint32_t layer_count, const char* debug_name = nullptr, VkResult* status = nullptr);
        void                                  FreeBuffer(BufferView& buffer);
        void                                  FreeImage(BufferImage& image, VkDevice device = VK_NULL_HANDLE);

        void                                  SampleBudgets();
        float                                 HeapPressure(uint32_t heap_index) const;
        [[nodiscard]] GpuMemoryStatistics     GetMemoryStatistics() const;
        [[nodiscard]] GpuAllocationStatistics GetAllocationStatistics() const;
        [[nodiscard]] GpuAllocationLimits     GetAllocationLimits() const;
        /// Refuses limits below already admitted work. Does not evict live owners.
        [[nodiscard]] bool                    SetAllocationLimits(const GpuAllocationLimits& limits);
        /// @brief Returns the byte range reserved for one owning VMA allocation.
        VkDeviceSize                          GetAllocationSize(VmaAllocation allocation) const;
        /// @brief Makes GPU writes visible to the CPU for a host-visible VMA allocation.
        /// @return True when the allocation was invalidated successfully.
        bool                                  InvalidateAllocation(VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size) const;

    private:
        // Includes buffers, textures, and deferred versions. Admission is
        // finite and does not fall back to an unbounded allocation ledger.
        static constexpr uint32_t AllocationCapacity = 16384;
        struct AllocationRecord
        {
            VmaAllocation Allocation = nullptr;
            VkDeviceSize  Bytes      = 0;
            VkDeviceSize  LiveBytes  = 0;
            uint32_t      References = 0;
            // Free records need a next index; charged records need a domain.
            union
            {
                uint32_t        NextFree = UINT32_MAX;
                GpuMemoryDomain Domain;
            };
        };
        AllocationRecord        m_allocations[AllocationCapacity] = {};
        uint32_t                m_free_allocation                 = UINT32_MAX;
        bool                    m_allocations_initialized         = false;
        mutable std::mutex      m_allocation_mutex;
        GpuAllocationStatistics m_allocation_statistics = {.Capacity = AllocationCapacity};
        GpuAllocationLimits     m_allocation_limits     = {};
        uint32_t                ReserveAllocation();
        bool                    AdmitAllocationBytes(uint32_t record, VkDeviceSize bytes, GpuMemoryDomain domain);
        BufferView              TryAllocateBufferWithPolicy(VkDeviceSize size, VkBufferUsageFlags usage, GpuMemoryDomain domain, const char* debug_name, VkResult* status, bool prefer_device);
        void                    CommitAllocation(uint32_t record, VmaAllocation allocation);
        bool                    RetainAllocation(uint32_t record, VmaAllocation allocation);
        void                    ReleaseAllocation(uint32_t record, VmaAllocation allocation);
    };

} // namespace ZEngine::Core::Memory
