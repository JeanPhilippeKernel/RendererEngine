#pragma once
#include <ZEngine.h>

namespace Tetragrama::Helpers {
    void DrawVec4Control(std::string_view label, ZEngine::Maths::Vector4& values, const std::function<void(ZEngine::Maths::Vector4&)>& callback = nullptr,
        float default_value = 0.0f, float column_width = 100.0f);
    void DrawVec3Control(std::string_view label, ZEngine::Maths::Vector3& values, const std::function<void(ZEngine::Maths::Vector3&)>& callback = nullptr,
        float default_value = 0.0f, float column_width = 100.0f);
    void DrawVec2Control(std::string_view label, ZEngine::Maths::Vector2& values, const std::function<void(ZEngine::Maths::Vector2&)>& callback = nullptr,
        float default_value = 0.0f, float column_width = 100.0f);

    void DrawInputTextControl(std::string_view label, std::string_view content, const std::function<void(std::string_view)>& callback = nullptr, bool read_only_mode = false,
        float column_width = 100.0f);

    void DrawDragFloatControl(std::string_view label, float value, float increment_speed = 1.0f, float min_value = 0.0f, float max_value = 0.0f, std::string_view fmt = "%.2f",
        const std::function<void(float)>& callback = nullptr, float column_width = 100.0f);

    void DrawCenteredButtonControl(std::string_view label, const std::function<void(void)>& callback = nullptr);
} // namespace Tetragrama::Helpers
