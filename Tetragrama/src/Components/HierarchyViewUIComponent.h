#pragma once
#include <string>
#include <ZEngine/ZEngine.h>
#include <Message.h>
#include <EditorCameraController.h>
#include <mutex>

namespace Tetragrama::Components {
    class HierarchyViewUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        HierarchyViewUIComponent(std::string_view name = "Hierarchy", bool visibility = true);
        virtual ~HierarchyViewUIComponent();

        void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render() override;


    public:
        void SceneAvailableMessageHandler(Messengers::GenericMessage<ZEngine::Ref<ZEngine::Rendering::Scenes::GraphicScene>>&);
        void EditorCameraAvailableMessageHandler(Messengers::GenericMessage<ZEngine::Ref<EditorCameraController>>&);
        void RequestStartOrPauseRenderMessageHandler(Messengers::GenericMessage<bool>&);

    protected:
        void         RenderEntityNode(ZEngine::Rendering::Entities::GraphicSceneEntity&&);
        virtual bool OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) override;

    private:
        ImGuiTreeNodeFlags                                         m_node_flag;
        bool                                                       m_is_node_opened{false};
        ZEngine::Rendering::Entities::GraphicSceneEntity           m_selected_scene_entity{entt::null, nullptr};
        ZEngine::WeakRef<ZEngine::Rendering::Scenes::GraphicScene> m_active_scene;
        ZEngine::WeakRef<EditorCameraController>                   m_active_editor_camera;
        int                                                        m_gizmo_operation{-1};
        std::mutex                                                 m_mutex;
    };
} // namespace Tetragrama::Components
