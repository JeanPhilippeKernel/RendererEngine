#include <pch.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererDisassembleStage.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererStartFrameBindingStage.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererPipelineContext.h>

namespace ZEngine::Rendering::Renderers::Pipelines {
    GraphicRendererDisassembleStage::GraphicRendererDisassembleStage() {
        m_next_stage = std::make_shared<GraphicRendererStartFrameBindingStage>();
    }

    GraphicRendererDisassembleStage::~GraphicRendererDisassembleStage() {}

    void GraphicRendererDisassembleStage::Run(GraphicRendererPipelineInformation& information) {
        auto const  pipeline_context  = reinterpret_cast<GraphicRendererPipelineContext*>(m_context);
        const auto& shader_collection = pipeline_context->GetRenderer()->GetRendererInformation()->ShaderCollection;

        std::for_each(std::begin(information.MeshCollection), std::end(information.MeshCollection), [&](const Meshes::Mesh& mesh) {
            GraphicRendererInformationRecord record;

            const auto& geometry             = mesh.GetGeometry();
            const auto& material             = mesh.GetMaterial();
            const auto  shader_built_in_type = material->GetShaderBuiltInType();

            information.GeometryCollection.push_back(geometry);
            record.GeometryIndex = information.GeometryCollectionCount++;

            information.MaterialCollection.push_back(material);
            record.MaterialIndex = information.MaterialCollectionCount++;

            size_t item_index{0};
            for (const auto& item : shader_collection) {
                if (item.first == shader_built_in_type) {
                    break;
                }
                item_index++;
            }
            record.ShaderIndex = item_index;

            information.RecordCollection.push_back(std::move(record));
        });
    }

} // namespace ZEngine::Rendering::Renderers::Pipelines
