#pragma once
#include <Rendering/Geometries/IGeometry.h>

namespace ZEngine::Rendering::Geometries {

    struct CubeGeometry : public IGeometry {
        explicit CubeGeometry() = default;
        explicit CubeGeometry(const Maths::Vector3& position, const Maths::Vector3& scale, const Maths::Vector3& rotation_axis, float rotation_angle);
        ~CubeGeometry() = default;
    };
} // namespace ZEngine::Rendering::Geometries
