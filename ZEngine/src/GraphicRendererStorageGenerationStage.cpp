#include <pch.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererStorageGenerationStage.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererDrawStage.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererPipelineContext.h>

namespace ZEngine::Rendering::Renderers::Pipelines {
    GraphicRendererStorageGenerationStage::GraphicRendererStorageGenerationStage() {
        m_next_stage = std::make_shared<GraphicRendererDrawStage>();
    }

    GraphicRendererStorageGenerationStage::~GraphicRendererStorageGenerationStage() {}

    void GraphicRendererStorageGenerationStage::Run(GraphicRendererPipelineInformation& information) {
        auto const  pipeline_context     = reinterpret_cast<GraphicRendererPipelineContext*>(m_context);
        const auto& renderer_information = pipeline_context->GetRenderer()->GetRendererInformation();

        std::for_each(std::begin(information.RecordCollection), std::end(information.RecordCollection), [&](const GraphicRendererInformationRecord& record) {
            auto shader_collection_it = std::begin(renderer_information->ShaderCollection);
            std::advance(shader_collection_it, record.ShaderIndex);

            Ref<Shaders::Shader> shader(Shaders::CreateShader(shader_collection_it->second.c_str(), true));

            Storages::GraphicRendererStorage<float, unsigned int> storage(
                shader, information.GeometryCollection[record.GeometryIndex], information.MaterialCollection[record.MaterialIndex], renderer_information->GraphicStorageType);

            information.GraphicStorageCollection.push(std::move(storage));
        });
    }

} // namespace ZEngine::Rendering::Renderers::Pipelines
