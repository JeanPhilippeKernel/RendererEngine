#pragma once
#include <Rendering/Geometries/IGeometry.h>

namespace ZEngine::Rendering::Geometries {

    struct SquareGeometry : public IGeometry {
        explicit SquareGeometry() = default;
        explicit SquareGeometry(const Maths::Vector3& position, const Maths::Vector3& scale, const Maths::Vector3& rotation_axis, float rotation_angle);
        ~SquareGeometry() = default;
    };
} // namespace ZEngine::Rendering::Geometries
