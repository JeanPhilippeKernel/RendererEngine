#pragma once
#include <utility>
#include <ZEngineDef.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <Rendering/Cameras/Camera.h>

namespace ZEngine::Rendering::Renderers
{
    struct SceneDrawData
    {
        int TransformIndex{-1};
        int MaterialIndex{-1};
    };

    struct SceneRenderer
    {
        static void SetCamera(const Ref<Cameras::Camera>& camera);
        static void Draw(const Ref<Rendering::Scenes::SceneRawData>& scene_data);

    private:
        static Ref<Cameras::Camera> s_scene_camera;
    };
} // namespace ZEngine::Rendering::Renderers