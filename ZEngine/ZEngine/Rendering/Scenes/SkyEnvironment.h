#pragma once
#include <ZEngine/Rendering/EnvironmentLighting.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <cstdint>

namespace ZEngine::Rendering::Scenes
{
    /// @brief Runtime state of the scene environment currently being prepared.
    enum class SkyEnvironmentState : uint8_t
    {
        Fallback = 0,
        Baking,
        Ready,
        Failed,
    };

    /// @brief Ordered GPU work needed to turn source radiance into IBL textures.
    enum class SkyEnvironmentBakeStage : uint8_t
    {
        AwaitingSource = 0,
        AtmosphereTransmittance,
        AtmosphereMultiscattering,
        AtmosphereSourceRadiance,
        SourceMipChain,
        DiffuseIrradiance,
        SpecularEnvironment,
        ReadyToPublish,
    };

    /// @brief Persistent static lookup tables generated for one atmosphere revision.
    struct AtmosphereStaticResources
    {
        Textures::TextureHandle Transmittance   = {};
        Textures::TextureHandle Multiscattering = {};

        [[nodiscard]] bool      Valid() const
        {
            return Transmittance.Valid() && Multiscattering.Valid();
        }
    };

    /// @brief The resources that are owned as one immutable environment revision.
    struct SkyEnvironmentResources
    {
        AtmosphereStaticResources    Atmosphere     = {};
        Textures::TextureHandle      SourceRadiance = {};
        EnvironmentLightingResources Lighting       = {};
    };

    /// @brief Immutable GPU-resource snapshot selected by one rendered frame.
    struct SkyEnvironmentSnapshot
    {
        SkyConfig                    Config          = {};
        /// @brief Resolved sun direction used to generate this immutable revision.
        /// @details Per-view atmosphere work consumes this copy rather than
        /// reading mutable scene light state on the render thread.
        SkyCelestialLight            CelestialLight  = {};
        AtmosphereStaticResources    Atmosphere      = {};
        Textures::TextureHandle      SourceRadiance  = {};
        EnvironmentLightingResources Lighting        = {};
        uint64_t                     Revision        = 0;
        uint64_t                     LastUseTimeline = 0;
        /// @brief Conservative persistent-memory reservation for this snapshot.
        /// @details Static atmosphere LUTs are counted by every snapshot that
        /// references them, which can defer a bake early but never undercounts.
        uint64_t                     MemoryBytes     = 0;
        uint32_t                     PinCount        = 0;
        SkyEnvironmentState          State           = SkyEnvironmentState::Fallback;
        bool                         IsFallback      = false;
        bool                         Retired         = false;
    };

    /// @brief Immutable bake input claimed by the render thread.
    struct SkyEnvironmentBakeRequest
    {
        SkyConfig                       Config            = {};
        SkyCelestialLight               CelestialLight    = {};
        EnvironmentLightingBakeSettings BakeSettings      = {};
        /// @brief Registry snapshot for the selected HDRI source. This keeps
        /// source reimports distinct even though scene data stores only a UUID.
        uint64_t                        HDRISourceHash    = 0;
        uint64_t                        Revision          = 0;
        /// @brief False until the registry has a completed cooked artifact for
        /// the selected HDRI. Ignored by atmosphere and SkySphere modes.
        bool                            HDRIArtifactReady = true;
        /// @brief True only when this request has every valid input needed to bake.
        bool                            BakeInputsValid   = true;
    };

    /// @brief Result of a revision-tagged environment bake completion.
    enum class SkyEnvironmentBakeResult : uint8_t
    {
        Ignored = 0,
        Published,
        Failed,
        Discarded,
    };

    /// @brief Render-thread-owned lifetime model for one scene's environment.
    ///
    /// Main/editor threads submit copied SkyConfig revisions through the frame
    /// mailbox. The render thread claims only the newest pending request, pins a
    /// published snapshot for each submitted frame, and retires replacements once
    /// that frame's graphics timeline has completed.
    struct SkyEnvironment
    {
        static constexpr uint32_t                         MaxSnapshots                              = 8;
        static constexpr uint32_t                         MaxPendingFramePins                       = 16;
        /// @brief Bounds source-radiance churn from an animated primary sun.
        /// @details Non-celestial edits always schedule immediately. A running
        ///          day/night controller submits at most one new source bake for
        ///          every eight observed sky revisions; an availability change
        ///          or a direction change of at least five degrees bypasses the
        ///          budget so an intentional editor edit is never delayed.
        static constexpr uint64_t                         DynamicCelestialBakeRevisionInterval      = 8;
        static constexpr float                            DynamicCelestialBakeDirectionCosThreshold = 0.9961947f; // cos(5 degrees)

        /// @brief Establishes the engine-provided source and lighting fallbacks.
        void                                              Initialize(Textures::TextureHandle fallback_source, const EnvironmentLightingResources& fallback_lighting = {}, const EnvironmentLightingBakeSettings& bake_settings = {});

        /// @brief Sets the persistent environment-texture budget after fallbacks are created.
        void                                              ConfigureMemoryBudget(uint64_t budget_bytes, uint64_t fallback_bytes);
        /// @brief Reserves the complete next-snapshot allocation before the renderer creates it.
        /// @return False when the current snapshots plus this bake would exceed the configured cap.
        bool                                              ReserveActiveBakeMemory(uint64_t revision, uint64_t bytes);

        /// @brief Coalesces an immutable config revision while preserving its identity.
        /// @return False if the revision is stale or already observed.
        bool                                              SubmitConfig(const SkyConfig& config, uint64_t revision, const EnvironmentLightingBakeSettings& bake_settings = {}, const SkyCelestialLight& celestial_light = {}, uint64_t hdri_source_hash = 0, bool hdri_artifact_ready = true);

        /// @brief Claims the newest revision when no other bake is in flight.
        bool                                              TakeBakeRequest(SkyEnvironmentBakeRequest& out_request);

        /// @brief Associates a render-thread-created source texture with the active bake.
        bool                                              AttachBakeResource(uint64_t revision, Textures::TextureHandle source_radiance);
        /// @brief Associates static atmosphere LUTs with the active atmosphere bake.
        bool                                              AttachBakeAtmosphere(uint64_t revision, const AtmosphereStaticResources& atmosphere, bool owns_resources = true);
        /// @brief Associates newly allocated, unpublished IBL textures with the active bake.
        bool                                              AttachBakeLighting(uint64_t revision, const EnvironmentLightingResources& lighting);
        /// @brief Begins staged GPU IBL generation after the source upload is complete.
        bool                                              BeginGpuBake(uint64_t revision);
        /// @brief Returns whether the active stage can be registered into this frame graph.
        [[nodiscard]] bool                                CanRecordGpuBakeStage() const;
        /// @brief Records that the active stage emitted its dispatch commands this frame.
        bool                                              NotifyGpuBakeStageRecorded(uint64_t revision, SkyEnvironmentBakeStage stage);
        /// @brief Marks a successfully recorded GPU stage once its frame was submitted.
        bool                                              MarkGpuBakeStageSubmitted(uint64_t revision, uint64_t timeline_value);
        /// @brief Advances one completed stage, exposing a cancellation point before the next one.
        bool                                              AdvanceCompletedGpuBakeStage(uint64_t completed_timeline_value);
        [[nodiscard]] bool                                IsGpuBakeReadyToPublish() const;
        /// @brief Returns false once a newer config, quality setting, or HDRI
        /// source snapshot supersedes the active request.
        [[nodiscard]] bool                                IsActiveBakeCurrent() const;

        /// @brief Completes a bake, publishing it only if its revision is still current.
        SkyEnvironmentBakeResult                          CompleteBake(uint64_t revision, Textures::TextureHandle source_radiance, bool success, const EnvironmentLightingResources& lighting = {}, const AtmosphereStaticResources& atmosphere = {});

        /// @brief Pins the single snapshot that every sky consumer must use this frame.
        const SkyEnvironmentSnapshot*                     AcquireForFrame();
        /// @brief Releases the earliest frame pin after its graphics submission succeeds.
        void                                              ReleaseSubmittedFrame(uint64_t timeline_value);
        /// @brief Releases the earliest frame pin when the frame was never submitted.
        void                                              ReleaseCancelledFrame();

        /// @brief Returns all owned textures from one GPU-idle retired revision.
        bool                                              TakeRetiredSnapshot(uint64_t completed_timeline_value, SkyEnvironmentResources& out_resources);
        /// @brief Marks all non-fallback snapshots collectible after the device has gone idle.
        /// @return An unsubmitted active-bake resource the caller must retire separately.
        SkyEnvironmentResources                           Shutdown();

        [[nodiscard]] const SkyEnvironmentBakeRequest*    GetActiveBake() const;
        [[nodiscard]] Textures::TextureHandle             GetActiveBakeSource() const;
        [[nodiscard]] const AtmosphereStaticResources&    GetActiveBakeAtmosphere() const;
        [[nodiscard]] bool                                ActiveBakeOwnsAtmosphere() const;
        /// @brief Returns compatible static LUTs from the published atmosphere revision, if any.
        [[nodiscard]] const AtmosphereStaticResources*    FindReusableAtmosphere(const SkyConfig& config) const;
        [[nodiscard]] const EnvironmentLightingResources& GetActiveBakeLighting() const;
        [[nodiscard]] SkyEnvironmentBakeStage             GetActiveBakeStage() const;
        [[nodiscard]] const SkyEnvironmentSnapshot*       GetPublishedSnapshot() const;
        /// @brief Returns the latest presentation state without mutating a published resource snapshot.
        [[nodiscard]] const SkyConfig&                    GetPresentationConfig() const;
        /// @brief Returns the latest resolved light used by presentation-only sky modes.
        [[nodiscard]] const SkyCelestialLight&            GetPresentationCelestialLight() const;
        /// @brief Returns the engine-owned IBL fallback used by SkySphere.
        [[nodiscard]] const EnvironmentLightingResources& GetFallbackLighting() const;
        [[nodiscard]] SkyEnvironmentState                 GetState() const;
        [[nodiscard]] uint64_t                            GetLatestRevision() const;
        [[nodiscard]] uint64_t                            GetReservedMemoryBytes() const;
        [[nodiscard]] uint64_t                            GetMemoryBudgetBytes() const;

    private:
        [[nodiscard]] static bool       HasEquivalentAtmosphereStaticInputs(const SkyConfig& left, const SkyConfig& right);
        [[nodiscard]] static bool       HasEquivalentAtmosphereSourceInputs(const SkyConfig& left, const SkyConfig& right);
        [[nodiscard]] static bool       HasSignificantCelestialLightChange(const SkyCelestialLight& previous, const SkyCelestialLight& next);
        [[nodiscard]] static bool       HasEquivalentBakeInputs(const SkyConfig& left, const SkyCelestialLight& left_celestial_light, uint64_t left_hdri_source_hash, bool left_hdri_artifact_ready, const SkyConfig& right, const SkyCelestialLight& right_celestial_light, uint64_t right_hdri_source_hash, bool right_hdri_artifact_ready);
        [[nodiscard]] bool              IsAtmosphereShared(uint32_t excluded_snapshot_slot, const AtmosphereStaticResources& atmosphere) const;
        void                            ReleaseNextFramePin(uint64_t timeline_value);
        int32_t                         FindFreeSnapshotSlot() const;

        SkyEnvironmentSnapshot          m_snapshots[MaxSnapshots]              = {};
        SkyEnvironmentBakeRequest       m_pending_request                      = {};
        SkyEnvironmentBakeRequest       m_active_bake                          = {};
        AtmosphereStaticResources       m_active_bake_atmosphere               = {};
        Textures::TextureHandle         m_active_bake_source                   = {};
        EnvironmentLightingResources    m_active_bake_lighting                 = {};
        EnvironmentLightingResources    m_fallback_lighting                    = {};
        EnvironmentLightingBakeSettings m_bake_settings                        = {};
        SkyConfig                       m_presentation_config                  = {};
        SkyCelestialLight               m_presentation_celestial_light         = {};
        SkyConfig                       m_bake_config                          = {};
        SkyCelestialLight               m_bake_celestial_light                 = {};
        uint64_t                        m_bake_hdri_source_hash                = 0;
        uint16_t                        m_frame_pin_slots[MaxPendingFramePins] = {};
        uint32_t                        m_published_slot                       = 0;
        uint32_t                        m_frame_pin_head                       = 0;
        uint32_t                        m_frame_pin_count                      = 0;
        uint64_t                        m_latest_revision                      = 0;
        uint64_t                        m_latest_bake_revision                 = 0;
        uint64_t                        m_active_stage_timeline                = 0;
        uint64_t                        m_memory_budget_bytes                  = 0;
        uint64_t                        m_active_bake_reserved_memory_bytes    = 0;
        SkyEnvironmentBakeStage         m_active_bake_stage                    = SkyEnvironmentBakeStage::AwaitingSource;
        bool                            m_has_pending_request                  = false;
        bool                            m_has_active_bake                      = false;
        bool                            m_active_bake_owns_atmosphere          = false;
        bool                            m_bake_inputs_valid                    = false;
        bool                            m_bake_hdri_artifact_ready             = true;
        bool                            m_active_stage_submitted               = false;
        bool                            m_active_stage_recorded                = false;
        SkyEnvironmentState             m_state                                = SkyEnvironmentState::Fallback;
    };
} // namespace ZEngine::Rendering::Scenes
