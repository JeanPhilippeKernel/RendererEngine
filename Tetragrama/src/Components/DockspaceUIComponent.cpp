#include <pch.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <DockspaceUIComponent.h>
#include <Event/EventDispatcher.h>

using namespace ZEngine::Components::UI::Event;

namespace Tetragrama::Components {

    DockspaceUIComponent::DockspaceUIComponent(std::string_view name, bool visibility)
        : UIComponent(name, visibility)    
    {
        m_dockspace_node_flag = ImGuiDockNodeFlags_None;
        m_window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    }

    DockspaceUIComponent::~DockspaceUIComponent()
    {
    }


    bool DockspaceUIComponent::OnUIComponentRaised(UIComponentEvent& event) {
        ZEngine::Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.Dispatch<UIComponentEvent>(std::bind(&DockspaceUIComponent::OnRequestCreateScene, this, std::placeholders::_1));
        event_dispatcher.Dispatch<UIComponentEvent>(std::bind(&DockspaceUIComponent::OnRequestOpenScene, this, std::placeholders::_1));
        event_dispatcher.Dispatch<UIComponentEvent>(std::bind(&DockspaceUIComponent::OnRequestSave, this, std::placeholders::_1));
        event_dispatcher.Dispatch<UIComponentEvent>(std::bind(&DockspaceUIComponent::OnRequestSaveAs, this, std::placeholders::_1));

        return true;
    }

    void DockspaceUIComponent::Render() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        m_window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        m_window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin(m_name.c_str(), &m_visibility, m_window_flags);
        
        ImGui::PopStyleVar();
        ImGui::PopStyleVar(2);

        ImGuiID dockspace_id = ImGui::GetID("EditorDockspaceUIComponent");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), m_dockspace_node_flag);

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Scene")) {
                    UIComponentEvent event{};
                    ZEngine::Event::EventDispatcher event_dispatcher(event);
                    event_dispatcher.Dispatch<UIComponentEvent>(std::bind(&DockspaceUIComponent::OnUIComponentRaised, this, std::placeholders::_1));
                }
                
                if (ImGui::MenuItem("Open Scene")) {
                    UIComponentEvent event{};
                    ZEngine::Event::EventDispatcher event_dispatcher(event);
                    event_dispatcher.Dispatch<UIComponentEvent>(std::bind(&DockspaceUIComponent::OnUIComponentRaised, this, std::placeholders::_1));
                }
                
                ImGui::Separator();
                
                if (ImGui::MenuItem("Save")) {
                    UIComponentEvent event{};
                    ZEngine::Event::EventDispatcher event_dispatcher(event);
                    event_dispatcher.Dispatch<UIComponentEvent>(std::bind(&DockspaceUIComponent::OnUIComponentRaised, this, std::placeholders::_1));
                }
                
                if (ImGui::MenuItem("Save As ...")) {
                    UIComponentEvent event{};
                    ZEngine::Event::EventDispatcher event_dispatcher(event);
                    event_dispatcher.Dispatch<UIComponentEvent>(std::bind(&DockspaceUIComponent::OnUIComponentRaised, this, std::placeholders::_1));
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Exit")) {
                    ZEngine::Event::WindowClosedEvent app_close_event{};
                    ZEngine::Event::EventDispatcher event_dispatcher(app_close_event);
                    event_dispatcher.Dispatch<ZEngine::Event::WindowClosedEvent>(std::bind(&DockspaceUIComponent::OnRequestExit, this, std::placeholders::_1));
                }

                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // Add child components here
        //ImGui::ShowDemoWindow(&m_visibility);

        ImGui::End();
    } 


    bool DockspaceUIComponent::OnRequestCreateScene(ZEngine::Components::UI::Event::UIComponentEvent& event) {
        return true;
    }
    
    bool DockspaceUIComponent::OnRequestOpenScene(ZEngine::Components::UI::Event::UIComponentEvent& event) {
        return true;
    }
    
    bool DockspaceUIComponent::OnRequestSave(ZEngine::Components::UI::Event::UIComponentEvent& event) {
        return true;
    }
    
    bool DockspaceUIComponent::OnRequestSaveAs(ZEngine::Components::UI::Event::UIComponentEvent& event) {
        return true;
    }

    bool DockspaceUIComponent::OnRequestExit(ZEngine::Event::WindowClosedEvent& event) {
        if (!m_parent_layer.expired()) {
            m_parent_layer.lock()->OnEvent(event);
        }
        return true;
    }
}