#include <Rendering/Renderers/SceneRenderer.h>


namespace ZEngine::Rendering::Renderers
{
    Ref<Cameras::Camera> SceneRenderer::s_scene_camera;

    void SceneRenderer::SetCamera(const Ref<Cameras::Camera>& camera)
    {
        s_scene_camera = camera;
    }

    void SceneRenderer::Draw(const Ref<Rendering::Scenes::SceneRawData>& scene_data)
    {
        std::vector<glm::mat4>     tranform_collection  = {};
        std::vector<SceneDrawData> draw_data_collection = {};

        int node_count = scene_data->NodeHierarchyCollection.size();

        /*
         */
        for (int node_index = 0; node_index < node_count; ++node_index)
        {
            SceneDrawData draw_data  = {};
            draw_data.TransformIndex = node_index;
            if (scene_data->SceneNodeMeshMap.contains(node_index))
            {
                draw_data_collection.push_back(std::move(draw_data));
            }
        }

        /*
         */
        for (const auto& draw_data : draw_data_collection)
        {
            if (draw_data.TransformIndex > 0)
            {
                tranform_collection.push_back(scene_data->GlobalTransformCollection[draw_data.TransformIndex]);
            }
        }
    }
} // namespace ZEngine::Rendering::Renderers
