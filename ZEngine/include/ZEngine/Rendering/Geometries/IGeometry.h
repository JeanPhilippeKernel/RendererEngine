#pragma once
#include <vector>
#include <algorithm>
#include <Maths/Math.h>

#include <Rendering/Renderers/Storages/GraphicVertex.h>
#include <Rendering/Buffers/VertexBuffer.h>

#include <ZEngineDef.h>

namespace ZEngine::Rendering::Geometries {

    struct IGeometry {
        explicit IGeometry() = default;
        explicit IGeometry(std::vector<Renderers::Storages::GraphicVertex>&& vertices) : m_vertices(std::move(vertices)) {}

        virtual ~IGeometry() = default;

        virtual void SetVertices(std::vector<Renderers::Storages::GraphicVertex>&& vertices) {
            m_vertices = std::move(vertices);
        }

        virtual void Update() {}

        virtual void ApplyTransform(const Maths::Matrix4& transform) {
            std::for_each(std::begin(m_vertices), std::end(m_vertices), [&](Renderers::Storages::GraphicVertex& vertex) { vertex.ApplyMatrixToPosition(transform); });
        }

        virtual std::vector<Renderers::Storages::GraphicVertex>& GetVertices() {
            return m_vertices;
        }

    protected:
        std::vector<Renderers::Storages::GraphicVertex> m_vertices{};
    };
} // namespace ZEngine::Rendering::Geometries
