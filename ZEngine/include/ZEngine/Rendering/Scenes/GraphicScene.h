#pragma once
#include <vector>
#include <functional>
#include <Rendering/Cameras/Camera.h>
#include <ZEngineDef.h>
#include <Core/IRenderable.h>
#include <Core/IInitializable.h>
#include <Core/IUpdatable.h>
#include <Controllers/ICameraController.h>
#include <Rendering/Meshes/Mesh.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Entities/GraphicSceneEntity.h>
#include <entt/entt.hpp>

namespace ZEngine::Rendering::Scenes {
    class GraphicScene : public Core::IInitializable, public Core::IUpdatable, public Core::IRenderable, public Core::IEventable {

    public:
        GraphicScene() {
            m_entity_registry = std::make_shared<entt::registry>();
        }

        virtual ~GraphicScene() = default;

        void         Initialize() override;
        void         Update(Core::TimeStep dt) override;
        virtual bool OnEvent(Event::CoreEvent&) override;
        virtual void Render() override;

        virtual void RequestNewSize(float, float);
        uint32_t     ToTextureRepresentation() const;

        void SetShouldReactToEvent(bool value);
        bool ShouldReactToEvent() const;

    public:
        Entities::GraphicSceneEntity CreateEntity(std::string_view entity_name = "empty entity");
        Entities::GraphicSceneEntity GetEntity(std::string_view entity_name);

        std::function<void(uint32_t)> OnSceneRenderCompleted;

    protected:
        Ref<Cameras::Camera>              m_scene_camera;
        Scope<Renderers::GraphicRenderer> m_renderer;
        std::vector<Ref<Meshes::Mesh>>    m_mesh_list;

    private:
        bool                    m_should_react_to_event{true};
        std::pair<float, float> m_scene_requested_size{0.0f, 0.0f};
        Ref<entt::registry>     m_entity_registry;
    };
} // namespace ZEngine::Rendering::Scenes
