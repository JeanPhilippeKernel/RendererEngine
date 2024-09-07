#pragma once
#include <Rendering/Geometries/IGeometry.h>

namespace ZEngine::Rendering::Geometries
{

    struct QuadGeometry : public IGeometry
    {
        QuadGeometry();
        ~QuadGeometry() = default;
    };
} // namespace ZEngine::Rendering::Geometries
