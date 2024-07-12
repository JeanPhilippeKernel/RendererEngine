#pragma once
#include <ZEngine.h>
#include <imgui/src/imgui_internal.h>

namespace Tetragrama::Helpers
{
    void DrawVec4Control(
        std::string_view                                     label,
        ZEngine::Maths::Vector4&                             values,
        const std::function<void(ZEngine::Maths::Vector4&)>& callback      = nullptr,
        float                                                default_value = 0.0f,
        float                                                column_width  = 100.0f);
    void DrawVec3Control(
        std::string_view                                     label,
        ZEngine::Maths::Vector3&                             values,
        const std::function<void(ZEngine::Maths::Vector3&)>& callback      = nullptr,
        float                                                default_value = 0.0f,
        float                                                column_width  = 100.0f);
    void DrawVec2Control(
        std::string_view                                     label,
        ZEngine::Maths::Vector2&                             values,
        const std::function<void(ZEngine::Maths::Vector2&)>& callback      = nullptr,
        float                                                default_value = 0.0f,
        float                                                column_width  = 100.0f);

    void DrawInputTextControl(
        std::string_view                             label,
        std::string_view                             content,
        const std::function<void(std::string_view)>& callback       = nullptr,
        bool                                         read_only_mode = false,
        float                                        column_width   = 100.0f);

    void DrawDragFloatControl(
        std::string_view                  label,
        float                             value,
        float                             increment_speed = 1.0f,
        float                             min_value       = 0.0f,
        float                             max_value       = 0.0f,
        std::string_view                  fmt             = "%.2f",
        const std::function<void(float)>& callback        = nullptr,
        float                             column_width    = 100.0f);

    void DrawCenteredButtonControl(std::string_view label, const std::function<void(void)>& callback = nullptr);

    void DrawColorEdit4Control(
        std::string_view                                     label,
        ZEngine::Maths::Vector4&                             values,
        const std::function<void(ZEngine::Maths::Vector4&)>& callback      = nullptr,
        float                                                default_value = 0.0f,
        float                                                column_width  = 100.0f);

    void DrawColorEdit3Control(
        std::string_view                                     label,
        ZEngine::Maths::Vector3&                             values,
        const std::function<void(ZEngine::Maths::Vector3&)>& callback,
        float                                                default_value = 0.0f,
        float                                                column_width  = 100.0f);

    void DrawTextureColorControl(
        std::string_view                                     label,
        ImTextureID                                          texture_id,
        ZEngine::Maths::Vector4&                             texture_tint_color,
        bool                                                 enable_zoom                = true,
        const std::function<void(void)>&                     image_click_callback       = nullptr,
        const std::function<void(ZEngine::Maths::Vector4&)>& tint_color_change_callback = nullptr,
        float                                                column_width               = 100.0f);

    void DrawColoredTextLine(const char* start, const char* end, const ImVec4& color);

    void DrawEntityControl(
        std::string_view                         component_name,
        ZEngine::Rendering::Scenes::SceneEntity& entity,
        ImGuiTreeNodeFlags                       flags,
        std::function<void(void)>                callback);

    template <typename TComponent>
    void DrawEntityComponentControl(
        std::string_view                         component_name,
        ZEngine::Rendering::Scenes::SceneEntity& entity,
        ImGuiTreeNodeFlags                       flags,
        bool                                     enable_removal_option,
        std::function<void(TComponent&)>         component_definition_func)
    {
        if (entity.HasComponent<TComponent>())
        {

            ImVec2 content_region_available = ImGui::GetContentRegionAvail();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{2, 2});
            float line_height = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;

            bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(TComponent).hash_code()), flags, "%s", component_name.data());
            ImGui::PopStyleVar();

            ImGui::SameLine(content_region_available.x - line_height * 0.5f);
            if (ImGui::Button("...", ImVec2{line_height, line_height}))
            {
                ImGui::OpenPopup("ComponentSettings");
            }

            bool request_component_removal = false;
            if (ImGui::BeginPopup("ComponentSettings"))
            {
                if (enable_removal_option)
                {
                    if (ImGui::MenuItem("Remove Component"))
                    {
                        request_component_removal = true;
                        ImGui::CloseCurrentPopup();
                    }
                }

                ImGui::EndPopup();
            }

            if (opened)
            {
                auto& component = entity.GetComponent<TComponent>();
                if (component_definition_func)
                {
                    component_definition_func(component);
                }
                ImGui::TreePop();
            }

            if (request_component_removal)
            {
                entity.RemoveComponent<TComponent>();
            }

            ImGui::Dummy(ImVec2(0, 5));
            ImGui::Separator();
        }
    }
} // namespace Tetragrama::Helpers
