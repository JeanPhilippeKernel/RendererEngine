#pragma once
#include "Mesh2D.h"
#include "../../Managers/ShaderManager.h"


namespace Z_Engine::Rendering::Meshes {

	struct Rectangle2D : public Mesh2D
	{
		Rectangle2D() : Mesh2D (
				"simple_mesh_2d", 
				{ 
					-0.75f, -0.5f, 1.0f, 0.0f, 0.0f, 
					 0.75f, -0.5f, 1.0f, 1.0f, 0.0f,
					 0.75f,	 0.5f, 1.0f, 1.0f, 1.0f,
					-0.75f,	 0.5f, 1.0f, 0.0f, 1.0f
				},
				{ 0, 1, 2, 2, 3, 0 },
				{ 
					Z_Engine::Rendering::Buffers::Layout::ElementLayout<float>{3, "position"}, 
					Z_Engine::Rendering::Buffers::Layout::ElementLayout<float>{2, "texture_coord"} 
				}
			)
		{}

		virtual ~Rectangle2D() =  default;
	};
}