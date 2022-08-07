#pragma once
#include <string>
#include <ZEngine/ZEngine.h>
#include <Message.h>

namespace Tetragrama::Components {
    class HierarchyViewUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        HierarchyViewUIComponent(std::string_view name = "HierarchyViewUIComponent", bool visibility = true);
        virtual ~HierarchyViewUIComponent();

        void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render() override;


    public:
        void SceneAvailableMessageHandler(Messengers::GenericMessage<ZEngine::Ref<ZEngine::Rendering::Scenes::GraphicScene>>&);

    protected:
        void         RenderEntityNode(ZEngine::Rendering::Entities::GraphicSceneEntity&&);
        virtual bool OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) override;

    private:
        ImGuiTreeNodeFlags                                         m_node_flag;
        bool                                                       m_is_node_opened{false};
        ZEngine::Rendering::Entities::GraphicSceneEntity           m_selected_scene_entity{entt::null, nullptr};
        ZEngine::WeakRef<ZEngine::Rendering::Scenes::GraphicScene> m_active_scene;
    };
} // namespace Tetragrama::Components
