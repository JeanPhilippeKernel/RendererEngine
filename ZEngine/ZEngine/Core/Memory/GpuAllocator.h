#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <limits>

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
        Count          = 5
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

    struct BufferView
    {
        const char*     DebugName  = nullptr;
        uint8_t         FrameIndex = std::numeric_limits<uint8_t>::max();
        BufferType      Type       = BufferType::UNKNOWN;
        GpuMemoryDomain Domain     = GpuMemoryDomain::DeviceGeometry;
        VkBuffer        Handle     = VK_NULL_HANDLE;
        VmaAllocation   Allocation = nullptr;
        // clang-format off
        operator bool() const
        {
            return (Handle != VK_NULL_HANDLE);
        }
        // clang-format on
    };

    struct BufferImage
    {
        const char*     DebugName  = nullptr;
        uint8_t         FrameIndex = std::numeric_limits<uint8_t>::max();
        GpuMemoryDomain Domain     = GpuMemoryDomain::DeviceTexture;
        VkImage         Handle     = VK_NULL_HANDLE;
        VkImageView     ViewHandle = VK_NULL_HANDLE;
        VkSampler       Sampler    = VK_NULL_HANDLE;
        VmaAllocation   Allocation = nullptr;
        // clang-format off
        operator bool() const
        {
            return (Handle != VK_NULL_HANDLE);
        }
        // clang-format on
    };

    struct StagingRingBuffer
    {
        static constexpr uint64_t kCapacity  = StagingBytes;
        static constexpr uint64_t kMaxChunks = 256;

        struct Chunk
        {
            uint32_t Offset        = 0;
            uint32_t Size          = 0;
            uint64_t TimelineValue = std::numeric_limits<uint64_t>::max(); // Timeline semaphore completion value when this chunk is safe to reuse
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

        void          Initialize(VmaAllocator alloc);
        void          Shutdown(VmaAllocator alloc);

        // Returns mapped pointer + VkBuffer byte offset. Returns nullptr when the ring is
        // full � caller falls back to a one-shot staging buffer for oversized transfers.
        // alignment has possible value as follow:
        void*         Allocate(uint32_t size, uint32_t alignment, uint32_t* out_vk_offset);

        // Record a submitted chunk so Drain() can release it.
        void          Submit(uint32_t vk_offset, uint32_t size, uint64_t timeline_value);

        // Advance ReadPos past all chunks whose TimelineValue <= completed. O(drained_count).
        void          Drain(uint64_t completed_value);
    };

    struct GpuAllocator
    {
        VmaAllocator      Allocator                                           = nullptr;
        VmaPool           Pools[static_cast<uint8_t>(GpuMemoryDomain::Count)] = {nullptr};
        StagingRingBuffer Ring                                                = {};
        VmaBudget         HeapBudgets[VK_MAX_MEMORY_HEAPS]                    = {};
        uint32_t          HeapCount                                           = 0;
        bool              HasBudgetExt                                        = false;

        void              Initialize(VkPhysicalDevice physical_device, VkDevice device, VkInstance instance, bool has_memory_budget_ext, bool has_buffer_device_address_ext);
        void              Shutdown();

        BufferView        AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, GpuMemoryDomain domain, const char* debug_name = nullptr);
        BufferImage       AllocateImage(VkImageCreateInfo& image_info, GpuMemoryDomain domain, VkDevice device, VkImageAspectFlagBits aspect, VkImageViewType view_type, uint32_t layer_count, const char* debug_name = nullptr);
        void              FreeBuffer(BufferView& buffer);
        void              FreeImage(BufferImage& image, VkDevice device = VK_NULL_HANDLE);

        void              SampleBudgets();
        float             HeapPressure(uint32_t heap_index) const;
    };

} // namespace ZEngine::Core::Memory