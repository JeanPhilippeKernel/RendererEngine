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
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines
