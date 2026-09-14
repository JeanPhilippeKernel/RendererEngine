#include <ZEngine/Rendering/Scenes/SkyEnvironment.h>

namespace ZEngine::Rendering::Scenes
{
    void SkyEnvironment::Initialize(Textures::TextureHandle fallback_source)
    {
        *this                         = {};
        m_snapshots[0].SourceRadiance = fallback_source;
        m_snapshots[0].State          = SkyEnvironmentState::Fallback;
        m_snapshots[0].IsFallback     = true;
        m_published_slot              = 0;
        m_state                       = SkyEnvironmentState::Fallback;
    }

    bool SkyEnvironment::SubmitConfig(const SkyConfig& config, uint64_t revision)
    {
        if (revision == 0 || revision <= m_latest_revision)
            return false;

        m_pending_request.Config = config;
        m_pending_request.Config.Sanitize();
        m_pending_request.Revision = revision;
        m_latest_revision          = revision;
        m_has_pending_request      = true;
        return true;
    }

    bool SkyEnvironment::TakeBakeRequest(SkyEnvironmentBakeRequest& out_request)
    {
        if (!m_has_pending_request || m_has_active_bake)
            return false;

        out_request           = m_pending_request;
        m_active_bake         = m_pending_request;
        m_active_bake_source  = {};
        m_has_pending_request = false;
        m_has_active_bake     = true;
        m_state               = SkyEnvironmentState::Baking;
        return true;
    }

    bool SkyEnvironment::AttachBakeResource(uint64_t revision, Textures::TextureHandle source_radiance)
    {
        if (!m_has_active_bake || m_active_bake.Revision != revision || !source_radiance.Valid())
            return false;

        m_active_bake_source = source_radiance;
        return true;
    }

    SkyEnvironmentBakeResult SkyEnvironment::CompleteBake(uint64_t revision, Textures::TextureHandle source_radiance, bool success)
    {
        if (!m_has_active_bake || m_active_bake.Revision != revision)
            return SkyEnvironmentBakeResult::Ignored;

        const SkyEnvironmentBakeRequest completed_request = m_active_bake;
        const Textures::TextureHandle   completed_source  = source_radiance.Valid() ? source_radiance : m_active_bake_source;
        m_active_bake                                     = {};
        m_active_bake_source                              = {};
        m_has_active_bake                                 = false;

        if (revision != m_latest_revision)
            return SkyEnvironmentBakeResult::Discarded;

        if (!success || !completed_source.Valid())
        {
            m_state = SkyEnvironmentState::Failed;
            return SkyEnvironmentBakeResult::Failed;
        }

        const int32_t new_slot = FindFreeSnapshotSlot();
        if (new_slot < 0)
        {
            m_state = SkyEnvironmentState::Failed;
            return SkyEnvironmentBakeResult::Failed;
        }

        SkyEnvironmentSnapshot& previous = m_snapshots[m_published_slot];
        if (!previous.IsFallback)
            previous.Retired = true;

        SkyEnvironmentSnapshot& published = m_snapshots[new_slot];
        published                         = {};
        published.Config                  = completed_request.Config;
        published.SourceRadiance          = completed_source;
        published.Revision                = revision;
        published.State                   = SkyEnvironmentState::Ready;
        m_published_slot                  = static_cast<uint32_t>(new_slot);
        m_state                           = SkyEnvironmentState::Ready;
        return SkyEnvironmentBakeResult::Published;
    }

    const SkyEnvironmentSnapshot* SkyEnvironment::AcquireForFrame()
    {
        if (m_published_slot >= MaxSnapshots || m_frame_pin_count == MaxPendingFramePins)
            return nullptr;

        SkyEnvironmentSnapshot& snapshot = m_snapshots[m_published_slot];
        if (!snapshot.SourceRadiance.Valid())
            return nullptr;

        const uint32_t pin_index     = (m_frame_pin_head + m_frame_pin_count) % MaxPendingFramePins;
        m_frame_pin_slots[pin_index] = static_cast<uint16_t>(m_published_slot);
        ++m_frame_pin_count;
        ++snapshot.PinCount;
        return &snapshot;
    }

    void SkyEnvironment::ReleaseSubmittedFrame(uint64_t timeline_value)
    {
        ReleaseNextFramePin(timeline_value);
    }

    void SkyEnvironment::ReleaseCancelledFrame()
    {
        ReleaseNextFramePin(0);
    }

    bool SkyEnvironment::TakeRetiredSnapshot(uint64_t completed_timeline_value, Textures::TextureHandle& out_source_radiance)
    {
        out_source_radiance = {};
        for (uint32_t index = 0; index < MaxSnapshots; ++index)
        {
            SkyEnvironmentSnapshot& snapshot = m_snapshots[index];
            if (!snapshot.Retired || snapshot.IsFallback || snapshot.PinCount != 0 || snapshot.LastUseTimeline > completed_timeline_value)
                continue;

            out_source_radiance = snapshot.SourceRadiance;
            snapshot            = {};
            return out_source_radiance.Valid();
        }
        return false;
    }

    Textures::TextureHandle SkyEnvironment::Shutdown()
    {
        while (m_frame_pin_count > 0)
            ReleaseNextFramePin(UINT64_MAX);

        for (uint32_t index = 0; index < MaxSnapshots; ++index)
        {
            if (!m_snapshots[index].IsFallback)
                m_snapshots[index].Retired = true;
        }
        const Textures::TextureHandle active_bake_source = m_active_bake_source;
        m_active_bake                                    = {};
        m_active_bake_source                             = {};
        m_has_active_bake                                = false;
        m_has_pending_request                            = false;
        return active_bake_source;
    }

    const SkyEnvironmentBakeRequest* SkyEnvironment::GetActiveBake() const
    {
        return m_has_active_bake ? &m_active_bake : nullptr;
    }

    Textures::TextureHandle SkyEnvironment::GetActiveBakeSource() const
    {
        return m_has_active_bake ? m_active_bake_source : Textures::TextureHandle{};
    }

    const SkyEnvironmentSnapshot* SkyEnvironment::GetPublishedSnapshot() const
    {
        return m_published_slot < MaxSnapshots ? &m_snapshots[m_published_slot] : nullptr;
    }

    SkyEnvironmentState SkyEnvironment::GetState() const
    {
        return m_state;
    }

    uint64_t SkyEnvironment::GetLatestRevision() const
    {
        return m_latest_revision;
    }

    void SkyEnvironment::ReleaseNextFramePin(uint64_t timeline_value)
    {
        if (m_frame_pin_count == 0)
            return;

        const uint32_t slot = m_frame_pin_slots[m_frame_pin_head];
        m_frame_pin_head    = (m_frame_pin_head + 1) % MaxPendingFramePins;
        --m_frame_pin_count;
        if (slot >= MaxSnapshots)
            return;

        SkyEnvironmentSnapshot& snapshot = m_snapshots[slot];
        if (snapshot.PinCount > 0)
            --snapshot.PinCount;
        if (timeline_value > snapshot.LastUseTimeline)
            snapshot.LastUseTimeline = timeline_value;
    }

    int32_t SkyEnvironment::FindFreeSnapshotSlot() const
    {
        for (uint32_t index = 1; index < MaxSnapshots; ++index)
        {
            const SkyEnvironmentSnapshot& snapshot = m_snapshots[index];
            if (!snapshot.SourceRadiance.Valid() && !snapshot.Retired && snapshot.PinCount == 0)
                return static_cast<int32_t>(index);
        }
        return -1;
    }
} // namespace ZEngine::Rendering::Scenes
