#include <pch.h>
#include <DockspaceUIComponent.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <Event/EventDispatcher.h>
#include <imgui/src/imgui_internal.h>
#include <MessageToken.h>
#include <Messengers/Messenger.h>
#include <Helpers/WindowsHelper.h>

using namespace ZEngine::Components::UI::Event;

namespace Tetragrama::Components
{

    DockspaceUIComponent::DockspaceUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility, false)
    {
        m_dockspace_node_flag = ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_PassthruCentralNode;
        m_window_flags        = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    }

    DockspaceUIComponent::~DockspaceUIComponent() {}

    void DockspaceUIComponent::Update(ZEngine::Core::TimeStep dt)
    {
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

        ImGui::PopStyleVar(3);

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
                ImGuiID dock_left_id       = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.2f, nullptr, &dock_main_id);
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

        RenderMenuBar();

        ImGui::End();
    }

    void DockspaceUIComponent::RenderMenuBar()
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Scene"))
                {
                    OnNewSceneAsync();
                }

                if (ImGui::MenuItem("Open Scene"))
                {
                    OnOpenSceneAsync();
                }

                if (ImGui::MenuItem("Import New Asset..."))
                {
                    OnImportAssetAsync();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Save"))
                {
                    OnSaveAsync();
                }

                if (ImGui::MenuItem("Save As ..."))
                {
                    OnSaveAsAsync();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Exit"))
                {
                    OnExitAsync();
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Settings"))
            {
                if (ImGui::MenuItem("Renderer"))
                {
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }
    }

    std::future<void> DockspaceUIComponent::OnNewSceneAsync()
    {
        Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::EmptyMessage>(EDITOR_RENDER_LAYER_SCENE_REQUEST_NEWSCENE, Messengers::EmptyMessage{});
        co_return;
    }

    std::future<void> DockspaceUIComponent::OnOpenSceneAsync()
    {
        auto scene_filename = co_await Helpers::OpenFileDialogAsync();
        Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(
            EDITOR_RENDER_LAYER_SCENE_REQUEST_OPENSCENE, Messengers::GenericMessage<std::string>{scene_filename});

        co_return;
    }

    std::future<void> DockspaceUIComponent::OnImportAssetAsync()
    {
        auto asset_filename = co_await Helpers::OpenFileDialogAsync();
        Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(
            EDITOR_RENDER_LAYER_SCENE_REQUEST_IMPORTASSETMODEL, Messengers::GenericMessage<std::string>{asset_filename});

        co_return;
    }

    std::future<void> DockspaceUIComponent::OnSaveAsync()
    {
        Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(
            EDITOR_RENDER_LAYER_SCENE_REQUEST_SERIALIZATION, Messengers::GenericMessage<std::string>{""});
        co_return;
    }

    std::future<void> DockspaceUIComponent::OnSaveAsAsync()
    {
        Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(
            EDITOR_RENDER_LAYER_SCENE_REQUEST_SERIALIZATION, Messengers::GenericMessage<std::string>{""});

        co_return;
    }

    std::future<void> DockspaceUIComponent::OnExitAsync()
    {
        Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::GenericMessage<ZEngine::Event::WindowClosedEvent>>(
            EDITOR_COMPONENT_DOCKSPACE_REQUEST_EXIT, Messengers::GenericMessage<ZEngine::Event::WindowClosedEvent>{ZEngine::Event::WindowClosedEvent{}});

        co_return;
    }

    std::future<void> DockspaceUIComponent::RequestExitMessageHandlerAsync(Messengers::GenericMessage<ZEngine::Event::WindowClosedEvent>& message)
    {
        if (auto layer = m_parent_layer.lock())
        {
            layer->OnEvent(message.GetValue());
        }
        ZENGINE_CORE_WARN("Editor stopped")
        co_return;
    }
} // namespace Tetragrama::Components
