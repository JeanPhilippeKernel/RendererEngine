#include <pch.h>
#include <UIComponentDrawerHelper.h>

namespace Tetragrama::Helpers {

    void DrawVec4Control(
        std::string_view label, ZEngine::Maths::Vector4& values, const std::function<void(ZEngine::Maths::Vector4&)>& callback, float default_value, float column_width) {
        ImGuiIO& io                = ImGui::GetIO();
        auto     default_bold_font = io.Fonts->Fonts[0];

        ImGui::PushID(label.data(), (label.data() + label.size()));

        ImGui::Columns(2);

        ImGui::SetColumnWidth(0, column_width);
        ImGui::Text(label.data());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(4, ImGui::CalcItemWidth());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.5f, 0});

        float  line_height = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
        ImVec2 button_size = {line_height + 3.0f, line_height};

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.9f, 0.2f, 0.2f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
        ImGui::PushFont(default_bold_font);
        if (ImGui::Button("X", button_size)) {
            values.x = default_value;
            if (callback) {
                callback(values);
            }
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        if (ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f")) {
            if (callback) {
                callback(values);
            }
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.1f, 0.8f, 0.15f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.2f, 0.9f, 0.2f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.1f, 0.8f, 0.15f, 1.0f});
        ImGui::PushFont(default_bold_font);
        if (ImGui::Button("Y", button_size)) {
            values.y = default_value;
            if (callback) {
                callback(values);
            }
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        if (ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f")) {
            if (callback) {
                callback(values);
            }
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();


        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.1f, 0.15f, 0.8f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.2f, 0.2f, 0.9f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.1f, 0.15f, 0.8f, 1.0f});
        ImGui::PushFont(default_bold_font);
        if (ImGui::Button("Z", button_size)) {
            values.z = default_value;
            if (callback) {
                callback(values);
            }
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        if (ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f")) {
            if (callback) {
                callback(values);
            }
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();


        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{1.0f, 1.0f, 1.0f, 0.5f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{1.f, 1.0f, 1.0f, 0.8f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{1.0f, 1.0f, 1.0f, 0.5f});
        ImGui::PushFont(default_bold_font);
        if (ImGui::Button("W", button_size)) {
            values.w = default_value;
            if (callback) {
                callback(values);
            }
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        if (ImGui::DragFloat("##W", &values.w, 0.1f, 0.0f, 0.0f, "%.2f")) {
            if (callback) {
                callback(values);
            }
        }

        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::Columns(1);
        ImGui::PopID();
    }

    void DrawVec3Control(
        std::string_view label, ZEngine::Maths::Vector3& values, const std::function<void(ZEngine::Maths::Vector3&)>& callback, float default_value, float column_width) {
        ImGuiIO& io                = ImGui::GetIO();
        auto     default_bold_font = io.Fonts->Fonts[0];

        ImGui::PushID(label.data(), (label.data() + label.size()));

        ImGui::Columns(2);

        ImGui::SetColumnWidth(0, column_width);
        ImGui::Text(label.data());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.5f, 0});

        float  line_height = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
        ImVec2 button_size = {line_height + 3.0f, line_height};

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.9f, 0.2f, 0.2f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
        ImGui::PushFont(default_bold_font);
        if (ImGui::Button("X", button_size)) {
            values.x = default_value;
            if (callback) {
                callback(values);
            }
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        if (ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f")) {
            if (callback) {
                callback(values);
            }
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.1f, 0.8f, 0.15f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.2f, 0.9f, 0.2f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.1f, 0.8f, 0.15f, 1.0f});
        ImGui::PushFont(default_bold_font);
        if (ImGui::Button("Y", button_size)) {
            values.y = default_value;
            if (callback) {
                callback(values);
            }
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        if (ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f")) {
            if (callback) {
                callback(values);
            }
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();


        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.1f, 0.15f, 0.8f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.2f, 0.2f, 0.9f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.1f, 0.15f, 0.8f, 1.0f});
        ImGui::PushFont(default_bold_font);
        if (ImGui::Button("Z", button_size)) {
            values.z = default_value;
            if (callback) {
                callback(values);
            }
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        if (ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f")) {
            if (callback) {
                callback(values);
            }
        }
        ImGui::PopItemWidth();

        ImGui::PopStyleVar();

        ImGui::Columns(1);

        ImGui::PopID();
    }


    void DrawVec2Control(
        std::string_view label, ZEngine::Maths::Vector2& values, const std::function<void(ZEngine::Maths::Vector2&)>& callback, float default_value, float column_width) {
        ImGuiIO& io                = ImGui::GetIO();
        auto     default_bold_font = io.Fonts->Fonts[0];

        ImGui::PushID(label.data(), (label.data() + label.size()));

        ImGui::Columns(2);

        ImGui::SetColumnWidth(0, column_width);
        ImGui::Text(label.data());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(2, ImGui::CalcItemWidth());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.5f, 0});

        float  line_height = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
        ImVec2 button_size = {line_height + 3.0f, line_height};

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.9f, 0.2f, 0.2f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
        ImGui::PushFont(default_bold_font);
        if (ImGui::Button("X", button_size)) {
            values.x = default_value;
            if (callback) {
                callback(values);
            }
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        if (ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f")) {
            if (callback) {
                callback(values);
            }
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.1f, 0.8f, 0.15f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.2f, 0.9f, 0.2f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.1f, 0.8f, 0.15f, 1.0f});
        ImGui::PushFont(default_bold_font);
        if (ImGui::Button("Y", button_size)) {
            values.y = default_value;
            if (callback) {
                callback(values);
            }
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        if (ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f")) {
            if (callback) {
                callback(values);
            }
        }

        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::Columns(1);
        ImGui::PopID();
    }


    void DrawInputTextControl(std::string_view label, std::string_view content, const std::function<void(std::string_view)>& callback, bool read_only_mode, float column_width) {
        ImGui::PushID(label.data(), (label.data() + label.size()));
        ImGui::Columns(2);

        ImGui::SetColumnWidth(0, column_width);
        ImGui::Text(label.data());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(1, ImGui::CalcItemWidth() + 60.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.5f, 0});

        char buffer[1024];
        memset(buffer, 0, sizeof(buffer));
        auto raw_entity_name = content.data();
        memcpy(buffer, raw_entity_name, strlen(raw_entity_name));

        auto flag = 0;
        if (read_only_mode) {
            flag |= ImGuiInputTextFlags_ReadOnly;
        }
        if (ImGui::InputTextEx("##Input", NULL, buffer, sizeof(buffer), ImVec2(0, 0), flag)) {
            if (callback) {
                callback(std::string_view{buffer});
            }
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::Columns(1);
        ImGui::PopID();
    }


    void DrawDragFloatControl(std::string_view label, float value, float increment_speed, float min_value, float max_value, std::string_view fmt,
        const std::function<void(float)>& callback, float column_width) {
        ImGui::PushID(label.data(), (label.data() + label.size()));
        ImGui::Columns(2);

        ImGui::SetColumnWidth(0, column_width);
        ImGui::Text(label.data());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(1, ImGui::CalcItemWidth() + 60.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.5f, 0});

        if (ImGui::DragFloat("##DragFloat", &value, increment_speed, min_value, max_value, fmt.data())) {
            if (callback) {
                callback(value);
            }
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::Columns(1);
        ImGui::PopID();
    }

    void DrawCenteredButtonControl(std::string_view label, const std::function<void(void)>& callback) {

        ImGui::PushID(label.data(), (label.data() + label.size()));

        ImGui::BeginTable("##table", 3);

        ImGui::TableNextColumn();

        ImGui::TableNextColumn();
        auto   table_colum_width = ImGui::GetColumnWidth();
        float  line_height       = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
        ImVec2 button_size       = {table_colum_width, line_height};

        if (ImGui::Button(label.data(), button_size)) {
            if (callback) {
                callback();
            }
        }

        ImGui::TableNextColumn();
        ImGui::EndTable();
        ImGui::PopID();
    }

    void DrawColorEdit4Control(
        std::string_view label, ZEngine::Maths::Vector4& values, const std::function<void(ZEngine::Maths::Vector4&)>& callback, float default_value, float column_width) {
        ImGui::PushID(label.data(), (label.data() + label.size()));

        ImGui::Columns(2);

        ImGui::SetColumnWidth(0, column_width);
        ImGui::Text(label.data());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(1, ImGui::CalcItemWidth() + 60.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.5f, 0});

        if (ImGui::ColorEdit4("##TintColor", ZEngine::Maths::value_ptr(values))) {
            if (callback) {
                callback(values);
            }
        }

        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::Columns(1);
        ImGui::PopID();
    }

    void DrawColorEdit3Control(
        std::string_view label, ZEngine::Maths::Vector3& values, const std::function<void(ZEngine::Maths::Vector3&)>& callback, float default_value, float column_width) {
        ImGui::PushID(label.data(), (label.data() + label.size()));

        ImGui::Columns(2);

        ImGui::SetColumnWidth(0, column_width);
        ImGui::Text(label.data());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(1, ImGui::CalcItemWidth() + 60.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.5f, 0});

        if (ImGui::ColorEdit3("##TintColor", ZEngine::Maths::value_ptr(values))) {
            if (callback) {
                callback(values);
            }
        }

        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::Columns(1);
        ImGui::PopID();
    }

    void DrawTextureColorControl(std::string_view label, ImTextureID texture_id, ZEngine::Maths::Vector4& tint_color, bool enable_zoom,
        const std::function<void(void)>& image_click_callback, const std::function<void(ZEngine::Maths::Vector4&)>& tint_color_change_callback, float column_width) {
        ImGui::PushID(label.data(), (label.data() + label.size()));
        ImGui::Columns(2);

        ImGui::SetColumnWidth(0, column_width);
        ImGui::Text(label.data());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(1, ImGui::CalcItemWidth() + 60.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.5f, 0});

        float  line_height = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
        ImVec2 button_size = {line_height + 3.0f, line_height};

        if (ImGui::ImageButton(texture_id, button_size, ImVec2(0, 0), ImVec2(1, 1), 1, ImVec4(0, 0, 0, 0), ImVec4{tint_color.x, tint_color.y, tint_color.z, tint_color.w})) {
            if (image_click_callback) {
                image_click_callback();
            }
        }
        if (enable_zoom) {
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Image(texture_id, ImVec2(200, 200), {0, 0}, {1, 1}, ImVec4{tint_color.x, tint_color.y, tint_color.z, tint_color.w});
                ImGui::EndTooltip();
            }
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1, 4));
        if (ImGui::ColorEdit4("##TintColor", ZEngine::Maths::value_ptr(tint_color))) {
            if (tint_color_change_callback) {
                tint_color_change_callback(tint_color);
            }
        }
        ImGui::PopItemWidth();

        ImGui::PopStyleVar(2);
        ImGui::Columns(1);
        ImGui::PopID();
    }
} // namespace Tetragrama::Helpers
