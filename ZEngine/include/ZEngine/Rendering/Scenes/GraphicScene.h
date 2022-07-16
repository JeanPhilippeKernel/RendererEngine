#pragma once
#include <vector>
#include <functional>
#include <Rendering/Cameras/Camera.h>
#include <ZEngineDef.h>
#include <Core/IRenderable.h>
#include <Core/IInitializable.h>
#include <Controllers/ICameraController.h>
#include <Rendering/Meshes/Mesh.h>
#include <Rendering/Renderers/GraphicRenderer.h>

namespace ZEngine::Rendering::Scenes {
    class GraphicScene : public Core::IInitializable, public Core::IRenderable {

    public:
        explicit GraphicScene(Controllers::ICameraController* const controller) : m_camera_controller(nullptr), m_renderer(nullptr) {
            m_camera_controller.reset(controller);
        }

        virtual ~GraphicScene() = default;

        void         Initialize() override;
        virtual void Render() override;

        void     RequestNewSize(uint32_t, uint32_t);
        uint32_t ToTextureRepresentation() const;

        void Add(Ref<Meshes::Mesh>& mesh);
        void Add(const std::vector<Ref<Meshes::Mesh>>& meshes);

        void SetShouldReactToEvent(bool value);
        bool ShouldReactToEvent() const;

        virtual const Scope<Controllers::ICameraController>& GetCameraController() const;

    public:
        std::function<void(uint32_t)> OnSceneRenderCompleted;

    protected:
        Scope<Controllers::ICameraController> m_camera_controller;
        Scope<Renderers::GraphicRenderer>     m_renderer;
        std::vector<Ref<Meshes::Mesh>>        m_mesh_list;

    private:
        bool m_should_react_to_event{true};
        std::queue<std::function<void(void)>> m_pending_operation;
    };
} // namespace ZEngine::Rendering::Scenes
