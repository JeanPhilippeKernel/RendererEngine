#pragma once
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Renderers/GizmoFrameData.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/GridLabelData.h>
#include <ZEngine/Rendering/Renderers/ZUIPass.h>
#include <ZEngine/Rendering/Scenes/SceneFrameData.h>

namespace ZEngine::UI
{
    struct ZUIContext;
}

namespace ZEngine::Applications
{
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
        Rendering::Cameras::CameraFrameData         Camera         = {};
        Rendering::Scenes::SceneFrameData           Scene          = {};
        Rendering::Renderers::GizmoFrameData        Gizmo          = {};
        Rendering::Renderers::GridLabelFontSnapshot GridLabelFont  = {};
        // Immutable authoring copy captured alongside draw/light/grid data.
        Rendering::Scenes::SkyConfig                Sky            = {};
        Rendering::Scenes::SkyCelestialLight        CelestialLight = {};
        uint64_t                                    SkyRevision    = 0;
        uint32_t                                    RenderTargetW  = 0;
        uint32_t                                    RenderTargetH  = 0;
        uint64_t                                    ResizeSequence = 0;
        // Physical window size, captured on the main thread with platform events.
        uint32_t                                    FramebufferW   = 0;
        uint32_t                                    FramebufferH   = 0;
        bool                                        RenderOverlay  = false;
        bool                                        ShowGrid       = true; // viewport preference, not an authored Grid setting mutation
    };

    /// @brief Immutable ZUI draw data retained while the render thread records it.
    struct OverlayPayload
    {
        Rendering::Renderers::ZUIRenderPayload ZUIOverlay = {};
        uint64_t                               Sequence   = 0;
    };

    struct AppRenderPipeline
    {
        static constexpr uint8_t                 MaxFrameStateBufferCount                      = 3;
        static constexpr uint8_t                 MaxOverlayBufferCount                         = 3;
        const uint8_t                            RenderMainThreadIndex                         = 0;
        uint8_t                                  RenderWorkerThreadCount                       = 0;
        uint32_t                                 CurrentFrameContextIndex                      = 0;
        RenderFrameState                         FrameStates[MaxFrameStateBufferCount]         = {};
        PaddedAtomic<uint32_t>                   FrameStateSlots[MaxFrameStateBufferCount]     = {};
        PaddedAtomic<uint64_t>                   FrameStateSequences[MaxFrameStateBufferCount] = {};
        OverlayPayload                           OverlayPayloads[MaxOverlayBufferCount]        = {};
        PaddedAtomic<uint32_t>                   OverlayPayloadStates[MaxOverlayBufferCount]   = {};
        PaddedAtomic<bool>                       OverlayBuildAvailable                         = {.value = true};
        ZEngine::Core::Memory::ArenaAllocator    ZUIPayloadArenas[MaxOverlayBufferCount]       = {};
        ZEngine::UI::ZUIContext*                 ZUICtx                                        = nullptr;
        Hardwares::VulkanDevicePtr               Device                                        = nullptr;
        Rendering::Renderers::GraphicRendererPtr SceneRenderer                                 = nullptr;
        Rendering::Renderers::ZUIPassPtr         ZUIRenderPass                                 = nullptr;
        Hardwares::CommandBufferPtr              CurrentCmdBuf                                 = nullptr;

        void                                     Initialize(Hardwares::VulkanDevicePtr device);
        void                                     Shutdown();

        void                                     ResizeRenderTarget(uint32_t w, uint32_t h);

        bool                                     BeginFrame();
        void                                     EndFrame();

        void                                     RenderScene(const RenderFrameState& state, const Rendering::Renderers::ZUIRenderPayload* overlay);

        void                                     BeginOverlayFrame(float dt = 0.f);
        void                                     EndOverlayFrame();
        void                                     FillOverlayPayload(OverlayPayload& payload, uint32_t payload_slot);

        /// @brief Reserve bounded scene buffers once, before either mailbox thread starts.
        void                                     InitializeFrameStorage(Core::Memory::ArenaAllocator* arena, uint32_t max_instances = Rendering::Scenes::SceneFrameData::MaxInstances);
        /// @brief Capture source_scene on its main-thread owner, then publish immutable state.
        /// Returns false on capacity pressure; never publishes a partial scene snapshot.
        bool                                     PublishFrameState(const RenderFrameState& state, const Rendering::Scenes::RenderScene* source_scene = nullptr);
        /// @brief Take and retain the newest complete state until the next read or explicit release.
        bool                                     TryReadFrameState(RenderFrameState& state);
        void                                     ReleaseFrameState();

        /// @brief Claim a UI slot after the render thread completes a frame.
        bool                                     BeginOverlayWrite(OverlayPayload*& payload, uint32_t& payload_slot);
        /// @brief Publish a completed UI payload to the render thread.
        void                                     PublishOverlay(uint32_t payload_slot);
        /// @brief Claim the newest UI payload, without waiting for one.
        bool                                     BeginOverlayRead(OverlayPayload*& payload, uint32_t& payload_slot);
        /// @brief Release a UI payload after it is no longer retained for rendering.
        void                                     EndOverlayRead(uint32_t payload_slot);
        /// @brief Permit the main thread to build one replacement UI payload.
        void                                     NotifyOverlayFrameComplete();

    private:
        // Main-thread only. Stamps UI payloads so the render thread can select
        // the latest completed overlay rather than consume a FIFO backlog.
        uint64_t                              m_next_frame_state_sequence                 = 1;
        uint64_t                              m_next_overlay_sequence                     = 1;
        Rendering::Scenes::SceneDrawInstance* m_frame_instances[MaxFrameStateBufferCount] = {};
        uint32_t                              m_frame_instance_capacity                   = 0;
        bool                                  m_snapshot_capacity_reported                = false;
        // Render-thread only. A retained scene's backing storage cannot be reused by the producer.
        uint32_t                              m_retained_frame_slot                       = MaxFrameStateBufferCount;
        uint64_t                              m_last_read_frame_sequence                  = 0;
    };
    ZDEFINE_PTR(AppRenderPipeline);

} // namespace ZEngine::Applications
