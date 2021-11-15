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
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(m_name.c_str(), &m_visibility, ImGuiWindowFlags_NoCollapse);
       
        ImGui::PopStyleVar();
        ImGui::PopStyleVar(2);
        
        if (HasParentLayer()) {
            auto layer = m_parent_layer.lock();
            const auto info = layer->GetLayerInformation();
            ImGui::Image((void*)info.Data, ImVec2{1080, 800}, ImVec2(0,1), ImVec2(1, 0));
        }
        
        ImGui::End();
    }
}