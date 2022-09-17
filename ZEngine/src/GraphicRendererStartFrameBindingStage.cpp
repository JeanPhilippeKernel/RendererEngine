#include <pch.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererStartFrameBindingStage.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererStorageGenerationStage.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererPipelineContext.h>
#include <Rendering/Renderers/RenderCommand.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    GraphicRendererStartFrameBindingStage::GraphicRendererStartFrameBindingStage()
    {
        m_next_stage = std::make_shared<GraphicRendererStorageGenerationStage>();
    }

    GraphicRendererStartFrameBindingStage::~GraphicRendererStartFrameBindingStage() {}

    void GraphicRendererStartFrameBindingStage::Run(GraphicRendererPipelineInformation& information)
    {
        auto const pipeline = reinterpret_cast<GraphicRendererPipelineContext*>(m_context);
        auto const renderer = pipeline->GetRenderer();

        auto framebuffer = renderer->GetFrameBuffer();
        framebuffer->Bind();

        Renderers::RendererCommand::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        framebuffer->ClearColorAttachments();
    }
} // namespace ZEngine::Rendering::Renderers::Pipelines
