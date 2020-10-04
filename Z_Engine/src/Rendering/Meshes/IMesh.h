#pragma once
#include <vector>
#include <initializer_list>
#include <glm/glm.hpp>
#include <algorithm>

#include "../Materials/IMaterial.h"
#include "../Geometries/IGeometry.h"

#include "../../Z_EngineDef.h"

namespace Z_Engine::Rendering::Meshes {

	struct IMesh
	{
		explicit IMesh() = default;
		explicit IMesh(Ref<Geometries::IGeometry>&& geometry, Ref<Materials::IMaterial>&& material)
			:m_geometry(geometry), m_material(material) 
		{
		}

		virtual ~IMesh() = default;

		void SetMaterial(const Ref<Materials::IMaterial>& material)	{
			m_material = m_material;
		}

		void SetGeometry(const Ref<Geometries::IGeometry>& geometry) {
			m_geometry =  geometry;
		}

		Ref<Materials::IMaterial>& GetMaterial() { return m_material; }
		Ref<Geometries::IGeometry>& GetGeometry() { return m_geometry; }

	protected:
	   Ref<Materials::IMaterial> m_material;
	   Ref<Geometries::IGeometry> m_geometry;
	};
}