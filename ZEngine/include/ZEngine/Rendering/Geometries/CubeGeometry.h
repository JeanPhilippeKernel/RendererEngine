#pragma once
#include <Rendering/Geometries/IGeometry.h>

namespace ZEngine::Rendering::Geometries {

    struct CubeGeometry : public IGeometry {
        CubeGeometry();
        ~CubeGeometry() = default;
    };
} // namespace ZEngine::Rendering::Geometries
