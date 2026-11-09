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
        StreamRequest request = {};
        while (m_load_queue.pop(request))
        {
            if (m_rrm && request.Handle.IsValid() && request.Handle.Index < m_rrm->m_mesh_slot_count)
            {
                auto& slot = m_rrm->m_mesh_slots[request.Handle.Index];
                if (slot.Generation == request.Handle.Generation && slot.Data.State == RenderResourceManager::StreamingState::Pending)
                {
                    slot.Data.State = RenderResourceManager::StreamingState::Unloaded;
                }
            }
        }
        m_device = nullptr;
        m_rrm    = nullptr;
    }

    bool GeometryStreamingManager::RequestLoad(const StreamRequest& req)
    {
        if (!m_rrm || !req.Handle.IsValid() || req.Handle.Index >= m_rrm->m_mesh_slot_count)
        {
            return false;
        }
        auto& slot = m_rrm->m_mesh_slots[req.Handle.Index];
        if (slot.Generation != req.Handle.Generation)
        {
            return false;
        }
        if (slot.Data.State == RenderResourceManager::StreamingState::Pending || slot.Data.State == RenderResourceManager::StreamingState::Resident)
        {
            return true;
        }
        if (slot.Data.State != RenderResourceManager::StreamingState::Unloaded || !m_load_queue.push(req))
        {
            return false;
        }
        slot.Data.State      = RenderResourceManager::StreamingState::Pending;
        slot.Data.LoadFailed = false;
        return true;
    }

    void GeometryStreamingManager::DrainLoadQueue(uint32_t frame_index)
    {
        StreamRequest req = {};
        for (uint32_t operation = 0; operation < LoadSliceOperations && m_load_queue.pop(req); ++operation)
        {
            if (!req.Handle.IsValid())
            {
                continue;
            }
            uint32_t slot_idx = req.Handle.Index;
            if (slot_idx >= m_rrm->m_mesh_slot_count)
            {
                continue;
            }
            auto& slot = m_rrm->m_mesh_slots[slot_idx];
            if (slot.Generation != req.Handle.Generation)
            {
                continue;
            }
            if (slot.Data.State != RenderResourceManager::StreamingState::Pending)
            {
                continue; // already loading or resident from a parallel path
            }

            // Re-upload the mesh data into a fresh pool region via the per-frame batch.
            RenderResourceManager::MeshSlot new_data = m_rrm->AppendMeshData(req.Asset, frame_index);
            if (new_data.VtxCount == 0)
            {
                slot.Data.State      = RenderResourceManager::StreamingState::Unloaded;
                slot.Data.LoadFailed = true;
                ZENGINE_CORE_WARN("[GeometryStreamingManager] RequestLoad: AppendMeshData failed for slot {}", slot_idx)
                continue;
            }
            m_rrm->ReplaceMeshGeometry(slot.Data, new_data);
            // The preparation pump can pin this version later in BeginFrame.
            // Give it one eviction grace cycle instead of immediately discarding
            // the just-completed reload under pool pressure.
            slot.Data.Referenced = true;
            ZENGINE_CORE_TRACE("[GeometryStreamingManager] Reloaded mesh slot {}", slot_idx)
        }
    }

    void GeometryStreamingManager::Tick(uint32_t frame_index)
    {
        if (!m_rrm)
        {
            return;
        }

        // Drain pending reload requests first — before eviction and compaction checks,
        // so newly reloaded meshes count toward the current usage before the thresholds
        // are evaluated.
        DrainLoadQueue(frame_index);

        // Request compaction if fragmentation exceeds the threshold.
        if (m_rrm->m_pool.FragmentationRatio() > kCompactionThreshold)
        {
            m_compact_requested = true;
        }

        // Run the eviction sweep if either buffer axis is under pressure.
        const auto& pool         = m_rrm->m_pool;
        bool        vtx_pressure = pool.VtxCapacity > 0 && static_cast<float>(pool.VtxUsed) / static_cast<float>(pool.VtxCapacity) > kEvictionThreshold;
        bool        idx_pressure = pool.IdxCapacity > 0 && static_cast<float>(pool.IdxUsed) / static_cast<float>(pool.IdxCapacity) > kEvictionThreshold;
        if (vtx_pressure || idx_pressure)
        {
            RunEvictionSweep();
        }

        // Clear all Referenced bits so RenderScene can mark fresh references this frame.
        uint32_t count = m_rrm->m_mesh_slot_count;
        for (uint32_t i = 0; i < count; ++i)
        {
            auto& slot = m_rrm->m_mesh_slots[i];
            if (slot.Generation != 0 && slot.Data.State == RenderResourceManager::StreamingState::Resident)
            {
                slot.Data.Referenced = false;
            }
        }
    }

    void GeometryStreamingManager::RunEvictionSweep()
    {
        uint32_t count = m_rrm->m_mesh_slot_count;
        if (count == 0)
        {
            return;
        }

        // Walk up to `count` slots from the current clock-hand position.
        // Slots with Referenced=true get a second chance (bit cleared, skipped this cycle).
        // The first non-referenced, non-pinned Resident slot is evicted.
        for (uint32_t visited = 0; visited < count; ++visited)
        {
            uint32_t i   = m_clock_hand % count;
            m_clock_hand = (m_clock_hand + 1) % count;

            auto& slot   = m_rrm->m_mesh_slots[i];
            if (slot.Generation == 0)
            {
                continue; // free slot
            }
            if (slot.Data.State != RenderResourceManager::StreamingState::Resident)
            {
                continue;
            }
            if (slot.Data.Pinned || slot.Data.PinRecord != UINT32_MAX)
            {
                continue;
            }

            if (slot.Data.Referenced)
            {
                slot.Data.Referenced = false; // second-chance: clear and skip
                continue;
            }

            // Evict: return bytes to the pool and mark the slot unloaded.
            m_rrm->RetireMeshGeometry(slot.Data);
            slot.Data.State = RenderResourceManager::StreamingState::Unloaded;
            ZENGINE_CORE_TRACE("[GeometryStreamingManager] Evicted mesh slot {}", i)
            return; // one eviction per sweep call
        }
    }

    void GeometryStreamingManager::RequestEvict(BufferHandle handle)
    {
        if (!m_rrm || !handle.IsValid())
        {
            return;
        }
        uint32_t slot_idx = handle.Index;
        if (slot_idx >= m_rrm->m_mesh_slot_count)
        {
            return;
        }
        auto& slot = m_rrm->m_mesh_slots[slot_idx];
        if (slot.Generation != handle.Generation)
        {
            return;
        }
        if (slot.Data.State != RenderResourceManager::StreamingState::Resident)
        {
            return;
        }
        if (slot.Data.Pinned || slot.Data.PinRecord != UINT32_MAX)
        {
            return;
        }
        m_rrm->RetireMeshGeometry(slot.Data);
        slot.Data.State = RenderResourceManager::StreamingState::Unloaded;
        ZENGINE_CORE_TRACE("[GeometryStreamingManager] Explicit eviction of mesh slot {}", slot_idx)
    }

} // namespace ZEngine::Rendering
