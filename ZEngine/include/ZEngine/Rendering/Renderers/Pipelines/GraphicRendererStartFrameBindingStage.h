#pragma once
#include <Rendering/Renderers/Pipelines/IGraphicRendererPipelineStage.h>

namespace ZEngine::Rendering::Renderers::Pipelines {
    class GraphicRendererStartFrameBindingStage : public IGraphicRendererPipelineStage {
    public:
        /**
         * Initialize a new GraphicRendererStartFrameBindingStage instance.
         */
        GraphicRendererStartFrameBindingStage();
        virtual ~GraphicRendererStartFrameBindingStage();

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
        virtual std::future<void> RunAsync(GraphicRendererPipelineInformation& information) override
        {
            return std::future<void>();
        }
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines
