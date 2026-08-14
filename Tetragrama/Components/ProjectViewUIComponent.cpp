#include <Tetragrama/Components/ContentBrowserIcons.h>
#include <Tetragrama/Components/ProjectViewUIComponent.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/Helpers/SearchPatternAlgorithm.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <imgui.h>
#include <cstdio>
#include <cstring>

using namespace ZEngine::Helpers;
using namespace ZEngine::Core::VFS;

namespace Tetragrama::Components
{
    ProjectViewUIComponent::ProjectViewUIComponent() {}

    ProjectViewUIComponent::~ProjectViewUIComponent() {}

    void ProjectViewUIComponent::Initialize(Layers::ImguiLayer* parent, const char* name, bool visibility, bool closed)
    {
        UIComponent::Initialize(parent, name, visibility, closed);
        parent->LocalArena.CreateSubArena(ZMega(1), &m_local_arena);

        m_vfs_context     = ZEngine::Engine::GetContext()->VFS;
        m_directory_cache = &ParentLayer->Cache;
        m_scanner         = &ParentLayer->Scanner;

        m_assets_vfs_root = VFSPath::Root();
        m_current_vfs_dir = m_assets_vfs_root;

        {
            cstring ws = ParentLayer->CurrentApp->WorkingSpacePath;
            secure_strcpy(m_workspace_root, sizeof(m_workspace_root), ws ? ws : "");
            const char* slash = strrchr(ws, '/');
            const char* label = (slash && slash[1] != '\0') ? slash + 1 : ws;
            secure_strcpy(m_root_label, sizeof(m_root_label), label);
        }

        // Reset popup input buffers to their defaults.
        secure_strcpy(m_popup_new_file_name, sizeof(m_popup_new_file_name), "NewFile.txt");
        secure_strcpy(m_popup_new_folder_name, sizeof(m_popup_new_folder_name), "New Folder");
        m_popup_rename_name[0]     = '\0';
        m_popup_rename_initialized = false;

        TriggerScan();
    }

    void ProjectViewUIComponent::TriggerScan()
    {
        if (m_scanner && m_vfs_context && m_directory_cache)
        {
            m_scanner->Scan(m_vfs_context, m_assets_vfs_root, m_directory_cache);
        }
    }

    void ProjectViewUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    void ProjectViewUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer)
    {
        ImGui::Begin(Name, (CanBeClosed ? &CanBeClosed : NULL), ImGuiWindowFlags_NoCollapse);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
        ImVec2 current_win_size = ImGui::GetContentRegionAvail();
        if (ImGui::BeginTable("#AssetBrowserArea", 2, ImGuiTableFlags_Resizable, current_win_size))
        {
            /*Left Pane*/
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            RenderTreeBrowser();

            /*Right Pane*/
            ImGui::TableSetColumnIndex(1);
            {
                ImVec2 col_min       = ImGui::GetCursorScreenPos();
                ImVec2 avail         = ImGui::GetContentRegionAvail();
                ImVec2 col_max       = {col_min.x + avail.x, col_min.y + avail.y};
                m_right_pane_hovered = ImGui::IsMouseHoveringRect(col_min, col_max, false);
            }
            if (m_right_pane_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                ImGui::OpenPopup("ContextMenu");
            }
            if (ImGui::BeginPopup("ContextMenu"))
            {
                char native[MAX_FILE_PATH_COUNT];
                m_current_vfs_dir.ResolveNative(m_workspace_root, native, sizeof(native));
                RenderContextMenu(ContextMenuType::RightPane, native);
                ImGui::EndPopup();
            }

            RenderBackButton();

            ImGui::SameLine();
            if (ImGui::Button("Refresh"))
            {
                TriggerScan();
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(300);
            ImGui::InputTextWithHint("##Search", "Search ...", m_search_buffer, IM_ARRAYSIZE(m_search_buffer));

            ImGui::SameLine();
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
            ImGui::Text("%s", m_current_vfs_dir.CStr());
            ImGui::PopFont();

            ImGui::Separator();

            RenderContentBrowser(renderer);

            ImGui::EndTable();
        }

        // Modals must be opened in the root window context, not inside a table cell,
        // so that the backdrop covers the full window and clipping is correct.
        RenderPopUpMenu();

        ImGui::PopStyleVar();

        ImGui::End();
    }

    void ProjectViewUIComponent::RenderContentBrowser(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer)
    {
        const float padding     = 16.0f;
        const float cellSize    = m_thumbnail_size + padding;
        const float panelWidth  = ImGui::GetContentRegionAvail().x;
        const int   columnCount = std::max(1, static_cast<int>(panelWidth / cellSize));

        if (ImGui::BeginTable("GridTable", columnCount))
        {
            if (auto len = secure_strlen(m_search_buffer))
            {
                // Rebuild cached results only when the query changes.
                if (strcmp(m_search_buffer, m_last_search) != 0)
                {
                    secure_strcpy(m_last_search, sizeof(m_last_search), m_search_buffer);
                    m_search_results.clear();

                    char search_term_lower[MAX_FILE_PATH_COUNT] = {};
                    for (size_t i = 0; i < len && i < sizeof(search_term_lower) - 1; ++i)
                        search_term_lower[i] = static_cast<char>(::tolower(static_cast<unsigned char>(m_search_buffer[i])));

                    auto scratch = ZGetScratch(&m_local_arena);
                    m_directory_cache->ForEachDir([&](const VFSPath& /*dir*/, ZEngine::Core::Containers::ArrayView<const VFSDirEntry> entries) {
                        for (size_t i = 0; i < entries.size(); ++i)
                        {
                            char name_lower[MAX_FILE_PATH_COUNT] = {};
                            char raw[MAX_FILE_PATH_COUNT];
                            entries[i].Path.CopyFilename(raw, sizeof(raw));
                            size_t rlen = secure_strlen(raw);
                            for (size_t j = 0; j < rlen && j < sizeof(name_lower) - 1; ++j)
                                name_lower[j] = static_cast<char>(::tolower(static_cast<unsigned char>(raw[j])));

                            if (!entries[i].Path.Extension().Equals(".meta") && Helpers::KMPSearch(scratch.Arena, name_lower, search_term_lower))
                                m_search_results.push_back(entries[i]);
                        }
                    });
                    ZReleaseScratch(scratch);
                }

                for (const auto& entry : m_search_results)
                {
                    ImGui::TableNextColumn();
                    RenderContentTile(renderer, entry);
                }
            }
            else
            {
                if (m_last_search[0] != '\0')
                {
                    m_last_search[0] = '\0';
                    m_search_results.clear();
                }
                auto listing = m_directory_cache->GetListing(m_current_vfs_dir);
                for (size_t i = 0; i < listing.size(); ++i)
                {
                    if (listing[i].Path.Extension().Equals(".meta"))
                        continue;
                    ImGui::TableNextColumn();
                    RenderContentTile(renderer, listing[i]);
                }
            }
            ImGui::EndTable();
        }
    }

    void ProjectViewUIComponent::RenderContentTile(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, const VFSDirEntry& entry)
    {
        char name[MAX_FILE_PATH_COUNT];
        entry.Path.CopyFilename(name, sizeof(name));

        // --- UE thumbnail-first card layout ---
        // Icon fills the top portion, name overlaid on a semi-transparent footer strip.
        const float sz       = m_thumbnail_size;
        const float pad      = 6.0f;
        const float line_h   = ImGui::GetTextLineHeightWithSpacing();
        const float footer_h = line_h * 2.0f + pad * 2.0f; // fixed 2-line footer
        const float card_w   = sz + pad * 2.0f;
        const float card_h   = sz + footer_h;
        const float rounding = 4.0f;

        ImGui::PushID(entry.Path.CStr());

        ImVec2     origin   = ImGui::GetCursorScreenPos();
        ImVec2     card_end = {origin.x + card_w, origin.y + card_h};
        const bool hov      = ImGui::IsMouseHoveringRect(origin, card_end);

        // Invisible button for interaction (hover, click, drag-drop)
        ImGui::InvisibleButton("##card", {card_w, card_h});

        // Drag-and-drop (files only)
        if (!entry.IsDirectory && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            char native[MAX_FILE_PATH_COUNT];
            entry.Path.ResolveNative(m_workspace_root, native, sizeof(native));
            ImGui::SetDragDropPayload("CONTENT_BROWSER_FILE_DRAG_OP", native, secure_strlen(native) + 1);
            ImGui::EndDragDropSource();
        }

        // Navigate into directory on double-click
        if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && entry.IsDirectory)
        {
            m_current_vfs_dir = entry.Path;
            secure_memset(m_search_buffer, 0, sizeof(m_search_buffer), sizeof(m_search_buffer));
        }

        // Context menu
        if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup("ItemContextMenu");
        if (ImGui::BeginPopup("ItemContextMenu"))
        {
            char native[MAX_FILE_PATH_COUNT];
            entry.Path.ResolveNative(m_workspace_root, native, sizeof(native));
            entry.IsDirectory ? RenderContextMenu(ContextMenuType::Folder, native) : RenderContextMenu(ContextMenuType::File, native);
            ImGui::EndPopup();
        }

        // ---- DrawList rendering ----
        ImDrawList* dl       = ImGui::GetWindowDrawList();
        ImVec2      icon_end = {origin.x + card_w, origin.y + sz};

        // Card body background
        dl->AddRectFilled(origin, card_end, hov ? IM_COL32(70, 70, 70, 200) : IM_COL32(42, 42, 42, 140), rounding);

        // --- Icon (vector, centered in the thumbnail area) ---
        // When a per-asset thumbnail is ready, call
        //   dl->AddImage((ImTextureID)(intptr_t)thumb.Index, ixo, {ixo.x + ic, ixo.y + ic * 0.92f})
        // instead of DrawContentIcon().
        const float ic  = sz * 0.85f;
        const float ofx = (sz - ic) * 0.5f + pad;
        const float ofy = (sz - ic * 0.92f) * 0.5f;
        ImVec2      ixo = {origin.x + ofx, origin.y + ofy};

        DrawContentIcon(dl, ixo, ic, GetContentIconType(entry.IsDirectory, entry.Path.Extension()), true);

        // Semi-transparent footer strip
        ImVec2 footer_min = {origin.x, origin.y + sz};
        dl->AddRectFilled(footer_min, card_end, IM_COL32(0, 0, 0, 140), rounding, ImDrawFlags_RoundCornersBottom);

        // Name text inside footer (single line, truncated)
        {
            const float  text_x = footer_min.x + pad;
            const float  text_y = footer_min.y + pad;
            const float  max_w  = card_w - pad * 2.0f;
            const ImVec4 clip   = {text_x, text_y, text_x + max_w, text_y + line_h * 2.0f};
            dl->AddText(nullptr, 0.0f, {text_x, text_y}, IM_COL32(230, 230, 230, 255), name, nullptr, max_w, &clip);
        }

        ImGui::PopID();
    }

    void ProjectViewUIComponent::RenderFilteredContent(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, const char* searchTerm)
    {
        auto scratch = ZGetScratch(&m_local_arena);
        char name_lower[MAX_FILE_PATH_COUNT];

        m_directory_cache->ForEachDir([&](const VFSPath& /*dir*/, ZEngine::Core::Containers::ArrayView<const VFSDirEntry> entries) {
            for (size_t i = 0; i < entries.size(); ++i)
            {
                const VFSDirEntry& entry = entries[i];

                char               raw[MAX_FILE_PATH_COUNT];
                entry.Path.CopyFilename(raw, sizeof(raw));

                size_t len  = secure_strlen(raw);
                size_t copy = (len < sizeof(name_lower) - 1) ? len : sizeof(name_lower) - 1;
                for (size_t j = 0; j < copy; ++j)
                {
                    name_lower[j] = static_cast<char>(::tolower(static_cast<unsigned char>(raw[j])));
                }
                name_lower[copy] = '\0';

                if (!entry.Path.Extension().Equals(".meta") && Helpers::KMPSearch(scratch.Arena, name_lower, searchTerm))
                {
                    ImGui::TableNextColumn();
                    RenderContentTile(renderer, entry);
                }
            }
        });

        ZReleaseScratch(scratch);
    }

    // Draws a small folder icon at `pos` with text-line height, using the
    // same golden palette as the content browser tiles.
    static void DrawTreeFolderIcon(ImDrawList* dl, ImVec2 pos, float line_h)
    {
        const float sz       = line_h * 0.80f;
        const float off_y    = (line_h - sz) * 0.50f;
        const float tab_w    = sz * 0.42f;
        const float tab_h    = sz * 0.18f;
        const float body_top = pos.y + off_y + tab_h;
        const ImU32 tab_col  = IM_COL32(220, 195, 120, 255);
        const ImU32 body_col = IM_COL32(200, 175, 100, 255);
        dl->AddRectFilled({pos.x, pos.y + off_y}, {pos.x + tab_w, body_top + 1.0f}, tab_col, 1.0f);
        dl->AddRectFilled({pos.x, body_top}, {pos.x + sz, pos.y + off_y + sz * 0.88f}, body_col, 1.0f);
    }

    void ProjectViewUIComponent::RenderTreeBrowser()
    {
        ImGui::PushID("##root");
        bool nodeOpen     = ImGui::TreeNodeEx("##", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen);
        bool clicked      = ImGui::IsItemClicked();
        bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

        // Folder icon + root label inline
        ImGui::SameLine();
        {
            float  lh  = ImGui::GetTextLineHeight();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            DrawTreeFolderIcon(ImGui::GetWindowDrawList(), pos, lh);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + lh * 0.80f + 3.0f);
        }
        ImGui::TextUnformatted(m_root_label);
        clicked      |= ImGui::IsItemClicked();
        rightClicked |= ImGui::IsItemClicked(ImGuiMouseButton_Right);
        ImGui::PopID();

        if (clicked)
        {
            m_current_vfs_dir = m_assets_vfs_root;
            secure_memset(m_search_buffer, 0, sizeof(m_search_buffer), sizeof(m_search_buffer));
        }
        if (rightClicked)
            ImGui::OpenPopup("RootContextMenu");

        if (ImGui::BeginPopup("RootContextMenu"))
        {
            char native[MAX_FILE_PATH_COUNT];
            m_assets_vfs_root.ResolveNative(m_workspace_root, native, sizeof(native));
            RenderContextMenu(ContextMenuType::LeftPane, native);
            ImGui::EndPopup();
        }

        if (nodeOpen)
        {
            RenderDirectoryNode(m_assets_vfs_root);
            ImGui::TreePop();
        }
    }

    void ProjectViewUIComponent::RenderDirectoryNode(const VFSPath& directory)
    {
        auto listing = m_directory_cache->GetListing(directory);
        for (size_t i = 0; i < listing.size(); ++i)
        {
            const VFSDirEntry& entry = listing[i];
            if (!entry.IsDirectory)
                continue;

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
            if (entry.Path == m_current_vfs_dir)
                flags |= ImGuiTreeNodeFlags_Selected;

            char label[MAX_FILE_PATH_COUNT];
            entry.Path.CopyFilename(label, sizeof(label));

            char popup_id[MAX_FILE_PATH_COUNT + 4 + 1];
            std::snprintf(popup_id, sizeof(popup_id), "Dir_%s", entry.Path.CStr());

            ImGui::PushID(entry.Path.CStr());
            bool nodeOpen     = ImGui::TreeNodeEx("##", flags);
            bool clicked      = ImGui::IsItemClicked();
            bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

            // Folder icon + label inline
            ImGui::SameLine();
            {
                float  lh  = ImGui::GetTextLineHeight();
                ImVec2 pos = ImGui::GetCursorScreenPos();
                DrawTreeFolderIcon(ImGui::GetWindowDrawList(), pos, lh);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + lh * 0.80f + 3.0f);
            }
            ImGui::TextUnformatted(label);
            clicked      |= ImGui::IsItemClicked();
            rightClicked |= ImGui::IsItemClicked(ImGuiMouseButton_Right);
            ImGui::PopID();

            if (clicked)
            {
                m_current_vfs_dir = entry.Path;
                secure_memset(m_search_buffer, 0, sizeof(m_search_buffer), sizeof(m_search_buffer));
            }
            if (rightClicked)
                ImGui::OpenPopup(popup_id);

            if (ImGui::BeginPopup(popup_id))
            {
                char native[MAX_FILE_PATH_COUNT];
                entry.Path.ResolveNative(m_workspace_root, native, sizeof(native));
                RenderContextMenu(ContextMenuType::LeftPane, native);
                ImGui::EndPopup();
            }

            if (nodeOpen)
            {
                RenderDirectoryNode(entry.Path);
                ImGui::TreePop();
            }
        }
    }

    void ProjectViewUIComponent::HandleCreateFilePopup(const char* path)
    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Create New File", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Enter file name (with extension):");
            ImGui::InputText("##create", m_popup_new_file_name, sizeof(m_popup_new_file_name));

            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                if (secure_strlen(m_popup_new_file_name) > 0)
                {
                    char new_native[MAX_FILE_PATH_COUNT];
                    snprintf(new_native, sizeof(new_native), "%s/%s", path, m_popup_new_file_name);

                    size_t      ws_len  = secure_strlen(m_workspace_root);
                    const char* rel     = (ws_len > 0 && std::strncmp(new_native, m_workspace_root, ws_len) == 0) ? new_native + ws_len : new_native;
                    auto        vfs_res = VFSPath::Parse(rel);

                    if (vfs_res.Succeeded())
                    {
                        auto exists = m_vfs_context->Exists(vfs_res.Value());
                        if (!exists.Succeeded() || !exists.Value())
                        {
                            auto file_res = m_vfs_context->Open(vfs_res.Value(), VFSOpenFlags::Write);
                            if (file_res.Succeeded())
                            {
                                m_vfs_context->Close(file_res.Value());
                                m_active_popup = PopupType::None;
                                TriggerScan();
                                ImGui::CloseCurrentPopup();
                            }
                            else
                            {
                                ZENGINE_CORE_ERROR("Failed to create file: {}", m_popup_new_file_name);
                            }
                        }
                        else
                        {
                            ZENGINE_CORE_ERROR("A file with the name {} already exists!", m_popup_new_file_name);
                        }
                    }
                }
                else
                {
                    ZENGINE_CORE_ERROR("File name cannot be empty.");
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                m_active_popup = PopupType::None;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void ProjectViewUIComponent::HandleCreateFolderPopup(const char* path)
    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Create New Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Enter folder name:");
            ImGui::InputText("##create", m_popup_new_folder_name, sizeof(m_popup_new_folder_name));

            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                if (secure_strlen(m_popup_new_folder_name) > 0)
                {
                    char new_native[MAX_FILE_PATH_COUNT];
                    snprintf(new_native, sizeof(new_native), "%s/%s", path, m_popup_new_folder_name);

                    size_t      ws_len  = secure_strlen(m_workspace_root);
                    const char* rel     = (ws_len > 0 && std::strncmp(new_native, m_workspace_root, ws_len) == 0) ? new_native + ws_len : new_native;
                    auto        vfs_res = VFSPath::Parse(rel);

                    if (vfs_res.Succeeded())
                    {
                        auto exists = m_vfs_context->Exists(vfs_res.Value());
                        if (!exists.Succeeded() || !exists.Value())
                        {
                            m_vfs_context->CreateDir(vfs_res.Value());
                            m_active_popup = PopupType::None;
                            TriggerScan();
                            ImGui::CloseCurrentPopup();
                        }
                        else
                        {
                            ZENGINE_CORE_ERROR("A folder with the name {} already exists!", m_popup_new_folder_name);
                        }
                    }
                }
                else
                {
                    ZENGINE_CORE_ERROR("Folder name cannot be empty.");
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                m_active_popup = PopupType::None;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void ProjectViewUIComponent::HandleRenameFolderPopup(const char* path)
    {
        char root_native[MAX_FILE_PATH_COUNT];
        m_assets_vfs_root.ResolveNative(m_workspace_root, root_native, sizeof(root_native));
        if (strcmp(m_popup_target_path, root_native) == 0)
        {
            ZENGINE_CORE_ERROR("Cannot rename root folder");
            m_active_popup = PopupType::None;
            return;
        }

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Rename Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (!m_popup_rename_initialized)
            {
                const char* slash = strrchr(path, '/');
                secure_strcpy(m_popup_rename_name, sizeof(m_popup_rename_name), slash ? slash + 1 : path);
                m_popup_rename_initialized = true;
            }

            ImGui::Text("Enter new folder name:");
            ImGui::InputText("##rename", m_popup_rename_name, sizeof(m_popup_rename_name));

            if (ImGui::Button("Rename", ImVec2(120, 0)))
            {
                if (secure_strlen(m_popup_rename_name) > 0)
                {
                    const char* last_slash                  = strrchr(path, '/');
                    char        parent[MAX_FILE_PATH_COUNT] = {};
                    if (last_slash && last_slash > path)
                        secure_strncpy(parent, sizeof(parent), path, (size_t) (last_slash - path));

                    char new_native[MAX_FILE_PATH_COUNT];
                    snprintf(new_native, sizeof(new_native), "%s/%s", parent, m_popup_rename_name);

                    size_t ws_len = secure_strlen(m_workspace_root);
                    auto   to_vfs = [&](const char* n) {
                        const char* rel = (ws_len > 0 && std::strncmp(n, m_workspace_root, ws_len) == 0) ? n + ws_len : n;
                        return VFSPath::Parse(rel);
                    };
                    auto src_res = to_vfs(path);
                    auto dst_res = to_vfs(new_native);

                    if (src_res.Succeeded() && dst_res.Succeeded())
                    {
                        auto exists = m_vfs_context->Exists(dst_res.Value());
                        if (!exists.Succeeded() || !exists.Value())
                        {
                            m_vfs_context->Rename(src_res.Value(), dst_res.Value());
                            m_current_vfs_dir          = m_assets_vfs_root;
                            m_active_popup             = PopupType::None;
                            m_popup_rename_initialized = false;
                            TriggerScan();
                            ImGui::CloseCurrentPopup();
                        }
                        else
                        {
                            ZENGINE_CORE_ERROR("A folder with the name {} already exists!", m_popup_rename_name);
                        }
                    }
                }
                else
                {
                    ZENGINE_CORE_ERROR("Folder name cannot be empty.");
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                m_active_popup             = PopupType::None;
                m_popup_rename_initialized = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void ProjectViewUIComponent::HandleDeleteFilePopup(const char* path)
    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Delete File", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const char* filename = strrchr(path, '/');
            filename             = filename ? filename + 1 : path;

            ImGui::Text("Are you sure you want to delete this file?");
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", filename);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Delete", ImVec2(120, 0)))
            {
                size_t      ws_len  = secure_strlen(m_workspace_root);
                const char* rel     = (ws_len > 0 && std::strncmp(path, m_workspace_root, ws_len) == 0) ? path + ws_len : path;
                auto        vfs_res = VFSPath::Parse(rel);

                if (vfs_res.Succeeded())
                {
                    auto exists = m_vfs_context->Exists(vfs_res.Value());
                    if (exists.Succeeded() && exists.Value())
                        m_vfs_context->Remove(vfs_res.Value());
                }

                m_active_popup = PopupType::None;
                TriggerScan();
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                m_active_popup = PopupType::None;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void ProjectViewUIComponent::HandleDeleteFolderPopup(const char* path)
    {
        char root_native[MAX_FILE_PATH_COUNT];
        m_assets_vfs_root.ResolveNative(m_workspace_root, root_native, sizeof(root_native));
        if (strcmp(m_popup_target_path, root_native) == 0)
        {
            ZENGINE_CORE_ERROR("Cannot delete root folder");
            m_active_popup = PopupType::None;
            return;
        }

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Delete Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const char* dirname = strrchr(path, '/');
            dirname             = dirname ? dirname + 1 : path;

            ImGui::Text("Are you sure you want to delete this folder?");
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", dirname);

            {
                size_t      ws_len = secure_strlen(m_workspace_root);
                const char* rel    = (ws_len > 0 && std::strncmp(path, m_workspace_root, ws_len) == 0) ? path + ws_len : path;
                auto        vp     = VFSPath::Parse(rel);
                if (vp.Succeeded())
                {
                    // Use a scratch arena so this per-frame listing doesn't leak into m_local_arena.
                    auto scratch = ZGetScratch(&m_local_arena);
                    auto listing = m_vfs_context->List(vp.Value(), scratch.Arena);
                    if (listing.Succeeded() && !listing.Value().empty())
                    {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Warning: This folder is not empty!");
                        ImGui::Text("All contents will be permanently deleted.");
                    }
                    ZReleaseScratch(scratch);
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Delete", ImVec2(120, 0)))
            {
                size_t      ws_len = secure_strlen(m_workspace_root);
                const char* rel    = (ws_len > 0 && std::strncmp(path, m_workspace_root, ws_len) == 0) ? path + ws_len : path;
                auto        vp     = VFSPath::Parse(rel);
                if (vp.Succeeded())
                    m_vfs_context->RemoveAll(vp.Value());
                m_current_vfs_dir = m_assets_vfs_root;
                m_active_popup    = PopupType::None;
                TriggerScan();
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                m_active_popup = PopupType::None;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void ProjectViewUIComponent::HandleRenameFilePopup(const char* path)
    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Rename File", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (!m_popup_rename_initialized)
            {
                const char* slash = strrchr(path, '/');
                secure_strcpy(m_popup_rename_name, sizeof(m_popup_rename_name), slash ? slash + 1 : path);
                m_popup_rename_initialized = true;
            }

            ImGui::Text("Enter new file name (with extension):");
            ImGui::InputText("##rename", m_popup_rename_name, sizeof(m_popup_rename_name));

            if (ImGui::Button("Rename", ImVec2(120, 0)))
            {
                if (secure_strlen(m_popup_rename_name) > 0)
                {
                    const char* last_slash                  = strrchr(path, '/');
                    char        parent[MAX_FILE_PATH_COUNT] = {};
                    if (last_slash && last_slash > path)
                        secure_strncpy(parent, sizeof(parent), path, (size_t) (last_slash - path));

                    char new_native[MAX_FILE_PATH_COUNT];
                    snprintf(new_native, sizeof(new_native), "%s/%s", parent, m_popup_rename_name);

                    size_t ws_len = secure_strlen(m_workspace_root);
                    auto   to_vfs = [&](const char* n) {
                        const char* rel = (ws_len > 0 && std::strncmp(n, m_workspace_root, ws_len) == 0) ? n + ws_len : n;
                        return VFSPath::Parse(rel);
                    };
                    auto src_res = to_vfs(path);
                    auto dst_res = to_vfs(new_native);

                    if (src_res.Succeeded() && dst_res.Succeeded())
                    {
                        auto exists = m_vfs_context->Exists(dst_res.Value());
                        if (!exists.Succeeded() || !exists.Value())
                        {
                            m_vfs_context->Rename(src_res.Value(), dst_res.Value());
                            m_active_popup             = PopupType::None;
                            m_popup_rename_initialized = false;
                            TriggerScan();
                            ImGui::CloseCurrentPopup();
                        }
                        else
                        {
                            ZENGINE_CORE_ERROR("A file with the name {} already exists!", m_popup_rename_name);
                        }
                    }
                }
                else
                {
                    ZENGINE_CORE_ERROR("File name cannot be empty.");
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                m_active_popup             = PopupType::None;
                m_popup_rename_initialized = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void ProjectViewUIComponent::RenderBackButton()
    {
        bool canGoBack = !(m_current_vfs_dir == m_assets_vfs_root);
        ImGui::BeginDisabled(!canGoBack);
        if (ImGui::ArrowButton("##left", ImGuiDir_Left) && canGoBack)
            m_current_vfs_dir = m_current_vfs_dir.Parent();
        ImGui::EndDisabled();
    }

    void ProjectViewUIComponent::RenderContextMenu(ContextMenuType type, const char* targetPath)
    {
        // Helper: set the target path, open the named popup, and reset the relevant
        // input buffer so each opening starts fresh.
        auto open_popup = [&](PopupType popup, const char* popup_name) {
            secure_strcpy(m_popup_target_path, sizeof(m_popup_target_path), targetPath ? targetPath : "");
            m_active_popup = popup;
            if (popup == PopupType::NewFile)
                secure_strcpy(m_popup_new_file_name, sizeof(m_popup_new_file_name), "NewFile.txt");
            else if (popup == PopupType::CreateFolder)
                secure_strcpy(m_popup_new_folder_name, sizeof(m_popup_new_folder_name), "New Folder");
            else if (popup == PopupType::RenameFolder || popup == PopupType::RenameFile)
            {
                m_popup_rename_name[0]     = '\0';
                m_popup_rename_initialized = false;
            }
            ImGui::OpenPopup(popup_name);
        };

        switch (type)
        {
            case ContextMenuType::RightPane:
                if (ImGui::MenuItem("Create New File"))
                    open_popup(PopupType::NewFile, "Create New File");
                if (ImGui::MenuItem("Create New Folder"))
                    open_popup(PopupType::CreateFolder, "Create New Folder");
                break;

            case ContextMenuType::LeftPane:
                if (ImGui::MenuItem("Create New Folder"))
                    open_popup(PopupType::CreateFolder, "Create New Folder");
                if (ImGui::MenuItem("Create New File"))
                    open_popup(PopupType::NewFile, "Create New File");
                if (ImGui::MenuItem("Delete Folder"))
                    open_popup(PopupType::DeleteFolder, "Delete Folder");
                if (ImGui::MenuItem("Rename Folder"))
                    open_popup(PopupType::RenameFolder, "Rename Folder");
                break;

            case ContextMenuType::File:
                if (ImGui::MenuItem("Rename File"))
                    open_popup(PopupType::RenameFile, "Rename File");
                if (ImGui::MenuItem("Delete File"))
                    open_popup(PopupType::RemoveFile, "Delete File");
                break;

            case ContextMenuType::Folder:
                if (ImGui::MenuItem("Rename Folder"))
                    open_popup(PopupType::RenameFolder, "Rename Folder");
                if (ImGui::MenuItem("Delete Folder"))
                    open_popup(PopupType::DeleteFolder, "Delete Folder");
                break;
        }
    }

    void ProjectViewUIComponent::RenderPopUpMenu()
    {
        switch (m_active_popup)
        {
            case PopupType::CreateFolder:
                HandleCreateFolderPopup(m_popup_target_path);
                break;
            case PopupType::RenameFolder:
                HandleRenameFolderPopup(m_popup_target_path);
                break;
            case PopupType::DeleteFolder:
                HandleDeleteFolderPopup(m_popup_target_path);
                break;
            case PopupType::NewFile:
                HandleCreateFilePopup(m_popup_target_path);
                break;
            case PopupType::RemoveFile:
                HandleDeleteFilePopup(m_popup_target_path);
                break;
            case PopupType::RenameFile:
                HandleRenameFilePopup(m_popup_target_path);
                break;
            case PopupType::None:
            default:
                break;
        }
    }

} // namespace Tetragrama::Components
