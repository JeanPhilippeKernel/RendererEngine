#include <pch.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererStorageGenerationStage.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererDrawStage.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererPipelineContext.h>
#include <Helpers/ContainerExtension.h>

namespace ZEngine::Rendering::Renderers::Pipelines {
    GraphicRendererStorageGenerationStage::GraphicRendererStorageGenerationStage() {
        m_next_stage = std::make_shared<GraphicRendererDrawStage>();
    }

    GraphicRendererStorageGenerationStage::~GraphicRendererStorageGenerationStage() {}

    void GraphicRendererStorageGenerationStage::Run(GraphicRendererPipelineInformation& information) {
        auto const  pipeline_context     = reinterpret_cast<GraphicRendererPipelineContext*>(m_context);
        const auto& renderer_information = pipeline_context->GetRenderer()->GetRendererInformation();

        std::unordered_set<uint32_t> geometry_indexes_set;
        std::transform(std::begin(information.RecordCollection), std::end(information.RecordCollection), std::inserter(geometry_indexes_set, std::end(geometry_indexes_set)),
            [&](const GraphicRendererInformationRecord& record) { return record.GeometryIndex; });

        for (auto geometry_index : geometry_indexes_set) {
            auto record_collection = Helpers::FindItems<GraphicRendererInformationRecord>(
                information.RecordCollection, [geometry_index](const GraphicRendererInformationRecord& record) { return record.GeometryIndex == geometry_index; });

            if (!record_collection.empty()) {
                std::vector<std::tuple<Ref<Shaders::Shader>, Ref<Materials::ShaderMaterial>>> shader_material_pair_collection;
                for (const auto& record : record_collection) {
                    auto shader_collection_it = std::begin(renderer_information->ShaderCollection);
                    std::advance(shader_collection_it, record.ShaderIndex);

                    Ref<Shaders::Shader> shader(Shaders::CreateShader(shader_collection_it->second.c_str(), true));

                    shader_material_pair_collection.emplace_back(std::make_pair(std::move(shader), information.MaterialCollection[record.MaterialIndex]));
                }

                // We use record_collection[0], because all of them have the same geometry index
                Storages::GraphicRendererStorage<float, unsigned int> storage(information.GeometryCollection[record_collection[0].GeometryIndex], renderer_information->GraphicStorageType);
                storage.AddShaderMaterialPair(std::move(shader_material_pair_collection));

                information.GraphicStorageCollection.push(std::move(storage));
            }
        }
    }

} // namespace ZEngine::Rendering::Renderers::Pipelines
