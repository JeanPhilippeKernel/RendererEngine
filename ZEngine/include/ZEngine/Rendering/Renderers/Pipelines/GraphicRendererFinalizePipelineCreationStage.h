#pragma once
#include <future>
#include <Rendering/Renderers/Pipelines/IGraphicRendererPipelineStage.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    class GraphicRendererFinalizePipelineCreationStage : public IGraphicRendererPipelineStage
    {
    public:
        /**
         * Initialize a new GraphicRendererFinalizePipelineCreationStage instance.
         */
        GraphicRendererFinalizePipelineCreationStage();
        virtual ~GraphicRendererFinalizePipelineCreationStage();

        /**
         * Run Graphic renderer stage
         *
         * @param information A Graphic renderer pipeline information
         */
        virtual void Run(GraphicRendererPipelineInformation& information) override;

        /**
         * Run asynchronously renderer stage
         *
         * @param information Collection of shader information
         */
        virtual std::future<void> RunAsync(GraphicRendererPipelineInformation& information) override;
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines
