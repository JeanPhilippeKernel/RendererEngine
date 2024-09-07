#include <pch.h>
#include <Rendering/Geometries/QuadGeometry.h>

namespace ZEngine::Rendering::Geometries
{
    QuadGeometry::QuadGeometry()
        : IGeometry(
              {Renderers::Storages::GraphicVertex({-0.75f, -0.5f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}),
               Renderers::Storages::GraphicVertex({0.75f, -0.5f, 1.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f}),
               Renderers::Storages::GraphicVertex({0.75f, 0.5f, 1.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f}),
               Renderers::Storages::GraphicVertex({-0.75f, 0.5f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f})})
    {
    }
} // namespace ZEngine::Rendering::Geometries
