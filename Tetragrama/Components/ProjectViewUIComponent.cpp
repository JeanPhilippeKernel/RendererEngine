#include <pch.h>
#include <Helpers/MemoryOperations.h>
#include <ProjectViewUIComponent.h>
#include <imgui.h>
#include <iostream>

using namespace ZEngine::Helpers;

namespace Tetragrama::Components
{
    ProjectViewUIComponent::ProjectViewUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility, false), m_currentDirectory(m_assets_directory), m_lastRenderedFolder(m_assets_directory) {}

    ProjectViewUIComponent::~ProjectViewUIComponent() {}

    void ProjectViewUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    void ProjectViewUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Rendering::Buffers::CommandBuffer* const command_buffer)
    {
        if (!m_texturesLoaded)
        {
            m_directoryIcon  = renderer->LoadTextureFileSync("Settings/Icons/DirectoryIcon.png");
            m_fileIcon       = renderer->LoadTextureFileSync("Settings/Icons/FileIcon.png");
            m_texturesLoaded = true;
        }

        ImGui::Begin(Name.c_str(), CanBeClosed ? &CanBeClosed : nullptr, ImGuiWindowFlags_NoCollapse);

        ImGui::BeginChild("Left Pane", ImVec2(ImGui::GetContentRegionAvail().x * 0.15f, 0), true);
        RenderTreeBrowser();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::GetWindowDrawList()->AddLine(ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + ImGui::GetContentRegionAvail().y), ImGui::GetColorU32(ImGuiCol_Separator), 0.5f);

        ImGui::BeginChild("Right Pane", ImVec2(0, 0), true);
        RenderContentBrowser(renderer);
        ImGui::EndChild();

        ImGui::End();
    }

    void ProjectViewUIComponent::RenderTreeBrowser()
    {
        bool nodeOpen = ImGui::TreeNodeEx(m_assets_directory.filename().string().c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen);

        if (ImGui::BeginPopupContextItem("root_context"))
        {
            HandleFolderContextMenu(m_assets_directory);
            ImGui::EndPopup();
        }

        if (nodeOpen)
        {
            RenderDirectoryNode(m_assets_directory);
            ImGui::TreePop();
        }

        HandleCreateFolderPopup();
    }

    void ProjectViewUIComponent::RenderDirectoryNode(const std::filesystem::path& directory)
    {

        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            if (entry.is_directory())
            {
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;

                if (m_currentDirectory == entry.path())
                {
                    flags |= ImGuiTreeNodeFlags_Selected;
                }

                bool nodeOpen = ImGui::TreeNodeEx(entry.path().filename().string().c_str(), flags);

                if (ImGui::IsItemClicked())
                {
                    auto relativePath  = std::filesystem::relative(entry.path(), m_assets_directory);
                    m_currentDirectory = m_assets_directory / relativePath;
                    secure_memset(m_search_buffer, 0, sizeof(m_search_buffer), sizeof(m_search_buffer));
                }

                if (ImGui::BeginPopupContextItem(entry.path().filename().string().c_str()))
                {
                    HandleFolderContextMenu(std::filesystem::absolute(entry.path()));
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

    void ProjectViewUIComponent::HandleFolderContextMenu(const std::filesystem::path& path)
    {
        if (ImGui::MenuItem("Create Folder"))
        {
            m_show_create_folder = true;         
            m_create_folder_path = path;        
            m_new_folder_name    = "New Folder"; 
        }
        if (ImGui::MenuItem("Delete Folder"))
        {
            // HandleFolderDeletion(path);
        }
        if (ImGui::MenuItem("Rename Folder"))
        {
            // BeginRenameFolder(path);
        }
    }

    void ProjectViewUIComponent::HandleCreateFolderPopup() 
    {
        if (m_show_create_folder)
        {
            ImGui::OpenPopup("Create New Folder");
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        }

        if (ImGui::BeginPopupModal("Create New Folder", &m_show_create_folder, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Enter folder name:");

            // Create a buffer for input
            char buffer[256];
            strncpy(buffer, m_new_folder_name.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';

            if (ImGui::InputText("##create", buffer, sizeof(buffer)))
            {
                m_new_folder_name = buffer;
            }

            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                if (!m_new_folder_name.empty())
                {
                    std::filesystem::path newPath = m_create_folder_path / m_new_folder_name;
                    if (!std::filesystem::exists(newPath))
                    {
                        std::filesystem::create_directory(newPath);
                        m_show_create_folder = false;
                        ImGui::CloseCurrentPopup();
                    }
                    else
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "A folder with this name already exists!");
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                m_show_create_folder = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void ProjectViewUIComponent::RenderContentBrowser(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer)
    {

        RenderBackButton();
        ImGui::SameLine();
        ImGui::InputTextWithHint("##Search", "Search ...", m_search_buffer, IM_ARRAYSIZE(m_search_buffer));
        ImGui::SameLine();
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        ImGui::Text(m_currentDirectory.string().c_str());
        ImGui::PopFont();
        ImGui::Separator();

        const float padding     = 16.0f;
        const float cellSize    = m_thumbnailSize + padding;
        const float panelWidth  = ImGui::GetContentRegionAvail().x;
        const int   columnCount = std::max(1, static_cast<int>(panelWidth / cellSize));

        if (ImGui::BeginTable("GridTable", columnCount))
        {
            if (secure_strlen(m_search_buffer) > 0)
            {
                std::string searchTerm = m_search_buffer;
                std::transform(searchTerm.begin(), searchTerm.end(), searchTerm.begin(), ::tolower);
                RenderSearchResults(renderer, searchTerm);
            }
            else
            {
                for (const auto& entry : std::filesystem::directory_iterator(m_currentDirectory))
                {
                    ImGui::TableNextColumn();
                    RenderGridItem(renderer, entry);
                }
            }
            ImGui::EndTable();
        }
    }

    void ProjectViewUIComponent::RenderGridItem(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, const std::filesystem::directory_entry& entry)
    {
        auto        relativePath = std::filesystem::relative(entry.path(), m_assets_directory);
        std::string name         = relativePath.filename().string();

        ImGui::PushID(name.c_str());

        ImTextureID icon = entry.is_directory() ? static_cast<ImTextureID>(renderer->ImguiRenderer->UpdateFileIconOutput(m_directoryIcon)) : static_cast<ImTextureID>(renderer->ImguiRenderer->UpdateDirIconOutput(m_fileIcon));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::ImageButton(icon, {m_thumbnailSize, m_thumbnailSize}, {0, 1}, {1, 0});
        ImGui::PopStyleColor();

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
                auto relativePath  = std::filesystem::relative(entry.path(), m_assets_directory);
                m_currentDirectory = m_assets_directory / relativePath;
                secure_memset(m_search_buffer, 0, sizeof(m_search_buffer), sizeof(m_search_buffer));
            }
        }

        // centered label
        float textWidth  = ImGui::CalcTextSize(name.c_str()).x;
        float cursorPosX = ImGui::GetCursorPosX();
        float centerPosX = cursorPosX + (m_thumbnailSize - textWidth) * 0.5f;

        ImGui::SetCursorPosX(centerPosX);
        ImGui::TextWrapped(name.c_str());

        ImGui::PopID();
    }

    void ProjectViewUIComponent::RenderSearchResults(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, const std::string& searchTerm)
    {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(m_assets_directory))
        {
            if (entry.is_regular_file() || entry.is_directory())
            {
                std::string nameLower = entry.path().filename().string();
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

                if (nameLower.find(searchTerm) != std::string::npos)
                {
                    auto relativeFolder = MakeRelative(entry.path().parent_path(), m_assets_directory).string();
                    if (m_lastRenderedFolder != relativeFolder)
                    {
                        m_lastRenderedFolder = relativeFolder;
                    }

                    ImGui::TableNextColumn();
                    RenderGridItem(renderer, entry);
                }
            }
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

        bool                   canGoBack           = (m_currentDirectory != m_assets_directory);
        ImU32                  triangleColor       = DefaultColor;

        if (canGoBack)
        {
            if (ImGui::Button("##BackButton", buttonSize))
            {
                m_currentDirectory = m_currentDirectory.parent_path();
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
} // namespace Tetragrama::Components
