#pragma once
#include <ZEngine/ZEngine.h>
#include <Components/DockspaceUIComponent.h>
#include <Components/SceneViewportUIComponent.h>
#include <Components/LogUIComponent.h>
#include <Components/DemoUIComponent.h>
#include <Components/ProjectViewUIComponent.h>
#include <Components/InspectorViewUIComponent.h>
#include <Components/HierarchyViewUIComponent.h>

namespace Tetragrama::Layers
{
    class UILayer : public ZEngine::Layers::ImguiLayer
    {
    public:
        UILayer(std::string_view name = "user interface layer") : ImguiLayer(name.data()) {}

        virtual ~UILayer();

        void Initialize() override;
        // void Deinitialize() override;

    private:
        ZEngine::Ref<ZEngine::Components::UI::UIComponent> m_dockspace_component;
        ZEngine::Ref<ZEngine::Components::UI::UIComponent> m_scene_component;
        ZEngine::Ref<ZEngine::Components::UI::UIComponent> m_editor_log_component;
        ZEngine::Ref<ZEngine::Components::UI::UIComponent> m_demo_component;
        ZEngine::Ref<ZEngine::Components::UI::UIComponent> m_project_view_component;
        ZEngine::Ref<ZEngine::Components::UI::UIComponent> m_inspector_view_component;
        ZEngine::Ref<ZEngine::Components::UI::UIComponent> m_hierarchy_view_component;
    };

} // namespace Tetragrama::Layers
