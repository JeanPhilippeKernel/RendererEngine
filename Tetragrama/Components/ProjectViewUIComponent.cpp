#include <pch.h>
#include <Editor.h>
#include <Helpers/MemoryOperations.h>
#include <ProjectViewUIComponent.h>
#include <imgui.h>

using namespace ZEngine::Helpers;

namespace Tetragrama::Components
{
    ProjectViewUIComponent::ProjectViewUIComponent(Layers::ImguiLayer* parent, std::string_view name, bool visibility) : UIComponent(parent, name, visibility, false)
    {
        auto context        = reinterpret_cast<EditorContext*>(ParentLayer->ParentContext);
        m_assets_directory  = context->ConfigurationPtr->WorkingSpacePath;
        m_current_directory = m_assets_directory;
    }

    ProjectViewUIComponent::~ProjectViewUIComponent() {}

    void ProjectViewUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    void ProjectViewUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer)
    {
        if (!m_textures_loaded)
        {
            m_directory_icon = renderer->AsyncLoader->LoadTextureFileSync("Settings/Icons/DirectoryIcon.png");
            m_file_icon      = renderer->AsyncLoader->LoadTextureFileSync("Settings/Icons/FileIcon.png");

            renderer->Device->TextureHandleToUpdates.Enqueue(m_directory_icon);
            renderer->Device->TextureHandleToUpdates.Enqueue(m_file_icon);

            m_textures_loaded = true;
        }

        ImGui::Begin(Name.c_str(), (CanBeClosed ? &CanBeClosed : NULL), ImGuiWindowFlags_NoCollapse);

        ImGui::BeginChild("Left Pane", ImVec2(ImGui::GetContentRegionAvail().x * 0.15f, 0), true);
        RenderTreeBrowser();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::GetWindowDrawList()->AddLine(ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + ImGui::GetContentRegionAvail().y), ImGui::GetColorU32(ImGuiCol_Separator), 0.5f);

        ImGui::BeginChild("Right Pane", ImVec2(0, 0), true);
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            ImGui::OpenPopup("ContextMenu");
        }
        if (ImGui::BeginPopup("ContextMenu"))
        {
            RenderContextMenu(ContextMenuType::RightPane, m_current_directory);
            ImGui::EndPopup();
        }
        RenderPopUpMenu();
        RenderContentBrowser(renderer);
        ImGui::EndChild();

        ImGui::End();
    }

    void ProjectViewUIComponent::RenderContentBrowser(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer)
    {

        RenderBackButton();
        ImGui::SameLine();
        ImGui::InputTextWithHint("##Search", "Search ...", m_search_buffer, IM_ARRAYSIZE(m_search_buffer));
        ImGui::SameLine();
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        auto relative_path = MakeRelative(m_current_directory, m_assets_directory.parent_path());
        ImGui::Text(relative_path.string().c_str());
        ImGui::PopFont();
        ImGui::Separator();

        const float padding     = 16.0f;
        const float cellSize    = m_thumbnail_size + padding;
        const float panelWidth  = ImGui::GetContentRegionAvail().x;
        const int   columnCount = std::max(1, static_cast<int>(panelWidth / cellSize));

        if (ImGui::BeginTable("GridTable", columnCount))
        {
            if (secure_strlen(m_search_buffer) > 0)
            {
                std::string searchTerm = m_search_buffer;
                std::transform(searchTerm.begin(), searchTerm.end(), searchTerm.begin(), ::tolower);
                RenderFilteredContent(renderer, searchTerm);
            }
            else
            {
                for (const auto& entry : std::filesystem::directory_iterator(m_current_directory))
                {
                    ImGui::TableNextColumn();
                    RenderContentTile(renderer, entry);
                }
            }
            ImGui::EndTable();
        }
    }

    void ProjectViewUIComponent::RenderContentTile(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, const std::filesystem::directory_entry& entry)
    {
        auto        relativePath = std::filesystem::relative(entry.path(), m_assets_directory);
        std::string name         = relativePath.filename().string();

        ImGui::PushID(name.c_str());

        ImTextureID icon      = entry.is_directory() ? (ImTextureID) m_directory_icon.Index : (ImTextureID) m_file_icon.Index;

        const float margin    = 5.0f;
        ImVec2      cursorPos = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(cursorPos.x + margin, cursorPos.y + margin));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::ImageButton(icon, {m_thumbnail_size, m_thumbnail_size}, {0, 1}, {1, 0});
        ImGui::PopStyleColor();
        ImGui::SetCursorPos(ImVec2(cursorPos.x, cursorPos.y + m_thumbnail_size + margin));

        if (ImGui::BeginDragDropSource())
        {
            std::string itemPath = entry.path().string();
            ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath.c_str(), (itemPath.length() + 1) * sizeof(char));
            ImGui::EndDragDropSource();
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (entry.is_directory())
            {
                auto relativePath   = std::filesystem::relative(entry.path(), m_assets_directory);
                m_current_directory = m_assets_directory / relativePath;
                secure_memset(m_search_buffer, 0, sizeof(m_search_buffer), sizeof(m_search_buffer));
            }
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            ImGui::OpenPopup("ItemContextMenu");
        }
        if (ImGui::BeginPopup("ItemContextMenu"))
        {
            entry.is_directory() ? RenderContextMenu(ContextMenuType::Folder, entry) : RenderContextMenu(ContextMenuType::File, entry);
            ImGui::EndPopup();
        }

        // centered label
        float textWidth  = ImGui::CalcTextSize(name.c_str()).x;
        float cursorPosX = ImGui::GetCursorPosX();
        float centerPosX = cursorPosX + (m_thumbnail_size - textWidth) * 0.5f;

        ImGui::SetCursorPosX(centerPosX);
        ImGui::TextWrapped(name.c_str());

        ImGui::PopID();
    }

    void ProjectViewUIComponent::RenderFilteredContent(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, std::string_view searchTerm)
    {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(m_assets_directory))
        {
            if (entry.is_regular_file() || entry.is_directory())
            {
                std::string nameLower = entry.path().filename().string();
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

                if (nameLower.find(searchTerm) != std::string::npos)
                {
                    ImGui::TableNextColumn();
                    RenderContentTile(renderer, entry);
                }
            }
        }
    }

    void ProjectViewUIComponent::RenderTreeBrowser()
    {
        bool nodeOpen = ImGui::TreeNodeEx(m_assets_directory.filename().string().c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen);

        if (ImGui::IsItemClicked())
        {
            m_current_directory = m_assets_directory;
            secure_memset(m_search_buffer, 0, sizeof(m_search_buffer), sizeof(m_search_buffer));
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            ImGui::OpenPopup("RootContextMenu");
        }

        if (ImGui::BeginPopup("RootContextMenu"))
        {
            RenderContextMenu(ContextMenuType::LeftPane, m_assets_directory);
            ImGui::EndPopup();
        }

        if (nodeOpen)
        {
            RenderDirectoryNode(m_assets_directory);
            ImGui::TreePop();
        }
    }

    void ProjectViewUIComponent::RenderDirectoryNode(const std::filesystem::path& directory)
    {
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            if (entry.is_directory())
            {
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
                if (m_current_directory == entry.path())
                {
                    flags |= ImGuiTreeNodeFlags_Selected;
                }

                std::string popupId  = "Dir_" + entry.path().filename().string();

                bool        nodeOpen = ImGui::TreeNodeEx(entry.path().filename().string().c_str(), flags);

                if (ImGui::IsItemClicked())
                {
                    auto relativePath   = std::filesystem::relative(entry.path(), m_assets_directory);
                    m_current_directory = m_assets_directory / relativePath;
                    secure_memset(m_search_buffer, 0, sizeof(m_search_buffer), sizeof(m_search_buffer));
                }

                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                {
                    ImGui::OpenPopup(popupId.c_str());
                }

                if (ImGui::BeginPopup(popupId.c_str()))
                {
                    RenderContextMenu(ContextMenuType::LeftPane, std::filesystem::absolute(entry.path()));
                    ImGui::EndPopup();
                }

                if (nodeOpen)
                {
                    RenderDirectoryNode(entry.path());
                    ImGui::TreePop();
                }
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
        if (m_popup_target_path == m_assets_directory)
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
                        if (m_current_directory == path)
                        {
                            m_current_directory = newPath;
                        }

                        m_active_popup = PopupType::None;
                        initialized    = false;
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
                    if (m_current_directory == path)
                    {
                        m_current_directory = path.parent_path();
                    }

                    std::filesystem::remove(path);
                }

                m_active_popup = PopupType::None;
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
        if (m_popup_target_path == m_assets_directory)
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
                auto parentPath = path;

                if (m_current_directory == path || m_current_directory.string().find(path.string()) == 0)
                {
                    m_current_directory = path.parent_path();
                }

                std::filesystem::remove_all(parentPath);
                m_active_popup = PopupType::None;
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
        ImGui::SameLine();
        static constexpr float ButtonSize    = 20.0f;
        static constexpr float TriangleSize  = 8.0f;
        const ImU32            DefaultColor  = IM_COL32(150, 150, 150, 255);
        const ImU32            HoverColor    = IM_COL32(200, 200, 200, 255);
        const ImU32            DisabledColor = IM_COL32(100, 100, 100, 128);
        ImDrawList*            drawList      = ImGui::GetWindowDrawList();
        ImVec2                 cursorPos     = ImGui::GetCursorScreenPos();
        // Calculate positions
        ImVec2                 buttonSize(ButtonSize, ButtonSize);
        ImVec2                 center              = {cursorPos.x + ButtonSize / 2, cursorPos.y + ButtonSize / 2};
        // Calculate triangle vertices
        ImVec2                 triangleLeft        = {center.x - TriangleSize, center.y};
        ImVec2                 triangleTopRight    = {center.x + TriangleSize, center.y - TriangleSize};
        ImVec2                 triangleBottomRight = {center.x + TriangleSize, center.y + TriangleSize};
        bool                   canGoBack           = (m_current_directory != m_assets_directory);
        ImU32                  triangleColor       = DefaultColor;
        if (canGoBack)
        {
            if (ImGui::Button("##BackButton", buttonSize))
            {
                m_current_directory = m_current_directory.parent_path();
            }
            triangleColor = ImGui::IsItemHovered() ? HoverColor : DefaultColor;
        }
        else
        {
            ImGui::Dummy(buttonSize);
            triangleColor = DisabledColor;
        }
        drawList->AddTriangleFilled(triangleLeft, triangleTopRight, triangleBottomRight, triangleColor);
    }

    void ProjectViewUIComponent::RenderContextMenu(ContextMenuType type, const std::filesystem::path& targetPath)
    {
        m_popup_target_path = targetPath;
        switch (type)
        {
            case ContextMenuType::RightPane:
                if (ImGui::MenuItem("Create New File"))
                {
                    m_active_popup = PopupType::CreateFile;
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
                    m_active_popup = PopupType::CreateFile;
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
                    m_active_popup = PopupType::DeleteFile;
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
            case PopupType::CreateFile:
                HandleCreateFilePopup(m_popup_target_path);
                break;
            case PopupType::DeleteFile:
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

    std::filesystem::path ProjectViewUIComponent::MakeRelative(const std::filesystem::path& path, const std::filesystem::path& base)
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

        return result;
    }

} // namespace Tetragrama::Components
