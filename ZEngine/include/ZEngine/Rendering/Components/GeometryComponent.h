#pragma once
#include <ZEngineDef.h>
#include <Rendering/Geometries/IGeometry.h>

namespace ZEngine::Rendering::Components {
    struct GeometryComponent {
        GeometryComponent(const Ref<Geometries::IGeometry>& geometry) {
            m_geometry = geometry;
        }

        GeometryComponent(Ref<Geometries::IGeometry>&& geometry) {
            m_geometry = geometry;
        }

        GeometryComponent(Geometries::IGeometry* const geometry_ptr) : m_geometry(geometry_ptr) {}

        Ref<Geometries::IGeometry> GetGeometry() const {
            return m_geometry;
        }

    private:
        Ref<Geometries::IGeometry> m_geometry;
    };

} // namespace ZEngine::Rendering::Components
