#include <ZEngine/Rendering/Renderers/Graphics/GizmoPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Core::Maths;
using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    namespace
    {
        enum class PrimitiveType : uint32_t
        {
            Axis = 0,
            Plane,
            Ring,
            Cube,
        };

        struct GizmoPrimitivePush
        {
            Mat4f    Model      = Identity<Mat4f>();
            float    Color[4]   = {1.f, 1.f, 1.f, 1.f};
            uint32_t Primitive  = 0;
            uint32_t Padding[3] = {};
        };

        static_assert(sizeof(GizmoPrimitivePush) == 96, "Gizmo primitive push constants must match gizmo shaders");

        Mat4f ScaledBasis(const GizmoRenderState& state)
        {
            Mat4f result = state.Basis;
            for (size_t column = 0; column < 3; ++column)
                for (size_t row = 0; row < 3; ++row)
                    result(row, column) *= state.Scale;
            return result;
        }

        Mat4f Reorient(const Mat4f& source, uint32_t primary, uint32_t secondary, uint32_t tertiary)
        {
            Mat4f result = Identity<Mat4f>();
            for (size_t row = 0; row < 3; ++row)
            {
                result(row, 0) = source(row, primary);
                result(row, 1) = source(row, secondary);
                result(row, 2) = source(row, tertiary);
                result(row, 3) = source(row, 3);
            }
            return result;
        }

        void DrawPrimitive(Hardwares::CommandBufferPtr const command_buffer, const Mat4f& model, const float (&color)[4], PrimitiveType primitive, uint32_t vertex_count)
        {
            GizmoPrimitivePush push = {};
            push.Model              = model;
            for (uint32_t index = 0; index < 4; ++index)
                push.Color[index] = color[index];
            push.Primitive = static_cast<uint32_t>(primitive);
            command_buffer->PushConstants(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
            command_buffer->Draw(vertex_count, 1, 0, 0);
        }

        const float (&ColorFor(uint32_t axis, GizmoHandle handle, const GizmoRenderState& state))[4]
        {
            static constexpr float axis_colors[3][4] = {
                {0.93f, 0.24f, 0.24f, 0.95f},
                {0.25f, 0.88f, 0.33f, 0.95f},
                {0.28f, 0.52f, 1.00f, 0.95f},
            };
            static constexpr float active_color[4] = {1.00f, 0.74f, 0.16f, 1.00f};
            if (state.Active == handle || state.Hovered == handle)
                return active_color;
            return axis_colors[axis];
        }
    } // namespace

    GraphicsPipelineDesc GizmoPass::BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* /*arena*/) const
    {
        GraphicsPipelineDesc description          = {};
        description.DebugName                     = "Editor-Gizmo-Pipeline";
        description.EnableDepthTest               = true;
        description.EnableDepthWrite              = false;
        description.DepthCompareOp                = VK_COMPARE_OP_LESS_OR_EQUAL;
        description.EnableBlending                = true;
        description.CullMode                      = 0;
        description.ShaderSpecificationValue.Name = "editor_gizmo";
        return description;
    }

    bool GizmoPass::Register(Hardwares::VulkanDevicePtr const /*device*/, cstring /*name*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const builder, RenderGraphResourceInspectorPtr /*inspector*/)
    {
        // The graph is compiled before the editor has a selection. Keep this
        // pass registered so a later value-only render snapshot can make it
        // visible without rebuilding the complete render graph.
        if (!builder)
            return false;
        builder->ReadDepth(RendererResourceName::FrameDepthRenderTargetName);
        builder->UpdateColorAttachment(RendererResourceName::FrameColorRenderTargetName, {.LoadOp = LoadOperation::LOAD});
        return true;
    }

    void GizmoPass::Prepare(Hardwares::VulkanDevicePtr const /*device*/, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr /*inspector*/, RenderPasses::RenderPass* const pass)
    {
        if (!scene || !pass)
            return;
        static_cast<RenderPasses::GraphicPass*>(pass)->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
    }

    void GizmoPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!State.Visible || !pass || !framebuffer || framebuffer->Handle == VK_NULL_HANDLE)
            return;
        auto* graphic_pass = static_cast<RenderPasses::GraphicPass*>(pass);
        if (!command_buffer->BeginRenderPass(graphic_pass, framebuffer->Handle, false))
            return;
        RecordDraw(device, inspector, scene, pass, framebuffer, command_buffer);
        command_buffer->EndRenderPass();
    }

    bool GizmoPass::RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*inspector*/, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!State.Visible || !scene || !pass || !device || !device->SwapchainPtr || !device->SwapchainPtr->CurrentFrame)
            return false;

        auto* graphic_pass = static_cast<RenderPasses::GraphicPass*>(pass);
        command_buffer->SetViewport(graphic_pass->GetRenderAreaWidth(), graphic_pass->GetRenderAreaHeight());
        command_buffer->SetScissor(graphic_pass->GetRenderAreaWidth(), graphic_pass->GetRenderAreaHeight());
        command_buffer->BindPipeline(graphic_pass->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, &scene->CameraHeapOffset, 1u);

        const Mat4f basis          = ScaledBasis(State);
        const Mat4f axis_models[3] = {
            Reorient(basis, 0, 1, 2),
            Reorient(basis, 1, 2, 0),
            Reorient(basis, 2, 0, 1),
        };
        const GizmoHandle axis_handles[3] = {GizmoHandle::AxisX, GizmoHandle::AxisY, GizmoHandle::AxisZ};

        if (State.Operation == GizmoOperation::Translate || State.Operation == GizmoOperation::Scale)
        {
            for (uint32_t axis = 0; axis < 3; ++axis)
                DrawPrimitive(command_buffer, axis_models[axis], ColorFor(axis, axis_handles[axis], State), PrimitiveType::Axis, 48);

            if (State.Operation == GizmoOperation::Translate)
            {
                const Mat4f plane_models[3] = {
                    Reorient(basis, 0, 1, 2),
                    Reorient(basis, 0, 2, 1),
                    Reorient(basis, 1, 2, 0),
                };
                const GizmoHandle plane_handles[3] = {GizmoHandle::PlaneXY, GizmoHandle::PlaneXZ, GizmoHandle::PlaneYZ};
                for (uint32_t plane = 0; plane < 3; ++plane)
                    DrawPrimitive(command_buffer, plane_models[plane], ColorFor(plane, plane_handles[plane], State), PrimitiveType::Plane, 6);
            }
            else
            {
                static constexpr float uniform_color[4] = {0.88f, 0.88f, 0.88f, 0.96f};
                const float (&color)[4]                 = (State.Active == GizmoHandle::Uniform || State.Hovered == GizmoHandle::Uniform) ? ColorFor(0, GizmoHandle::Uniform, State) : uniform_color;
                DrawPrimitive(command_buffer, basis, color, PrimitiveType::Cube, 36);
            }
        }
        else if (State.Operation == GizmoOperation::Rotate)
        {
            for (uint32_t axis = 0; axis < 3; ++axis)
                DrawPrimitive(command_buffer, axis_models[axis], ColorFor(axis, axis_handles[axis], State), PrimitiveType::Ring, 288);
        }
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
