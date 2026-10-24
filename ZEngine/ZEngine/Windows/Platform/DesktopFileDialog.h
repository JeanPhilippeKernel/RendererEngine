#pragma once
#include <ZEngine/Windows/FileDialogRequest.h>
#include <span>
#include <string_view>

struct GLFWwindow;

namespace ZEngine::Windows::Platform
{
    [[nodiscard]] FileDialogRequest ShowDesktopFileDialog(GLFWwindow* window, bool save, std::span<std::string_view> filters, std::string_view directory, std::string_view filename, std::string_view title);
}
