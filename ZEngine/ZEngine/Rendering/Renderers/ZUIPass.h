#pragma once
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/UI/ZUIDrawList.h>
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

namespace ZEngine::UI
{
    struct ZUIContext;
}

namespace ZEngine::Rendering::Renderers
{
    /// @brief Frame-local draw data consumed by ZUIPass.
    struct ZUIRenderPayload
    {
        UI::ZUIDrawVtx*     Vtx              = nullptr;
        uint32_t            VtxCount         = 0;
        uint16_t*           Idx              = nullptr;
        uint32_t            IdxCount         = 0;
        UI::ZUIDrawListCmd* Cmds             = nullptr;
        uint32_t            CmdCount         = 0;
        /// @brief Physical-to-logical scale used for scissor rectangles.
        float               FramebufferScale = 1.f;
        /// @brief NDC scale: 2 / framebuffer extent.
        float               Scale[2]         = {};
        /// @brief NDC origin offset.
        float               Translate[2]     = {};
    };

    /// @brief Push-constant layout shared with zui_draw.vert.
    struct ZUIDrawPushConstant
    {
        float    Scale[2]     = {};
        float    Translate[2] = {};
        uint32_t TexIdx       = 0;
        /// @brief Physical-to-logical scale consumed by uFbScale.
        float    FbScale      = 1.f;
    };

    /// @brief Final graphics pass that uploads and draws the ZUI draw list.
    struct ZUIPass final : public IRenderGraphCallbackPass
    {
        /// @brief Number of frame-local upload-buffer slots owned by the pass.
        static constexpr uint32_t            FRAMES_IN_FLIGHT              = 3;

        /// @brief Graphics-pass state used to render the current ZUI payload.
        RenderPasses::GraphicPass*           DrawPass                      = nullptr;

        /// @brief Per-frame vertex upload buffers.
        Core::Memory::BufferView             VtxBHandles[FRAMES_IN_FLIGHT] = {};
        /// @brief Per-frame index upload buffers.
        Core::Memory::BufferView             IdxBHandles[FRAMES_IN_FLIGHT] = {};

        /// @brief Allocates the frame-local geometry buffers used by the pass.
        void                                 Initialize(Hardwares::VulkanDevicePtr device);

        /// @brief Translates the ZUI box tree into a flat draw payload.
        void                                 PreparePayload(UI::ZUIContext* ctx, ZUIRenderPayload* out, Core::Memory::ArenaAllocator* payload_arena);

        /// @brief Selects the payload recorded by the graph in the current frame.
        void                                 SetPayload(const ZUIRenderPayload* payload);

        /// @brief Declares ZUI resources and uploads for the current graph frame.
        bool                                 Register(Hardwares::VulkanDevicePtr const device, cstring name, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        /// @brief Builds the static PSO recipe for ZUI geometry.
        Specifications::GraphicsPipelineDesc BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena) const override;
        /// @brief Refreshes frame-local ZUI descriptor bindings.
        void                                 Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass) override;
        /// @brief Executes ZUI draw commands on the graph-selected command buffer.
        void                                 Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
        /// @brief Records the body-only ZUI draw commands for graph-managed rendering.
        bool                                 RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
        /// @brief Allows graph-managed secondary command-buffer recording.
        bool                                 SupportsSecondaryRecording() const override
        {
            return true;
        }
        /// @brief Releases ZUI GPU resources.
        void Deinitialize(Hardwares::VulkanDevicePtr const device) override;

    private:
        void ReleaseResources();

    private:
        Hardwares::VulkanDevicePtr Device        = nullptr;
        const ZUIRenderPayload*    m_payload     = nullptr;
        bool                       m_initialized = false;
        uint32_t                   m_vtx_upload  = 0;
        uint32_t                   m_idx_upload  = 0;
    };

    ZDEFINE_PTR(ZUIPass);

} // namespace ZEngine::Rendering::Renderers
