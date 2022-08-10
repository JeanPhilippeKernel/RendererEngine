#include <pch.h>
#include <DockspaceUIComponent.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <Event/EventDispatcher.h>
#include <imgui/src/imgui_internal.h>
#include <MessageToken.h>
#include <Messengers/Messenger.h>

using namespace ZEngine::Components::UI::Event;

namespace Tetragrama::Components {

    DockspaceUIComponent::DockspaceUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility, false) {
        m_dockspace_node_flag = ImGuiDockNodeFlags_NoWindowMenuButton;
        m_window_flags        = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    }

    DockspaceUIComponent::~DockspaceUIComponent() {}

    bool DockspaceUIComponent::OnUIComponentRaised(UIComponentEvent& event) {
        return true;
    }

    void DockspaceUIComponent::Update(ZEngine::Core::TimeStep dt) {
        if (HasChildren()) {
            for (const auto& child : m_children) {
                child->Update(dt);
            }
        }
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

        ImGui::Begin(m_name.c_str(), (m_can_be_closed ? &m_can_be_closed : NULL), m_window_flags);

        ImGui::PopStyleVar();
        ImGui::PopStyleVar(2);

        ImGuiID dockspace_id = ImGui::GetID("Dockspace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), m_dockspace_node_flag);

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Scene")) {
                    Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(
                        EDITOR_RENDER_LAYER_SCENE_REQUEST_DESERIALIZATION, Messengers::GenericMessage<std::string>{""});
                }

                if (ImGui::MenuItem("Open Scene")) {
                    Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(
                        EDITOR_RENDER_LAYER_SCENE_REQUEST_DESERIALIZATION, Messengers::GenericMessage<std::string>{""});
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Save")) {
                    Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(
                        EDITOR_RENDER_LAYER_SCENE_REQUEST_SERIALIZATION, Messengers::GenericMessage<std::string>{""});
                }

                if (ImGui::MenuItem("Save As ...")) {
                    Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(
                        EDITOR_RENDER_LAYER_SCENE_REQUEST_SERIALIZATION, Messengers::GenericMessage<std::string>{""});
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Exit")) {
                    ZEngine::Event::WindowClosedEvent app_close_event{};
                    Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::GenericMessage<ZEngine::Event::WindowClosedEvent>>(
                        EDITOR_COMPONENT_DOCKSPACE_REQUEST_EXIT, Messengers::GenericMessage<ZEngine::Event::WindowClosedEvent>{std::move(app_close_event)});
                }

                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        if (HasChildren()) {
            std::for_each(std::begin(m_children), std::end(m_children), [this](const ZEngine::Ref<UIComponent>& item) { item->Render(); });
        }

        ImGui::End();
    }

    void DockspaceUIComponent::RequestExitMessageHandler(Messengers::GenericMessage<ZEngine::Event::WindowClosedEvent>& message) {
        if (!m_parent_layer.expired()) {
            auto layer = m_parent_layer.lock();
            layer->OnEvent(message.GetValue());

            ZENGINE_EDITOR_WARN("zEngine editor stopped")
        }
    }
} // namespace Tetragrama::Components
