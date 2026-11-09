#include <ZEngine/Rendering/Renderers/Graphics/ObjectIdPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

namespace ZEngine::Rendering::Renderers
{
    bool ObjectIdPass::Register(Hardwares::VulkanDevicePtr device, cstring, const RenderGraphFrameContext& frame, RenderGraphResourceBuilderPtr builder, RenderGraphResourceInspectorPtr)
    {
        const uint32_t width  = frame.RenderWidth ? frame.RenderWidth : device->SwapchainPtr->SwapchainImageWidth;
        const uint32_t height = frame.RenderHeight ? frame.RenderHeight : device->SwapchainPtr->SwapchainImageHeight;
        builder->ReadBuffer(RendererBufferName::GlobalVertex, "VertexSB");
        builder->ReadBuffer(RendererBufferName::GlobalIndex, "IndexSB");
        builder->ReadBuffer(RendererBufferName::Transform, "TransformSB");
        builder->ReadBuffer(RendererBufferName::RenderData, "DrawDataSB");
        builder->ReadBuffer(RendererBufferName::Material, "MatSB");
        builder->ReadIndirectBuffer(RendererBufferName::CulledIndirect);
        builder->ReadBindless();
        const auto depth = DescribeSceneTarget(SceneRenderTarget::ObjectDepth, width, height);
        const auto color = DescribeSceneTarget(SceneRenderTarget::ObjectId, width, height);
        builder->WriteDepthAttachment(depth.Name, depth.Spec);
        const auto ids = builder->WriteColorAttachment(color.Name, color.Spec);
        RegisterObjectIdReadbacks(builder, ids, width, height);
        return true;
    }

    bool ObjectIdPass::TryBuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena, Specifications::GraphicsPipelineDesc* out) const
    {
        if (!out)
        {
            return false;
        }
        Specifications::GraphicsPipelineDesc desc = {};
        desc.DebugName                            = "Object-ID-Pipeline";
        desc.EnableDepthTest                      = true;
        desc.EnableDepthWrite                     = true;
        desc.EnableBlending                       = false;
        desc.ShaderSpecificationValue.Name        = "object_id";
        *out                                      = std::move(desc);
        return true;
    }

    bool ObjectIdPass::RecordDraw(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr, Scenes::SceneDataPtr scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext*, Hardwares::CommandBufferPtr commands)
    {
        if (!scene || !scene->IndirectCommandCount || !scene->RMMVertexHandle.IsValid())
            return true;
        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        commands->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        commands->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        commands->BindPipeline(gp->Pipeline);
        commands->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, &scene->CameraHeapOffset, 1u);
        commands->DrawIndirect(scene->GetCulledIndirectBuffer(device->SwapchainPtr->CurrentFrame->Index).Handle, 0, scene->IndirectCommandCount);
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
