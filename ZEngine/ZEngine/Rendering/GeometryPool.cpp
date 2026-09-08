#include <ZEngine/Rendering/GeometryPool.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::Rendering
{
    void GeometryPool::Initialize(Core::Memory::ArenaAllocator* arena, VkDeviceSize vtx_capacity, VkDeviceSize idx_capacity, uint32_t max_free_nodes)
    {
        ZENGINE_VALIDATE_ASSERT(arena != nullptr, "GeometryPool::Initialize: arena must not be null")
        ZENGINE_VALIDATE_ASSERT(vtx_capacity > 0, "GeometryPool::Initialize: vtx_capacity must be > 0")
        ZENGINE_VALIDATE_ASSERT(idx_capacity > 0, "GeometryPool::Initialize: idx_capacity must be > 0")
        ZENGINE_VALIDATE_ASSERT(max_free_nodes > 0, "GeometryPool::Initialize: max_free_nodes must be > 0")

        VtxCapacity = vtx_capacity;
        IdxCapacity = idx_capacity;
        VtxUsed     = 0;
        IdxUsed     = 0;
        VtxCursor   = 0;
        IdxCursor   = 0;
        FreeVtxHead = nullptr;
        FreeIdxHead = nullptr;

        FreeNodePool.Initialize(arena, static_cast<size_t>(max_free_nodes) * sizeof(FreeNode), sizeof(FreeNode), alignof(FreeNode));
    }

    // Search the sorted free list for the first node >= need (first-fit).
    // On success removes the node (returning it to the pool) and, if it was larger,
    // allocates a new node for the remainder and inserts it in its sorted position.
    static bool TryAllocFromList(GeometryPool::FreeNode*& head, Core::Memory::PoolAllocator& pool, VkDeviceSize need, VkDeviceSize& out_offset)
    {
        GeometryPool::FreeNode* prev = nullptr;
        GeometryPool::FreeNode* cur  = head;
        while (cur)
        {
            if (cur->Size >= need)
            {
                out_offset                         = cur->Offset;

                VkDeviceSize            rem_offset = cur->Offset + need;
                VkDeviceSize            rem_size   = cur->Size - need;

                // Unlink and return the matched node.
                GeometryPool::FreeNode* next       = cur->Next;
                if (prev)
                    prev->Next = next;
                else
                    head = next;
                pool.Free(cur);

                if (rem_size > 0)
                {
                    // Remainder starts after the allocated block — insert it where the removed
                    // node was (sorted order preserved: rem_offset > cur->Offset > any prev).
                    auto* rem   = static_cast<GeometryPool::FreeNode*>(pool.Allocate());
                    rem->Offset = rem_offset;
                    rem->Size   = rem_size;
                    rem->Next   = next;
                    if (prev)
                        prev->Next = rem;
                    else
                        head = rem;
                }
                return true;
            }
            prev = cur;
            cur  = cur->Next;
        }
        return false;
    }

    // Insert a freed range into the sorted free list and merge with adjacent neighbours.
    static void InsertAndMerge(GeometryPool::FreeNode*& head, Core::Memory::PoolAllocator& pool, VkDeviceSize offset, VkDeviceSize size)
    {
        auto* node                   = static_cast<GeometryPool::FreeNode*>(pool.Allocate());
        node->Offset                 = offset;
        node->Size                   = size;
        node->Next                   = nullptr;

        // Find insertion position (sorted by Offset).
        GeometryPool::FreeNode* prev = nullptr;
        GeometryPool::FreeNode* cur  = head;
        while (cur && cur->Offset < offset)
        {
            prev = cur;
            cur  = cur->Next;
        }

        node->Next = cur;
        if (prev)
            prev->Next = node;
        else
            head = node;

        // Merge with the next node if adjacent.
        if (node->Next && node->Offset + node->Size == node->Next->Offset)
        {
            GeometryPool::FreeNode* merge  = node->Next;
            node->Size                    += merge->Size;
            node->Next                     = merge->Next;
            pool.Free(merge);
        }

        // Merge with the previous node if adjacent.
        if (prev && prev->Offset + prev->Size == node->Offset)
        {
            prev->Size += node->Size;
            prev->Next  = node->Next;
            pool.Free(node);
        }
    }

    bool GeometryPool::Allocate(VkDeviceSize vtx_bytes, VkDeviceSize idx_bytes, GeometryRegion& out)
    {
        ZENGINE_VALIDATE_ASSERT(vtx_bytes > 0, "GeometryPool::Allocate: vtx_bytes must be > 0")
        ZENGINE_VALIDATE_ASSERT(idx_bytes > 0, "GeometryPool::Allocate: idx_bytes must be > 0")

        VkDeviceSize vtx_offset    = 0;
        VkDeviceSize idx_offset    = 0;
        bool         vtx_from_free = TryAllocFromList(FreeVtxHead, FreeNodePool, vtx_bytes, vtx_offset);
        bool         idx_from_free = TryAllocFromList(FreeIdxHead, FreeNodePool, idx_bytes, idx_offset);

        // If one axis succeeded from the free list but the other did not, undo it.
        if (vtx_from_free && !idx_from_free)
        {
            InsertAndMerge(FreeVtxHead, FreeNodePool, vtx_offset, vtx_bytes);
            vtx_from_free = false;
        }
        if (idx_from_free && !vtx_from_free)
        {
            InsertAndMerge(FreeIdxHead, FreeNodePool, idx_offset, idx_bytes);
            idx_from_free = false;
        }

        if (!vtx_from_free)
        {
            if (VtxCursor + vtx_bytes > VtxCapacity)
                return false;
            vtx_offset  = VtxCursor;
            VtxCursor  += vtx_bytes;
        }

        if (!idx_from_free)
        {
            if (IdxCursor + idx_bytes > IdxCapacity)
            {
                if (!vtx_from_free)
                    VtxCursor -= vtx_bytes;
                else
                    InsertAndMerge(FreeVtxHead, FreeNodePool, vtx_offset, vtx_bytes);
                return false;
            }
            idx_offset  = IdxCursor;
            IdxCursor  += idx_bytes;
        }

        out.VtxByteOffset  = vtx_offset;
        out.VtxByteSize    = vtx_bytes;
        out.IdxByteOffset  = idx_offset;
        out.IdxByteSize    = idx_bytes;
        VtxUsed           += vtx_bytes;
        IdxUsed           += idx_bytes;
        return true;
    }

    void GeometryPool::Free(const GeometryRegion& region)
    {
        ZENGINE_VALIDATE_ASSERT(region.VtxByteSize > 0, "GeometryPool::Free: region has zero vtx size")
        ZENGINE_VALIDATE_ASSERT(region.IdxByteSize > 0, "GeometryPool::Free: region has zero idx size")
        ZENGINE_VALIDATE_ASSERT(VtxUsed >= region.VtxByteSize, "GeometryPool::Free: VtxUsed underflow")
        ZENGINE_VALIDATE_ASSERT(IdxUsed >= region.IdxByteSize, "GeometryPool::Free: IdxUsed underflow")

        InsertAndMerge(FreeVtxHead, FreeNodePool, region.VtxByteOffset, region.VtxByteSize);
        InsertAndMerge(FreeIdxHead, FreeNodePool, region.IdxByteOffset, region.IdxByteSize);
        VtxUsed -= region.VtxByteSize;
        IdxUsed -= region.IdxByteSize;
    }

    float GeometryPool::FragmentationRatio() const
    {
        VkDeviceSize total_capacity = VtxCapacity + IdxCapacity;
        if (total_capacity == 0)
            return 0.f;

        VkDeviceSize free_vtx = 0;
        for (const FreeNode* n = FreeVtxHead; n; n = n->Next)
            free_vtx += n->Size;

        VkDeviceSize free_idx = 0;
        for (const FreeNode* n = FreeIdxHead; n; n = n->Next)
            free_idx += n->Size;

        return static_cast<float>(free_vtx + free_idx) / static_cast<float>(total_capacity);
    }

    void GeometryPool::Reset()
    {
        FreeNodePool.Clear();
        FreeVtxHead = nullptr;
        FreeIdxHead = nullptr;
        VtxCursor   = 0;
        IdxCursor   = 0;
        VtxUsed     = 0;
        IdxUsed     = 0;
    }

} // namespace ZEngine::Rendering
