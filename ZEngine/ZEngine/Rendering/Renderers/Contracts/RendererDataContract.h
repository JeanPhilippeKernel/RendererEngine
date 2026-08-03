#pragma once
#include <ZEngine/Core/Maths/Matrix.h>

namespace ZEngine::Rendering::Renderers::Contracts
{
    struct UBOCameraLayout
    {
        alignas(16) ZEngine::Core::Maths::Mat4f View       = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        alignas(16) ZEngine::Core::Maths::Mat4f Projection = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        alignas(16) ZEngine::Core::Maths::Vec4f Position   = ZEngine::Core::Maths::Vec4f(0.0f, 0.0f, 0.0f, 1.0f);
    };

    struct UBOModelLayout
    {
        alignas(16) ZEngine::Core::Maths::Mat4f Model = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
    };
} // namespace ZEngine::Rendering::Renderers::Contracts
