#include <pch.h>
#include <ProjectViewUIComponent.h>
#include <imgui.h>

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

        ImGui::Begin(Name.c_str(), (CanBeClosed ? &CanBeClosed : NULL), ImGuiWindowFlags_NoCollapse);
        ImGui::SameLine();
        RenderBackButton();
        ImGui::Separator();

        float padding       = 16.0f;
        float thumbnailSize = 128.0f;
        float cellSize      = thumbnailSize + padding;
        float panelWidth    = ImGui::GetContentRegionAvail().x;
        int   columnCount   = static_cast<int>(panelWidth / cellSize);
        columnCount         = (columnCount < 1) ? 1 : columnCount;

        ImGui::Columns(columnCount, 0, false);

        for (auto& itr : std::filesystem::directory_iterator(m_currentDirectory))
        {
            auto        relativePath = std::filesystem::relative(itr.path(), m_assets_directory);
            std::string name         = relativePath.filename().string();

            ImGui::PushID(name.c_str());

            ImGui::Text("Is Directory: %s", itr.is_directory() ? "true" : "false");
            auto&       iconHandle   = itr.is_directory() ? m_directoryIcon : m_fileIcon;
            ImTextureID imguiTexture = (ImTextureID) renderer->ImguiRenderer->UpdateIconOutput(iconHandle);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::ImageButton(imguiTexture, {thumbnailSize, thumbnailSize}, {0, 1}, {1, 0});

            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (itr.is_directory())
                    m_currentDirectory /= itr.path().filename();
            }

            float textWidth  = ImGui::CalcTextSize(name.c_str()).x;
            float cursorPosX = ImGui::GetCursorPosX();
            float centerPosX = cursorPosX + (thumbnailSize - textWidth) * 0.5f;
            ImGui::SetCursorPosX(centerPosX);
            ImGui::TextWrapped("%s", name.c_str());
            ImGui::PopID();
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
        ImGui::End();
    }

    void ProjectViewUIComponent::RenderBackButton()
    {
        const float BUTTON_SIZE    = 20.0f;
        const float TRIANGLE_SIZE  = 8.0f;
        const ImU32 DEFAULT_COLOR  = IM_COL32(150, 150, 150, 255);
        const ImU32 HOVER_COLOR    = IM_COL32(200, 200, 200, 255);
        const ImU32 DISABLED_COLOR = IM_COL32(100, 100, 100, 128);

        ImDrawList* draw_list      = ImGui::GetWindowDrawList();
        ImVec2      cursor_pos     = ImGui::GetCursorScreenPos();
        ImVec2      button_size(BUTTON_SIZE, BUTTON_SIZE);
        ImVec2      center(cursor_pos.x + BUTTON_SIZE / 2, cursor_pos.y + BUTTON_SIZE / 2);

        bool        can_go_back    = (m_currentDirectory != m_assets_directory);
        ImU32       triangle_color = DEFAULT_COLOR;

        if (can_go_back)
        {
            if (ImGui::Button("##BackButton", button_size))
            {
                m_currentDirectory = m_currentDirectory.parent_path();
            }

            if (ImGui::IsItemHovered())
            {
                triangle_color = HOVER_COLOR;
            }
        }
        else
        {
            ImGui::Dummy(button_size);
            triangle_color = DISABLED_COLOR;
        }

        draw_list->AddTriangleFilled(ImVec2(center.x - TRIANGLE_SIZE, center.y), ImVec2(center.x + TRIANGLE_SIZE, center.y - TRIANGLE_SIZE), ImVec2(center.x + TRIANGLE_SIZE, center.y + TRIANGLE_SIZE), triangle_color);
    }
} // namespace Tetragrama::Components
