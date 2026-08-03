#pragma once
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Rendering/Geometries/GeometryEnum.h>
#include <ZEngine/Rendering/Renderers/Storages/GraphicVertex.h>
#include <vector>

namespace ZEngine::Rendering::Geometries
{

    /*Need to be deprecated*/
    struct IGeometry : public Helpers::RefCounted
    {
        IGeometry() = default;

        IGeometry(std::vector<Renderers::Storages::GraphicVertex>&& vertices) : m_vertices(std::move(vertices)) {}

        virtual ~IGeometry() = default;

        virtual void SetVertices(std::vector<Renderers::Storages::GraphicVertex>&& vertices)
        {
            m_vertices = std::move(vertices);
        }

        virtual const ZEngine::Core::Maths::Mat4f& GetTransform() const
        {
            return m_transform;
        }

        virtual void SetTransform(const ZEngine::Core::Maths::Mat4f& transform)
        {
            m_transform = transform;
        }

        virtual std::vector<Renderers::Storages::GraphicVertex>& GetVertices()
        {
            return m_vertices;
        }

        virtual GeometryType GetGeometryType() const
        {
            return m_geometry_type;
        }

    protected:
        ZEngine::Core::Maths::Mat4f                     m_transform{ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>()};
        std::vector<Renderers::Storages::GraphicVertex> m_vertices{};
        GeometryType                                    m_geometry_type{GeometryType::CUSTOM};
    };
} // namespace ZEngine::Rendering::Geometries
