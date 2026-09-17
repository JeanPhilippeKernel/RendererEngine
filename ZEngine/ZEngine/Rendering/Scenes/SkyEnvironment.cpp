#include <ZEngine/Rendering/Scenes/SkyEnvironment.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace ZEngine::Rendering::Scenes
{
    void SkyEnvironment::Initialize(Textures::TextureHandle fallback_source, const EnvironmentLightingResources& fallback_lighting, const EnvironmentLightingBakeSettings& bake_settings)
    {
        *this                         = {};
        m_bake_settings               = bake_settings.IsValid() ? bake_settings : ResolveEnvironmentLightingQuality(EnvironmentLightingQualityTier::Standard);
        m_snapshots[0].SourceRadiance = fallback_source;
        m_snapshots[0].Lighting       = fallback_lighting;
        m_snapshots[0].State          = SkyEnvironmentState::Fallback;
        m_snapshots[0].IsFallback     = true;
        m_fallback_lighting           = fallback_lighting;
        // An invalid mode forces the first submitted scene configuration through
        // the bake path instead of treating the fallback snapshot as authored data.
        m_bake_config.Mode            = static_cast<SkyMode>(UINT8_MAX);
        m_published_slot              = 0;
        m_state                       = SkyEnvironmentState::Fallback;
    }

    void SkyEnvironment::ConfigureMemoryBudget(uint64_t budget_bytes, uint64_t fallback_bytes)
    {
        m_memory_budget_bytes      = budget_bytes;
        m_snapshots[0].MemoryBytes = fallback_bytes;
    }

    bool SkyEnvironment::ReserveActiveBakeMemory(uint64_t revision, uint64_t bytes)
    {
        if (!m_has_active_bake || m_active_bake.Revision != revision)
            return false;

        // Unit tests and integrations that do not configure a budget retain
        // the existing scheduler behavior. The renderer always configures its
        // non-zero project/default budget before accepting a bake.
        if (m_memory_budget_bytes == 0)
            return true;

        if (m_active_bake_reserved_memory_bytes != 0)
            return false;

        const uint64_t reserved_memory_bytes = GetReservedMemoryBytes();
        if (reserved_memory_bytes > m_memory_budget_bytes || bytes > m_memory_budget_bytes - reserved_memory_bytes)
            return false;

        m_active_bake_reserved_memory_bytes = bytes;
        return true;
    }

    bool SkyEnvironment::SubmitConfig(const SkyConfig& config, uint64_t revision, const EnvironmentLightingBakeSettings& bake_settings, const SkyCelestialLight& celestial_light, uint64_t hdri_source_hash, bool hdri_artifact_ready)
    {
        if (revision == 0 || revision < m_latest_revision)
            return false;

        const bool                            has_required_sun       = !config.IsAtmosphere() || celestial_light.IsAvailable;
        SkyConfig                             sanitized              = config;
        const EnvironmentLightingBakeSettings resolved_bake_settings = bake_settings.IsValid() ? bake_settings : ResolveEnvironmentLightingQuality(EnvironmentLightingQualityTier::Standard);
        sanitized.Sanitize();
        const bool is_hdri         = sanitized.IsHDRI();
        const bool source_is_ready = !is_hdri || hdri_artifact_ready;
        const bool inputs_valid    = config.IsValid() && celestial_light.IsValid() && has_required_sun && source_is_ready;

        // A normal scene revision remains immutable. The exception is an HDRI
        // source transition: imports keep the scene's UUID and revision stable,
        // but a new source hash or cooked-artifact readiness must replace any
        // pending/active work for the prior source.
        if (revision == m_latest_revision && (!is_hdri || (m_bake_config.IsHDRI() && m_bake_config.EnvironmentMap == sanitized.EnvironmentMap && m_bake_hdri_source_hash == hdri_source_hash && m_bake_hdri_artifact_ready == hdri_artifact_ready)))
            return false;

        m_presentation_config          = sanitized;
        m_presentation_celestial_light = celestial_light.IsValid() ? celestial_light : SkyCelestialLight{};
        m_latest_revision              = revision;

        // SkySphere is presentation-only. It deliberately uses the permanent
        // engine fallback rather than retaining an unrelated HDRI/atmosphere
        // IBL snapshot, and it must not enter the asset or GPU bake path.
        if (sanitized.IsSkySphere())
        {
            SkyEnvironmentSnapshot& published = m_snapshots[m_published_slot];
            if (!published.IsFallback)
                published.Retired = true;

            m_published_slot           = 0;
            m_pending_request          = {};
            m_has_pending_request      = false;
            m_bake_config.Mode         = static_cast<SkyMode>(UINT8_MAX);
            m_bake_celestial_light     = {};
            m_bake_inputs_valid        = false;
            m_bake_hdri_source_hash    = 0;
            m_bake_hdri_artifact_ready = true;
            m_latest_bake_revision     = revision;
            m_state                    = SkyEnvironmentState::Fallback;
            return true;
        }

        // A moving primary sun can otherwise invalidate every stage of the
        // atmosphere chain once per rendered frame. Keep the last bake key
        // current until the fixed revision budget opens; the most recently
        // submitted direction is still retained as presentation state above.
        const bool only_dynamic_celestial_input_changed = sanitized.IsAtmosphere() && inputs_valid && m_bake_inputs_valid && m_bake_config.IsAtmosphere() && m_bake_settings.Matches(resolved_bake_settings) && HasEquivalentAtmosphereSourceInputs(m_bake_config, sanitized) && !m_bake_celestial_light.Matches(celestial_light) && !HasSignificantCelestialLightChange(m_bake_celestial_light, celestial_light);
        if (only_dynamic_celestial_input_changed && revision >= m_latest_bake_revision && revision - m_latest_bake_revision < DynamicCelestialBakeRevisionInterval)
            return true;

        if (inputs_valid && m_bake_inputs_valid && HasEquivalentBakeInputs(m_bake_config, m_bake_celestial_light, m_bake_hdri_source_hash, m_bake_hdri_artifact_ready, sanitized, celestial_light, hdri_source_hash, hdri_artifact_ready) && m_bake_settings.Matches(resolved_bake_settings))
        {
            // The source radiance remains valid. Keep editor-facing presentation
            // changes (tint, intensity, and yaw) off the bake path.
            if (m_has_pending_request)
            {
                m_pending_request.Config   = sanitized;
                m_pending_request.Revision = revision;
            }
            // A pending quality change leaves the active bake on its original
            // immutable budget. It must finish as stale rather than being
            // relabelled as the newer quality revision.
            if (m_has_active_bake && m_active_bake.BakeSettings.Matches(resolved_bake_settings))
            {
                m_active_bake.Config         = sanitized;
                m_active_bake.CelestialLight = celestial_light;
                m_active_bake.Revision       = revision;
            }
            m_latest_bake_revision = revision;
            return true;
        }

        m_bake_config                       = sanitized;
        m_bake_celestial_light              = celestial_light;
        m_bake_hdri_source_hash             = is_hdri ? hdri_source_hash : 0;
        m_bake_hdri_artifact_ready          = source_is_ready;
        m_bake_inputs_valid                 = inputs_valid;
        m_bake_settings                     = resolved_bake_settings;
        m_pending_request.Config            = sanitized;
        m_pending_request.CelestialLight    = celestial_light;
        m_pending_request.BakeSettings      = resolved_bake_settings;
        m_pending_request.HDRISourceHash    = is_hdri ? hdri_source_hash : 0;
        m_pending_request.Revision          = revision;
        m_pending_request.HDRIArtifactReady = source_is_ready;
        m_pending_request.BakeInputsValid   = inputs_valid;
        m_latest_bake_revision              = revision;
        m_has_pending_request               = true;
        return true;
    }

    bool SkyEnvironment::TakeBakeRequest(SkyEnvironmentBakeRequest& out_request)
    {
        if (!m_has_pending_request || m_has_active_bake)
            return false;

        out_request                         = m_pending_request;
        m_active_bake                       = m_pending_request;
        m_active_bake_atmosphere            = {};
        m_active_bake_owns_atmosphere       = false;
        m_active_bake_source                = {};
        m_active_bake_lighting              = {};
        m_active_bake_reserved_memory_bytes = 0;
        m_active_stage_timeline             = 0;
        m_active_bake_stage                 = SkyEnvironmentBakeStage::AwaitingSource;
        m_active_stage_submitted            = false;
        m_active_stage_recorded             = false;
        m_has_pending_request               = false;
        m_has_active_bake                   = true;
        m_state                             = SkyEnvironmentState::Baking;
        return true;
    }

    bool SkyEnvironment::AttachBakeResource(uint64_t revision, Textures::TextureHandle source_radiance)
    {
        if (!m_has_active_bake || m_active_bake.Revision != revision || !source_radiance.Valid())
            return false;

        m_active_bake_source = source_radiance;
        return true;
    }

    bool SkyEnvironment::AttachBakeAtmosphere(uint64_t revision, const AtmosphereStaticResources& atmosphere, bool owns_resources)
    {
        if (!m_has_active_bake || m_active_bake.Revision != revision || !m_active_bake.Config.IsAtmosphere() || !atmosphere.Valid())
            return false;

        m_active_bake_atmosphere      = atmosphere;
        m_active_bake_owns_atmosphere = owns_resources;
        return true;
    }

    bool SkyEnvironment::AttachBakeLighting(uint64_t revision, const EnvironmentLightingResources& lighting)
    {
        if (!m_has_active_bake || m_active_bake.Revision != revision || !lighting.Valid() || !lighting.BakeSettings.Matches(m_active_bake.BakeSettings))
            return false;

        m_active_bake_lighting = lighting;
        return true;
    }

    bool SkyEnvironment::BeginGpuBake(uint64_t revision)
    {
        if (!m_has_active_bake || m_active_bake.Revision != revision || !m_active_bake_source.Valid() || !m_active_bake_lighting.Valid() || (m_active_bake.Config.IsAtmosphere() && !m_active_bake_atmosphere.Valid()))
            return false;

        m_active_bake_stage      = m_active_bake.Config.IsAtmosphere() ? SkyEnvironmentBakeStage::AtmosphereTransmittance : SkyEnvironmentBakeStage::SourceMipChain;
        m_active_stage_timeline  = 0;
        m_active_stage_submitted = false;
        m_active_stage_recorded  = false;
        return true;
    }

    bool SkyEnvironment::CanRecordGpuBakeStage() const
    {
        return m_has_active_bake && m_active_bake_stage != SkyEnvironmentBakeStage::AwaitingSource && m_active_bake_stage != SkyEnvironmentBakeStage::ReadyToPublish && !m_active_stage_submitted;
    }

    bool SkyEnvironment::NotifyGpuBakeStageRecorded(uint64_t revision, SkyEnvironmentBakeStage stage)
    {
        if (!CanRecordGpuBakeStage() || m_active_bake.Revision != revision || m_active_bake_stage != stage)
            return false;

        m_active_stage_recorded = true;
        return true;
    }

    bool SkyEnvironment::MarkGpuBakeStageSubmitted(uint64_t revision, uint64_t timeline_value)
    {
        if (!CanRecordGpuBakeStage() || !m_active_stage_recorded || m_active_bake.Revision != revision || timeline_value == 0)
            return false;

        m_active_stage_timeline  = timeline_value;
        m_active_stage_submitted = true;
        return true;
    }

    bool SkyEnvironment::AdvanceCompletedGpuBakeStage(uint64_t completed_timeline_value)
    {
        if (!m_has_active_bake || !m_active_stage_submitted || completed_timeline_value < m_active_stage_timeline)
            return false;

        m_active_stage_submitted = false;
        m_active_stage_recorded  = false;
        m_active_stage_timeline  = 0;
        switch (m_active_bake_stage)
        {
            case SkyEnvironmentBakeStage::AtmosphereTransmittance:
                m_active_bake_stage = SkyEnvironmentBakeStage::AtmosphereMultiscattering;
                return true;
            case SkyEnvironmentBakeStage::AtmosphereMultiscattering:
                m_active_bake_stage = SkyEnvironmentBakeStage::AtmosphereSourceRadiance;
                return true;
            case SkyEnvironmentBakeStage::AtmosphereSourceRadiance:
                m_active_bake_stage = SkyEnvironmentBakeStage::SourceMipChain;
                return true;
            case SkyEnvironmentBakeStage::SourceMipChain:
                m_active_bake_stage = SkyEnvironmentBakeStage::DiffuseIrradiance;
                return true;
            case SkyEnvironmentBakeStage::DiffuseIrradiance:
                m_active_bake_stage = SkyEnvironmentBakeStage::SpecularEnvironment;
                return true;
            case SkyEnvironmentBakeStage::SpecularEnvironment:
                m_active_bake_stage = SkyEnvironmentBakeStage::ReadyToPublish;
                return true;
            default:
                return false;
        }
    }

    bool SkyEnvironment::IsGpuBakeReadyToPublish() const
    {
        return m_has_active_bake && m_active_bake_stage == SkyEnvironmentBakeStage::ReadyToPublish && !m_active_stage_submitted;
    }

    bool SkyEnvironment::IsActiveBakeCurrent() const
    {
        return m_has_active_bake && m_active_bake.Revision == m_latest_bake_revision && m_active_bake.BakeSettings.Matches(m_bake_settings) && HasEquivalentBakeInputs(m_active_bake.Config, m_active_bake.CelestialLight, m_active_bake.HDRISourceHash, m_active_bake.HDRIArtifactReady, m_bake_config, m_bake_celestial_light, m_bake_hdri_source_hash, m_bake_hdri_artifact_ready);
    }

    SkyEnvironmentBakeResult SkyEnvironment::CompleteBake(uint64_t revision, Textures::TextureHandle source_radiance, bool success, const EnvironmentLightingResources& lighting, const AtmosphereStaticResources& atmosphere)
    {
        if (!m_has_active_bake || m_active_bake.Revision != revision)
            return SkyEnvironmentBakeResult::Ignored;

        const bool                         active_bake_is_current = IsActiveBakeCurrent();
        const bool                         requires_atmosphere    = m_active_bake.Config.IsAtmosphere();
        const SkyCelestialLight            completed_celestial    = m_active_bake.CelestialLight;
        const Textures::TextureHandle      completed_source       = source_radiance.Valid() ? source_radiance : m_active_bake_source;
        const EnvironmentLightingResources completed_lighting     = lighting.Valid() ? lighting : m_active_bake_lighting.Valid() ? m_active_bake_lighting : m_fallback_lighting;
        const AtmosphereStaticResources    completed_atmosphere   = atmosphere.Valid() ? atmosphere : m_active_bake_atmosphere;
        const uint64_t                     completed_memory_bytes = m_active_bake_reserved_memory_bytes;
        m_active_bake                                             = {};
        m_active_bake_atmosphere                                  = {};
        m_active_bake_owns_atmosphere                             = false;
        m_active_bake_source                                      = {};
        m_active_bake_lighting                                    = {};
        m_active_bake_reserved_memory_bytes                       = 0;
        m_active_bake_stage                                       = SkyEnvironmentBakeStage::AwaitingSource;
        m_active_stage_timeline                                   = 0;
        m_active_stage_submitted                                  = false;
        m_active_stage_recorded                                   = false;
        m_has_active_bake                                         = false;

        if (!active_bake_is_current)
            return SkyEnvironmentBakeResult::Discarded;

        if (!success || !completed_source.Valid() || (requires_atmosphere && !completed_atmosphere.Valid()))
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
        published.Config                  = m_presentation_config;
        published.CelestialLight          = completed_celestial;
        published.Atmosphere              = completed_atmosphere;
        published.SourceRadiance          = completed_source;
        published.Lighting                = completed_lighting;
        published.Revision                = revision;
        published.MemoryBytes             = completed_memory_bytes;
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

    bool SkyEnvironment::TakeRetiredSnapshot(uint64_t completed_timeline_value, SkyEnvironmentResources& out_resources)
    {
        out_resources = {};
        for (uint32_t index = 0; index < MaxSnapshots; ++index)
        {
            SkyEnvironmentSnapshot& snapshot = m_snapshots[index];
            if (!snapshot.Retired || snapshot.IsFallback || snapshot.PinCount != 0 || snapshot.LastUseTimeline > completed_timeline_value)
                continue;

            out_resources.SourceRadiance = snapshot.SourceRadiance;
            if (!IsAtmosphereShared(index, snapshot.Atmosphere))
                out_resources.Atmosphere = snapshot.Atmosphere;
            out_resources.Lighting = snapshot.Lighting;
            snapshot               = {};
            return out_resources.SourceRadiance.Valid();
        }
        return false;
    }

    SkyEnvironmentResources SkyEnvironment::Shutdown()
    {
        while (m_frame_pin_count > 0)
            ReleaseNextFramePin(UINT64_MAX);

        for (uint32_t index = 0; index < MaxSnapshots; ++index)
        {
            if (!m_snapshots[index].IsFallback)
                m_snapshots[index].Retired = true;
        }
        SkyEnvironmentResources active_bake_resources = {
            .Atmosphere     = m_active_bake_owns_atmosphere ? m_active_bake_atmosphere : AtmosphereStaticResources{},
            .SourceRadiance = m_active_bake_source,
            .Lighting       = m_active_bake_lighting,
        };
        m_active_bake                       = {};
        m_active_bake_atmosphere            = {};
        m_active_bake_owns_atmosphere       = false;
        m_active_bake_source                = {};
        m_active_bake_lighting              = {};
        m_active_bake_reserved_memory_bytes = 0;
        m_active_bake_stage                 = SkyEnvironmentBakeStage::AwaitingSource;
        m_active_stage_timeline             = 0;
        m_active_stage_submitted            = false;
        m_active_stage_recorded             = false;
        m_has_active_bake                   = false;
        m_has_pending_request               = false;
        return active_bake_resources;
    }

    const SkyEnvironmentBakeRequest* SkyEnvironment::GetActiveBake() const
    {
        return m_has_active_bake ? &m_active_bake : nullptr;
    }

    Textures::TextureHandle SkyEnvironment::GetActiveBakeSource() const
    {
        return m_has_active_bake ? m_active_bake_source : Textures::TextureHandle{};
    }

    const AtmosphereStaticResources& SkyEnvironment::GetActiveBakeAtmosphere() const
    {
        return m_active_bake_atmosphere;
    }

    bool SkyEnvironment::ActiveBakeOwnsAtmosphere() const
    {
        return m_has_active_bake && m_active_bake_owns_atmosphere;
    }

    const AtmosphereStaticResources* SkyEnvironment::FindReusableAtmosphere(const SkyConfig& config) const
    {
        const SkyEnvironmentSnapshot* const published = GetPublishedSnapshot();
        if (!published || published->IsFallback || !published->Config.IsAtmosphere() || !published->Atmosphere.Valid() || !HasEquivalentAtmosphereStaticInputs(published->Config, config))
            return nullptr;
        return &published->Atmosphere;
    }

    const EnvironmentLightingResources& SkyEnvironment::GetActiveBakeLighting() const
    {
        return m_active_bake_lighting;
    }

    SkyEnvironmentBakeStage SkyEnvironment::GetActiveBakeStage() const
    {
        return m_active_bake_stage;
    }

    const SkyEnvironmentSnapshot* SkyEnvironment::GetPublishedSnapshot() const
    {
        return m_published_slot < MaxSnapshots ? &m_snapshots[m_published_slot] : nullptr;
    }

    const SkyConfig& SkyEnvironment::GetPresentationConfig() const
    {
        return m_presentation_config;
    }

    const SkyCelestialLight& SkyEnvironment::GetPresentationCelestialLight() const
    {
        return m_presentation_celestial_light;
    }

    const EnvironmentLightingResources& SkyEnvironment::GetFallbackLighting() const
    {
        return m_fallback_lighting;
    }

    SkyEnvironmentState SkyEnvironment::GetState() const
    {
        return m_state;
    }

    uint64_t SkyEnvironment::GetLatestRevision() const
    {
        return m_latest_revision;
    }

    uint64_t SkyEnvironment::GetReservedMemoryBytes() const
    {
        uint64_t reserved_memory_bytes = m_active_bake_reserved_memory_bytes;
        for (const SkyEnvironmentSnapshot& snapshot : m_snapshots)
        {
            if (snapshot.MemoryBytes > std::numeric_limits<uint64_t>::max() - reserved_memory_bytes)
                return std::numeric_limits<uint64_t>::max();
            reserved_memory_bytes += snapshot.MemoryBytes;
        }
        return reserved_memory_bytes;
    }

    uint64_t SkyEnvironment::GetMemoryBudgetBytes() const
    {
        return m_memory_budget_bytes;
    }

    bool SkyEnvironment::HasEquivalentAtmosphereStaticInputs(const SkyConfig& left, const SkyConfig& right)
    {
        const auto                equal3 = [](const float (&first)[3], const float (&second)[3]) { return first[0] == second[0] && first[1] == second[1] && first[2] == second[2]; };

        const AtmosphereSettings& first  = left.Atmosphere;
        const AtmosphereSettings& second = right.Atmosphere;
        // Source capture is planet-centred and has no render-view position. Its
        // bake key therefore excludes scene placement and unit-scale values;
        // those belong to the later per-view atmosphere path. The resolved
        // direction, rather than the selected entity UUID, is the solar input.
        return first.PlanetRadiusKilometers == second.PlanetRadiusKilometers && first.AtmosphereRadiusKilometers == second.AtmosphereRadiusKilometers && equal3(first.RayleighScatteringPerKilometer, second.RayleighScatteringPerKilometer) && first.RayleighScaleHeightKilometers == second.RayleighScaleHeightKilometers && first.MieScatteringPerKilometer == second.MieScatteringPerKilometer && first.MieAbsorptionPerKilometer == second.MieAbsorptionPerKilometer && first.MieScaleHeightKilometers == second.MieScaleHeightKilometers &&
               first.MieAnisotropy == second.MieAnisotropy && equal3(first.OzoneAbsorptionPerKilometer, second.OzoneAbsorptionPerKilometer) && first.OzoneCenterKilometers == second.OzoneCenterKilometers && first.OzoneThicknessKilometers == second.OzoneThicknessKilometers;
    }

    bool SkyEnvironment::HasEquivalentAtmosphereSourceInputs(const SkyConfig& left, const SkyConfig& right)
    {
        if (!left.IsAtmosphere() || !right.IsAtmosphere() || !HasEquivalentAtmosphereStaticInputs(left, right))
            return false;

        const auto equal3 = [](const float (&first)[3], const float (&second)[3]) { return first[0] == second[0] && first[1] == second[1] && first[2] == second[2]; };
        return left.Atmosphere.SunAngularRadiusRadians == right.Atmosphere.SunAngularRadiusRadians && left.Atmosphere.SunIlluminanceLux == right.Atmosphere.SunIlluminanceLux && equal3(left.Atmosphere.GroundAlbedo, right.Atmosphere.GroundAlbedo) && left.Atmosphere.GroundAmbientIrradiance == right.Atmosphere.GroundAmbientIrradiance;
    }

    bool SkyEnvironment::HasSignificantCelestialLightChange(const SkyCelestialLight& previous, const SkyCelestialLight& next)
    {
        if (previous.IsAvailable != next.IsAvailable)
            return true;
        if (!previous.IsAvailable)
            return false;

        const auto  squared_length          = [](const SkyCelestialLight& light) { return light.DirectionToLight[0] * light.DirectionToLight[0] + light.DirectionToLight[1] * light.DirectionToLight[1] + light.DirectionToLight[2] * light.DirectionToLight[2]; };
        const float previous_length_squared = squared_length(previous);
        const float next_length_squared     = squared_length(next);
        if (!std::isfinite(previous_length_squared) || !std::isfinite(next_length_squared) || previous_length_squared <= 1.0e-8f || next_length_squared <= 1.0e-8f)
            return true;

        const float dot            = previous.DirectionToLight[0] * next.DirectionToLight[0] + previous.DirectionToLight[1] * next.DirectionToLight[1] + previous.DirectionToLight[2] * next.DirectionToLight[2];
        const float normalized_dot = std::clamp(dot / std::sqrt(previous_length_squared * next_length_squared), -1.0f, 1.0f);
        return normalized_dot < DynamicCelestialBakeDirectionCosThreshold;
    }

    bool SkyEnvironment::HasEquivalentBakeInputs(const SkyConfig& left, const SkyCelestialLight& left_celestial_light, uint64_t left_hdri_source_hash, bool left_hdri_artifact_ready, const SkyConfig& right, const SkyCelestialLight& right_celestial_light, uint64_t right_hdri_source_hash, bool right_hdri_artifact_ready)
    {
        if (left.Mode != right.Mode)
            return false;
        if (left.IsHDRI())
            return left.EnvironmentMap == right.EnvironmentMap && left_hdri_source_hash == right_hdri_source_hash && left_hdri_artifact_ready == right_hdri_artifact_ready;
        if (left.IsSkySphere())
            return true;

        return HasEquivalentAtmosphereSourceInputs(left, right) && left_celestial_light.Matches(right_celestial_light);
    }

    bool SkyEnvironment::IsAtmosphereShared(uint32_t excluded_snapshot_slot, const AtmosphereStaticResources& atmosphere) const
    {
        if (!atmosphere.Valid())
            return false;

        const auto matches_texture = [](const Textures::TextureHandle& left, const Textures::TextureHandle& right) { return left.Index == right.Index && left.Generation == right.Generation; };
        const auto matches         = [&atmosphere, &matches_texture](const AtmosphereStaticResources& other) { return matches_texture(other.Transmittance, atmosphere.Transmittance) || matches_texture(other.Multiscattering, atmosphere.Multiscattering); };
        if (m_has_active_bake && matches(m_active_bake_atmosphere))
            return true;

        for (uint32_t index = 0; index < MaxSnapshots; ++index)
        {
            if (index != excluded_snapshot_slot && matches(m_snapshots[index].Atmosphere))
                return true;
        }
        return false;
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
