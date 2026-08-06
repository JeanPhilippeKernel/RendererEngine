#include <Tetragrama/Components/ProjectViewUIComponent.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/Helpers/SearchPatternAlgorithm.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <imgui.h>
#include <cstdio>
#include <filesystem>
#include <fstream>

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
            std::filesystem::path ws(ParentLayer->CurrentApp->WorkingSpacePath);
            std::string           label = ws.filename().string();
            if (label.empty())
            {
                label = ws.string();
            }
            secure_strcpy(m_root_label, sizeof(m_root_label), label.c_str());
        }

        auto       asset_mgr           = ZEngine::Managers::AssetManager::Instance();

        const auto current_directoy    = std::filesystem::current_path();
        const auto directory_icon_path = fmt::format("{0}{1}{2}", current_directoy.string(), PLATFORM_OS_BACKSLASH, "Settings/Icons/DirectoryIcon.png");
        const auto file_icon_path      = fmt::format("{0}{1}{2}", current_directoy.string(), PLATFORM_OS_BACKSLASH, "Settings/Icons/FileIcon.png");

        m_directory_icon               = asset_mgr->LoadTextureFileAsAsset(directory_icon_path.c_str(), true);
        m_file_icon                    = asset_mgr->LoadTextureFileAsAsset(file_icon_path.c_str(), true);

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
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                ImGui::OpenPopup("ContextMenu");
            }
            if (ImGui::BeginPopup("ContextMenu"))
            {
                char native[MAX_FILE_PATH_COUNT];
                m_current_vfs_dir.ResolveNative(ParentLayer->CurrentApp->WorkingSpacePath, native, sizeof(native));
                RenderContextMenu(ContextMenuType::RightPane, native);
                ImGui::EndPopup();
            }
            RenderPopUpMenu();

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
                char search_term_lower[256] = {0};
                for (unsigned i = 0; i < len; ++i)
                {
                    search_term_lower[i] = ::tolower(m_search_buffer[i]);
                }

                RenderFilteredContent(renderer, search_term_lower);
            }
            else
            {
                auto listing = m_directory_cache->GetListing(m_current_vfs_dir);
                for (size_t i = 0; i < listing.size(); ++i)
                {
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

        ImGui::PushID(entry.Path.CStr());

        ImTextureID icon      = entry.IsDirectory ? (ImTextureID) m_directory_icon->Handle.Index : (ImTextureID) m_file_icon->Handle.Index;

        const float margin    = 5.0f;
        ImVec2      cursorPos = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(cursorPos.x + margin, cursorPos.y + margin));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::ImageButton("#image", icon, {m_thumbnail_size, m_thumbnail_size}, {0, 1}, {1, 0});
        ImGui::PopStyleColor();
        ImGui::SetCursorPos(ImVec2(cursorPos.x, cursorPos.y + m_thumbnail_size + margin));

        /*
         * Drag and Drop scene file
         * We don't support drag-and-drop for folder for now
         */
        if (!entry.IsDirectory)
        {
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                char native[MAX_FILE_PATH_COUNT];
                entry.Path.ResolveNative(ParentLayer->CurrentApp->WorkingSpacePath, native, sizeof(native));
                ImGui::SetDragDropPayload("CONTENT_BROWSER_FILE_DRAG_OP", native, (secure_strlen(native) + 1) * sizeof(char));
                ImGui::EndDragDropSource();
            }
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (entry.IsDirectory)
            {
                m_current_vfs_dir = entry.Path;
                secure_memset(m_search_buffer, 0, sizeof(m_search_buffer), sizeof(m_search_buffer));
            }
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            ImGui::OpenPopup("ItemContextMenu");
        }
        if (ImGui::BeginPopup("ItemContextMenu"))
        {
            char native[MAX_FILE_PATH_COUNT];
            entry.Path.ResolveNative(ParentLayer->CurrentApp->WorkingSpacePath, native, sizeof(native));
            entry.IsDirectory ? RenderContextMenu(ContextMenuType::Folder, native) : RenderContextMenu(ContextMenuType::File, native);
            ImGui::EndPopup();
        }

        // centered label
        float textWidth  = ImGui::CalcTextSize(name).x;
        float cursorPosX = ImGui::GetCursorPosX();
        float centerPosX = cursorPosX + (m_thumbnail_size - textWidth) * 0.5f;

        ImGui::SetCursorPosX(centerPosX);
        ImGui::TextWrapped("%s", name);

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

                if (Helpers::KMPSearch(scratch.Arena, name_lower, searchTerm))
                {
                    ImGui::TableNextColumn();
                    RenderContentTile(renderer, entry);
                }
            }
        });

        ZReleaseScratch(scratch);
    }

    void ProjectViewUIComponent::RenderTreeBrowser()
    {
        bool nodeOpen = ImGui::TreeNodeEx(m_root_label, ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen);

        if (ImGui::IsItemClicked())
        {
            m_current_vfs_dir = m_assets_vfs_root;
            secure_memset(m_search_buffer, 0, sizeof(m_search_buffer), sizeof(m_search_buffer));
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            ImGui::OpenPopup("RootContextMenu");
        }

        if (ImGui::BeginPopup("RootContextMenu"))
        {
            char native[MAX_FILE_PATH_COUNT];
            m_assets_vfs_root.ResolveNative(ParentLayer->CurrentApp->WorkingSpacePath, native, sizeof(native));
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
            {
                continue;
            }

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
            if (entry.Path == m_current_vfs_dir)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }

            char label[MAX_FILE_PATH_COUNT];
            entry.Path.CopyFilename(label, sizeof(label));

            char popup_id[MAX_FILE_PATH_COUNT + 8];
            std::snprintf(popup_id, sizeof(popup_id), "Dir_%s", entry.Path.CStr());

            bool nodeOpen = ImGui::TreeNodeEx(label, flags);

            if (ImGui::IsItemClicked())
            {
                m_current_vfs_dir = entry.Path;
                secure_memset(m_search_buffer, 0, sizeof(m_search_buffer), sizeof(m_search_buffer));
            }

            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                ImGui::OpenPopup(popup_id);
            }

            if (ImGui::BeginPopup(popup_id))
            {
                char native[MAX_FILE_PATH_COUNT];
                entry.Path.ResolveNative(ParentLayer->CurrentApp->WorkingSpacePath, native, sizeof(native));
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

    void ProjectViewUIComponent::HandleCreateFilePopup(const std::filesystem::path& path)
    {
        ImGui::OpenPopup("Create New File");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Create New File", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            static char new_file_name[MAX_FILE_PATH_COUNT] = "NewFile.txt";
            ImGui::Text("Enter file name (with extension):");
            ImGui::InputText("##create", new_file_name, sizeof(new_file_name));

            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                if (secure_strlen(new_file_name) > 0)
                {
                    std::filesystem::path newPath = path / new_file_name;
                    if (!std::filesystem::exists(newPath))
                    {

                        std::ofstream file(newPath.string());
                        if (file.is_open())
                        {
                            file.close();
                            m_active_popup = PopupType::None;
                            TriggerScan();
                            ImGui::CloseCurrentPopup();
                        }
                        else
                        {
                            ZENGINE_CORE_ERROR("Failed to create file: {}", new_file_name);
                        }
                    }
                    else
                    {
                        ZENGINE_CORE_ERROR("A file with the name {} already exists!", new_file_name);
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

    void ProjectViewUIComponent::HandleCreateFolderPopup(const std::filesystem::path& path)
    {
        ImGui::OpenPopup("Create New Folder");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Create New Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            static char new_folder_name[MAX_FILE_PATH_COUNT] = "New Folder";
            ImGui::Text("Enter folder name:");
            ImGui::InputText("##create", new_folder_name, sizeof(new_folder_name));

            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                if (secure_strlen(new_folder_name) > 0)
                {
                    std::filesystem::path newPath = path / new_folder_name;
                    if (!std::filesystem::exists(newPath))
                    {
                        std::filesystem::create_directory(newPath);
                        m_active_popup = PopupType::None;
                        TriggerScan();
                        ImGui::CloseCurrentPopup();
                    }
                    else
                    {
                        ZENGINE_CORE_ERROR("A folder with the name {} already exists!", new_folder_name);
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

    void ProjectViewUIComponent::HandleRenameFolderPopup(const std::filesystem::path& path)
    {
        char root_native[MAX_FILE_PATH_COUNT];
        m_assets_vfs_root.ResolveNative(ParentLayer->CurrentApp->WorkingSpacePath, root_native, sizeof(root_native));
        if (m_popup_target_path == std::filesystem::path(root_native))
        {
            ZENGINE_CORE_ERROR("Cannot rename root folder");
            m_active_popup = PopupType::None;
            return;
        }
        ImGui::OpenPopup("Rename Folder");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Rename Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            static char new_name[MAX_FILE_PATH_COUNT];
            static bool initialized = false;

            if (!initialized)
            {
                secure_strcpy(new_name, sizeof(new_name), path.filename().string().c_str());
                initialized = true;
            }

            ImGui::Text("Enter new folder name:");
            ImGui::InputText("##rename", new_name, sizeof(new_name));

            if (ImGui::Button("Rename", ImVec2(120, 0)))
            {
                if (secure_strlen(new_name) > 0)
                {
                    std::filesystem::path newPath = path.parent_path() / new_name;

                    if (!std::filesystem::exists(newPath))
                    {
                        std::filesystem::rename(path, newPath);
                        m_current_vfs_dir = m_assets_vfs_root;

                        m_active_popup    = PopupType::None;
                        initialized       = false;
                        TriggerScan();
                        ImGui::CloseCurrentPopup();
                    }
                    else
                    {
                        ZENGINE_CORE_ERROR("A folder with the name {} already exists!", new_name);
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
                initialized    = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void ProjectViewUIComponent::HandleDeleteFilePopup(const std::filesystem::path& path)
    {
        ImGui::OpenPopup("Delete File");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Delete File", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Are you sure you want to delete this file?");
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", path.filename().string().c_str());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Delete", ImVec2(120, 0)))
            {
                if (std::filesystem::exists(path))
                {
                    std::filesystem::remove(path);
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

    void ProjectViewUIComponent::HandleDeleteFolderPopup(const std::filesystem::path& path)
    {
        char root_native[MAX_FILE_PATH_COUNT];
        m_assets_vfs_root.ResolveNative(ParentLayer->CurrentApp->WorkingSpacePath, root_native, sizeof(root_native));
        if (m_popup_target_path == std::filesystem::path(root_native))
        {
            ZENGINE_CORE_ERROR("Cannot rename root folder");
            m_active_popup = PopupType::None;
            return;
        }
        ImGui::OpenPopup("Delete Folder");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Delete Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Are you sure you want to delete this folder?");
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", path.filename().string().c_str());

            if (!std::filesystem::is_empty(path))
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Warning: This folder is not empty!");
                ImGui::Text("All contents will be permanently deleted.");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Delete", ImVec2(120, 0)))
            {
                std::filesystem::remove_all(path);
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

    void ProjectViewUIComponent::HandleRenameFilePopup(const std::filesystem::path& path)
    {
        ImGui::OpenPopup("Rename File");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Rename File", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            static char new_name[MAX_FILE_PATH_COUNT];
            static bool initialized = false;

            if (!initialized)
            {
                secure_strcpy(new_name, sizeof(new_name), path.filename().string().c_str());
                initialized = true;
            }

            ImGui::Text("Enter new file name (with extension):");
            ImGui::InputText("##rename", new_name, sizeof(new_name));

            if (ImGui::Button("Rename", ImVec2(120, 0)))
            {
                if (secure_strlen(new_name) > 0)
                {
                    std::filesystem::path newPath = path.parent_path() / new_name;

                    if (!std::filesystem::exists(newPath))
                    {
                        std::filesystem::rename(path, newPath);
                        m_active_popup = PopupType::None;
                        initialized    = false;
                        TriggerScan();
                        ImGui::CloseCurrentPopup();
                    }
                    else
                    {
                        ZENGINE_CORE_ERROR("A file with the name {} already exists!", new_name);
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
                initialized    = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void ProjectViewUIComponent::RenderBackButton()
    {
        bool canGoBack = !(m_current_vfs_dir == m_assets_vfs_root);
        if (canGoBack)
        {
            if (ImGui::ArrowButton("##left", ImGuiDir_Left))
            {
                m_current_vfs_dir = m_current_vfs_dir.Parent();
            }
        }
        else
        {
            ImVec4 color = {0.2f, 0.2f, 0.2f, .2f};
            ImGui::PushStyleColor(ImGuiCol_Button, color); // Grayed out color
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);

            ImGui::ArrowButton("##left", ImGuiDir_Left);

            ImGui::PopStyleColor(3);
        }
    }

    void ProjectViewUIComponent::RenderContextMenu(ContextMenuType type, const std::filesystem::path& targetPath)
    {
        m_popup_target_path = targetPath;
        switch (type)
        {
            case ContextMenuType::RightPane:
                if (ImGui::MenuItem("Create New File"))
                {
                    m_active_popup = PopupType::NewFile;
                }
                if (ImGui::MenuItem("Create New Folder"))
                {
                    m_active_popup = PopupType::CreateFolder;
                }
                break;

            case ContextMenuType::LeftPane:
                if (ImGui::MenuItem("Create New Folder"))
                {
                    m_active_popup = PopupType::CreateFolder;
                }
                if (ImGui::MenuItem("Create New File"))
                {
                    m_active_popup = PopupType::NewFile;
                }
                if (ImGui::MenuItem("Delete Folder"))
                {
                    m_active_popup = PopupType::DeleteFolder;
                }
                if (ImGui::MenuItem("Rename Folder"))
                {
                    m_active_popup = PopupType::RenameFolder;
                }
                break;

            case ContextMenuType::File:
                if (ImGui::MenuItem("Rename File"))
                {
                    m_active_popup = PopupType::RenameFile;
                }
                if (ImGui::MenuItem("Delete File"))
                {
                    m_active_popup = PopupType::RemoveFile;
                }
                break;

            case ContextMenuType::Folder:
                if (ImGui::MenuItem("Rename Folder"))
                {
                    m_active_popup = PopupType::RenameFolder;
                }
                if (ImGui::MenuItem("Delete Folder"))
                {
                    m_active_popup = PopupType::DeleteFolder;
                }
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

    void ProjectViewUIComponent::MakeRelative(const std::filesystem::path& path, const std::filesystem::path& base, char* output)
    {
        auto path_itr = path.begin();
        auto base_itr = base.begin();

        while (path_itr != path.end() && base_itr != base.end() && *path_itr == *base_itr)
        {
            ++path_itr;
            ++base_itr;
        }

        std::filesystem::path result;
        while (base_itr != base.end())
        {
            result /= "..";
            ++base_itr;
        }

        while (path_itr != path.end())
        {
            result /= *path_itr;
            ++path_itr;
        }

        // Formatted
        auto path_str   = result.string();
        auto c_path_str = path_str.c_str();

        while (*c_path_str)
        {
            if (*c_path_str == PLATFORM_OS_BACKSLASH)
            {
                *output++ = ' ';
                *output++ = '>';
                *output++ = ' ';
            }
            else
            {
                *output++ = *c_path_str;
            }

            c_path_str++;
        }
    }

} // namespace Tetragrama::Components
