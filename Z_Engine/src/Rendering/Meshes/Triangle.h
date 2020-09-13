#pragma once
#include "Mesh2D.h"
#include "../../Managers/ShaderManager.h"


namespace Z_Engine::Rendering::Meshes {

	struct Triangle2D : public Mesh2D
	{
		Triangle2D() : Mesh2D (
			"simple_mesh_2d",
			{
				0.5f, -0.5f, 1.0f, 1.0f, 0.0f,
			   -0.5f, -0.5f, 1.0f, 0.0f, 0.0f,
				0.0f,  0.5f, 1.0f, 0.0f, 1.0f
			},
			{ 0, 1, 2 },
			{ 
				Z_Engine::Rendering::Buffers::Layout::ElementLayout<float>{3, "position"},
				Z_Engine::Rendering::Buffers::Layout::ElementLayout<float>{2, "texture_coord"}
			}
		)
		{}

		virtual ~Triangle2D() =  default;
	};
}