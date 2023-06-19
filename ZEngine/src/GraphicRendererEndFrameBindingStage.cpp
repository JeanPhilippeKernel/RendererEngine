#include <pch.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererEndFrameBindingStage.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererCleanupStage.h>

namespace ZEngine::Rendering::Renderers::Pipelines {
    GraphicRendererEndFrameBindingStage::GraphicRendererEndFrameBindingStage() {
        m_next_stage = std::make_shared<GraphicRendererCleanupStage>();
    }

    GraphicRendererEndFrameBindingStage::~GraphicRendererEndFrameBindingStage() {}

    void GraphicRendererEndFrameBindingStage::Run(GraphicRendererPipelineInformation& information) {
        //auto const pipeline = reinterpret_cast<GraphicRendererPipelineContext*>(m_context);
        //auto const renderer = pipeline->GetRenderer();

        //renderer->GetFrameBuffer()->Unbind();
    }
} // namespace ZEngine::Rendering::Renderers::Pipelines
