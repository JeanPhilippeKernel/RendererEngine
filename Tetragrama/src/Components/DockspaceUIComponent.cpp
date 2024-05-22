#include <pch.h>
#include <DockspaceUIComponent.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <Event/EventDispatcher.h>
#include <imgui/src/imgui_internal.h>
#include <MessageToken.h>
#include <Messengers/Messenger.h>
#include <Helpers/WindowsHelper.h>
#include <Editor.h>
#include <Helpers/UIDispatcher.h>


#include <fmt/format.h>
#include <Importers/AssimpImporter.h>

namespace fs = std::filesystem;
using namespace ZEngine::Components::UI::Event;

namespace Tetragrama::Components
{
    ImVec4      DockspaceUIComponent::s_asset_importer_report_msg_color   = {1, 1, 1, 1};
    char        DockspaceUIComponent::s_asset_importer_input_buffer[1024] = {0};
    char        DockspaceUIComponent::s_save_as_input_buffer[1024]        = {0};
    std::string DockspaceUIComponent::s_asset_importer_report_msg         = "";
    float       DockspaceUIComponent::s_editor_scene_serializer_progress  = 0.0f;


    DockspaceUIComponent::DockspaceUIComponent(std::string_view name, bool visibility)
        : UIComponent(name, visibility, false), m_asset_importer(ZEngine::CreateScope<Importers::AssimpImporter>()),
          m_editor_serializer(ZEngine::CreateScope<Serializers::EditorSceneSerializer>())
    {
        m_dockspace_node_flag = ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_PassthruCentralNode;
        m_window_flags        = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        const auto& editor_config = Editor::GetCurrentEditorConfiguration();

        m_default_import_configuration = {
            .OutputModelFilePath    = fmt::format("{0}/{1}", editor_config.WorkingSpacePath, editor_config.SceneDataPath),
            .OutputMeshFilePath     = fmt::format("{0}/{1}", editor_config.WorkingSpacePath, editor_config.SceneDataPath),
            .OutputTextureFilesPath = fmt::format("{0}/{1}", editor_config.WorkingSpacePath, editor_config.DefaultImportTexturePath),
            .OutputMaterialsPath    = fmt::format("{0}/{1}", editor_config.WorkingSpacePath, editor_config.SceneDataPath)};

        std::replace(m_default_import_configuration.OutputModelFilePath.begin(), m_default_import_configuration.OutputModelFilePath.end(), '/', '\\');
        std::replace(m_default_import_configuration.OutputMeshFilePath.begin(), m_default_import_configuration.OutputMeshFilePath.end(), '/', '\\');
        std::replace(m_default_import_configuration.OutputTextureFilesPath.begin(), m_default_import_configuration.OutputTextureFilesPath.end(), '/', '\\');
        std::replace(m_default_import_configuration.OutputMaterialsPath.begin(), m_default_import_configuration.OutputMaterialsPath.end(), '/', '\\');

        auto editor_serializer_default_output = fmt::format("{0}/{1}", editor_config.WorkingSpacePath, editor_config.ScenePath);
        std::replace(editor_serializer_default_output.begin(), editor_serializer_default_output.end(), '/', '\\');
        m_editor_serializer->SetDefaultOutput(editor_serializer_default_output);
        m_editor_serializer->SetOnProgressCallback(OnEditorSceneSerializerProgress);
        m_editor_serializer->SetOnDeserializeCompleteCallback(OnEditorSceneSerializerDeserializeComplete);
        m_editor_serializer->SetOnErrorCallback(OnEditorSceneSerializerError);

        m_asset_importer->SetOnCompleteCallback(OnAssetImporterComplete);
        m_asset_importer->SetOnProgressCallback(OnAssetImporterProgress);
        m_asset_importer->SetOnLogCallback(OnAssetImporterLog);
        m_asset_importer->SetOnErrorCallback(OnAssetImporterError);

        auto scene_fullname = fmt::format("{0}/{1}/{2}.zescene", editor_config.WorkingSpacePath, editor_config.ScenePath, editor_config.ActiveSceneName);
        std::replace(scene_fullname.begin(), scene_fullname.end(), '/', '\\'); // Todo : Move this replace into an helper function....
        m_editor_serializer->Deserialize(scene_fullname);
    }

    DockspaceUIComponent::~DockspaceUIComponent() {}

    void DockspaceUIComponent::Update(ZEngine::Core::TimeStep dt) {}

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
        RenderImporter();

        RenderExitPopup();
        RenderSaveScene();
        RenderSaveSceneAs();

        ImGui::End();
    }

    void DockspaceUIComponent::RenderImporter()
    {
        if (!m_open_asset_importer)
        {
            std::string_view buffer_view = s_asset_importer_input_buffer;
            if (!buffer_view.empty())
            {
                ResetImporterBuffers();
            }
            return;
        }

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(700, 100), ImGuiCond_Always);

        if (!ImGui::Begin("Model Importer", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        bool is_import_button_enabled = !m_asset_importer->IsImporting();

        ImGui::PushItemWidth(620);
        ImGui::InputText("##ModelImporterUI", s_asset_importer_input_buffer, IM_ARRAYSIZE(s_asset_importer_input_buffer), ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();

        ImGui::SameLine();

        if (ImGui::Button("...", ImVec2(50, 0)) && is_import_button_enabled)
        {
            Helpers::UIDispatcher::RunAsync([]() -> std::future<void> {
                std::string filename = co_await Helpers::OpenFileDialogAsync();
                if (!filename.empty())
                {
                    ZEngine::Helpers::secure_memset(s_asset_importer_input_buffer, 0, IM_ARRAYSIZE(s_asset_importer_input_buffer), IM_ARRAYSIZE(s_asset_importer_input_buffer));
                    ZEngine::Helpers::secure_memcpy(s_asset_importer_input_buffer, IM_ARRAYSIZE(s_asset_importer_input_buffer), filename.c_str(), filename.size());
                }
            });
        }

        ImGui::Separator();

        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 180);
        ImGui::SetCursorPosY(ImGui::GetWindowSize().y - ImGui::GetFrameHeightWithSpacing() - 5);

        if (!is_import_button_enabled || std::string_view(s_asset_importer_input_buffer).empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // Grayed out color
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        }

        if (ImGui::Button("Launch", ImVec2(80, 0)) && is_import_button_enabled)
        {
            Helpers::UIDispatcher::RunAsync([this] {
                OnImportAssetAsync(s_asset_importer_input_buffer);
            });
        }

        if (!is_import_button_enabled || std::string_view(s_asset_importer_input_buffer).empty())
        {
            ImGui::PopStyleColor(3); // Pop the grayed out color
        }

        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(80, 0)))
        {
            if (m_asset_importer->IsImporting())
            {
                ZENGINE_CORE_WARN("Import in progress : {}", s_asset_importer_input_buffer)
            }
            else
            {
                m_open_asset_importer = false;
                ResetImporterBuffers();
            }
        }

        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        ImGui::SetCursorPos(ImVec2(10, ImGui::GetWindowSize().y - 30));
        ImGui::TextColored(s_asset_importer_report_msg_color, s_asset_importer_report_msg.c_str());
        ImGui::PopFont();

        ImGui::End();
    }

    void DockspaceUIComponent::RenderSaveScene()
    {
        if (!m_open_save_scene)
        {
            return;
        }

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(700, 70), ImGuiCond_Always);

        if (!ImGui::Begin("Saving Scene", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        // Calculate position for the progress bar
        ImVec2 progress_bar_pos = ImVec2((ImGui::GetWindowSize().x - ImGui::GetContentRegionAvail().x) * 0.5f, (ImGui::GetWindowSize().y - ImGui::GetContentRegionAvail().y));

        // Display the progress bar
        ImGui::SetCursorPos(progress_bar_pos);
        ImGui::ProgressBar(s_editor_scene_serializer_progress, ImVec2(ImGui::GetContentRegionAvail().x, 10.0f), " ");
        ImGui::End();

        if (m_editor_serializer->IsSerializing())
        {
            return;
        }

        if (m_pending_shutdown)
        {
            m_open_save_scene = false;
            Helpers::UIDispatcher::RunAsync([this] {
                OnExitAsync();
            });
        }
        else if (m_request_save_scene_ui_close)
        {
            m_open_save_scene             = false;
            m_request_save_scene_ui_close = false;
        }
    }

    void DockspaceUIComponent::RenderSaveSceneAs()
    {
        if (!m_open_save_scene_as)
        {
            std::string_view buffer_view = s_save_as_input_buffer;
            if (!buffer_view.empty())
            {
                ResetSaveAsBuffers();
            }
            return;
        }

        const char* str_id = "Scene name";
        ImGui::OpenPopup(str_id);
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(500, 100), ImGuiCond_Always);

        bool is_save_button_enabled = !std::string_view(s_save_as_input_buffer).empty();

        if (ImGui::BeginPopupModal(str_id, NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::PushItemWidth(485);
            ImGui::InputText("##SaveAsUI", s_save_as_input_buffer, IM_ARRAYSIZE(s_save_as_input_buffer));
            ImGui::PopItemWidth();

            ImGui::Separator();

            ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 180);
            ImGui::SetCursorPosY(ImGui::GetWindowSize().y - ImGui::GetFrameHeightWithSpacing() - 5);

            if (!is_save_button_enabled)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // Grayed out color
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            }

            if (ImGui::Button("Save", ImVec2(80, 0)) && is_save_button_enabled)
            {
                auto active_scene = Editor::GetCurrentEditorScene();
                active_scene->SetName(s_save_as_input_buffer);
                m_editor_serializer->Serialize(active_scene);

                m_open_save_scene_as          = false;
                m_open_save_scene             = true;
                m_request_save_scene_ui_close = true;
                ImGui::CloseCurrentPopup();
            }

            if (!is_save_button_enabled)
            {
                ImGui::PopStyleColor(3); // Pop the grayed out color
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0)))
            {
                m_open_save_scene_as = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void DockspaceUIComponent::RenderExitPopup()
    {
        if (!m_open_exit)
        {
            return;
        }

        m_pending_shutdown = true;

        auto current_scene = Editor::GetCurrentEditorScene();
        if (!current_scene->HasPendingChange())
        {
            Helpers::UIDispatcher::RunAsync([this] {
                OnExitAsync();
            });
        }

        const char* str_id = "Saving changes to the current Scene ?";
        ImGui::OpenPopup(str_id);
        // Always center this window when appearing
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal(str_id, NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text(fmt::format("You have unsaved changes for your current scene : {}", current_scene->GetName()).c_str());
            ImGui::Separator();

            if (ImGui::Button("Save", ImVec2(120, 0)))
            {
                m_open_save_scene = true;
                m_open_exit       = false;
                ImGui::CloseCurrentPopup();

                m_editor_serializer->Serialize(Editor::GetCurrentEditorScene());
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Don't save", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();

                Helpers::UIDispatcher::RunAsync([this] {
                    OnExitAsync();
                });
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                m_open_exit = false;
                m_pending_shutdown = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void DockspaceUIComponent::ResetImporterBuffers()
    {
        s_asset_importer_report_msg       = "";
        s_asset_importer_report_msg_color = {1.0f, 1.0f, 1.0f, 1.0f};
        ZEngine::Helpers::secure_memset(s_asset_importer_input_buffer, 0, IM_ARRAYSIZE(s_asset_importer_input_buffer), IM_ARRAYSIZE(s_asset_importer_input_buffer));
    }

    void DockspaceUIComponent::ResetSaveAsBuffers()
    {
        ZEngine::Helpers::secure_memset(s_save_as_input_buffer, 0, IM_ARRAYSIZE(s_save_as_input_buffer), IM_ARRAYSIZE(s_save_as_input_buffer));
    }

    void DockspaceUIComponent::OnAssetImporterComplete(Importers::ImporterData&& data)
    {
        s_asset_importer_report_msg_color = {0.0f, 1.0f, 0.0f, 1.0f};
        s_asset_importer_report_msg       = "Completed";

        auto editor_scene  = Editor::GetCurrentEditorScene();
        auto editor_config = Editor::GetCurrentEditorConfiguration();
        /*
         * Removing the WorkingSpace Path
         */
        auto ws = editor_config.WorkingSpacePath + "\\";
        if (data.SerializedMeshesPath.find(ws) != std::string::npos)
        {
            data.SerializedMeshesPath.replace(data.SerializedMeshesPath.find(ws), ws.size(), "");
        }

        if (data.SerializedMaterialsPath.find(ws) != std::string::npos)
        {
            data.SerializedMaterialsPath.replace(data.SerializedMaterialsPath.find(ws), ws.size(), "");
        }

        if (data.SerializedModelPath.find(ws) != std::string::npos)
        {
            data.SerializedModelPath.replace(data.SerializedModelPath.find(ws), ws.size(), "");
        }
        editor_scene->AddModel({.Name = data.Name, .MeshesPath = data.SerializedMeshesPath, .MaterialsPath = data.SerializedMaterialsPath, .ModelPath = data.SerializedModelPath});
    }

    void DockspaceUIComponent::OnAssetImporterProgress(float value)
    {
        s_asset_importer_report_msg_color = {1.0f, 1.0f, 1.0f, 1.0f};
        s_asset_importer_report_msg       = fmt::format("Reading file: {:.1f} %%", (value * 100.f));
    }

    void DockspaceUIComponent::OnAssetImporterError(std::string_view msg)
    {
        s_asset_importer_report_msg_color = {1.0f, 0.0f, 0.0f, 1.0f};
        s_asset_importer_report_msg       = msg;
    }

    void DockspaceUIComponent::OnEditorSceneSerializerError(std::string_view msg)
    {
        ZENGINE_CORE_ERROR("{}", msg)
    }

    void DockspaceUIComponent::OnAssetImporterLog(std::string_view msg)
    {
        s_asset_importer_report_msg_color = {1.0f, 1.0f, 1.0f, 1.0f};
        s_asset_importer_report_msg       = msg;
    }

    void DockspaceUIComponent::RenderMenuBar()
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Scene"))
                {
                    Helpers::UIDispatcher::RunAsync([this] {
                        OnNewSceneAsync();
                    });
                }

                if (ImGui::MenuItem("Open Scene"))
                {
                    Helpers::UIDispatcher::RunAsync([this] {
                        OnOpenSceneAsync();
                    });
                }

                ImGui::MenuItem("Import New Asset...", NULL, &m_open_asset_importer);
                ImGui::Separator();

                if (ImGui::MenuItem("Save"))
                {
                    m_open_save_scene             = true;
                    m_request_save_scene_ui_close = true;
                    Helpers::UIDispatcher::RunAsync([this] {
                        m_editor_serializer->Serialize(Editor::GetCurrentEditorScene());
                    });
                }

                ImGui::MenuItem("Save As...", NULL, &m_open_save_scene_as);
                ImGui::Separator();

                ImGui::MenuItem("Exit", NULL, &m_open_exit);

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

    void DockspaceUIComponent::OnEditorSceneSerializerProgress(float value)
    {
        s_editor_scene_serializer_progress = value;
    }

    void DockspaceUIComponent::OnEditorSceneSerializerComplete() {}

    void DockspaceUIComponent::OnEditorSceneSerializerDeserializeComplete(EditorScene&& scene)
    {
        Editor::SetCurrentEditorScene(std::move(scene));
    }

    std::future<void> DockspaceUIComponent::OnNewSceneAsync()
    {
        co_return;
    }

    std::future<void> DockspaceUIComponent::OnOpenSceneAsync()
    {
        auto scene_filename = co_await Helpers::OpenFileDialogAsync();

        if (!scene_filename.empty())
        {
            m_editor_serializer->Deserialize(scene_filename);
        }
        co_return;
    }

    std::future<void> DockspaceUIComponent::OnImportAssetAsync(std::string_view filename)
    {
        if (!filename.empty())
        {
            auto config                          = Editor::GetCurrentEditorConfiguration();
            auto parent_path                     = std::filesystem::path(filename).parent_path().string();
            auto asset_name                      = fs::path(filename).filename().replace_extension().string();
            auto import_config                   = m_default_import_configuration;
            import_config.AssetFilename          = asset_name;
            import_config.InputBaseAssetFilePath = parent_path;
            co_await m_asset_importer->ImportAsync(filename, import_config);
        }
        co_return;
    }

    std::future<void> DockspaceUIComponent::OnExitAsync()
    {
        if (auto layer = m_parent_layer.lock())
        {
            ZEngine::Event::WindowClosedEvent e{};
            layer->OnEvent(e);
        }
        ZENGINE_CORE_WARN("Editor stopped")
        co_return;
    }
} // namespace Tetragrama::Components
