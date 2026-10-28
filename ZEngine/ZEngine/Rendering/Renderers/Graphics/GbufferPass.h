#pragma once
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>

namespace ZEngine::Rendering::Renderers
{
    struct ObjectIdPickRequest
    {
        uint32_t           X         = 0;
        uint32_t           Y         = 0;
        RGReadbackFn       Callback  = nullptr;
        RGReadbackCancelFn Cancelled = nullptr;
        void*              Context   = nullptr;

        [[nodiscard]] bool Valid() const
        {
            return Callback != nullptr && Context != nullptr;
        }
    };

    struct GbufferPass : public IRenderGraphCallbackPass
    {
        static constexpr uint32_t MaxObjectIdPickRequests = 2;

        /// @brief Schedules a one-pixel integer-ID readback for the next graph frame.
        void                      SetObjectIdPickRequest(const ObjectIdPickRequest& request)
        {
            if (request.Valid() && m_object_id_pick_count < MaxObjectIdPickRequests)
            {
                m_object_id_picks[m_object_id_pick_count++] = request;
            }
        }
        bool                                 Register(Hardwares::VulkanDevicePtr const device, cstring name, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        /// @brief Builds the static PSO recipe for G-buffer geometry.
        Specifications::GraphicsPipelineDesc BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena) const override;
        void                                 Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass) override;
        virtual void                         Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
        bool                                 RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
        bool                                 SupportsSecondaryRecording() const override
        {
            return true;
        }

    private:
        ObjectIdPickRequest m_object_id_picks[MaxObjectIdPickRequests] = {};
        uint32_t            m_object_id_pick_count                     = 0;
    };
} // namespace ZEngine::Rendering::Renderers
