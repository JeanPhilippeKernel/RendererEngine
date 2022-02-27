#pragma once
#include <Rendering/Geometries/IGeometry.h>

namespace ZEngine::Rendering::Geometries {

    struct QuadGeometry : public IGeometry {
        explicit QuadGeometry() = default;
        explicit QuadGeometry(const Maths::Vector3& position, const Maths::Vector3& scale, const Maths::Vector3& rotation_axis, float rotation_angle);
        ~QuadGeometry() = default;
    };
} // namespace ZEngine::Rendering::Geometries
