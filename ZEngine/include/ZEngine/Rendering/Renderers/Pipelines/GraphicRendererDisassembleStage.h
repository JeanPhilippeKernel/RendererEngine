#pragma once
#include <vector>
#include <Rendering/Meshes/Mesh.h>
#include <Rendering/Renderers/Pipelines/IGraphicRendererPipelineStage.h>

namespace ZEngine::Rendering::Renderers::Pipelines {
    class GraphicRendererDisassembleStage : public IGraphicRendererPipelineStage {
    public:
        /**
         * Initialize a new GraphicRendererDisassembleStage instance.
         */
        GraphicRendererDisassembleStage();
        virtual ~GraphicRendererDisassembleStage();

        /**
         * Run Graphic renderer stage
         *
         * @param information A Graphic renderer pipeline information
         */
        virtual void Run(GraphicRendererPipelineInformation& information) override;

        /**
         * Run Graphic renderer stage
         *
         * @param information A Graphic renderer pipeline information
         */
        virtual std::future<void> RunAsync(GraphicRendererPipelineInformation& information) override;
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines
