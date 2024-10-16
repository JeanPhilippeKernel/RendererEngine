#pragma once

namespace ZEngine::Core
{

    struct IRenderable
    {
        IRenderable()          = default;
        virtual ~IRenderable() = default;

        virtual void Render() = 0;
    };
} // namespace ZEngine::Core
