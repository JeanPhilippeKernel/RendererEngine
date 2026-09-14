#pragma once
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

    /// @brief Immutable GPU-resource snapshot selected by one rendered frame.
    struct SkyEnvironmentSnapshot
    {
        SkyConfig               Config          = {};
        Textures::TextureHandle SourceRadiance  = {};
        uint64_t                Revision        = 0;
        uint64_t                LastUseTimeline = 0;
        uint32_t                PinCount        = 0;
        SkyEnvironmentState     State           = SkyEnvironmentState::Fallback;
        bool                    IsFallback      = false;
        bool                    Retired         = false;
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
        static constexpr uint32_t                      MaxSnapshots        = 8;
        static constexpr uint32_t                      MaxPendingFramePins = 16;

        /// @brief Establishes the engine-provided cubemap fallback.
        void                                           Initialize(Textures::TextureHandle fallback_source);

        /// @brief Coalesces an immutable config revision while preserving its identity.
        /// @return False if the revision is stale or already observed.
        bool                                           SubmitConfig(const SkyConfig& config, uint64_t revision);

        /// @brief Claims the newest revision when no other bake is in flight.
        bool                                           TakeBakeRequest(SkyEnvironmentBakeRequest& out_request);

        /// @brief Associates a render-thread-created source texture with the active bake.
        bool                                           AttachBakeResource(uint64_t revision, Textures::TextureHandle source_radiance);

        /// @brief Completes a bake, publishing it only if its revision is still current.
        SkyEnvironmentBakeResult                       CompleteBake(uint64_t revision, Textures::TextureHandle source_radiance, bool success);

        /// @brief Pins the single snapshot that every sky consumer must use this frame.
        const SkyEnvironmentSnapshot*                  AcquireForFrame();
        /// @brief Releases the earliest frame pin after its graphics submission succeeds.
        void                                           ReleaseSubmittedFrame(uint64_t timeline_value);
        /// @brief Releases the earliest frame pin when the frame was never submitted.
        void                                           ReleaseCancelledFrame();

        /// @brief Returns one snapshot whose replacement is no longer referenced by the GPU.
        bool                                           TakeRetiredSnapshot(uint64_t completed_timeline_value, Textures::TextureHandle& out_source_radiance);
        /// @brief Marks all non-fallback snapshots collectible after the device has gone idle.
        /// @return An unsubmitted active-bake resource the caller must retire separately.
        Textures::TextureHandle                        Shutdown();

        [[nodiscard]] const SkyEnvironmentBakeRequest* GetActiveBake() const;
        [[nodiscard]] Textures::TextureHandle          GetActiveBakeSource() const;
        [[nodiscard]] const SkyEnvironmentSnapshot*    GetPublishedSnapshot() const;
        [[nodiscard]] SkyEnvironmentState              GetState() const;
        [[nodiscard]] uint64_t                         GetLatestRevision() const;

    private:
        void                      ReleaseNextFramePin(uint64_t timeline_value);
        int32_t                   FindFreeSnapshotSlot() const;

        SkyEnvironmentSnapshot    m_snapshots[MaxSnapshots]              = {};
        SkyEnvironmentBakeRequest m_pending_request                      = {};
        SkyEnvironmentBakeRequest m_active_bake                          = {};
        Textures::TextureHandle   m_active_bake_source                   = {};
        uint16_t                  m_frame_pin_slots[MaxPendingFramePins] = {};
        uint32_t                  m_published_slot                       = 0;
        uint32_t                  m_frame_pin_head                       = 0;
        uint32_t                  m_frame_pin_count                      = 0;
        uint64_t                  m_latest_revision                      = 0;
        bool                      m_has_pending_request                  = false;
        bool                      m_has_active_bake                      = false;
        SkyEnvironmentState       m_state                                = SkyEnvironmentState::Fallback;
    };
} // namespace ZEngine::Rendering::Scenes
