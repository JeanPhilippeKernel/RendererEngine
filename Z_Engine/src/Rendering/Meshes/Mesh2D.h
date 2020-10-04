#pragma once
#include "IMesh.h"

namespace Z_Engine::Rendering::Meshes {

	struct Mesh2D : public IMesh
	{
		explicit Mesh2D() = default;
		explicit Mesh2D(Ref<Geometries::IGeometry> geometry, Ref<Materials::IMaterial> material)
			: IMesh(std::move(geometry), std::move(material))
		{
		}

		explicit Mesh2D(Ref<Geometries::IGeometry>& geometry, Ref<Materials::IMaterial>& material)
			: IMesh(std::move(geometry), std::move(material))
		{
		}

		virtual ~Mesh2D() = default;
	};
}