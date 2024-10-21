#pragma once
#include <Components/DemoUIComponent.h>
#include <Components/DockspaceUIComponent.h>
#include <Components/HierarchyViewUIComponent.h>
#include <Components/InspectorViewUIComponent.h>
#include <Components/LogUIComponent.h>
#include <Components/ProjectViewUIComponent.h>
#include <Components/SceneViewportUIComponent.h>
#include <ImguiLayer.h>

namespace Tetragrama::Layers
{
    class UILayer : public ImguiLayer
    {
    public:
        UILayer(std::string_view name = "user interface layer") : ImguiLayer(name.data()) {}

        virtual ~UILayer();

        void Initialize() override;

    private:
        ZEngine::Ref<Components::DockspaceUIComponent>     m_dockspace_component;
        ZEngine::Ref<Components::SceneViewportUIComponent> m_scene_component;
        ZEngine::Ref<Components::LogUIComponent>           m_editor_log_component;
        ZEngine::Ref<Components::DemoUIComponent>          m_demo_component;
        ZEngine::Ref<Components::ProjectViewUIComponent>   m_project_view_component;
        ZEngine::Ref<Components::InspectorViewUIComponent> m_inspector_view_component;
        ZEngine::Ref<Components::HierarchyViewUIComponent> m_hierarchy_view_component;
    };

} // namespace Tetragrama::Layers
