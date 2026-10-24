#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/Graphics/GizmoPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>
#include <cmath>

namespace ZEngine::Rendering::Renderers
{
    namespace
    {
        bool Finite(const Core::Maths::Vec3f& point)
        {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        }
        void Point(float (&out)[4], const Core::Maths::Vec3f& point)
        {
            out[0] = point.x;
            out[1] = point.y;
            out[2] = point.z;
        }
    } // namespace

    bool GizmoPass::Pack(const GizmoFrameData& frame, GizmoGPUInstance* output, uint32_t capacity, uint32_t* count, MeshVisible visible, void* context)
    {
        if (!count || !frame.ValidCounts() || !frame.ValidMeshLineGroups())
            return false;
        bool     group_visible[GizmoFrameData::MaxMeshLineGroups] = {};
        uint32_t total                                            = frame.TriangleCount + frame.LineCount + frame.SpriteCount;
        for (uint32_t i = 0; i < frame.MeshLineGroupCount; ++i)
        {
            group_visible[i] = visible && visible(context, frame.MeshLineGroups[i].MeshUUID);
            if (!group_visible[i])
                total -= frame.MeshLineGroups[i].Count;
        }
        if (total > capacity || (total && !output))
            return false;
        // Validate the complete recipe before touching output. No partial uploads.
        for (uint32_t i = 0; i < frame.TriangleCount; ++i)
        {
            const auto& p = frame.Triangles[i];
            if (!Finite(p.A) || !Finite(p.B) || !Finite(p.C) || (p.AntiAliasEdges & ~7u))
                return false;
        }
        for (uint32_t i = 0; i < frame.LineCount; ++i)
        {
            const auto& p = frame.Lines[i];
            if (!Finite(p.A) || !Finite(p.B) || !std::isfinite(p.Width) || p.Width <= 0.f || !std::isfinite(p.DashOn) || p.DashOn < 0.f || !std::isfinite(p.DashOff) || p.DashOff < 0.f || !std::isfinite(p.DashPhase) || !p.ValidJoins())
                return false;
        }
        for (uint32_t i = 0; i < frame.SpriteCount; ++i)
        {
            const auto& p = frame.Sprites[i];
            if (!Finite(p.Position) || !std::isfinite(p.Size) || p.Size <= 0.f || !std::isfinite(p.OutlineWidth) || p.OutlineWidth < 0.f || p.OutlineWidth > p.Size * 0.5f)
                return false;
        }
        uint32_t offset = 0;
        for (uint32_t i = 0; i < frame.TriangleCount; ++i)
        {
            const auto&      p        = frame.Triangles[i];
            GizmoGPUInstance instance = {};
            Point(instance.A, p.A);
            Point(instance.B, p.B);
            Point(instance.C, p.C);
            instance.Parameters[0] = float(p.AntiAliasEdges);
            instance.Color         = p.Color;
            output[offset++]       = instance;
        }
        uint32_t group = 0;
        for (uint32_t i = 0; i < frame.LineCount; ++i)
        {
            while (group < frame.MeshLineGroupCount && i >= frame.MeshLineGroups[group].First + frame.MeshLineGroups[group].Count)
                ++group;
            if (group < frame.MeshLineGroupCount && i >= frame.MeshLineGroups[group].First && !group_visible[group])
                continue;
            const auto&      p        = frame.Lines[i];
            GizmoGPUInstance instance = {};
            Point(instance.A, p.A);
            Point(instance.B, p.B);
            instance.A[3]          = p.Width;
            instance.B[3]          = p.DashOn;
            instance.C[3]          = p.DashOff;
            instance.C[0]          = p.StartJoinNormal.NormalX();
            instance.C[1]          = p.StartJoinNormal.NormalY();
            instance.C[2]          = p.EndJoinNormal.NormalX();
            instance.Parameters[0] = p.DashPhase;
            instance.Parameters[1] = float((p.RoundCaps ? 1u : 0u) | (p.StartJoinNormal.Enabled() ? 2u : 0u) | (p.EndJoinNormal.Enabled() ? 4u : 0u));
            instance.Parameters[2] = p.EndJoinNormal.NormalY();
            instance.Parameters[3] = 1.f;
            instance.Color         = p.Color;
            output[offset++]       = instance;
        }
        for (uint32_t i = 0; i < frame.SpriteCount; ++i)
        {
            const auto&      p        = frame.Sprites[i];
            GizmoGPUInstance instance = {};
            Point(instance.A, p.Position);
            instance.Parameters[0] = p.Round ? 1.f : 0.f;
            instance.Parameters[1] = p.Size;
            instance.Parameters[2] = p.OutlineWidth;
            instance.Parameters[3] = 2.f;
            instance.Color         = p.Fill;
            instance.Outline       = p.Outline;
            output[offset++]       = instance;
        }
        *count = total;
        return true;
    }

    void GizmoPass::Initialize(Hardwares::VulkanDevicePtr device)
    {
        if (m_initialized)
            return;
        ZENGINE_VALIDATE_ASSERT(device && device->SwapchainPtr->BufferredFrameCount <= FramesInFlight, "Invalid gizmo buffer frame count")
        m_staging = ZPushArray(device->Arena, GizmoGPUInstance, MaxInstances);
        for (uint32_t i = 0; i < device->SwapchainPtr->BufferredFrameCount; ++i)
            m_buffers[i] = device->GpuMem.AllocateBuffer(sizeof(GizmoGPUInstance) * MaxInstances, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, "GizmoInstances");
        m_initialized = true;
    }

    Specifications::GraphicsPipelineDesc GizmoPass::BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena) const
    {
        using namespace Specifications;
        GraphicsPipelineDesc desc          = {};
        desc.DebugName                     = "Gizmo-Pipeline";
        desc.EnableDepthTest               = false;
        desc.EnableDepthWrite              = false;
        desc.EnableBlending                = true;
        desc.CullMode                      = 0;
        desc.ShaderSpecificationValue.Name = "gizmo";
        desc.VertexInputBindingSpecifications.init(arena, 1, 1);
        desc.VertexInputBindingSpecifications[0] = {.Stride = sizeof(GizmoGPUInstance), .Rate = VK_VERTEX_INPUT_RATE_INSTANCE, .Binding = 0};
        desc.VertexInputAttributeSpecifications.init(arena, 6, 6);
        for (uint32_t i = 0; i < 4; ++i)
            desc.VertexInputAttributeSpecifications[i] = {.Location = i, .Binding = 0, .Offset = i * 16, .Format = ImageFormat::R32G32B32A32_SFLOAT};
        desc.VertexInputAttributeSpecifications[4] = {.Location = 4, .Binding = 0, .Offset = offsetof(GizmoGPUInstance, Color), .Format = ImageFormat::R8G8B8A8_UNORM};
        desc.VertexInputAttributeSpecifications[5] = {.Location = 5, .Binding = 0, .Offset = offsetof(GizmoGPUInstance, Outline), .Format = ImageFormat::R8G8B8A8_UNORM};
        return desc;
    }

    bool GizmoPass::Register(Hardwares::VulkanDevicePtr device, cstring, const RenderGraphFrameContext& context, RenderGraphResourceBuilderPtr builder, RenderGraphResourceInspectorPtr)
    {
        m_count = 0;
        if (m_frame.Empty() || !m_frame.ValidCounts())
            return false;
        if (!m_initialized || !device || !builder || context.FrameIndex >= FramesInFlight)
            return false;
        m_slot              = context.FrameIndex;
        auto*      rrm      = reinterpret_cast<RenderResourceManager*>(device->RRM);
        const auto resident = [](void* owner, const uuids::uuid& id) {
            const auto* resources = static_cast<RenderResourceManager*>(owner);
            return resources && resources->IsMeshResident(resources->FindMeshBuffer(id));
        };
        if (!rrm || !m_buffers[m_slot] || !Pack(m_frame, m_staging, MaxInstances, &m_count, resident, rrm) || !m_count)
        {
            m_count = 0;
            return false;
        }
        rrm->UpdateBuffer(m_buffers[m_slot], m_staging, sizeof(GizmoGPUInstance) * m_count);
        builder->UpdateColorAttachment(RendererResourceName::FrameColorRenderTargetName, {.LoadOp = Specifications::LoadOperation::LOAD});
        return true;
    }

    void GizmoPass::Prepare(Hardwares::VulkanDevicePtr, Scenes::SceneDataPtr scene, RenderGraphResourceInspectorPtr, RenderPasses::RenderPass* pass)
    {
        if (scene && pass)
            static_cast<RenderPasses::GraphicPass*>(pass)->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
    }

    void GizmoPass::Execute(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr inspector, Scenes::SceneDataPtr scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Hardwares::CommandBufferPtr command_buffer)
    {
        if (!m_count || !framebuffer || !command_buffer || !pass)
            return;
        if (!command_buffer->BeginRenderPass(static_cast<RenderPasses::GraphicPass*>(pass), framebuffer->Handle, false))
            return;
        RecordDraw(device, inspector, scene, pass, framebuffer, command_buffer);
        command_buffer->EndRenderPass();
    }

    bool GizmoPass::RecordDraw(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr, Scenes::SceneDataPtr scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext*, Hardwares::CommandBufferPtr command_buffer)
    {
        if (!m_count || !device || !scene || !pass || !command_buffer)
            return false;
        auto*       gp        = static_cast<RenderPasses::GraphicPass*>(pass);
        const float extent[2] = {float(gp->GetRenderAreaWidth()), float(gp->GetRenderAreaHeight())};
        if (extent[0] <= 0.f || extent[1] <= 0.f)
            return false;
        command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->BindPipeline(gp->Pipeline);
        command_buffer->BindVertexBuffer(m_buffers[m_slot]);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, &scene->CameraHeapOffset, 1);
        command_buffer->PushConstants(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(extent), extent);
        command_buffer->Draw(6, m_count, 0, 0);
        return true;
    }

    void GizmoPass::Deinitialize(Hardwares::VulkanDevicePtr device)
    {
        for (auto& buffer : m_buffers)
        {
            if (buffer && device)
                device->GpuMem.FreeBuffer(buffer);
            buffer = {};
        }
        m_staging     = nullptr;
        m_count       = 0;
        m_initialized = false;
        m_frame.Reset();
    }
} // namespace ZEngine::Rendering::Renderers
