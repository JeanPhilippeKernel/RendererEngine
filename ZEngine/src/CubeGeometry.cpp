#include <pch.h>
#include <Rendering/Geometries/CubeGeometry.h>

using namespace ZEngine::Rendering::Renderers::Storages;

namespace ZEngine::Rendering::Geometries {
    CubeGeometry::CubeGeometry()
        : IGeometry({
            GraphicVertex({-0.75f, -0.75f, -0.75f}, {0.0f, 0.0f}),
            GraphicVertex({0.75f, -0.75f, -0.75f}, {1.0f, 0.0f}),
            GraphicVertex({0.75f, 0.75f, -0.75f}, {1.0f, 1.0f}),
            GraphicVertex({0.75f, 0.75f, -0.75f}, {1.0f, 1.0f}),
            GraphicVertex({-0.75f, 0.75f, -0.75f}, {0.0f, 1.0f}),
            GraphicVertex({-0.75f, -0.75f, -0.75f}, {0.0f, 0.0f}),

            GraphicVertex({-0.75f, -0.75f, 0.75f}, {0.0f, 0.0f}),
            GraphicVertex({0.75f, -0.75f, 0.75f}, {1.0f, 0.0f}),
            GraphicVertex({0.75f, 0.75f, 0.75f}, {1.0f, 1.0f}),
            GraphicVertex({0.75f, 0.75f, 0.75f}, {1.0f, 1.0f}),
            GraphicVertex({-0.75f, 0.75f, 0.75f}, {0.0f, 1.0f}),
            GraphicVertex({-0.75f, -0.75f, 0.75f}, {0.0f, 0.0f}),

            GraphicVertex({-0.75f, 0.75f, 0.75f}, {1.0f, 0.0f}),
            GraphicVertex({-0.75f, 0.75f, -0.75f}, {1.0f, 1.0f}),
            GraphicVertex({-0.75f, -0.75f, -0.75f}, {0.0f, 1.0f}),
            GraphicVertex({-0.75f, -0.75f, -0.75f}, {0.0f, 1.0f}),
            GraphicVertex({-0.75f, -0.75f, 0.75f}, {0.0f, 0.0f}),
            GraphicVertex({-0.75f, 0.75f, 0.75f}, {1.0f, 0.0f}),

            GraphicVertex({0.75f, 0.75f, 0.75f}, {1.0f, 0.0f}),
            GraphicVertex({0.75f, 0.75f, -0.75f}, {1.0f, 1.0f}),
            GraphicVertex({0.75f, -0.75f, -0.75f}, {0.0f, 1.0f}),
            GraphicVertex({0.75f, -0.75f, -0.75f}, {0.0f, 1.0f}),
            GraphicVertex({0.75f, -0.75f, 0.75f}, {0.0f, 0.0f}),
            GraphicVertex({0.75f, 0.75f, 0.75f}, {1.0f, 0.0f}),

            GraphicVertex({-0.75f, -0.75f, -0.75f}, {0.0f, 1.0f}),
            GraphicVertex({0.75f, -0.75f, -0.75f}, {1.0f, 1.0f}),
            GraphicVertex({0.75f, -0.75f, 0.75f}, {1.0f, 0.0f}),
            GraphicVertex({0.75f, -0.75f, 0.75f}, {1.0f, 0.0f}),
            GraphicVertex({-0.75f, -0.75f, 0.75f}, {0.0f, 0.0f}),
            GraphicVertex({-0.75f, -0.75f, -0.75f}, {0.0f, 1.0f}),

            GraphicVertex({-0.75f, 0.75f, -0.75f}, {0.0f, 1.0f}),
            GraphicVertex({0.75f, 0.75f, -0.75f}, {1.0f, 1.0f}),
            GraphicVertex({0.75f, 0.75f, 0.75f}, {1.0f, 0.0f}),
            GraphicVertex({0.75f, 0.75f, 0.75f}, {1.0f, 0.0f}),
            GraphicVertex({-0.75f, 0.75f, 0.75f}, {0.0f, 0.0f}),
            GraphicVertex({-0.75f, 0.75f, -0.75f}, {0.0f, 1.0f}),
        }) {}
} // namespace ZEngine::Rendering::Geometries
