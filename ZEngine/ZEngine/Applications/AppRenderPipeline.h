#pragma once
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/GizmoFrameData.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/GridLabelData.h>
#include <ZEngine/Rendering/Renderers/Readback/RGReadbackRing.h>
#include <ZEngine/Rendering/Renderers/SelectionOutlineFrame.h>
#include <ZEngine/Rendering/Renderers/ZUIPass.h>
#include <ZEngine/Rendering/Scenes/SceneFrameData.h>
#include <ZEngine/Rendering/Scenes/SceneSubmissionProgress.h>

namespace ZEngine::UI
{
    struct ZUIContext;
}

namespace ZEngine::Applications
{
    /// @brief Main-thread owned object-pick lifetime carried by one immutable
    /// render mailbox state. The callbacks keep editor state opaque to the
    /// render thread while it assigns compact GPU tokens.
    struct RenderObjectPickRequest
    {
        using BeginFn                                        = bool (*)(void* context);
        using TokenFn                                        = void (*)(void* context, uint32_t token, ECS::EntityID entity);
        using SubmitFn                                       = void (*)(void* context);

        uint32_t                                 X           = 0;
        uint32_t                                 Y           = 0;
        void*                                    Context     = nullptr;
        BeginFn                                  Begin       = nullptr;
        TokenFn                                  RecordToken = nullptr;
        SubmitFn                                 Submitted   = nullptr;
        Rendering::Renderers::RGReadbackFn       Complete    = nullptr;
        Rendering::Renderers::RGReadbackCancelFn Cancelled   = nullptr;

        [[nodiscard]] bool                       Valid() const
        {
            return Context && Begin && RecordToken && Submitted && Complete && Cancelled;
        }
    };

    /// @brief Ownership state for a producer/consumer payload slot.
    enum class PayloadSlotState : uint32_t
    {
        Free    = 0,
        Writing = 1,
        Ready   = 2,
        Reading = 3,
    };

    /// @brief Latest render state transferred from the main thread to the render thread.
    struct RenderFrameState
    {
        Rendering::Cameras::CameraFrameData                          Camera           = {};
        Rendering::Scenes::SceneFrameData                            Scene            = {};
        Rendering::Renderers::GizmoFrameData                         Gizmo            = {};
        Rendering::Renderers::GridLabelFontSnapshot                  GridLabelFont    = {};
        // Immutable authoring copy captured alongside draw/light/grid data.
        Rendering::Scenes::SkyConfig                                 Sky              = {};
        Rendering::Scenes::SkyCelestialLight                         CelestialLight   = {};
        uint64_t                                                     SkyRevision      = 0;
        // Main-thread activation identity, copied with the immutable payload.
        uint64_t                                                     SceneGeneration  = 0;
        Rendering::RenderResourceManager::SceneResourcePreparationId SceneResources   = {};
        uint32_t                                                     RenderTargetW    = 0;
        uint32_t                                                     RenderTargetH    = 0;
        uint64_t                                                     ResizeSequence   = 0;
        /// Immutable entity membership for the editor outline. The renderer
        /// resolves this only while producing frame-local draw tokens.
        Rendering::Renderers::SelectionOutlineFrame                  SelectionOutline = {};
        /// One optional asynchronous object-ID readback. It carries only opaque
        /// callbacks and a physical image pixel; editor state remains main-thread owned.
        RenderObjectPickRequest                                      ObjectPick       = {};
        /// Hover has its own immutable token table and completion context. It
        /// must never consume or resolve a click-selection request.
        RenderObjectPickRequest                                      HoverPick        = {};
        // Physical window size, captured on the main thread with platform events.
        uint32_t                                                     FramebufferW     = 0;
        uint32_t                                                     FramebufferH     = 0;
        bool                                                         RenderOverlay    = false;
        bool                                                         ShowGrid         = true; // viewport preference, not an authored Grid setting mutation
    };

    /// @brief Immutable ZUI draw data retained while the render thread records it.
    struct OverlayPayload
    {
        Rendering::Renderers::ZUIRenderPayload ZUIOverlay = {};
        uint64_t                               Sequence   = 0;
    };

    struct AppRenderPipeline
    {
        /// Main-producer-thread reservation. The pipeline must outlive this token.
        /// Destruction cancels an unpublished frame; Publish only releases an
        /// already captured slot and cannot allocate, copy a scene or fail.
        class PreparedFrameState final
        {
        public:
            PreparedFrameState() = default;
            ~PreparedFrameState();
            PreparedFrameState(const PreparedFrameState&)            = delete;
            PreparedFrameState& operator=(const PreparedFrameState&) = delete;
            PreparedFrameState(PreparedFrameState&& other) noexcept;
            PreparedFrameState& operator=(PreparedFrameState&& other) noexcept;
            explicit            operator bool() const noexcept
            {
                return m_owner != nullptr;
            }
            void Reset() noexcept;
            void Publish() noexcept;

        private:
            friend struct AppRenderPipeline;
            AppRenderPipeline* m_owner = nullptr;
            uint32_t           m_slot  = 0;
        };

        static constexpr uint8_t                                        MaxFrameStateBufferCount                      = 3;
        static constexpr uint8_t                                        MaxOverlayBufferCount                         = 3;
        const uint8_t                                                   RenderMainThreadIndex                         = 0;
        uint8_t                                                         RenderWorkerThreadCount                       = 0;
        uint32_t                                                        CurrentFrameContextIndex                      = 0;
        RenderFrameState                                                FrameStates[MaxFrameStateBufferCount]         = {};
        PaddedAtomic<uint32_t>                                          FrameStateSlots[MaxFrameStateBufferCount]     = {};
        PaddedAtomic<uint64_t>                                          FrameStateSequences[MaxFrameStateBufferCount] = {};
        OverlayPayload                                                  OverlayPayloads[MaxOverlayBufferCount]        = {};
        PaddedAtomic<uint32_t>                                          OverlayPayloadStates[MaxOverlayBufferCount]   = {};
        PaddedAtomic<bool>                                              OverlayBuildAvailable                         = {.value = true};
        ZEngine::Core::Memory::ArenaAllocator                           ZUIPayloadArenas[MaxOverlayBufferCount]       = {};
        ZEngine::UI::ZUIContext*                                        ZUICtx                                        = nullptr;
        Hardwares::VulkanDevicePtr                                      Device                                        = nullptr;
        Rendering::Renderers::GraphicRendererPtr                        SceneRenderer                                 = nullptr;
        Rendering::Renderers::ZUIPassPtr                                ZUIRenderPass                                 = nullptr;
        Hardwares::CommandBufferPtr                                     CurrentCmdBuf                                 = nullptr;

        [[nodiscard]] const Rendering::Scenes::SceneSubmissionProgress& SceneSubmissions() const noexcept
        {
            return m_scene_submissions;
        }

        void Initialize(Hardwares::VulkanDevicePtr device);
        void Shutdown();

        bool ResizeRenderTarget(uint32_t w, uint32_t h);
        /// Refusal preserves the applied sequence so the same immutable mailbox
        /// request can retry. Do not record that view into mismatched old targets.
        bool ApplyRenderTargetResize(const RenderFrameState& state, uint64_t& applied_sequence);
        /// Render owner, before resize/recording. First-frame view remains tied
        /// to prepared storage even if newer mailbox frames have coalesced.
        bool ResolvePreparedSceneView(RenderFrameState& state) const;

        bool BeginFrame();
        void EndFrame();

        void RenderScene(const RenderFrameState& state, const Rendering::Renderers::ZUIRenderPayload* overlay);

        void BeginOverlayFrame(float dt = 0.f);
        void EndOverlayFrame();
        void FillOverlayPayload(OverlayPayload& payload, uint32_t payload_slot);

        /// @brief Reserve bounded scene buffers once, before either mailbox thread starts.
        void InitializeFrameStorage(Core::Memory::ArenaAllocator* arena, uint32_t max_instances = Rendering::Scenes::SceneFrameData::MaxInstances);
        /// @brief Capture source_scene on its main-thread owner, then publish immutable state.
        /// Returns false on capacity pressure; never publishes a partial scene snapshot.
        bool PublishFrameState(const RenderFrameState& state, const Rendering::Scenes::RenderScene* source_scene = nullptr);
        /// Capture into a reserved slot without exposing it to the consumer.
        /// Failure preserves out. A reserved slot cannot be reclaimed by normal
        /// publication or read-side coalescing. This is CPU handoff, not GPU readiness.
        bool PrepareFrameState(const RenderFrameState& state, const Rendering::Scenes::RenderScene* source_scene, PreparedFrameState* out);
        /// @brief Take and retain the newest complete state until the next read or explicit release.
        bool TryReadFrameState(RenderFrameState& state);
        void ReleaseFrameState();
        /// Main owner only, after render readers joined and before RRM shutdown.
        void ReleaseSceneResourceFramesAfterReadersJoined();

        /// @brief Claim a UI slot after the render thread completes a frame.
        bool BeginOverlayWrite(OverlayPayload*& payload, uint32_t& payload_slot);
        /// @brief Publish a completed UI payload to the render thread.
        void PublishOverlay(uint32_t payload_slot);
        /// @brief Claim the newest UI payload, without waiting for one.
        bool BeginOverlayRead(OverlayPayload*& payload, uint32_t& payload_slot);
        /// @brief Release a UI payload after it is no longer retained for rendering.
        void EndOverlayRead(uint32_t payload_slot);
        /// @brief Permit the main thread to build one replacement UI payload.
        void NotifyOverlayFrameComplete();

    private:
        Rendering::Scenes::SceneSubmissionProgress           m_scene_submissions = {};
        static void                                          OnSceneFrameSubmitted(void* context, Rendering::Primitives::Semaphore* timeline, uint64_t value);
        uint64_t                                             m_recorded_scene_generation                 = 0; // Render owner, until synchronous submit callback.
        uint32_t                                             m_recorded_scene_draw_count                 = 0;
        // Main-thread only. Stamps UI payloads so the render thread can select
        // the latest completed overlay rather than consume a FIFO backlog.
        uint64_t                                             m_next_frame_state_sequence                 = 1;
        uint32_t                                             m_prepared_frame_count                      = 0;
        uint64_t                                             m_next_overlay_sequence                     = 1;
        Rendering::Scenes::SceneDrawInstance*                m_frame_instances[MaxFrameStateBufferCount] = {};
        uint32_t                                             m_frame_instance_capacity                   = 0;
        bool                                                 m_snapshot_capacity_reported                = false;
        // Render-thread only. A retained scene's backing storage cannot be reused by the producer.
        uint32_t                                             m_retained_frame_slot                       = MaxFrameStateBufferCount;
        uint64_t                                             m_last_read_frame_sequence                  = 0;
        Rendering::RenderResourceManager::SceneResourceLease m_frame_resources[MaxFrameStateBufferCount] = {};
    };
    ZDEFINE_PTR(AppRenderPipeline);

} // namespace ZEngine::Applications
