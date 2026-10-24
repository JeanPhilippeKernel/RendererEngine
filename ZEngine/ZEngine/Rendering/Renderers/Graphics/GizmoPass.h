#pragma once
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/GizmoFrameData.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>

namespace ZEngine::Rendering::Renderers
{
    /// One instance recipe for all primitive kinds; vertex input matches gizmo.vert.
    struct GizmoGPUInstance
    {
        float    A[4] = {}, B[4] = {}, C[4] = {}, Parameters[4] = {};
        uint32_t Color = 0, Outline = 0;
    };
    static_assert(sizeof(GizmoGPUInstance) == 72);

    struct GizmoPass final : IRenderGraphCallbackPass
    {
        static constexpr uint32_t MaxInstances   = GizmoFrameData::MaxLines + GizmoFrameData::MaxTriangles + GizmoFrameData::MaxSprites;
        static constexpr uint32_t FramesInFlight = 3;

        void                      Initialize(Hardwares::VulkanDevicePtr device);
        void                      SetFrame(const GizmoFrameData& frame)
        {
            m_frame = frame;
        }
        /// Checked, allocation-free upload preparation. Public for CPU ABI/order tests.
        using MeshVisible = bool (*)(void*, const uuids::uuid&);
        /// Missing residency service omits mesh-bound decorations, not handles.
        /// The callback is synchronous, once per live group, and never retained.
        [[nodiscard]] static bool            Pack(const GizmoFrameData& frame, GizmoGPUInstance* output, uint32_t capacity, uint32_t* count, MeshVisible visible = nullptr, void* context = nullptr);

        Specifications::GraphicsPipelineDesc BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena) const override;
        bool                                 Register(Hardwares::VulkanDevicePtr device, cstring name, const RenderGraphFrameContext& context, RenderGraphResourceBuilderPtr builder, RenderGraphResourceInspectorPtr inspector) override;
        void                                 Prepare(Hardwares::VulkanDevicePtr device, Scenes::SceneDataPtr scene, RenderGraphResourceInspectorPtr inspector, RenderPasses::RenderPass* pass) override;
        void                                 Execute(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr inspector, Scenes::SceneDataPtr scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Hardwares::CommandBufferPtr command_buffer) override;
        bool                                 RecordDraw(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr inspector, Scenes::SceneDataPtr scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Hardwares::CommandBufferPtr command_buffer) override;
        bool                                 SupportsSecondaryRecording() const override
        {
            return true;
        }
        void Deinitialize(Hardwares::VulkanDevicePtr device) override;

    private:
        GizmoFrameData           m_frame                   = {};
        GizmoGPUInstance*        m_staging                 = nullptr;
        Core::Memory::BufferView m_buffers[FramesInFlight] = {};
        uint32_t                 m_count = 0, m_slot = 0;
        bool                     m_initialized = false;
    };
} // namespace ZEngine::Rendering::Renderers
