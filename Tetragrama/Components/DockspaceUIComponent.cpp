#include <Tetragrama/Components/DockspaceUIComponent.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/Helpers/UIDispatcher.h>
#include <Tetragrama/MessageToken.h>
#include <Tetragrama/Messengers/Messenger.h>
#include <ZEngine/Importers/AssimpImporter.h>
#include <ZEngine/Importers/EnvironmentMapImporter.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <fmt/format.h>
#include <imgui/imgui_internal.h>
#include <filesystem>

namespace fs = std::filesystem;

using namespace ZEngine::Helpers;

namespace Tetragrama::Components
{
    ImVec4        DockspaceUIComponent::s_asset_importer_report_msg_color     = {1, 1, 1, 1};
    char          DockspaceUIComponent::s_asset_importer_input_buffer[1024]   = {0};
    char          DockspaceUIComponent::s_save_as_input_buffer[1024]          = {0};
    std::string   DockspaceUIComponent::s_asset_importer_report_msg           = "";
    float         DockspaceUIComponent::s_editor_scene_serializer_progress    = 0.0f;

    ImVec4        DockspaceUIComponent::s_env_map_importer_report_msg_color   = {1, 1, 1, 1};
    char          DockspaceUIComponent::s_env_map_importer_input_buffer[1024] = {0};
    std::string   DockspaceUIComponent::s_env_map_importer_report_msg         = "";

    static bool   s_is_scene_loading                                          = false;
    static char   s_scene_serializer_log[DEFAULT_STR_BUFFER]                  = {0};
    static ImVec4 s_scene_serializer_log_color                                = {1, 1, 1, 1};

    DockspaceUIComponent::DockspaceUIComponent() {}

    DockspaceUIComponent::~DockspaceUIComponent() {}

    void DockspaceUIComponent::Initialize(Layers::ImguiLayer* parent, const char* name, bool visibility, bool closed)
    {
        UIComponent::Initialize(parent, name, visibility, closed);

        parent->LocalArena.CreateSubArena(ZMega(1), &LocalArena);

        m_asset_importer    = ZPushStructCtor(parent->Arena, ZEngine::Importers::AssimpImporter);
        m_env_map_importer  = ZPushStructCtor(parent->Arena, ZEngine::Importers::EnvironmentMapImporter);
        m_editor_serializer = ZPushStructCtor(parent->Arena, Serializers::EditorSceneSerializer);

        m_editor_serializer->Initialize(parent->Arena);
        m_asset_importer->Initialize(parent->Arena);
        m_env_map_importer->Initialize(parent->Arena);

        m_dockspace_node_flag                 = ImGuiDockNodeFlags_NoWindowMenuButton | static_cast<decltype(ImGuiDockNodeFlags_NoWindowMenuButton)>(ImGuiDockNodeFlags_PassthruCentralNode);
        m_window_flags                        = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        auto app                              = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        m_editor_serializer->Context          = app;
        m_asset_importer->Context             = app;
        m_env_map_importer->Context           = app;

        auto editor_serializer_default_output = fmt::format("{0}{1}{2}", app->Configuration->WorkingSpacePath.c_str(), PLATFORM_OS_BACKSLASH, app->Configuration->ScenePath.c_str());

        m_editor_serializer->SetDefaultOutput(editor_serializer_default_output);
        m_editor_serializer->SetOnProgressCallback(OnEditorSceneSerializerProgress);
        m_editor_serializer->SetOnCompleteCallback(OnEditorSceneSerializerComplete);
        m_editor_serializer->SetOnDeserializeCompleteCallback(OnEditorSceneSerializerDeserializeComplete);
        m_editor_serializer->SetOnLogCallback(OnEditorSceneSerializerLog);
        m_editor_serializer->SetOnErrorCallback(OnEditorSceneSerializerError);

        m_asset_importer->SetOnCompleteCallback(OnAssetImporterComplete);
        m_asset_importer->SetOnProgressCallback(OnAssetImporterProgress);
        m_asset_importer->SetOnLogCallback(OnAssetImporterLog);
        m_asset_importer->SetOnErrorCallback(OnAssetImporterError);

        m_env_map_importer->SetOnCompleteCallback(OnEnvMapImporterComplete);
        m_env_map_importer->SetOnProgressCallback(OnEnvMapImporterProgress);
        m_env_map_importer->SetOnLogCallback(OnEnvMapImporterLog);
        m_env_map_importer->SetOnErrorCallback(OnEnvMapImporterError);
    }

    void DockspaceUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    void DockspaceUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer)
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

        ImGui::Begin(Name, (CanBeClosed ? &CanBeClosed : NULL), m_window_flags);

        ImGui::PopStyleVar(3);

        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            // Dock space
            const auto window_id = ImGui::GetID(Name);
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

        RenderLoadScene();
        RenderSaveScene();
        RenderSaveSceneAs();

        RenderImporter();
        RenderEnvironmentMapImporter();

        RenderExitPopup();

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

        const char* str_id = "Model Importer";
        ImGui::OpenPopup(str_id);
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(700, 100), ImGuiCond_Always);

        bool is_import_button_enabled = !m_asset_importer->IsImporting();

        if (ImGui::BeginPopupModal(str_id, NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::PushItemWidth(620);
            ImGui::InputText("##ModelImporterUI", s_asset_importer_input_buffer, IM_ARRAYSIZE(s_asset_importer_input_buffer), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();

            ImGui::SameLine();

            if (ImGui::Button("...", ImVec2(50, 0)) && is_import_button_enabled)
            {
                Helpers::UIDispatcher::RunAsync([this]() -> std::future<void> {
                    if (ParentLayer && ParentLayer->CurrentApp)
                    {
                        auto                          window = ParentLayer->CurrentApp->CurrentWindow;
                        std::vector<std::string_view> filters{".obj", ".gltf"};
                        std::string                   filename = co_await window->OpenFileDialogAsync(filters);

                        if (!filename.empty())
                        {
                            ZEngine::Helpers::secure_memset(s_asset_importer_input_buffer, 0, IM_ARRAYSIZE(s_asset_importer_input_buffer), IM_ARRAYSIZE(s_asset_importer_input_buffer));
                            ZEngine::Helpers::secure_memcpy(s_asset_importer_input_buffer, IM_ARRAYSIZE(s_asset_importer_input_buffer), filename.c_str(), filename.size());
                        }
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
                OnImportAssetAsync(s_asset_importer_input_buffer);
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
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
            ImGui::SetCursorPos(ImVec2(10, ImGui::GetWindowSize().y - 30));
            ImGui::TextColored(s_asset_importer_report_msg_color, "%s", s_asset_importer_report_msg.c_str());
            ImGui::PopFont();

            ImGui::EndPopup();
        }
    }

    void DockspaceUIComponent::RenderLoadScene()
    {
        if (!s_is_scene_loading)
        {
            return;
        }

        const char* str_id = "Loading Scene";
        ImGui::OpenPopup(str_id);
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(700, 100), ImGuiCond_Always);

        if (ImGui::BeginPopupModal(str_id, NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
        {
            // Calculate position for the progress bar
            ImVec2 wind_size        = ImGui::GetWindowSize();
            ImVec2 reg_available    = ImGui::GetContentRegionAvail();
            ImVec2 progress_bar_pos = ImVec2((wind_size.x - reg_available.x) * 0.5f, (wind_size.y - reg_available.y));

            // Display the progress bar
            ImGui::SetCursorPos(progress_bar_pos);
            ImGui::ProgressBar(s_editor_scene_serializer_progress, ImVec2(reg_available.x, 20.0f), " ");

            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
            ImGui::SetCursorPos(ImVec2(10, wind_size.y - 30));
            ImGui::TextColored(s_scene_serializer_log_color, "%s", s_scene_serializer_log);
            ImGui::PopFont();

            ImGui::EndPopup();
        }
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
            Helpers::UIDispatcher::RunAsync([this] { OnExitAsync(); });
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
                auto app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
                auto editor_scene  = reinterpret_cast<EditorScenePtr>(app->CurrentScene);
                editor_scene->Name = s_save_as_input_buffer;
                m_editor_serializer->Serialize(editor_scene);

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

        auto app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        auto current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);

        if (!current_scene->HasPendingChange())
        {
            Helpers::UIDispatcher::RunAsync([this] { OnExitAsync(); });
        }

        const char* str_id = "Saving changes to the current Scene ?";
        ImGui::OpenPopup(str_id);
        // Always center this window when appearing
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal(str_id, NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("%s", fmt::format("You have unsaved changes for your current scene : {}", current_scene->Name).c_str());
            ImGui::Separator();

            if (ImGui::Button("Save", ImVec2(120, 0)))
            {
                m_open_save_scene = true;
                m_open_exit       = false;
                ImGui::CloseCurrentPopup();

                m_editor_serializer->Serialize(current_scene);
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Don't save", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();

                Helpers::UIDispatcher::RunAsync([this] { OnExitAsync(); });
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                m_open_exit        = false;
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

    void DockspaceUIComponent::RenderEnvironmentMapImporter()
    {
        if (!m_open_env_map_importer)
        {
            std::string_view buffer_view = s_env_map_importer_input_buffer;
            if (!buffer_view.empty())
            {
                ResetEnvironmentMapImporterBuffers();
            }
            return;
        }

        const char* str_id = "Environment Map Importer";
        ImGui::OpenPopup(str_id);
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(700, 100), ImGuiCond_Always);

        if (ImGui::BeginPopupModal(str_id, NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::PushItemWidth(620);
            ImGui::InputText("##EnvMapImporterUI", s_env_map_importer_input_buffer, IM_ARRAYSIZE(s_env_map_importer_input_buffer), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();

            ImGui::SameLine();

            if (ImGui::Button("...", ImVec2(50, 0)))
            {
                Helpers::UIDispatcher::RunAsync([this]() -> std::future<void> {
                    if (ParentLayer && ParentLayer->CurrentApp)
                    {
                        auto                          window = ParentLayer->CurrentApp->CurrentWindow;
                        std::vector<std::string_view> filters{".hdr", ".exr"};
                        std::string                   filename = co_await window->OpenFileDialogAsync(filters);

                        if (!filename.empty())
                        {
                            ZEngine::Helpers::secure_memset(s_env_map_importer_input_buffer, 0, IM_ARRAYSIZE(s_env_map_importer_input_buffer), IM_ARRAYSIZE(s_env_map_importer_input_buffer));
                            ZEngine::Helpers::secure_memcpy(s_env_map_importer_input_buffer, IM_ARRAYSIZE(s_env_map_importer_input_buffer), filename.c_str(), filename.size());
                        }
                    }
                });
            }

            ImGui::Separator();

            ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 180);
            ImGui::SetCursorPosY(ImGui::GetWindowSize().y - ImGui::GetFrameHeightWithSpacing() - 5);

            bool is_import_button_enabled = !std::string_view(s_env_map_importer_input_buffer).empty() && !m_env_map_importer->IsImporting();

            if (!is_import_button_enabled)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            }

            if (ImGui::Button("Import", ImVec2(80, 0)) && is_import_button_enabled)
            {
                Helpers::UIDispatcher::RunAsync([this]() -> std::future<void> { co_await OnImportEnvironmentMapAsync(s_env_map_importer_input_buffer); });
            }

            if (!is_import_button_enabled)
            {
                ImGui::PopStyleColor(3);
            }

            if (m_env_map_importer->IsImporting())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(importing...)");
            }

            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(80, 0)))
            {
                m_open_env_map_importer = false;
                ResetEnvironmentMapImporterBuffers();
                ImGui::CloseCurrentPopup();
            }

            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
            ImGui::SetCursorPos(ImVec2(10, ImGui::GetWindowSize().y - 30));
            ImGui::TextColored(s_env_map_importer_report_msg_color, "%s", s_env_map_importer_report_msg.c_str());
            ImGui::PopFont();

            ImGui::EndPopup();
        }
    }

    void DockspaceUIComponent::ResetEnvironmentMapImporterBuffers()
    {
        s_env_map_importer_report_msg       = "";
        s_env_map_importer_report_msg_color = {1.0f, 1.0f, 1.0f, 1.0f};
        ZEngine::Helpers::secure_memset(s_env_map_importer_input_buffer, 0, IM_ARRAYSIZE(s_env_map_importer_input_buffer), IM_ARRAYSIZE(s_env_map_importer_input_buffer));
    }

    std::future<void> DockspaceUIComponent::OnImportEnvironmentMapAsync(const char* filename)
    {
        if (ZEngine::Helpers::secure_strlen(filename) == 0 || m_env_map_importer->IsImporting())
        {
            co_return;
        }

        LocalArena.Clear();

        auto app         = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        auto arena       = &LocalArena;
        auto asset_name  = fs::path(filename).filename().replace_extension().string();
        auto output_file = fmt::format("{}.zenvmap", asset_name.c_str());

        auto config      = ZPushStruct(arena, ZEngine::Importers::ImportConfiguration);
        config->OutputWorkingSpacePath.init(arena, app->Configuration->WorkingSpacePath.c_str());
        config->OutputAssetsPath.init(arena, app->Configuration->EnvironmentMapImportPath.c_str());
        config->AssetName.init(arena, asset_name.c_str());
        config->OutputAssetFile.init(arena, output_file.c_str());

        s_env_map_importer_report_msg_color = {1.0f, 1.0f, 1.0f, 1.0f};
        s_env_map_importer_report_msg       = "Importing...";

        m_env_map_importer->ImportAsync(filename, *config);
        co_return;
    }

    void DockspaceUIComponent::OnEnvMapImporterComplete(void* const, ZEngine::Core::Containers::ArrayView<ZEngine::Importers::AssetImporterOutput> result)
    {
        if (result.size() > 0)
        {
            s_env_map_importer_report_msg_color = {0.0f, 1.0f, 0.0f, 1.0f};
            s_env_map_importer_report_msg       = fmt::format("Saved");
        }
    }

    void DockspaceUIComponent::OnEnvMapImporterProgress(void* const, float value)
    {
        s_env_map_importer_report_msg_color = {1.0f, 1.0f, 1.0f, 1.0f};
        s_env_map_importer_report_msg       = fmt::format("Progress: {:.0f}%%", value * 100.f);
    }

    void DockspaceUIComponent::OnEnvMapImporterError(void* const, std::string_view msg)
    {
        s_env_map_importer_report_msg_color = {1.0f, 0.0f, 0.0f, 1.0f};
        s_env_map_importer_report_msg       = msg;
    }

    void DockspaceUIComponent::OnEnvMapImporterLog(void* const, std::string_view msg)
    {
        s_env_map_importer_report_msg_color = {1.0f, 1.0f, 1.0f, 1.0f};
        s_env_map_importer_report_msg       = msg;
    }

    void DockspaceUIComponent::ResetSaveAsBuffers()
    {
        ZEngine::Helpers::secure_memset(s_save_as_input_buffer, 0, IM_ARRAYSIZE(s_save_as_input_buffer), IM_ARRAYSIZE(s_save_as_input_buffer));
    }

    void DockspaceUIComponent::OnAssetImporterComplete(void* const context, ZEngine::Core::Containers::ArrayView<ZEngine::Importers::AssetImporterOutput> result)
    {
        auto app           = reinterpret_cast<EditorPtr>(context);
        auto current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);

        for (unsigned i = 0; i < result.size(); ++i)
        {
            current_scene->PushAssetFile(result[i]);
        }

        s_asset_importer_report_msg_color = {0.0f, 1.0f, 0.0f, 1.0f};
        s_asset_importer_report_msg       = "Completed";
    }

    void DockspaceUIComponent::OnAssetImporterProgress(void* const context, float value)
    {
        s_asset_importer_report_msg_color = {1.0f, 1.0f, 1.0f, 1.0f};
        s_asset_importer_report_msg       = fmt::format("Reading file: {:.1f} %%", (value * 100.f));
    }

    void DockspaceUIComponent::OnAssetImporterError(void* const, std::string_view msg)
    {
        s_asset_importer_report_msg_color = {1.0f, 0.0f, 0.0f, 1.0f};
        s_asset_importer_report_msg       = msg;
    }

    void DockspaceUIComponent::OnEditorSceneSerializerError(void* const, std::string_view msg)
    {
        ZENGINE_CORE_ERROR("{}", msg)
    }

    void DockspaceUIComponent::OnEditorSceneSerializerLog(void* const, std::string_view msg)
    {
        ZEngine::Helpers::secure_strcpy(s_scene_serializer_log, DEFAULT_STR_BUFFER, msg.data());
    }

    void DockspaceUIComponent::OnAssetImporterLog(void* const, std::string_view msg)
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
                    Helpers::UIDispatcher::RunAsync([this] { OnNewSceneAsync(); });
                }

                if (ImGui::MenuItem("Open Scene"))
                {
                    Helpers::UIDispatcher::RunAsync([this] { OnOpenSceneAsync(); });
                }

                ImGui::MenuItem("Import New Asset...", NULL, &m_open_asset_importer);
                ImGui::Separator();

                if (ImGui::MenuItem("Save"))
                {
                    m_open_save_scene             = true;
                    m_request_save_scene_ui_close = true;
                    Helpers::UIDispatcher::RunAsync([this] {
                        if (ParentLayer->CurrentApp)
                        {
                            auto app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
                            auto current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);
                            m_editor_serializer->Serialize(current_scene);
                        }
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

                ImGui::MenuItem("Import Environment Map", NULL, &m_open_env_map_importer);
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }
    }

    void DockspaceUIComponent::OnEditorSceneSerializerProgress(void* const, float value)
    {
        s_editor_scene_serializer_progress = value;
    }

    void DockspaceUIComponent::OnEditorSceneSerializerComplete(void* const context)
    {
        auto app           = reinterpret_cast<EditorPtr>(context);
        auto current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);
        current_scene->HasPendingChanges.store(false, std::memory_order_release);
    }

    void DockspaceUIComponent::OnEditorSceneSerializerDeserializeComplete(void* const context, EditorScene&& scene)
    {
        auto app           = reinterpret_cast<EditorPtr>(context);
        auto current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);

        // Todo : Ensure no data race on CurrentScenePtr
        app->Configuration->ActiveSceneName.clear();
        app->Configuration->ActiveSceneName.append(scene.Name);

        current_scene->MarkDirty(true);
        current_scene->SelectedSceneNode.store(-1, std::memory_order_release);
        current_scene->Reset();
        current_scene->ExtractAsync(scene);

        current_scene->Name = app->Configuration->ActiveSceneName.c_str();

        current_scene->MarkDirty(false);

        {
            auto msg = fmt::format("Scene {} deserialized successfully", current_scene->Name);
            ZEngine::Helpers::secure_strcpy(s_scene_serializer_log, DEFAULT_STR_BUFFER, msg.data());

            ZENGINE_CORE_INFO("{}", msg.c_str())
        }

        s_is_scene_loading = false;
    }

    std::future<void> DockspaceUIComponent::OnNewSceneAsync()
    {
        co_return;
    }

    std::future<void> DockspaceUIComponent::OnOpenSceneAsync()
    {
        if (ParentLayer && ParentLayer->CurrentApp->CurrentWindow)
        {
            auto                          window         = ParentLayer->CurrentApp->CurrentWindow;
            std::vector<std::string_view> filters        = {".zescene"};
            std::string                   scene_filename = co_await window->OpenFileDialogAsync(filters);

            if (!scene_filename.empty())
            {
                s_is_scene_loading = true;
                m_editor_serializer->Deserialize(scene_filename.c_str());
            }
        }
        co_return;
    }

    std::future<void> DockspaceUIComponent::OnOpenSceneRequestAsync(const char* filename)
    {
        if (!ZEngine::Helpers::secure_strlen(filename))
        {
            co_return;
        }

        s_is_scene_loading = true;
        m_editor_serializer->Deserialize(filename);
        co_return;
    }

    std::future<void> DockspaceUIComponent::OnOpenMeshRequestAsync(const char* filename)
    {
        ZEngine::Importers::AssetMeshFileHeader header;
        if (ZEngine::Importers::IAssetImporter::ReadAssetMeshFileHeader(filename, header))
        {
            auto asset_mesh = ZEngine::Managers::AssetManager::GetAsset<ZEngine::Importers::AssetMesh>(header.Id);
            if (asset_mesh)
            {
                auto app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
                auto current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);
                auto asset_manager = ZEngine::Managers::AssetManager::Instance();

                auto handle_ref    = asset_manager->GetMeshNodeHierarchyHandle(asset_mesh->MeshUUID);
                current_scene->PendingOnLoadHierarchies->Enqueue(handle_ref);
            }
        }
        co_return;
    }

    std::future<void> DockspaceUIComponent::OnImportAssetAsync(const char* filename)
    {
        if ((ZEngine::Helpers::secure_strlen(filename) == 0) || m_asset_importer->IsImporting())
        {
            co_return;
        }

        LocalArena.Clear();

        auto        app               = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);

        auto        arena             = &LocalArena;
        const auto& editor_config     = *(app->Configuration);
        auto        parent_path       = std::filesystem::path(filename).parent_path().string();
        auto        asset_name        = fs::path(filename).filename().replace_extension().string();
        auto        output_asset_file = fmt::format("{}.zemesh", asset_name.c_str());

        auto        config            = ZPushStruct(arena, ZEngine::Importers::ImportConfiguration);
        config->OutputWorkingSpacePath.init(arena, editor_config.WorkingSpacePath.c_str());
        config->OutputTextureFilesPath.init(arena, editor_config.TexturePath.c_str());
        config->OutputAssetsPath.init(arena, editor_config.SceneDataPath.c_str());
        config->AssetName.init(arena, asset_name.c_str());
        config->OutputAssetFile.init(arena, output_asset_file.c_str());
        config->InputBaseAssetFilePath.init(arena, parent_path.c_str());

        m_asset_importer->ImportAsync(filename, *config);
        co_return;
    }

    std::future<void> DockspaceUIComponent::OnExitAsync()
    {
        if (ParentLayer)
        {
            ZEngine::Windows::Events::WindowClosedEvent e{};
            ParentLayer->OnEvent(e);
        }
        ZENGINE_CORE_WARN("Editor stopped")
        co_return;
    }
} // namespace Tetragrama::Components
