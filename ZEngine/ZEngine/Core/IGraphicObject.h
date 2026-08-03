#pragma once

namespace ZEngine::Core
{
    struct IGraphicObject
    {
        IGraphicObject()                       = default;
        virtual ~IGraphicObject()              = default;
        virtual uint32_t GetIdentifier() const = 0;
    };
} // namespace ZEngine::Core
