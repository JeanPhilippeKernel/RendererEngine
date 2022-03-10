#pragma once
#include <Core/IPipeline.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererPipelineInformation.h>

namespace ZEngine::Rendering::Renderers {
    class GraphicRenderer;
}

namespace ZEngine::Rendering::Renderers::Pipelines {
    class GraphicRendererPipelineContext : public Core::IPipelineContext {
    public:
        GraphicRendererPipelineContext();
        GraphicRendererPipelineContext(Renderers::GraphicRenderer* const renderer);

        virtual ~GraphicRendererPipelineContext() = default;

        void AttachRenderer(Renderers::GraphicRenderer* const renderer);
        void SetPipelineData(std::vector<Meshes::Mesh>&& data);

        Renderers::GraphicRenderer* const GetRenderer() const;

        GraphicRendererPipelineInformation&       GetPipelineData();
        const GraphicRendererPipelineInformation& GetPipelineData() const;

        void Execute();
        void Reset();

    private:
        Renderers::GraphicRenderer*        m_renderer;
        GraphicRendererPipelineInformation m_pipeline_data;
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines
