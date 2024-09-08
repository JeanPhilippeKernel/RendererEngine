#pragma once
#include <string>

namespace ZEngine::Rendering::Components
{
    struct ValidComponent
    {
        ValidComponent() = default;
        ValidComponent(bool value) : IsValid(value) {}
        ~ValidComponent() = default;

        bool IsValid{true};
    };
} // namespace ZEngine::Rendering::Components
