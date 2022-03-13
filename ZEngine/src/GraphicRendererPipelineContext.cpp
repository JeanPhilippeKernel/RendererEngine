#include <pch.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererDisassembleStage.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererPipelineContext.h>

namespace ZEngine::Rendering::Renderers::Pipelines {

    GraphicRendererPipelineContext::GraphicRendererPipelineContext() : m_is_completed(false), m_renderer(nullptr), m_pipeline_data() {
        this->Reset();
    }

    GraphicRendererPipelineContext::GraphicRendererPipelineContext(Renderers::GraphicRenderer* const renderer) : GraphicRendererPipelineContext() {
        this->AttachRenderer(renderer);
    }

    void GraphicRendererPipelineContext::AttachRenderer(Renderers::GraphicRenderer* const renderer) {
        if (renderer) {
            m_renderer = renderer;
        }
    }

    void GraphicRendererPipelineContext::SetPipelineData(std::vector<Meshes::Mesh>&& data) {
        m_pipeline_data.MeshCollection = std::move(data);
    }

    Renderers::GraphicRenderer* const GraphicRendererPipelineContext::GetRenderer() const {
        assert(m_renderer != nullptr);
        return m_renderer;
    }

    bool GraphicRendererPipelineContext::HasPipelineData() const {
        return !m_pipeline_data.MeshCollection.empty();
    }

    void GraphicRendererPipelineContext::Execute() {
        while (m_running_stages) {

            IGraphicRendererPipelineStage* stage = reinterpret_cast<IGraphicRendererPipelineStage*>(m_stage.get());
            stage->Run(m_pipeline_data);

            const auto& stage_info = stage->GetInformation();
            if (stage_info.IsSuccess && m_stage->HasNext()) {
                m_stage->Next();
            } else {
                m_running_stages = false;
            }
        }
        m_is_completed = true;
        Reset();
    }

    bool GraphicRendererPipelineContext::IsCompleted() const {
        return m_is_completed;
    }

    void GraphicRendererPipelineContext::Reset() {
        m_running_stages = true;
        m_is_completed   = false;
        m_stage          = std::make_shared<GraphicRendererDisassembleStage>();
        m_pipeline_data  = {};

        m_stage->SetContext(this);
    }
} // namespace ZEngine::Rendering::Renderers::Pipelines
