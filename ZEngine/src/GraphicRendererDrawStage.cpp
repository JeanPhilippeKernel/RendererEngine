#include <pch.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererDrawStage.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererEndFrameBindingStage.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererPipelineContext.h>
#include <Rendering/Renderers/RenderCommand.h>

namespace ZEngine::Rendering::Renderers::Pipelines {
    GraphicRendererDrawStage::GraphicRendererDrawStage() {
        m_uniform_view_projection_buffer   = CreateScope<Buffers::UniformBuffer<Maths::Matrix4>>();
        m_uniform_camera_properties_buffer = CreateScope<Buffers::UniformBuffer<Maths::Vector4>>(1);
        m_next_stage                       = CreateRef<GraphicRendererEndFrameBindingStage>();
    }

    GraphicRendererDrawStage::~GraphicRendererDrawStage() {}

    void GraphicRendererDrawStage::Run(GraphicRendererPipelineInformation& information) {

        auto const  pipeline = reinterpret_cast<GraphicRendererPipelineContext*>(m_context);
        auto const  renderer = pipeline->GetRenderer();
        const auto& camera   = renderer->GetCamera();

        m_uniform_view_projection_buffer->SetData({camera->GetViewMatrix(), camera->GetProjectionMatrix()});
        m_uniform_camera_properties_buffer->SetData({Maths::Vector4(camera->GetPosition(), 1.0f)});

        m_uniform_view_projection_buffer->Bind();
        m_uniform_camera_properties_buffer->Bind();

        while (!information.GraphicStorageCollection.empty()) {
            const auto& storage = information.GraphicStorageCollection.front();

            const auto& geometry = storage.GetGeometry();

            const auto& shader_material_pair_collection = storage.GetShaderMaterialPairCollection();
            for (const auto& shader_material_pair : shader_material_pair_collection) {
                auto& shader   = std::get<0>(shader_material_pair);
                auto& material = std::get<1>(shader_material_pair);

                shader->CreateProgram();
                material->Apply(shader);

                shader->SetUniform("model", geometry->GetTransform());
            }

            for (const auto& shader_material_pair : shader_material_pair_collection) {
                auto& shader = std::get<0>(shader_material_pair);

                const auto& vertex_array = storage.GetVertexArray();
                RendererCommand::DrawIndexed(shader, vertex_array);
            }

            information.GraphicStorageCollection.pop();
        }
    }

} // namespace ZEngine::Rendering::Renderers::Pipelines
