#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>
#include <ZEngine/Rendering/Renderers/Sky/SkySpherePass.h>

using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    void SkySpherePass::Setup(Hardwares::VulkanDevicePtr const device, cstring /*name*/, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        res_builder->ReadDepth(RendererResourceName::FrameDepthRenderTargetName);
        res_builder->WriteColorAttachment(RendererResourceName::FrameColorRenderTargetName, {});
    }

    void SkySpherePass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr /*res_inspector*/, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("SkySphere-Pipeline").SetInputBindingCount(0).EnablePipelineDepthTest(true).EnablePipelineDepthWrite(false).PipelineDepthCompareOp(static_cast<uint32_t>(VK_COMPARE_OP_LESS_OR_EQUAL)).SetCullMode(static_cast<uint32_t>(VK_CULL_MODE_NONE)).UseShader("sky_sphere").Detach();
            *output_pass   = device->CreateRenderPass(std::move(pass_spec));
            (*output_pass)->Bake();
        }

        static_cast<RenderPasses::GraphicPass*>(*output_pass)->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
    }

    void SkySpherePass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        auto*              gp = static_cast<RenderPasses::GraphicPass*>(pass);

        Sky::SkySpherePush pc = {};
        pc.HorizonColor[0]    = Config.HorizonColor.x;
        pc.HorizonColor[1]    = Config.HorizonColor.y;
        pc.HorizonColor[2]    = Config.HorizonColor.z;
        pc.HorizonColor[3]    = Config.HorizonColor.w;
        pc.ZenithColor[0]     = Config.ZenithColor.x;
        pc.ZenithColor[1]     = Config.ZenithColor.y;
        pc.ZenithColor[2]     = Config.ZenithColor.z;
        pc.ZenithColor[3]     = Config.ZenithColor.w;
        pc.GroundColor[0]     = Config.GroundColor.x;
        pc.GroundColor[1]     = Config.GroundColor.y;
        pc.GroundColor[2]     = Config.GroundColor.z;
        pc.GroundColor[3]     = Config.GroundColor.w;
        pc.SunDiscSize        = Config.SunDiscSize;
        pc.SunDiscIntensity   = Config.SunDiscIntensity;
        pc.HorizonSharpness   = Config.HorizonSharpness;
        pc.ShowSunDisc        = Config.ShowSunDisc ? 1.0f : 0.0f;
        pc.SunDirection[0]    = Config.SunDirection.x;
        pc.SunDirection[1]    = Config.SunDirection.y;
        pc.SunDirection[2]    = Config.SunDirection.z;
        pc.SunDirection[3]    = 0.0f;

        command_buffer->BeginRenderPass(gp, framebuffer->Handle, false);
        command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->BindPipeline(gp->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, scene ? &scene->CameraHeapOffset : nullptr, scene ? 1u : 0u);
        command_buffer->PushConstants(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Sky::SkySpherePush), &pc);
        command_buffer->Draw(3, 1, 0, 0);
        command_buffer->EndRenderPass();
    }
} // namespace ZEngine::Rendering::Renderers
