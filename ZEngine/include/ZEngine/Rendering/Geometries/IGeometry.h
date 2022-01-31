#pragma once
#include <vector>
#include <algorithm>
#include <Maths/Math.h>
#include <Rendering/Renderers/Storages/GraphicVertex.h>

namespace ZEngine::Rendering::Geometries {

    struct IGeometry {
        explicit IGeometry() = default;
        explicit IGeometry(std::vector<Renderers::Storages::GraphicVertex>&& vertices) : m_transform(Maths::Matrix4(1.0f)), m_vertices(std::move(vertices)) {}

        virtual ~IGeometry() = default;

        virtual void SetVertices(std::vector<Renderers::Storages::GraphicVertex>&& vertices) {
            m_vertices = std::move(vertices);
        }

        virtual void Update() {}

        virtual void Transform(const Maths::Matrix4& transform) {
            m_transform = transform * m_transform;
        }

        const Maths::Matrix4& GetTransform() const {
            return m_transform;
        }

        virtual std::vector<Renderers::Storages::GraphicVertex>& GetVertices() {
            return m_vertices;
        }

    protected:
        Maths::Matrix4                                  m_transform;
        std::vector<Renderers::Storages::GraphicVertex> m_vertices{};
    };
} // namespace ZEngine::Rendering::Geometries
