#include <Tetragrama/Components/DockspaceUIComponent.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/Helpers/UIDispatcher.h>
#include <Tetragrama/MessageToken.h>
#include <Tetragrama/Messengers/Messenger.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/ImportJob.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Profiling/MemoryProfiler.h>
#include <fmt/format.h>
#include <imgui/imgui_internal.h>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;
using ZEngine::Core::VFS::VFSPath;

using namespace ZEngine::Helpers;

namespace Tetragrama::Components
{
    char                     DockspaceUIComponent::s_save_as_input_buffer[1024]       = {0};
    float                    DockspaceUIComponent::s_editor_scene_serializer_progress = 0.0f;

    static bool              s_is_scene_loading                                       = false;
    static char              s_scene_serializer_log[DEFAULT_STR_BUFFER]               = {0};
    static ImVec4            s_scene_serializer_log_color                             = {1, 1, 1, 1};

    static constexpr cstring kLayoutsDir                                              = "ZodiacEngine/Settings/Layouts";

    // Serialize the live dock tree as a pre-order sequence of SPLIT/LEAF lines.
    // Format: SPLIT <X|Y> <ratio>  or  LEAF <window_name> [window_name ...]
    static void              WriteNodeToFile(FILE* f, ImGuiDockNode* node)
    {
        if (!node)
            return;
        if (node->IsSplitNode())
        {
            float ratio = 0.5f;
            if (node->SplitAxis == ImGuiAxis_X && node->Size.x > 0.0f)
                ratio = node->ChildNodes[0]->Size.x / node->Size.x;
            else if (node->SplitAxis == ImGuiAxis_Y && node->Size.y > 0.0f)
                ratio = node->ChildNodes[0]->Size.y / node->Size.y;
            fprintf(f, "SPLIT %c %f\n", node->SplitAxis == ImGuiAxis_X ? 'X' : 'Y', ratio);
            WriteNodeToFile(f, node->ChildNodes[0]);
            WriteNodeToFile(f, node->ChildNodes[1]);
        }
        else
        {
            fprintf(f, "LEAF");
            for (int i = 0; i < node->Windows.Size; ++i)
            {
                cstring name = node->Windows[i]->Name;
                if (name[0] != '#') // skip internal/popup windows
                    fprintf(f, " %s", name);
            }
            fprintf(f, "\n");
        }
    }

    // Replay one node from the file into the given dock node ID.
    // Returns true if a record was consumed.
    static bool LoadNodeFromFile(FILE* f, ImGuiID node_id)
    {
        char line[512];
        while (fgets(line, sizeof(line), f))
        {
            if (line[0] == '\n' || line[0] == '\0')
                continue;

            if (strncmp(line, "SPLIT", 5) == 0)
            {
                char  axis  = 'X';
                float ratio = 0.5f;
                sscanf(line, "SPLIT %c %f", &axis, &ratio);
                ImGuiID  child0 = 0, child1 = 0;
                ImGuiDir dir = (axis == 'X') ? ImGuiDir_Left : ImGuiDir_Up;
                ImGui::DockBuilderSplitNode(node_id, dir, ratio, &child0, &child1);
                LoadNodeFromFile(f, child0);
                LoadNodeFromFile(f, child1);
                return true;
            }
            if (strncmp(line, "LEAF", 4) == 0)
            {
                char* nl = strchr(line, '\n');
                if (nl)
                    *nl = '\0';
                char* tok = strtok(line + 4, " ");
                while (tok)
                {
                    if (tok[0] != '\0')
                        ImGui::DockBuilderDockWindow(tok, node_id);
                    tok = strtok(nullptr, " ");
                }
                return true;
            }
        }
        return false;
    }

    static ImGuiID BuildLayout_Default(ImGuiID root)
    {
        ImGuiID main       = root;
        ImGuiID left       = ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, 0.18f, nullptr, &main);
        ImGuiID right      = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.22f, nullptr, &main);
        ImGuiID down       = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.25f, nullptr, &main);
        ImGuiID down_right = ImGui::DockBuilderSplitNode(down, ImGuiDir_Right, 0.60f, nullptr, &down);
        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Scene", main);
        ImGui::DockBuilderDockWindow("Project", down_right);
        ImGui::DockBuilderDockWindow("Console", down);
        ImGui::DockBuilderDockWindow("Asset Importer", down);
        return down;
    }

    ImGuiID DockspaceUIComponent::ApplyBuiltinLayout(ImGuiID root, EditorLayout /*layout*/)
    {
        ImGui::DockBuilderRemoveNode(root);
        ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodeSize(root, ImGui::GetMainViewport()->Size);
        ImGuiID console_dock = BuildLayout_Default(root);
        ImGui::DockBuilderFinish(root);
        return console_dock;
    }

    void DockspaceUIComponent::ScanCustomLayouts()
    {
        m_custom_layout_count = 0;
        std::error_code ec;
        if (!fs::exists(kLayoutsDir, ec))
            return;
        for (auto& entry : fs::directory_iterator(kLayoutsDir, ec))
        {
            if (m_custom_layout_count >= kMaxCustomLayouts)
                break;
            if (entry.path().extension() != ".zlayout")
                continue;
            auto& slot = m_custom_layouts[m_custom_layout_count++];
            auto  stem = entry.path().stem().string();
            auto  path = entry.path().string();
            ZEngine::Helpers::secure_strncpy(slot.Name, sizeof(slot.Name), stem.c_str(), stem.size());
            ZEngine::Helpers::secure_strncpy(slot.Path, sizeof(slot.Path), path.c_str(), path.size());
        }
    }

    void DockspaceUIComponent::SaveCurrentLayout(cstring name)
    {
        if (!ParentLayer->DockspaceId)
            return;
        ImGuiDockNode* root = ImGui::DockBuilderGetNode(ParentLayer->DockspaceId);
        if (!root)
            return;

        std::error_code ec;
        fs::create_directories(kLayoutsDir, ec);

        auto path = fmt::format("{}/{}.zlayout", kLayoutsDir, name);
        if (FILE* f = fopen(path.c_str(), "w"))
        {
            WriteNodeToFile(f, root);
            fclose(f);
        }
        ScanCustomLayouts();
    }

    void DockspaceUIComponent::DeleteCustomLayout(int index)
    {
        if (index < 0 || index >= m_custom_layout_count)
            return;
        std::error_code ec;
        fs::remove(m_custom_layouts[index].Path, ec);
        ScanCustomLayouts();
    }

    void DockspaceUIComponent::RenderLayoutMenu()
    {
        if (!ImGui::BeginMenu("Layout"))
            return;

        for (auto& bl : kBuiltinLayouts)
        {
            bool active = (m_active_layout == bl.Id && m_custom_layout_count == 0);
            if (ImGui::MenuItem(bl.Name, nullptr, active))
            {
                m_pending_layout = bl.Id;
                m_layout_dirty   = true;
            }
        }

        if (m_custom_layout_count > 0)
        {
            ImGui::Separator();
            for (int i = 0; i < m_custom_layout_count; ++i)
            {
                if (ImGui::MenuItem(m_custom_layouts[i].Name))
                {
                    // Defer to next frame — DockBuilder must run before DockSpace, not inside a menu
                    ZEngine::Helpers::secure_strncpy(m_pending_layout_path, sizeof(m_pending_layout_path), m_custom_layouts[i].Path, sizeof(m_pending_layout_path) - 1);
                }
            }
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Save Current Layout..."))
        {
            m_save_layout_buf[0] = '\0';
            m_open_save_layout   = true;
        }
        if (m_custom_layout_count > 0 && ImGui::MenuItem("Manage Layouts..."))
            m_open_manage_layouts = true;

        ImGui::EndMenu();
    }

    void DockspaceUIComponent::RenderSaveLayoutModal()
    {
        if (m_open_save_layout)
        {
            ImGui::OpenPopup("Save Layout##modal");
            m_open_save_layout = false;
        }

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({340.0f, 0.0f}, ImGuiCond_Always);
        if (ImGui::BeginPopupModal("Save Layout##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        {
            ImGui::TextUnformatted("Layout name:");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##layout_name", m_save_layout_buf, sizeof(m_save_layout_buf));

            bool empty = (m_save_layout_buf[0] == '\0');
            if (empty)
                ImGui::BeginDisabled();
            if (ImGui::Button("Save", {100.0f, 0.0f}))
            {
                SaveCurrentLayout(m_save_layout_buf);
                ImGui::CloseCurrentPopup();
            }
            if (empty)
                ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {100.0f, 0.0f}))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    void DockspaceUIComponent::RenderManageLayoutsModal()
    {
        if (m_open_manage_layouts)
        {
            ImGui::OpenPopup("Manage Layouts##modal");
            m_open_manage_layouts = false;
        }

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({360.0f, 0.0f}, ImGuiCond_Always);
        if (ImGui::BeginPopupModal("Manage Layouts##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        {
            if (m_custom_layout_count == 0)
            {
                ImGui::TextDisabled("No custom layouts saved yet.");
            }
            else
            {
                int to_delete = -1;
                for (int i = 0; i < m_custom_layout_count; ++i)
                {
                    ImGui::TextUnformatted(m_custom_layouts[i].Name);
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 56.0f);
                    ImGui::PushID(i);
                    if (ImGui::SmallButton("Delete"))
                        to_delete = i;
                    ImGui::PopID();
                }
                if (to_delete >= 0)
                    DeleteCustomLayout(to_delete);
            }

            ImGui::Separator();
            if (ImGui::Button("Close", {100.0f, 0.0f}))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    DockspaceUIComponent::DockspaceUIComponent() {}

    DockspaceUIComponent::~DockspaceUIComponent() {}

    void DockspaceUIComponent::Initialize(Layers::ImguiLayer* parent, const char* name, bool visibility, bool closed)
    {
        UIComponent::Initialize(parent, name, visibility, closed);

        parent->LocalArena.CreateSubArena(ZMega(1), &LocalArena);

        m_editor_serializer = ZPushStructCtor(parent->Arena, Serializers::EditorSceneSerializer);

        m_editor_serializer->Initialize(parent->Arena);

        m_dockspace_node_flag                                  = ImGuiDockNodeFlags_NoWindowMenuButton | static_cast<decltype(ImGuiDockNodeFlags_NoWindowMenuButton)>(ImGuiDockNodeFlags_PassthruCentralNode);
        m_window_flags                                         = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        auto app                                               = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        m_editor_serializer->Context                           = app;

        char editor_serializer_output_buf[MAX_FILE_PATH_COUNT] = {};
        VFSPath::Parse(app->Configuration->ScenePath.c_str()).Value().ResolveNative(app->Configuration->WorkingSpacePath.c_str(), editor_serializer_output_buf, sizeof(editor_serializer_output_buf));
        std::string editor_serializer_default_output = editor_serializer_output_buf;

        m_editor_serializer->SetDefaultOutput(editor_serializer_default_output);
        m_editor_serializer->SetOnProgressCallback(OnEditorSceneSerializerProgress);
        m_editor_serializer->SetOnCompleteCallback(OnEditorSceneSerializerComplete);
        m_editor_serializer->SetOnDeserializeCompleteCallback(OnEditorSceneSerializerDeserializeComplete);
        m_editor_serializer->SetOnLogCallback(OnEditorSceneSerializerLog);
        m_editor_serializer->SetOnErrorCallback(OnEditorSceneSerializerError);

        ApplyTheme(m_active_theme);
        ScanCustomLayouts();
    }

    void DockspaceUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    void DockspaceUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer)
    {
        static constexpr float kStatusBarHeight = 28.0f;
        const ImGuiViewport*   viewport         = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize({viewport->Size.x, viewport->Size.y - kStatusBarHeight});
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
            const auto     window_id         = ImGui::GetID(Name);
            static ImGuiID s_console_dock_id = 0;
            ParentLayer->DockspaceId         = window_id;
            ParentLayer->ConsoleDockId       = s_console_dock_id;

            if (m_pending_layout_path[0] != '\0')
            {
                // Apply a saved .zlayout — runs before DockSpace so DockBuilder is in a clean state
                if (FILE* lf = fopen(m_pending_layout_path, "r"))
                {
                    ImGui::DockBuilderRemoveNode(window_id);
                    ImGui::DockBuilderAddNode(window_id, ImGuiDockNodeFlags_None);
                    ImGui::DockBuilderSetNodeSize(window_id, ImGui::GetMainViewport()->Size);

                    // DockBuilderRemoveNode clears DockId for currently-active windows but
                    // NOT for inactive ones (e.g. Console/Project when their toggle is off).
                    // Those windows still hold the old stale DockId pointing to removed nodes.
                    // When they next appear via the status bar button, ImGui tries to attach
                    // them to those dead nodes and crashes in its internal table code.
                    // Fix: clear DockId for all managed windows — both live and persisted settings.
                    static constexpr cstring kManaged[] = {"Hierarchy", "Inspector", "Scene", "Project", "Console"};
                    for (cstring wname : kManaged)
                    {
                        if (ImGuiWindow* w = ImGui::FindWindowByName(wname))
                            w->DockId = 0;
                        if (ImGuiWindowSettings* ws = ImGui::FindWindowSettingsByID(ImHashStr(wname)))
                            ws->DockId = 0;
                    }

                    LoadNodeFromFile(lf, window_id); // re-assigns via DockBuilderDockWindow for windows in layout
                    ImGui::DockBuilderFinish(window_id);
                    fclose(lf);
                }
                m_pending_layout_path[0] = '\0';
            }
            else if (m_layout_dirty || !ImGui::DockBuilderGetNode(window_id))
            {
                EditorLayout target        = m_layout_dirty ? m_pending_layout : EditorLayout::Default;
                s_console_dock_id          = ApplyBuiltinLayout(window_id, target);
                ParentLayer->ConsoleDockId = s_console_dock_id;
                m_active_layout            = target;
                m_layout_dirty             = false;
            }

            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::DockSpace(window_id, ImVec2(0.0f, 0.0f), m_dockspace_node_flag);
            ImGui::PopStyleVar();
        }

        RenderMenuBar();

        RenderLoadScene();
        RenderSaveScene();
        RenderSaveSceneAs();
        RenderSaveLayoutModal();
        RenderManageLayoutsModal();

        RenderEngineSettingsWindow();
        RenderMemoryProfilerWindow();

        RenderExitPopup();

        ImGui::End();
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

    void DockspaceUIComponent::DrawSettingsIcon(ImDrawList* dl, ImVec2 pos, SettingsPageId id, bool selected)
    {
        const float sz = 14.0f;
        ImU32       col;
        switch (id)
        {
            case SettingsPageId::Theme:
                col = selected ? IM_COL32(170, 110, 255, 255) : IM_COL32(120, 70, 180, 160);
                break;
            case SettingsPageId::Grid:
                col = selected ? IM_COL32(55, 210, 200, 255) : IM_COL32(35, 130, 125, 160);
                break;
            case SettingsPageId::Renderer:
                col = selected ? IM_COL32(255, 165, 50, 255) : IM_COL32(170, 100, 30, 160);
                break;
            default:
                col = selected ? IM_COL32(220, 220, 220, 255) : IM_COL32(140, 140, 140, 180);
                break;
        }

        switch (id)
        {
            case SettingsPageId::Grid:
                for (int i = 1; i <= 3; ++i)
                {
                    float tx = pos.x + sz * i / 4.0f;
                    float ty = pos.y + sz * i / 4.0f;
                    dl->AddLine({tx, pos.y}, {tx, pos.y + sz}, col, 1.2f);
                    dl->AddLine({pos.x, ty}, {pos.x + sz, ty}, col, 1.2f);
                }
                break;
            case SettingsPageId::Renderer:
                dl->AddRect(pos, {pos.x + sz, pos.y + sz}, col, 2.0f, 0, 1.5f);
                dl->AddCircleFilled({pos.x + sz * 0.5f, pos.y + sz * 0.5f}, sz * 0.22f, col, 8);
                break;
            case SettingsPageId::Theme:
            {
                const float cx = pos.x + sz * 0.5f, cy = pos.y + sz * 0.5f;
                dl->AddCircleFilled({cx, cy}, sz * 0.28f, col, 12);
                for (int r = 0; r < 8; ++r)
                {
                    float a  = r * 3.14159f / 4.0f;
                    float r0 = sz * 0.38f, r1 = sz * 0.50f;
                    dl->AddLine({cx + cosf(a) * r0, cy + sinf(a) * r0}, {cx + cosf(a) * r1, cy + sinf(a) * r1}, col, 1.2f);
                }
                break;
            }
            default:
                break;
        }
    }

    void DockspaceUIComponent::RenderEngineSettingsWindow()
    {
        if (!m_open_engine_settings)
            return;

        static constexpr struct
        {
            cstring        Label;
            SettingsPageId Id;
        } kPages[] = {
            {   "Theme",    SettingsPageId::Theme},
            {    "Grid",     SettingsPageId::Grid},
            {"Renderer", SettingsPageId::Renderer},
        };
        static constexpr int kPageCount = static_cast<int>(SettingsPageId::COUNT);

        ImGui::SetNextWindowSize({700, 500}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Engine Settings", &m_open_engine_settings))
        {
            ImGui::End();
            return;
        }

        ImGui::BeginChild("##settings_sidebar", ImVec2(170, 0), true);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < kPageCount; ++i)
        {
            bool active = (m_active_settings_page == kPages[i].Id);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 22.0f);
            if (ImGui::Selectable(kPages[i].Label, active, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 22)))
                m_active_settings_page = kPages[i].Id;
            // Draw icon after Selectable so it renders on top of the selection highlight
            ImVec2 icon_pos = ImGui::GetItemRectMin() + ImVec2{4.0f, 4.0f};
            DrawSettingsIcon(dl, icon_pos, kPages[i].Id, active);
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##settings_content", ImVec2(0, 0), false);
        switch (m_active_settings_page)
        {
            case SettingsPageId::Theme:
                RenderSettingsContentTheme();
                break;
            case SettingsPageId::Grid:
                RenderSettingsContentGrid();
                break;
            case SettingsPageId::Renderer:
                RenderSettingsContentRenderer();
                break;
            default:
                break;
        }
        ImGui::EndChild();

        ImGui::End();
    }

    void DockspaceUIComponent::RenderSettingsContentGrid()
    {
        if (!ParentLayer || !ParentLayer->CurrentApp)
            return;

        auto  app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        auto* current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);
        if (!current_scene)
            return;

        ImGui::TextUnformatted("Grid");
        ImGui::Separator();
        ImGui::Spacing();

        auto& cfg      = current_scene->Grid;
        bool  changed  = false;

        changed       |= ImGui::Checkbox("Show Grid", &cfg.Enabled);
        ImGui::Spacing();
        changed |= ImGui::SliderFloat("Cell Size", &cfg.CellSize, 0.001f, 1.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::SliderFloat("Fade Radius", &cfg.FadeRadius, 10.0f, 2000.0f, "%.1f");
        changed |= ImGui::SliderFloat("Fade Strength", &cfg.FadeStrength, 0.1f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Line Width", &cfg.LineWidth, 0.5f, 4.0f, "%.2f");
        changed |= ImGui::SliderInt("Max LOD", &cfg.MaxLOD, 1, 6);
        changed |= ImGui::SliderFloat("Ground Y", &cfg.GroundY, -100.0f, 100.0f, "%.2f");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        changed |= ImGui::ColorEdit4("Thin Lines", cfg.ColorThin);
        changed |= ImGui::ColorEdit4("Thick Lines", cfg.ColorThick);
        changed |= ImGui::ColorEdit4("X Axis", cfg.ColorXAxis);
        changed |= ImGui::ColorEdit4("Z Axis", cfg.ColorZAxis);

        if (changed)
        {
            current_scene->GridDirty[0].value.store(true, std::memory_order_release);
            current_scene->GridDirty[1].value.store(true, std::memory_order_release);
            current_scene->GridDirty[2].value.store(true, std::memory_order_release);
        }
    }

    void DockspaceUIComponent::RenderSettingsContentRenderer()
    {
        ImGui::TextUnformatted("Renderer");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("No renderer settings yet.");
    }

    void DockspaceUIComponent::RenderMemoryProfilerWindow()
    {
        if (!m_open_memory_profiler)
            return;

        ZEngine::Profiling::MemoryProfiler::Update();

        ImGui::SetNextWindowSize({520, 500}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Memory Profiler", &m_open_memory_profiler))
        {
            ImGui::End();
            return;
        }

        auto                                                             scratch = ZGetScratch(&LocalArena);
        ZEngine::Core::Containers::Array<ZEngine::Profiling::ArenaStats> stats;
        stats.init(scratch.Arena, 32);
        ZEngine::Profiling::MemoryProfiler::GetStats(stats);

        if (stats.size() == 0)
        {
            ImGui::TextDisabled("No arenas tracked — ensure ZENGINE_PROFILING=1.");
            ZReleaseScratch(scratch);
            ImGui::End();
            return;
        }

        auto fmt_bytes = [](uint64_t b, char* buf, size_t n) {
            if (b >= 1024u * 1024u)
                snprintf(buf, n, "%.1f MB", b / (1024.0 * 1024.0));
            else if (b >= 1024u)
                snprintf(buf, n, "%.1f KB", b / 1024.0);
            else
                snprintf(buf, n, "%u B", static_cast<uint32_t>(b));
        };

        // Resize history array if arena count changed
        uint32_t n = static_cast<uint32_t>(stats.size());
        if (n > kMaxArenas)
            n = kMaxArenas;
        if (m_arena_history_count != n)
        {
            for (uint32_t i = m_arena_history_count; i < n; ++i)
                m_arena_history[i] = {};
            m_arena_history_count = n;
        }

        // Append this frame's samples
        for (uint32_t i = 0; i < n; ++i)
        {
            auto& h           = m_arena_history[i];
            float val_mb      = static_cast<float>(stats[i].CurrentOffset) / (1024.0f * 1024.0f);
            h.samples[h.head] = val_mb;
            h.head            = (h.head + 1) % kMemHistorySize;
            if (h.count < kMemHistorySize)
                ++h.count;
        }

        // Header
        uint64_t total_used = 0, total_cap = 0;
        for (uint32_t i = 0; i < n; ++i)
        {
            total_used += stats[i].CurrentOffset;
            total_cap  += stats[i].Capacity;
        }
        char t_used[32], t_cap[32];
        fmt_bytes(total_used, t_used, sizeof(t_used));
        fmt_bytes(total_cap, t_cap, sizeof(t_cap));
        ImGui::Text("Total  %s / %s", t_used, t_cap);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset Peaks"))
            ZEngine::Profiling::MemoryProfiler::ResetPeaks();
        ImGui::Separator();

        const float graph_h = 45.0f;
        const float avail_w = ImGui::GetContentRegionAvail().x;

        for (uint32_t i = 0; i < n; ++i)
        {
            const auto& s        = stats[i];
            auto&       h        = m_arena_history[i];
            float       frac     = s.Capacity > 0 ? static_cast<float>(s.CurrentOffset) / s.Capacity : 0.0f;
            float       cap_mb   = static_cast<float>(s.Capacity) / (1024.0f * 1024.0f);

            // Color by usage level
            ImVec4      line_col = frac > 0.85f ? ImVec4{0.90f, 0.25f, 0.25f, 1.0f} : frac > 0.60f ? ImVec4{0.90f, 0.70f, 0.10f, 1.0f} : ImVec4{0.30f, 0.75f, 0.45f, 1.0f};

            // Label row: name + numbers
            char        used_s[32], peak_s[32], cap_s[32];
            fmt_bytes(s.CurrentOffset, used_s, sizeof(used_s));
            fmt_bytes(s.PeakOffset, peak_s, sizeof(peak_s));
            fmt_bytes(s.Capacity, cap_s, sizeof(cap_s));

            ImGui::PushStyleColor(ImGuiCol_Text, line_col);
            ImGui::TextUnformatted(s.Name ? s.Name : "?");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("%s / %s  (peak %s)", used_s, cap_s, peak_s);

            // Graph
            char graph_id[64];
            snprintf(graph_id, sizeof(graph_id), "##graph_%u", i);
            char overlay[32];
            snprintf(overlay, sizeof(overlay), "%.1f%%", frac * 100.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotLines, line_col);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{line_col.x, line_col.y, line_col.z, 0.08f});
            ImGui::PlotLines(graph_id, h.samples, kMemHistorySize, h.head, overlay, 0.0f, cap_mb > 0.0f ? cap_mb : 1.0f, ImVec2(avail_w, graph_h));
            ImGui::PopStyleColor(2);

            ImGui::Spacing();
        }

        ZReleaseScratch(scratch);
        ImGui::End();
    }

    void DockspaceUIComponent::ApplyTheme(ThemeId theme)
    {
        // Propagate to EditorConfiguration so other components can read it.
        if (ParentLayer && ParentLayer->CurrentApp)
        {
            auto app = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
            if (app->Configuration)
                app->Configuration->DarkTheme = (theme == ThemeId::Dark);
        }

        auto& colors = ImGui::GetStyle().Colors;
        if (theme == ThemeId::Dark)
        {
            ImGui::StyleColorsDark();
            colors[ImGuiCol_WindowBg]           = {0.10f, 0.105f, 0.11f, 1.0f};
            colors[ImGuiCol_Header]             = {0.20f, 0.205f, 0.21f, 1.0f};
            colors[ImGuiCol_HeaderHovered]      = {0.30f, 0.305f, 0.31f, 1.0f};
            colors[ImGuiCol_HeaderActive]       = {0.15f, 0.150f, 0.15f, 1.0f};
            colors[ImGuiCol_Button]             = {0.20f, 0.205f, 0.21f, 1.0f};
            colors[ImGuiCol_ButtonHovered]      = {0.30f, 0.305f, 0.31f, 1.0f};
            colors[ImGuiCol_ButtonActive]       = {0.15f, 0.150f, 0.15f, 1.0f};
            colors[ImGuiCol_FrameBg]            = {0.20f, 0.205f, 0.21f, 1.0f};
            colors[ImGuiCol_FrameBgHovered]     = {0.30f, 0.305f, 0.31f, 1.0f};
            colors[ImGuiCol_FrameBgActive]      = {0.15f, 0.150f, 0.15f, 1.0f};
            colors[ImGuiCol_Tab]                = {0.15f, 0.150f, 0.15f, 1.0f};
            colors[ImGuiCol_TabHovered]         = {0.38f, 0.380f, 0.38f, 1.0f};
            colors[ImGuiCol_TabActive]          = {0.28f, 0.280f, 0.28f, 1.0f};
            colors[ImGuiCol_TabUnfocused]       = {0.15f, 0.150f, 0.15f, 1.0f};
            colors[ImGuiCol_TabUnfocusedActive] = {0.20f, 0.205f, 0.21f, 1.0f};
            colors[ImGuiCol_TitleBg]            = {0.15f, 0.150f, 0.15f, 1.0f};
            colors[ImGuiCol_TitleBgActive]      = {0.15f, 0.150f, 0.15f, 1.0f};
            colors[ImGuiCol_TitleBgCollapsed]   = {0.15f, 0.150f, 0.15f, 1.0f};
            colors[ImGuiCol_DockingPreview]     = {0.20f, 0.205f, 0.21f, 0.5f};
            colors[ImGuiCol_SeparatorHovered]   = {1.00f, 1.000f, 1.00f, 0.5f};
            colors[ImGuiCol_SeparatorActive]    = {1.00f, 1.000f, 1.00f, 0.5f};
            colors[ImGuiCol_CheckMark]          = {1.00f, 1.000f, 1.00f, 1.0f};
            colors[ImGuiCol_PlotHistogram]      = {1.00f, 1.000f, 1.00f, 1.0f};
        }
        else
        {
            // Option C: pure neutral charcoal — all grays have R=G=B so zero colour cast.
            ImGui::StyleColorsLight();

            // -- palette (all R=G=B) --
            static constexpr ImVec4 kText          = {0.110f, 0.110f, 0.110f, 1.0f}; // #1C1C1C
            static constexpr ImVec4 kInk           = {0.110f, 0.110f, 0.110f, 1.0f}; // #1C1C1C  title active
            static constexpr ImVec4 kDark          = {0.235f, 0.235f, 0.235f, 1.0f}; // #3C3C3C  pressed / marks
            static constexpr ImVec4 kMid           = {0.361f, 0.361f, 0.361f, 1.0f}; // #5C5C5C  hover grabs
            static constexpr ImVec4 kBorder        = {0.878f, 0.878f, 0.878f, 1.0f}; // #E0E0E0
            static constexpr ImVec4 kBgAlt         = {0.941f, 0.941f, 0.941f, 1.0f}; // #F0F0F0
            static constexpr ImVec4 kBg            = {0.973f, 0.973f, 0.973f, 1.0f}; // #F8F8F8
            static constexpr ImVec4 kSel           = {0.863f, 0.863f, 0.863f, 1.0f}; // #DCDCDC  selection fill
            static constexpr ImVec4 kWhite         = {1.000f, 1.000f, 1.000f, 1.0f};

            // -- all colors that StyleColorsLight leaves blue-tinted --
            colors[ImGuiCol_Text]                  = kText;
            colors[ImGuiCol_WindowBg]              = kBg;
            colors[ImGuiCol_ChildBg]               = kWhite;
            colors[ImGuiCol_PopupBg]               = kWhite;
            colors[ImGuiCol_Border]                = kBorder;
            colors[ImGuiCol_FrameBg]               = {0.910f, 0.910f, 0.910f, 1.0f}; // #E8E8E8 visible trough
            colors[ImGuiCol_FrameBgHovered]        = {0.863f, 0.863f, 0.863f, 1.0f}; // slightly darker on hover
            colors[ImGuiCol_FrameBgActive]         = {0.780f, 0.780f, 0.780f, 1.0f};
            colors[ImGuiCol_TitleBg]               = kBgAlt;
            colors[ImGuiCol_TitleBgActive]         = {0.780f, 0.780f, 0.780f, 1.0f}; // #C7C7C7 — medium gray
            colors[ImGuiCol_TitleBgCollapsed]      = kBgAlt;
            colors[ImGuiCol_MenuBarBg]             = kBg;
            colors[ImGuiCol_ScrollbarBg]           = kBg;
            colors[ImGuiCol_ScrollbarGrab]         = kBorder;
            colors[ImGuiCol_ScrollbarGrabHovered]  = kMid;
            colors[ImGuiCol_ScrollbarGrabActive]   = kDark;
            colors[ImGuiCol_CheckMark]             = kDark;
            colors[ImGuiCol_SliderGrab]            = kMid;
            colors[ImGuiCol_SliderGrabActive]      = kDark;
            colors[ImGuiCol_Button]                = kBorder;
            colors[ImGuiCol_ButtonHovered]         = kBgAlt;
            colors[ImGuiCol_ButtonActive]          = kDark;
            colors[ImGuiCol_Header]                = kSel;
            colors[ImGuiCol_HeaderHovered]         = kBgAlt;
            colors[ImGuiCol_HeaderActive]          = kBorder;
            colors[ImGuiCol_ResizeGrip]            = {kBorder.x, kBorder.y, kBorder.z, 0.5f};
            colors[ImGuiCol_ResizeGripHovered]     = kMid;
            colors[ImGuiCol_ResizeGripActive]      = kDark;
            colors[ImGuiCol_Separator]             = kBorder;
            colors[ImGuiCol_SeparatorHovered]      = kMid;
            colors[ImGuiCol_SeparatorActive]       = kDark;
            colors[ImGuiCol_Tab]                   = kBgAlt;
            colors[ImGuiCol_TabHovered]            = kSel;
            colors[ImGuiCol_TabActive]             = kWhite;
            colors[ImGuiCol_TabUnfocused]          = kBgAlt;
            colors[ImGuiCol_TabUnfocusedActive]    = kBg;
            colors[ImGuiCol_NavHighlight]          = {kDark.x, kDark.y, kDark.z, 0.7f};
            colors[ImGuiCol_NavWindowingHighlight] = {kDark.x, kDark.y, kDark.z, 0.7f};
            colors[ImGuiCol_NavWindowingDimBg]     = {kDark.x, kDark.y, kDark.z, 0.2f};
            colors[ImGuiCol_DockingPreview]        = {kDark.x, kDark.y, kDark.z, 0.4f};
            colors[ImGuiCol_TextSelectedBg]        = {kSel.x, kSel.y, kSel.z, 0.6f};
            colors[ImGuiCol_PlotLines]             = kMid;
            colors[ImGuiCol_PlotLinesHovered]      = kDark;
            colors[ImGuiCol_PlotHistogram]         = kMid;
            colors[ImGuiCol_PlotHistogramHovered]  = kDark;
        }
    }

    void DockspaceUIComponent::RenderSettingsContentTheme()
    {
        ImGui::TextUnformatted("Theme");
        ImGui::Separator();
        ImGui::Spacing();

        auto render_theme_card = [](bool active, cstring label, cstring desc, ImVec4 preview_bg, ImVec4 preview_text) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, preview_bg);
            ImGui::PushStyleColor(ImGuiCol_Border, active ? ImVec4{0.30f, 0.55f, 1.0f, 1.0f} : ImVec4{0.50f, 0.50f, 0.50f, 0.40f});
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, active ? 2.0f : 1.0f);
            ImGui::BeginChild(label, ImVec2(160, 70), true);
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, preview_text);
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
            ImGui::TextDisabled("%s", desc);
            ImGui::EndChild();
        };

        {
            bool active = (m_active_theme == ThemeId::Dark);
            render_theme_card(active, "Dark", "Dark background", ImVec4{0.15f, 0.15f, 0.17f, 1.0f}, ImVec4{0.90f, 0.90f, 0.90f, 1.0f});
            if (ImGui::IsItemClicked() && !active)
            {
                m_active_theme = ThemeId::Dark;
                ApplyTheme(ThemeId::Dark);
            }
        }
        ImGui::SameLine();
        {
            bool active = (m_active_theme == ThemeId::Light);
            render_theme_card(active, "Light", "Light background", ImVec4{0.94f, 0.94f, 0.94f, 1.0f}, ImVec4{0.12f, 0.12f, 0.12f, 1.0f});
            if (ImGui::IsItemClicked() && !active)
            {
                m_active_theme = ThemeId::Light;
                ApplyTheme(ThemeId::Light);
            }
        }
    }

    void DockspaceUIComponent::ResetSaveAsBuffers()
    {
        ZEngine::Helpers::secure_memset(s_save_as_input_buffer, 0, IM_ARRAYSIZE(s_save_as_input_buffer), IM_ARRAYSIZE(s_save_as_input_buffer));
    }

    void DockspaceUIComponent::OnEditorSceneSerializerError(void* const, std::string_view msg)
    {
        ZENGINE_CORE_ERROR("{}", msg)
    }

    void DockspaceUIComponent::OnEditorSceneSerializerLog(void* const, std::string_view msg)
    {
        ZEngine::Helpers::secure_strcpy(s_scene_serializer_log, DEFAULT_STR_BUFFER, msg.data());
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

                if (ImGui::MenuItem("Import New Asset..."))
                {
                    auto app_ptr                          = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
                    app_ptr->Configuration->ShowImporter  = true;
                    app_ptr->Configuration->FocusImporter = true;
                }
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
                ImGui::MenuItem("Engine", NULL, &m_open_engine_settings);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Performances"))
            {
                ImGui::MenuItem("Memory Profiler", NULL, &m_open_memory_profiler);
                ImGui::EndMenu();
            }

            RenderLayoutMenu();

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
        current_scene->HasPendingChanges.value.store(false, std::memory_order_release);
    }

    void DockspaceUIComponent::OnEditorSceneSerializerDeserializeComplete(void* const context, EditorScene&& scene)
    {
        auto app           = reinterpret_cast<EditorPtr>(context);
        auto current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);

        // Todo : Ensure no data race on CurrentScenePtr
        app->Configuration->ActiveSceneName.clear();
        app->Configuration->ActiveSceneName.append(scene.Name);

        current_scene->MarkDirty(true);
        current_scene->SelectedInstanceId.value.store(-1, std::memory_order_release);
        current_scene->Reset();
        current_scene->ExtractAsync(scene);

        current_scene->Name = app->Configuration->ActiveSceneName.c_str();

        // Copy sky config from deserialized scene; resolve env map filename to absolute path
        current_scene->Sky.Mode.init(&current_scene->LocalArena, scene.Sky.Mode.empty() ? "atmosphere" : scene.Sky.Mode.c_str());
        if (!scene.Sky.EnvironmentMap.empty() && !app->Configuration->EnvironmentMapImportPath.empty())
        {
            auto abs_env = fmt::format("{}/{}", app->Configuration->EnvironmentMapImportPath.c_str(), scene.Sky.EnvironmentMap.c_str());
            current_scene->Sky.EnvironmentMap.init(&current_scene->LocalArena, abs_env.c_str());
        }
        current_scene->SkyDirty[0].value.store(true, std::memory_order_release);
        current_scene->SkyDirty[1].value.store(true, std::memory_order_release);
        current_scene->SkyDirty[2].value.store(true, std::memory_order_release);

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
        ZEngine::Importers::AssetCodec::AssetMeshFileHeader header;
        if (!ZEngine::Importers::AssetCodec::ReadAssetMeshFileHeader(filename, header))
            co_return;

        // If the mesh isn't in memory (fresh session), ingest it from disk first
        // so both the CPU asset registry and the RRM GPU buffers are populated.
        if (!ZEngine::Managers::AssetManager::GetAsset<ZEngine::Importers::AssetMesh>(header.Id))
        {
            // Phase 1: deserialize mesh. Copy material names to stack before releasing
            // the scratch — material names live in scratch.Arena and are invalidated on release.
            static constexpr size_t kMaxMaterials                 = 64;
            char                    mat_names[kMaxMaterials][256] = {};
            size_t                  mat_count                     = 0;

            {
                auto                                   scratch = ZGetScratch(&LocalArena);
                ZEngine::Importers::AssetMesh          mesh{};
                ZEngine::Importers::AssetNodeHierarchy hier{};
                ZEngine::Importers::AssetCodec::DeserializeMeshAssetFile(scratch.Arena, filename, mesh, hier);

                mat_count = hier.MaterialNames.size() < kMaxMaterials ? hier.MaterialNames.size() : kMaxMaterials;
                for (size_t i = 0; i < mat_count; ++i)
                    ZEngine::Helpers::secure_strncpy(mat_names[i], sizeof(mat_names[i]), hier.MaterialNames[i].c_str(), sizeof(mat_names[i]) - 1);

                ZEngine::Managers::AssetManager::IngestMesh(std::move(mesh), std::move(hier));
                ZReleaseScratch(scratch);
            }

            // Phase 2: load associated .zematerial files in a fresh scratch so mesh and
            // material deserializations never compete for the same 1 MB LocalArena.
            auto* app_cfg = ParentLayer && ParentLayer->CurrentApp ? reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp)->Configuration : nullptr;
            if (app_cfg)
            {
                for (size_t i = 0; i < mat_count; ++i)
                {
                    char mat_path[MAX_FILE_PATH_COUNT] = {};
                    snprintf(mat_path, sizeof(mat_path), "%s/%s/%s.zematerial", app_cfg->WorkingSpacePath.c_str(), app_cfg->MaterialPath.c_str(), mat_names[i]);

                    auto                              scratch = ZGetScratch(&LocalArena);
                    ZEngine::Importers::AssetMaterial mat{};
                    ZEngine::Importers::AssetCodec::DeserializeMaterialAssetFile(scratch.Arena, mat_path, mat);
                    if (!mat.MaterialUUID.is_nil())
                        ZEngine::Managers::AssetManager::IngestMaterial(std::move(mat));
                    ZReleaseScratch(scratch);
                }
            }
        }

        auto        app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        auto        current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);
        const char* name          = strrchr(filename, '/');
        name                      = name ? name + 1 : filename;
        current_scene->SpawnMeshActor(header.Id, name);

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
