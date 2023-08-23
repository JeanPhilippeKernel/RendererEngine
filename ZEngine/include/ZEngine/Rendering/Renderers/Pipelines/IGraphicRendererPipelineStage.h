#pragma once
#include <Core/IPipeline.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererPipelineInformation.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct IGraphicRendererPipelineStage : public Core::IPipelineStage
    {

        /**
         * Initialize a new IGraphicRendererPipelineStage instance.
         */
        IGraphicRendererPipelineStage()          = default;
        virtual ~IGraphicRendererPipelineStage() = default;

        /**
         * Run Graphic renderer stage
         *
         * @param information A Graphic renderer pipeline information
         */
        virtual void Run(GraphicRendererPipelineInformation& information) = 0;

        /**
         * Run asynchronously renderer stage
         *
         * @param information Collection of shader information
         */
        virtual std::future<void> RunAsync(GraphicRendererPipelineInformation& information) = 0;

    protected:
        std::recursive_mutex m_mutex;
    };

} // namespace ZEngine::Rendering::Renderers::Pipelines
