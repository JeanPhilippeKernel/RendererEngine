#pragma once
#include <Rendering/Geometries/IGeometry.h>

namespace ZEngine::Rendering::Geometries
{

    struct SquareGeometry : public IGeometry
    {
        SquareGeometry();
        ~SquareGeometry() = default;
    };
} // namespace ZEngine::Rendering::Geometries
