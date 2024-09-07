#include <pch.h>
#include <Rendering/Geometries/CubeGeometry.h>

namespace ZEngine::Rendering::Geometries
{
    CubeGeometry::CubeGeometry()
        : IGeometry({
              Renderers::Storages::GraphicVertex({-0.75f, -0.75f, -0.75f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}),
              Renderers::Storages::GraphicVertex({0.75f, -0.75f, -0.75f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}),
              Renderers::Storages::GraphicVertex({0.75f, 0.75f, -0.75f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}),
              Renderers::Storages::GraphicVertex({0.75f, 0.75f, -0.75f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}),
              Renderers::Storages::GraphicVertex({-0.75f, 0.75f, -0.75f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}),
              Renderers::Storages::GraphicVertex({-0.75f, -0.75f, -0.75f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}),

              Renderers::Storages::GraphicVertex({-0.75f, -0.75f, 0.75f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}),
              Renderers::Storages::GraphicVertex({0.75f, -0.75f, 0.75f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}),
              Renderers::Storages::GraphicVertex({0.75f, 0.75f, 0.75f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}),
              Renderers::Storages::GraphicVertex({0.75f, 0.75f, 0.75f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}),
              Renderers::Storages::GraphicVertex({-0.75f, 0.75f, 0.75f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}),
              Renderers::Storages::GraphicVertex({-0.75f, -0.75f, 0.75f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}),

              Renderers::Storages::GraphicVertex({-0.75f, 0.75f, 0.75f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}),
              Renderers::Storages::GraphicVertex({-0.75f, 0.75f, -0.75f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}),
              Renderers::Storages::GraphicVertex({-0.75f, -0.75f, -0.75f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}),
              Renderers::Storages::GraphicVertex({-0.75f, -0.75f, -0.75f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}),
              Renderers::Storages::GraphicVertex({-0.75f, -0.75f, 0.75f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}),
              Renderers::Storages::GraphicVertex({-0.75f, 0.75f, 0.75f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}),

              Renderers::Storages::GraphicVertex({0.75f, 0.75f, 0.75f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}),
              Renderers::Storages::GraphicVertex({0.75f, 0.75f, -0.75f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}),
              Renderers::Storages::GraphicVertex({0.75f, -0.75f, -0.75f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}),
              Renderers::Storages::GraphicVertex({0.75f, -0.75f, -0.75f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}),
              Renderers::Storages::GraphicVertex({0.75f, -0.75f, 0.75f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}),
              Renderers::Storages::GraphicVertex({0.75f, 0.75f, 0.75f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}),

              Renderers::Storages::GraphicVertex({-0.75f, -0.75f, -0.75f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}),
              Renderers::Storages::GraphicVertex({0.75f, -0.75f, -0.75f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}),
              Renderers::Storages::GraphicVertex({0.75f, -0.75f, 0.75f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}),
              Renderers::Storages::GraphicVertex({0.75f, -0.75f, 0.75f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}),
              Renderers::Storages::GraphicVertex({-0.75f, -0.75f, 0.75f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}),
              Renderers::Storages::GraphicVertex({-0.75f, -0.75f, -0.75f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}),

              Renderers::Storages::GraphicVertex({-0.75f, 0.75f, -0.75f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}),
              Renderers::Storages::GraphicVertex({0.75f, 0.75f, -0.75f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}),
              Renderers::Storages::GraphicVertex({0.75f, 0.75f, 0.75f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}),
              Renderers::Storages::GraphicVertex({0.75f, 0.75f, 0.75f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}),
              Renderers::Storages::GraphicVertex({-0.75f, 0.75f, 0.75f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}),
              Renderers::Storages::GraphicVertex({-0.75f, 0.75f, -0.75f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}),
          })
    {
        m_geometry_type = GeometryType::CUBE;
    }
} // namespace ZEngine::Rendering::Geometries
