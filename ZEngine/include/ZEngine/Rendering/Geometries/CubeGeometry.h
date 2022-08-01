#pragma once
#include <Rendering/Geometries/IGeometry.h>

namespace ZEngine::Rendering::Geometries {

    struct CubeGeometry : public IGeometry {
        explicit CubeGeometry(const Maths::Vector3& position = {0.0f, 0.0f, 0.0f}, const Maths::Vector3& scale = {0.0f, 0.0f, 0.0f},
            const Maths::Vector3& rotation_axis = {0.0f, 0.0f, 0.0f}, float rotation_angle = 0.0f);
        ~CubeGeometry() = default;
    };
} // namespace ZEngine::Rendering::Geometries
