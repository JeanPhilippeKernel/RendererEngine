#include <pch.h>
#include <Rendering/Geometries/SquareGeometry.h>

namespace ZEngine::Rendering::Geometries {
    SquareGeometry::SquareGeometry(const Maths::Vector3& position, const Maths::Vector3& scale, const Maths::Vector3& rotation_axis, float rotation_angle)
        : IGeometry(position, scale, rotation_axis, rotation_angle,
            {Renderers::Storages::GraphicVertex({-0.5f, -0.5f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}),
                Renderers::Storages::GraphicVertex({0.5f, -0.5f, 1.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f}),
                Renderers::Storages::GraphicVertex({0.0f, 0.5f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f}),
                Renderers::Storages::GraphicVertex({0.5f, -0.5f, 1.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f})}) {}
} // namespace ZEngine::Rendering::Geometries
