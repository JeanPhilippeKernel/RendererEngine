#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/GridPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers
{
    void GridPass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        // Y=0 — the vertex shader adds groundY + 0.001 epsilon for z-fighting prevention.
        // DrawVertex layout: x y z nx ny nz u v (8 floats = 32 bytes)
        // Normal points up (0,1,0); UVs mapped to world XZ position.
        static constexpr float verts[] = {
            -1000.f, 0.f, -1000.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1000.f, 0.f, -1000.f, 0.f, 1.f, 0.f, 1.f, 0.f, 1000.f, 0.f, 1000.f, 0.f, 1.f, 0.f, 1.f, 1.f, -1000.f, 0.f, 1000.f, 0.f, 1.f, 0.f, 0.f, 1.f,
        };
        static constexpr uint32_t idxs[] = {0, 1, 2, 2, 3, 0};

        auto*                     rrm    = ZEngine::Engine::GetContext()->RenderResourceManager;
        ZENGINE_VALIDATE_ASSERT(rrm, "GridPass::Setup: RenderResourceManager not available")
        rrm->RegisterBuiltinGeometry(verts, sizeof(verts), idxs, 6, m_vtx_offset, m_idx_offset);

        res_builder->ReadDepth(RendererResourceName::FrameDepthRenderTargetName);
        res_builder->UpdateColorAttachment(RendererResourceName::FrameColorRenderTargetName, {.LoadOp = LoadOperation::LOAD});
    }

    void GridPass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("Infinite-Grid-Pipeline")
                                 .SetInputBindingCount(1)
                                 .SetStride(0, sizeof(float) * 8)
                                 .SetRate(0, VK_VERTEX_INPUT_RATE_VERTEX)
                                 .SetInputAttributeCount(1)
                                 .SetLocation(0, 0)

                                 .SetBinding(0, 0)
                                 .SetFormat(0, Specifications::ImageFormat::R32G32B32_SFLOAT)
                                 .SetOffset(0, 0)

                                 .EnablePipelineDepthTest(true)
                                 .EnablePipelineDepthWrite(false)
                                 .PipelineDepthCompareOp(3)

                                 .EnablePipelineBlending(true)

                                 .SetCullMode(0)
                                 .UseShader("infinite_grid")
                                 .Detach();
            // clang-format off
            *output_pass = device->CreateRenderPass(std::move(pass_spec));
            // clang-format on
            (*output_pass)->Bake();
        }

        {
            auto* gp = static_cast<RenderPasses::GraphicPass*>(*output_pass);
            if (scene)
                gp->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
            gp->Verify();
        }
    }

    void GridPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        auto* gp  = static_cast<RenderPasses::GraphicPass*>(pass);
        auto* rrm = ZEngine::Engine::GetContext()->RenderResourceManager;
        command_buffer->BeginRenderPass(gp, framebuffer->Handle, false);
        command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->BindPipeline(gp->Pipeline);
        command_buffer->BindVertexBuffer(*rrm->GetBuiltinVertexBuffer());
        command_buffer->BindIndexBuffer(*rrm->GetBuiltinIndexBuffer(), VK_INDEX_TYPE_UINT32);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, scene ? &scene->CameraHeapOffset : nullptr, scene ? 1u : 0u);
        command_buffer->PushConstants(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GridPushConstantData), &PushData);
        command_buffer->DrawIndexed(6, 1, m_idx_offset, static_cast<int32_t>(m_vtx_offset), 0);
        command_buffer->EndRenderPass();
    }
} // namespace ZEngine::Rendering::Renderers
