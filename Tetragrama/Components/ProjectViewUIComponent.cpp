#include <pch.h>
#include <Helpers/MemoryOperations.h>
#include <ProjectViewUIComponent.h>
#include <imgui.h>

using namespace ZEngine::Helpers;

namespace Tetragrama::Components
{
    ProjectViewUIComponent::ProjectViewUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility, false), m_currentDirectory(m_assets_directory) {}

    ProjectViewUIComponent::~ProjectViewUIComponent() {}

    void ProjectViewUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    void ProjectViewUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Rendering::Buffers::CommandBuffer* const command_buffer)
    {
        if (!m_texturesLoaded)
        {
            m_directoryIcon  = renderer->LoadTextureFileSync(m_assets_directory.string() + "/DirectoryIcon.png");
            m_fileIcon       = renderer->LoadTextureFileSync(m_assets_directory.string() + "/FileIcon.png");
            m_texturesLoaded = true;
        }

        ImGui::Begin(Name.c_str(), CanBeClosed ? &CanBeClosed : nullptr, ImGuiWindowFlags_NoCollapse);
        ImGui::SameLine();
        RenderBackButton();
        ImGui::SameLine();
        ImGui::InputTextWithHint("##Search", "Search ...", m_search_buffer, IM_ARRAYSIZE(m_search_buffer));
        ImGui::SameLine();
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        ImGui::Text(m_currentDirectory.string().c_str());
        ImGui::PopFont();
        ImGui::Separator();

        // grid layout
        const float padding     = 16.0f;
        const float cellSize    = THUMBNAIL_SIZE + padding;
        const float panelWidth  = ImGui::GetContentRegionAvail().x;
        const int   columnCount = std::max(1, static_cast<int>(panelWidth / cellSize));

        ImGui::Columns(columnCount, nullptr, false);

        for (const auto& entry : std::filesystem::directory_iterator(m_currentDirectory))
        {
            RenderGridItem(renderer, entry);
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
        ImGui::End();
    }

    void ProjectViewUIComponent::RenderGridItem(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, const std::filesystem::directory_entry& entry)
    {
        auto        relativePath = std::filesystem::relative(entry.path(), m_assets_directory);
        std::string name         = relativePath.filename().string();

        ImGui::PushID(name.c_str());

        ImTextureID icon = entry.is_directory() ? static_cast<ImTextureID>(renderer->ImguiRenderer->UpdateFileIconOutput(m_directoryIcon)) : static_cast<ImTextureID>(renderer->ImguiRenderer->UpdateDirIconOutput(m_fileIcon));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::ImageButton(icon, {THUMBNAIL_SIZE, THUMBNAIL_SIZE}, {0, 1}, {1, 0});
        ImGui::PopStyleColor();

        if (ImGui::BeginDragDropSource())
        {
            const auto     path     = entry.path();
            const wchar_t* itemPath = path.c_str();
            ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath, (wcslen(itemPath) + 1) * sizeof(wchar_t));
            ImGui::EndDragDropSource();
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (entry.is_directory())
            {
                m_currentDirectory /= entry.path().filename();
            }
        }

        // centered label
        float textWidth  = ImGui::CalcTextSize(name.c_str()).x;
        float cursorPosX = ImGui::GetCursorPosX();
        float centerPosX = cursorPosX + (THUMBNAIL_SIZE - textWidth) * 0.5f;

        ImGui::SetCursorPosX(centerPosX);
        ImGui::TextWrapped(name.c_str());

        ImGui::PopID();
    }

    void ProjectViewUIComponent::RenderBackButton()
    {
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
