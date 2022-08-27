#pragma once
#include <Rendering/Renderers/Pipelines/IGraphicRendererPipelineStage.h>
#include <Rendering/Buffers/UniformBuffer.h>

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

    private:
        Scope<Buffers::UniformBuffer<Maths::Matrix4>> m_uniform_view_projection_buffer;
        Scope<Buffers::UniformBuffer<Maths::Vector3>> m_uniform_camera_properties_buffer;
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines
