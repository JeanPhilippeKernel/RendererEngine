#pragma once
#include <Rendering/Renderers/Pipelines/IGraphicRendererPipelineStage.h>

namespace ZEngine::Rendering::Renderers::Pipelines {
    class GraphicRendererDrawStage : public IGraphicRendererPipelineStage {
    public:
        /**
         * Initialize a new GraphicRendererDrawStage instance.
         */
        GraphicRendererDrawStage();
        virtual ~GraphicRendererDrawStage();

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
