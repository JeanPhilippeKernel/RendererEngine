#pragma once
#include <glm/glm.hpp>

namespace ZEngine::Rendering::Renderers::Contracts
{
    struct UBOCameraLayout
    {
        alignas(16) glm::mat4 View         = glm::mat4(1.0f);
        alignas(16) glm::mat4 RotScaleView = glm::mat4(1.0f);
        alignas(16) glm::mat4 Projection   = glm::mat4(1.0f);
        alignas(16) glm::vec4 Position     = glm::vec4(1.0f);
    };

    struct UBOModelLayout
    {
        alignas(16) glm::mat4 Model = glm::mat4(1.0f);
    };
} // namespace ZEngine::Rendering::Renderers::Contracts
