#pragma once
#include <string>

namespace ZEngine::Window {
    struct WindowConfiguration {
        uint32_t    Height;
        uint32_t    Width;
        bool        EnableVsync;
        std::string Title;
    };

} // namespace ZEngine::Window
