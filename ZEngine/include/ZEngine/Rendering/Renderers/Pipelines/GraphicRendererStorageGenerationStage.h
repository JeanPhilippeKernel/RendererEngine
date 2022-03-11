#pragma once
#include <Rendering/Renderers/Pipelines/IGraphicRendererPipelineStage.h>

namespace ZEngine::Rendering::Renderers::Pipelines {
    class GraphicRendererStorageGenerationStage : public IGraphicRendererPipelineStage {
    public:
        /**
         * Initialize a new GraphicRendererStorageGenerationStage instance.
         */
        GraphicRendererStorageGenerationStage();
        virtual ~GraphicRendererStorageGenerationStage();

        /**
         * Run Graphic renderer stage
         *
         * @param information A Graphic renderer information
         */
        virtual void Run(GraphicRendererPipelineInformation& information) override;
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines
