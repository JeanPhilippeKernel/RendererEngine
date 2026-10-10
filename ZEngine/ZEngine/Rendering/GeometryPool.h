#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering
{
    /// @brief Contiguous byte ranges allocated from the streaming geometry pool for one mesh.
    /// @details VtxByteOffset/VtxByteSize describe a region inside GeometryPool::VertexBuffer;
    ///          IdxByteOffset/IdxByteSize describe a region inside GeometryPool::IndexBuffer.
    ///          Both axes are allocated and freed as a unit via GeometryPool::Allocate/Free.
    struct GeometryRegion
    {
        VkDeviceSize VtxByteOffset = 0;
        VkDeviceSize IdxByteOffset = 0;
        VkDeviceSize VtxByteSize   = 0;
        VkDeviceSize IdxByteSize   = 0;
    };

    /// @brief Variable-size region allocator over the global streaming vertex and index buffers.
    ///
    /// One region per mesh — allocation granularity matches the natural upload unit
    /// (AppendMeshData appends one contiguous vtx block + one contiguous idx block per mesh).
    /// Freed regions are returned to sorted intrusive free lists backed by a PoolAllocator,
    /// and merged with adjacent free ranges on free. When the append cursor is exhausted and
    /// no free range is large enough, Allocate returns false — the caller should compact.
    struct GeometryPool
    {
        Core::Memory::BufferView VertexBuffer = {}; ///< Device-local VkBuffer for vertex data.
        Core::Memory::BufferView IndexBuffer  = {}; ///< Device-local VkBuffer for index data.

        VkDeviceSize             VtxCapacity  = 0; ///< Total vertex buffer capacity in bytes.
        VkDeviceSize             IdxCapacity  = 0; ///< Total index buffer capacity in bytes.
        VkDeviceSize             VtxUsed      = 0; ///< Bytes held by live (non-freed) regions.
        VkDeviceSize             IdxUsed      = 0;
        VkDeviceSize             VtxCursor    = 0; ///< Append cursor; advanced only when the free list has no fit.
        VkDeviceSize             IdxCursor    = 0;

        /// @brief A node in the sorted intrusive free list for one buffer axis.
        struct FreeNode
        {
            VkDeviceSize Offset = 0;
            VkDeviceSize Size   = 0;
            FreeNode*    Next   = nullptr;
        };

        FreeNode*                   FreeVtxHead  = nullptr; ///< Head of the vtx free list, sorted by Offset.
        FreeNode*                   FreeIdxHead  = nullptr; ///< Head of the idx free list, sorted by Offset.
        Core::Memory::PoolAllocator FreeNodePool = {};      ///< Fixed-chunk pool backing all FreeNode allocations.

        /// @brief Initialise the pool with the given capacities.
        /// @param arena          Arena backing the PoolAllocator's node storage.
        /// @param vtx_capacity   Total vertex buffer capacity in bytes.
        /// @param idx_capacity   Total index buffer capacity in bytes.
        /// @param max_free_nodes Upper bound on simultaneous free nodes across both axes;
        ///                       size to 2 * max_mesh_slots to guarantee no overflow.
        void                        Initialize(Core::Memory::ArenaAllocator* arena, VkDeviceSize vtx_capacity, VkDeviceSize idx_capacity, uint32_t max_free_nodes);

        /// @brief Allocate a region large enough for vtx_bytes of vertex data and idx_bytes of
        ///        index data. Searches the free list first (first-fit), then the append cursor.
        /// @param vtx_bytes Byte size of the vertex data to reserve.
        /// @param idx_bytes Byte size of the index data to reserve.
        /// @param out       Filled with the allocated offsets and sizes on success.
        /// @return true on success; false if the pool is full (caller should compact).
        bool                        Allocate(VkDeviceSize vtx_bytes, VkDeviceSize idx_bytes, GeometryRegion& out);

        /// @brief Return a region to the free lists.
        /// @details Inserts in sorted order and merges adjacent free ranges on both axes.
        ///          Subtracts from VtxUsed/IdxUsed.
        /// @param region The region previously returned by Allocate.
        void                        Free(const GeometryRegion& region);

        /// @brief Ratio of hole bytes (freed but not yet compacted) to total capacity, in [0, 1].
        /// @details 0 = fully packed, 1 = everything is holes. Compaction is typically
        ///          triggered when this exceeds 0.30.
        float                       FragmentationRatio() const;

        /// @brief Reset the pool to empty: zero cursors, return all nodes to the pool,
        ///        zero Used counters. Does not touch the underlying VkBuffer allocations.
        void                        Reset();
    };

} // namespace ZEngine::Rendering
