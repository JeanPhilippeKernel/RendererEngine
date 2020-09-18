#pragma once
#include "IMesh.h"
#include "../Materials/SimpleMaterial2D.h"

namespace Z_Engine::Rendering::Meshes {

	struct Mesh2D : public IMesh
	{
		Mesh2D() = default;
		Mesh2D(const std::initializer_list<Renderers::Storages::GraphicVertex>& graphic_vertices)
			: IMesh(graphic_vertices)
		{
			this->m_material.reset(new Materials::SimpleMaterial2D());
		}

		virtual ~Mesh2D() = default;
	};
}