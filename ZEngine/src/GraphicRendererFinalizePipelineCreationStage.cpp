#include <pch.h>
#include <vulkan/vulkan.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererFinalizePipelineCreationStage.h>
#include <Rendering/Renderers/GraphicRenderer3D.h>
#include <Core/Coroutine.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererDrawStage.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    GraphicRendererFinalizePipelineCreationStage::GraphicRendererFinalizePipelineCreationStage()
    {
        m_next_stage = CreateRef<GraphicRendererDrawStage>();
    }

    GraphicRendererFinalizePipelineCreationStage::~GraphicRendererFinalizePipelineCreationStage() {}

    void GraphicRendererFinalizePipelineCreationStage::Run(GraphicRendererPipelineInformation& information) {}

    std::future<void> GraphicRendererFinalizePipelineCreationStage::RunAsync(GraphicRendererPipelineInformation& information)
    {
        co_return;
    }
} // namespace ZEngine::Rendering::Renderers::Pipelines
