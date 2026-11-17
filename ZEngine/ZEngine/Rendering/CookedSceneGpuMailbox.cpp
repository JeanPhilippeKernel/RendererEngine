#include <ZEngine/Profiling/Profiling.h>
#include <ZEngine/Rendering/CookedSceneGpuMailbox.h>
#include <limits>

namespace ZEngine::Rendering
{
    bool CookedSceneGpuMailbox::Begin(const Managers::CookedCpuClosureCache::Lease& lease, std::span<const uuids::uuid> meshes, const Cook::CookAssetIdentity& environment, Core::Memory::IAllocationRoute& route, Core::Memory::TLSFSlab& metadata, RenderResourceManager& manager, Hardwares::VulkanDevice& device, RenderResourceManager::SceneResourcePreparationId scene, uint32_t max_textures, Ticket* output)
    {
        return TryBegin(lease, meshes, environment, route, metadata, manager, device, scene, max_textures, output) == Admission::Queued;
    }

    CookedSceneGpuMailbox::Admission CookedSceneGpuMailbox::TryBegin(const Managers::CookedCpuClosureCache::Lease& lease, std::span<const uuids::uuid> meshes, const Cook::CookAssetIdentity& environment, Core::Memory::IAllocationRoute& route, Core::Memory::TLSFSlab& metadata, RenderResourceManager& manager, Hardwares::VulkanDevice& device, RenderResourceManager::SceneResourcePreparationId scene, uint32_t max_textures, Ticket* output, bool pause_after_mesh_record)
    {
        if (!output || *output || m_sealed.load(std::memory_order_acquire) || m_next_serial == std::numeric_limits<uint64_t>::max())
            return Admission::Failed;
        for (const auto& slot : m_slots)
        {
            const auto phase = slot.PhaseValue.load(std::memory_order_acquire);
            if (phase != Phase::Free && phase != Phase::Writing && slot.Scene == scene)
                return Admission::Failed;
        }
        for (uint32_t i = 0; i < Capacity; ++i)
        {
            auto& slot     = m_slots[i];
            auto  expected = Phase::Free;
            if (!slot.PhaseValue.compare_exchange_strong(expected, Phase::Writing, std::memory_order_acq_rel))
                continue;
            if (!slot.Driver.Initialize(lease, meshes, environment, route, metadata, manager, device, scene, max_textures))
            {
                slot.PhaseValue.store(Phase::Free, std::memory_order_release);
                return Admission::Failed;
            }
            slot.Serial = ++m_next_serial;
            slot.Scene  = scene;
            slot.Result = Outcome::Pending;
            slot.Cancelled.store(false, std::memory_order_relaxed);
            slot.MeshRecorded.store(false, std::memory_order_relaxed);
            slot.PauseAfterMeshRecord = pause_after_mesh_record;
            *output                   = {i, slot.Serial};
            slot.PhaseValue.store(Phase::Queued, std::memory_order_release);
            return Admission::Queued;
        }
        return Admission::Backpressure;
    }

    bool CookedSceneGpuMailbox::MeshUploadRecorded(Ticket ticket) const noexcept
    {
        if (!ticket)
            return false;
        const auto& slot  = m_slots[ticket.Slot];
        const auto  phase = slot.PhaseValue.load(std::memory_order_acquire);
        return (phase == Phase::Queued || phase == Phase::Running || phase == Phase::Finished) && slot.Serial == ticket.Serial && slot.MeshRecorded.load(std::memory_order_acquire);
    }

    bool CookedSceneGpuMailbox::Cancel(Ticket ticket) noexcept
    {
        if (!ticket)
            return false;
        auto&      slot  = m_slots[ticket.Slot];
        const auto phase = slot.PhaseValue.load(std::memory_order_acquire);
        if ((phase != Phase::Queued && phase != Phase::Running) || slot.Serial != ticket.Serial)
            return false;
        slot.Cancelled.store(true, std::memory_order_release);
        return true;
    }

    bool CookedSceneGpuMailbox::Poll(Ticket ticket, Outcome* output) const noexcept
    {
        if (!ticket || !output)
            return false;
        const auto& slot  = m_slots[ticket.Slot];
        const auto  phase = slot.PhaseValue.load(std::memory_order_acquire);
        if ((phase != Phase::Queued && phase != Phase::Running && phase != Phase::Finished) || slot.Serial != ticket.Serial)
            return false;
        *output = phase == Phase::Finished ? slot.Result : Outcome::Pending;
        return true;
    }

    bool CookedSceneGpuMailbox::Acknowledge(Ticket ticket) noexcept
    {
        if (!ticket)
            return false;
        auto& slot = m_slots[ticket.Slot];
        if (slot.PhaseValue.load(std::memory_order_acquire) != Phase::Finished || slot.Serial != ticket.Serial)
            return false;
        slot.PhaseValue.store(Phase::Free, std::memory_order_release);
        return true;
    }

    void CookedSceneGpuMailbox::CancelAll() noexcept
    {
        for (auto& slot : m_slots)
        {
            const auto phase = slot.PhaseValue.load(std::memory_order_acquire);
            if (phase == Phase::Queued || phase == Phase::Running)
                slot.Cancelled.store(true, std::memory_order_release);
        }
    }

    bool CookedSceneGpuMailbox::DrainAfterDeviceIdle() noexcept
    {
        if (!m_sealed.load(std::memory_order_acquire))
            return false;
        for (auto& slot : m_slots)
        {
            const auto phase = slot.PhaseValue.load(std::memory_order_acquire);
            if (phase == Phase::Writing)
                return false;
            if (phase != Phase::Queued && phase != Phase::Running)
                continue;
            slot.Driver.Cancel();
            if (!slot.Driver.AbandonAfterDeviceIdle())
                return false;
            const auto state = slot.Driver.Status();
            slot.Result      = state == CookedSceneGpuPreparation::State::Failed ? Outcome::Failed : Outcome::Cancelled;
            if (!slot.Driver.Reset())
                return false;
            slot.PhaseValue.store(Phase::Finished, std::memory_order_release);
        }
        return true;
    }

    bool CookedSceneGpuMailbox::PumpOne(uint8_t frame_index, uint8_t thread_index)
    {
        for (uint32_t offset = 0; offset < Capacity; ++offset)
        {
            const uint32_t index = (m_turn + offset) % Capacity;
            auto&          slot  = m_slots[index];
            auto           phase = slot.PhaseValue.load(std::memory_order_acquire);
            if (phase == Phase::Queued)
            {
                if (!slot.PhaseValue.compare_exchange_strong(phase, Phase::Running, std::memory_order_acq_rel))
                    continue;
                phase = Phase::Running;
            }
            if (phase != Phase::Running)
                continue;
            ZENGINE_PROFILE_SCOPE("SceneLoad.Render.CookedUploadVisit");
            m_turn               = (index + 1) % Capacity;
            const bool cancelled = slot.Cancelled.load(std::memory_order_acquire);
            if (cancelled)
                slot.Driver.Cancel();
            else if (slot.PauseAfterMeshRecord && slot.MeshRecorded.load(std::memory_order_acquire))
                return true;
            const auto state = slot.Driver.Step(frame_index, thread_index);
            if (slot.Driver.HasRecordedMeshUpload())
                slot.MeshRecorded.store(true, std::memory_order_release);
            if (state == CookedSceneGpuPreparation::State::Submitted || state == CookedSceneGpuPreparation::State::Failed || state == CookedSceneGpuPreparation::State::Cancelled)
            {
                slot.Result = state == CookedSceneGpuPreparation::State::Submitted ? Outcome::Submitted : state == CookedSceneGpuPreparation::State::Cancelled ? Outcome::Cancelled : Outcome::Failed;
                if (!slot.Driver.Reset())
                    return true;
                slot.PhaseValue.store(Phase::Finished, std::memory_order_release);
            }
            return true;
        }
        return false;
    }

    Textures::CookedTextureUpload* CookedSceneGpuMailbox::FindTexture(Ticket ticket, const Cook::CookAssetIdentity& identity) noexcept
    {
        if (!ticket)
            return nullptr;
        auto&      slot  = m_slots[ticket.Slot];
        const auto phase = slot.PhaseValue.load(std::memory_order_acquire);
        return (phase == Phase::Queued || phase == Phase::Running) && slot.Serial == ticket.Serial ? slot.Driver.FindTexture(identity) : nullptr;
    }

    bool CookedSceneGpuMailbox::Drained() const noexcept
    {
        for (const auto& slot : m_slots)
        {
            const auto phase = slot.PhaseValue.load(std::memory_order_acquire);
            if (phase != Phase::Free && phase != Phase::Finished)
                return false;
        }
        return true;
    }
} // namespace ZEngine::Rendering
