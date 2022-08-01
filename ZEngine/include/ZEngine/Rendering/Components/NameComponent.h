#pragma once
#include <string>

namespace ZEngine::Rendering::Components {
    struct NameComponent {
        NameComponent() = default;
        NameComponent(std::string_view name) : Name(name) {}
        ~NameComponent() = default;

        std::string Name;
    };
} // namespace ZEngine::Rendering::Components
