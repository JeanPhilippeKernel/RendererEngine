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
        SourceMipChain,
        DiffuseIrradiance,
        SpecularEnvironment,
        ReadyToPublish,
    };

    /// @brief The resources that are owned as one immutable environment revision.
    struct SkyEnvironmentResources
    {
        Textures::TextureHandle      SourceRadiance = {};
        EnvironmentLightingResources Lighting       = {};

        [[nodiscard]] bool           Valid() const
        {
            return SourceRadiance.Valid() && Lighting.Valid();
        }
    };

    /// @brief Immutable GPU-resource snapshot selected by one rendered frame.
    struct SkyEnvironmentSnapshot
    {
        SkyConfig                    Config          = {};
        Textures::TextureHandle      SourceRadiance  = {};
        EnvironmentLightingResources Lighting        = {};
        uint64_t                     Revision        = 0;
        uint64_t                     LastUseTimeline = 0;
        uint32_t                     PinCount        = 0;
        SkyEnvironmentState          State           = SkyEnvironmentState::Fallback;
        bool                         IsFallback      = false;
        bool                         Retired         = false;
    };

    /// @brief Immutable bake input claimed by the render thread.
    struct SkyEnvironmentBakeRequest
    {
        SkyConfig Config   = {};
        uint64_t  Revision = 0;
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
        static constexpr uint32_t                         MaxSnapshots        = 8;
        static constexpr uint32_t                         MaxPendingFramePins = 16;

        /// @brief Establishes the engine-provided source and lighting fallbacks.
        void                                              Initialize(Textures::TextureHandle fallback_source, const EnvironmentLightingResources& fallback_lighting = {});

        /// @brief Coalesces an immutable config revision while preserving its identity.
        /// @return False if the revision is stale or already observed.
        bool                                              SubmitConfig(const SkyConfig& config, uint64_t revision);

        /// @brief Claims the newest revision when no other bake is in flight.
        bool                                              TakeBakeRequest(SkyEnvironmentBakeRequest& out_request);

        /// @brief Associates a render-thread-created source texture with the active bake.
        bool                                              AttachBakeResource(uint64_t revision, Textures::TextureHandle source_radiance);
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

        /// @brief Completes a bake, publishing it only if its revision is still current.
        SkyEnvironmentBakeResult                          CompleteBake(uint64_t revision, Textures::TextureHandle source_radiance, bool success, const EnvironmentLightingResources& lighting = {});

        /// @brief Pins the single snapshot that every sky consumer must use this frame.
        const SkyEnvironmentSnapshot*                     AcquireForFrame();
        /// @brief Releases the earliest frame pin after its graphics submission succeeds.
        void                                              ReleaseSubmittedFrame(uint64_t timeline_value);
        /// @brief Releases the earliest frame pin when the frame was never submitted.
        void                                              ReleaseCancelledFrame();

        /// @brief Returns one snapshot whose replacement is no longer referenced by the GPU.
        bool                                              TakeRetiredSnapshot(uint64_t completed_timeline_value, Textures::TextureHandle& out_source_radiance);
        /// @brief Returns all owned textures from one GPU-idle retired revision.
        bool                                              TakeRetiredSnapshot(uint64_t completed_timeline_value, SkyEnvironmentResources& out_resources);
        /// @brief Marks all non-fallback snapshots collectible after the device has gone idle.
        /// @return An unsubmitted active-bake resource the caller must retire separately.
        SkyEnvironmentResources                           Shutdown();

        [[nodiscard]] const SkyEnvironmentBakeRequest*    GetActiveBake() const;
        [[nodiscard]] Textures::TextureHandle             GetActiveBakeSource() const;
        [[nodiscard]] const EnvironmentLightingResources& GetActiveBakeLighting() const;
        [[nodiscard]] SkyEnvironmentBakeStage             GetActiveBakeStage() const;
        [[nodiscard]] const SkyEnvironmentSnapshot*       GetPublishedSnapshot() const;
        /// @brief Returns the latest presentation state without mutating a published resource snapshot.
        [[nodiscard]] const SkyConfig&                    GetPresentationConfig() const;
        [[nodiscard]] SkyEnvironmentState                 GetState() const;
        [[nodiscard]] uint64_t                            GetLatestRevision() const;

    private:
        [[nodiscard]] static bool    HasEquivalentBakeInputs(const SkyConfig& left, const SkyConfig& right);
        void                         ReleaseNextFramePin(uint64_t timeline_value);
        int32_t                      FindFreeSnapshotSlot() const;

        SkyEnvironmentSnapshot       m_snapshots[MaxSnapshots]              = {};
        SkyEnvironmentBakeRequest    m_pending_request                      = {};
        SkyEnvironmentBakeRequest    m_active_bake                          = {};
        Textures::TextureHandle      m_active_bake_source                   = {};
        EnvironmentLightingResources m_active_bake_lighting                 = {};
        EnvironmentLightingResources m_fallback_lighting                    = {};
        SkyConfig                    m_presentation_config                  = {};
        SkyConfig                    m_bake_config                          = {};
        uint16_t                     m_frame_pin_slots[MaxPendingFramePins] = {};
        uint32_t                     m_published_slot                       = 0;
        uint32_t                     m_frame_pin_head                       = 0;
        uint32_t                     m_frame_pin_count                      = 0;
        uint64_t                     m_latest_revision                      = 0;
        uint64_t                     m_latest_bake_revision                 = 0;
        uint64_t                     m_active_stage_timeline                = 0;
        SkyEnvironmentBakeStage      m_active_bake_stage                    = SkyEnvironmentBakeStage::AwaitingSource;
        bool                         m_has_pending_request                  = false;
        bool                         m_has_active_bake                      = false;
        bool                         m_active_stage_submitted               = false;
        bool                         m_active_stage_recorded                = false;
        SkyEnvironmentState          m_state                                = SkyEnvironmentState::Fallback;
    };
} // namespace ZEngine::Rendering::Scenes
