#include <pch.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererCleanupStage.h>

namespace ZEngine::Rendering::Renderers::Pipelines {
    GraphicRendererCleanupStage::GraphicRendererCleanupStage() {
        m_next_stage = nullptr;
    }

    GraphicRendererCleanupStage::~GraphicRendererCleanupStage() {}

    void GraphicRendererCleanupStage::Run(GraphicRendererPipelineInformation& information) {
        information.GeometryCollectionCount = 0;
        information.GeometryCollection.clear();
        information.GeometryCollection.shrink_to_fit();

        information.MaterialCollectionCount = 0;
        information.MaterialCollection.clear();
        information.MaterialCollection.shrink_to_fit();

        information.RecordCollection.clear();
        information.RecordCollection.shrink_to_fit();

        information.MeshCollection.clear();
        information.MeshCollection.shrink_to_fit();
    }

} // namespace ZEngine::Rendering::Renderers::Pipelines
