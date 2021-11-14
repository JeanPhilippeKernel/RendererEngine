#include <pch.h>
#include <SceneViewportUIComponent.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <Event/EventDispatcher.h>

using namespace ZEngine::Components::UI::Event;

namespace Tetragrama::Components {
    SceneViewportUIComponent::SceneViewportUIComponent(std::string_view name, bool visibility)  
        : UIComponent(name, visibility)
    {}
    
    SceneViewportUIComponent::~SceneViewportUIComponent() 
    {
    }

    bool SceneViewportUIComponent::OnUIComponentRaised(UIComponentEvent&) {
        return false;
    }

    void SceneViewportUIComponent::Render() {
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(main_viewport->WorkSize, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(main_viewport->WorkSize);
        ImGui::Begin(m_name.c_str(), &m_visibility, ImGuiWindowFlags_NoCollapse);
        ImGui::Text("Scene");
        ImGui::End();
    }
}