#pragma once
#include <ZEngine/Core/Maths/Vec.h>
#include <imgui/imgui_internal.h>

namespace Tetragrama::Helpers
{
    void DrawVec4Control(std::string_view label, ZEngine::Core::Maths::Vec4f& values, const std::function<void(ZEngine::Core::Maths::Vec4f&)>& callback = nullptr, float default_value = 0.0f, float column_width = 100.0f);
    void DrawVec3Control(std::string_view label, ZEngine::Core::Maths::Vec3f& values, const std::function<void(ZEngine::Core::Maths::Vec3f&)>& callback = nullptr, float default_value = 0.0f, float column_width = 100.0f);
    void DrawVec2Control(std::string_view label, ZEngine::Core::Maths::Vec2f& values, const std::function<void(ZEngine::Core::Maths::Vec2f&)>& callback = nullptr, float default_value = 0.0f, float column_width = 100.0f);

    void DrawInputTextControl(std::string_view label, std::string_view content, const std::function<void(std::string_view)>& callback = nullptr, bool read_only_mode = false, float column_width = 50.f);

    void DrawDragFloatControl(std::string_view label, float value, float increment_speed = 1.0f, float min_value = 0.0f, float max_value = 0.0f, std::string_view fmt = "%.2f", const std::function<void(float)>& callback = nullptr, float column_width = 100.0f);

    void DrawCenteredButtonControl(std::string_view label, const std::function<void(void)>& callback = nullptr);

    void DrawColorEdit4Control(std::string_view label, ZEngine::Core::Maths::Vec4f& values, const std::function<void(ZEngine::Core::Maths::Vec4f&)>& callback = nullptr, float default_value = 0.0f, float column_width = 100.0f);

    void DrawColorEdit3Control(std::string_view label, ZEngine::Core::Maths::Vec3f& values, const std::function<void(ZEngine::Core::Maths::Vec3f&)>& callback, float default_value = 0.0f, float column_width = 100.0f);

    void DrawTextureColorControl(std::string_view label, ImTextureID texture_id, ZEngine::Core::Maths::Vec4f& texture_tint_color, bool enable_zoom = true, const std::function<void(void)>& image_click_callback = nullptr, const std::function<void(ZEngine::Core::Maths::Vec4f&)>& tint_color_change_callback = nullptr, float column_width = 100.0f);

    void DrawColoredTextLine(const char* start, const char* end, const ImVec4& color);
} // namespace Tetragrama::Helpers
