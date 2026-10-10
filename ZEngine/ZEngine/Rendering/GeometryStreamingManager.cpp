#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Rendering/GeometryStreamingManager.h>
#include <ZEngine/Rendering/RenderResourceManager.h>

namespace ZEngine::Rendering
{
    void GeometryStreamingManager::Initialize(Hardwares::VulkanDevice* device, RenderResourceManager* rrm)
    {
        m_device            = device;
        m_rrm               = rrm;
        m_clock_hand        = 0;
        m_compact_requested = false;
    }

    void GeometryStreamingManager::Deinitialize()
    {
        m_device = nullptr;
        m_rrm    = nullptr;
    }

    bool GeometryStreamingManager::RequestLoad(const StreamRequest& req)
    {
        return m_load_queue.push(req);
    }

    void GeometryStreamingManager::DrainLoadQueue(uint32_t frame_index)
    {
        StreamRequest req;
        while (m_load_queue.pop(req))
        {
            if (!req.Handle.IsValid())
                continue;
            uint32_t slot_idx = req.Handle.Index;
            if (slot_idx >= m_rrm->m_mesh_slot_count)
                continue;
            auto& slot = m_rrm->m_mesh_slots[slot_idx];
            if (slot.Generation != req.Handle.Generation)
                continue;
            if (slot.Data.State != RenderResourceManager::StreamingState::Unloaded)
                continue; // already loading or resident from a parallel path

            // Re-upload the mesh data into a fresh pool region via the per-frame batch.
            RenderResourceManager::MeshSlot new_data = m_rrm->AppendMeshData(req.Asset, frame_index);
            if (new_data.VtxCount == 0)
            {
                ZENGINE_CORE_WARN("[GeometryStreamingManager] RequestLoad: AppendMeshData failed for slot {}", slot_idx)
                continue;
            }
            slot.Data.Region   = new_data.Region;
            slot.Data.VtxCount = new_data.VtxCount;
            slot.Data.IdxCount = new_data.IdxCount;
            slot.Data.State    = RenderResourceManager::StreamingState::Resident;
            ZENGINE_CORE_TRACE("[GeometryStreamingManager] Reloaded mesh slot {}", slot_idx)
        }
    }

    void GeometryStreamingManager::Tick(uint32_t frame_index)
    {
        if (!m_rrm)
            return;

        // Drain pending reload requests first — before eviction and compaction checks,
        // so newly reloaded meshes count toward the current usage before the thresholds
        // are evaluated.
        DrainLoadQueue(frame_index);

        // Request compaction if fragmentation exceeds the threshold.
        if (m_rrm->m_pool.FragmentationRatio() > kCompactionThreshold)
            m_compact_requested = true;

        // Run the eviction sweep if either buffer axis is under pressure.
        const auto& pool         = m_rrm->m_pool;
        bool        vtx_pressure = pool.VtxCapacity > 0 && static_cast<float>(pool.VtxUsed) / static_cast<float>(pool.VtxCapacity) > kEvictionThreshold;
        bool        idx_pressure = pool.IdxCapacity > 0 && static_cast<float>(pool.IdxUsed) / static_cast<float>(pool.IdxCapacity) > kEvictionThreshold;
        if (vtx_pressure || idx_pressure)
            RunEvictionSweep();

        // Clear all Referenced bits so RenderScene can mark fresh references this frame.
        uint32_t count = m_rrm->m_mesh_slot_count;
        for (uint32_t i = 0; i < count; ++i)
        {
            auto& slot = m_rrm->m_mesh_slots[i];
            if (slot.Generation != 0 && slot.Data.State == RenderResourceManager::StreamingState::Resident)
                slot.Data.Referenced = false;
        }
    }

    void GeometryStreamingManager::RunEvictionSweep()
    {
        uint32_t count = m_rrm->m_mesh_slot_count;
        if (count == 0)
            return;

        // Walk up to `count` slots from the current clock-hand position.
        // Slots with Referenced=true get a second chance (bit cleared, skipped this cycle).
        // The first non-referenced, non-pinned Resident slot is evicted.
        for (uint32_t visited = 0; visited < count; ++visited)
        {
            uint32_t i   = m_clock_hand % count;
            m_clock_hand = (m_clock_hand + 1) % count;

            auto& slot   = m_rrm->m_mesh_slots[i];
            if (slot.Generation == 0)
                continue; // free slot
            if (slot.Data.State != RenderResourceManager::StreamingState::Resident)
                continue;
            if (slot.Data.Pinned)
                continue;

            if (slot.Data.Referenced)
            {
                slot.Data.Referenced = false; // second-chance: clear and skip
                continue;
            }

            // Evict: return bytes to the pool and mark the slot unloaded.
            slot.Data.State = RenderResourceManager::StreamingState::Unloaded;
            if (slot.Data.Region.VtxByteSize > 0)
                m_rrm->m_pool.Free(slot.Data.Region);
            slot.Data.Region = {};
            ZENGINE_CORE_TRACE("[GeometryStreamingManager] Evicted mesh slot {}", i)
            return; // one eviction per sweep call
        }
    }

    void GeometryStreamingManager::RequestEvict(BufferHandle handle)
    {
        if (!m_rrm || !handle.IsValid())
            return;
        uint32_t slot_idx = handle.Index;
        if (slot_idx >= m_rrm->m_mesh_slot_count)
            return;
        auto& slot = m_rrm->m_mesh_slots[slot_idx];
        if (slot.Generation != handle.Generation)
            return;
        if (slot.Data.State != RenderResourceManager::StreamingState::Resident)
            return;
        if (slot.Data.Pinned)
            return;
        slot.Data.State = RenderResourceManager::StreamingState::Unloaded;
        if (slot.Data.Region.VtxByteSize > 0)
            m_rrm->m_pool.Free(slot.Data.Region);
        slot.Data.Region = {};
        ZENGINE_CORE_TRACE("[GeometryStreamingManager] Explicit eviction of mesh slot {}", slot_idx)
    }

} // namespace ZEngine::Rendering
