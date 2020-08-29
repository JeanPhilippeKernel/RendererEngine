#pragma once
#include "Mesh2D.h"
#include "../../Managers/ShaderManager.h"


namespace Z_Engine::Rendering::Meshes {

	struct Triangle : public Mesh2D
	{
		Triangle() : Mesh2D (
			Z_Engine::Managers::ShaderManager::Get("simple_mesh_2d"),
			{
				0.5f, -0.5f, 1.0f,
			   -0.5f, -0.5f, 1.0f,
				0.0f,  0.5f, 1.0f
			},
			{ 0, 1, 2 },
			{ Z_Engine::Rendering::Buffers::Layout::ElementLayout<float>{3, "position"} }
		)
		{}

		virtual ~Triangle() =  default;
	};
}