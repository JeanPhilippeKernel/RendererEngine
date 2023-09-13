#include <pch.h>
#include <DockspaceUIComponent.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <Event/EventDispatcher.h>
#include <imgui/src/imgui_internal.h>
#include <MessageToken.h>
#include <Messengers/Messenger.h>

using namespace ZEngine::Components::UI::Event;

namespace Tetragrama::Components {

    DockspaceUIComponent::DockspaceUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility, false)
    {
        m_dockspace_node_flag = ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_PassthruCentralNode;
        m_window_flags        = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
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

    void DockspaceUIComponent::Render()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        m_window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        m_window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin(m_name.c_str(), (m_can_be_closed ? &m_can_be_closed : NULL), m_window_flags);

        ImGui::PopStyleVar();
        ImGui::PopStyleVar(2);

        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            // Dock space
            const auto window_id = ImGui::GetID(m_name.c_str());
            if (!ImGui::DockBuilderGetNode(window_id))
            {
                // Reset current docking state
                ImGui::DockBuilderRemoveNode(window_id);
                ImGui::DockBuilderAddNode(window_id, ImGuiDockNodeFlags_None);
                ImGui::DockBuilderSetNodeSize(window_id, ImGui::GetMainViewport()->Size);

                ImGuiID dock_main_id       = window_id;
                ImGuiID dock_left_id      = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.2f, nullptr, &dock_main_id);
                ImGuiID dock_right_id      = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.2f, nullptr, &dock_main_id);

                ImGuiID dock_right_down_id = ImGui::DockBuilderSplitNode(dock_right_id, ImGuiDir_Down, 0.3f, nullptr, &dock_right_id);
                ImGuiID dock_down_id       = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.25f, nullptr, &dock_main_id);
                ImGuiID dock_down_right_id = ImGui::DockBuilderSplitNode(dock_down_id, ImGuiDir_Right, 0.6f, nullptr, &dock_down_id);

                // Dock windows
                ImGui::DockBuilderDockWindow("Hierarchy", dock_left_id);
                ImGui::DockBuilderDockWindow("Inspector", dock_right_id);
                ImGui::DockBuilderDockWindow("Console", dock_right_down_id);
                ImGui::DockBuilderDockWindow("Project", dock_down_right_id);
                ImGui::DockBuilderDockWindow("Scene", dock_main_id);

                ImGui::DockBuilderFinish(dock_main_id);
            }

            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::DockSpace(window_id, ImVec2(0.0f, 0.0f), m_dockspace_node_flag);
            ImGui::PopStyleVar();

        }

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Scene"))
                {
#ifdef _WIN32
                    Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::EmptyMessage>(EDITOR_RENDER_LAYER_SCENE_REQUEST_NEWSCENE, Messengers::EmptyMessage{});
#else
                    Messengers::IMessenger::Send<ZEngine::Layers::Layer, Messengers::EmptyMessage>(EDITOR_RENDER_LAYER_SCENE_REQUEST_NEWSCENE, Messengers::EmptyMessage{});
#endif
                }

                if (ImGui::MenuItem("Open Scene"))
                {
#ifdef _WIN32
                    Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(
                        EDITOR_RENDER_LAYER_SCENE_REQUEST_OPENSCENE, Messengers::GenericMessage<std::string>{""});
#else
                    Messengers::IMessenger::Send<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(
                        EDITOR_RENDER_LAYER_SCENE_REQUEST_OPENSCENE, Messengers::GenericMessage<std::string>{""});
#endif
                }

                if (ImGui::MenuItem("Import New Asset..."))
                {
                    //Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(
                    //    EDITOR_RENDER_LAYER_SCENE_REQUEST_OPENSCENE, Messengers::GenericMessage<std::string>{""});
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Save"))
                {
                    Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(
                        EDITOR_RENDER_LAYER_SCENE_REQUEST_SERIALIZATION, Messengers::GenericMessage<std::string>{""});
                }

                if (ImGui::MenuItem("Save As ..."))
                {
                    Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(
                        EDITOR_RENDER_LAYER_SCENE_REQUEST_SERIALIZATION, Messengers::GenericMessage<std::string>{""});
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Exit"))
                {
                    ZEngine::Event::WindowClosedEvent app_close_event{};
                    Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::GenericMessage<ZEngine::Event::WindowClosedEvent>>(
                        EDITOR_COMPONENT_DOCKSPACE_REQUEST_EXIT, Messengers::GenericMessage<ZEngine::Event::WindowClosedEvent>{std::move(app_close_event)});
                }

                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        if (HasChildren())
        {
            std::for_each(std::begin(m_children), std::end(m_children), [this](const ZEngine::Ref<UIComponent>& item) {
                item->Render();
            });
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
