#pragma once
#include "Mesh.h"
#include "../../Z_EngineDef.h"

namespace Z_Engine::Rendering::Meshes {
	
	struct  MeshBuilder {

		MeshBuilder() = delete;
		MeshBuilder(const MeshBuilder&) = delete;

		 static Mesh* CreateQuad(const glm::vec2& position, const glm::vec2& size, float angle); 
		 static Mesh* CreateSquare(const glm::vec2& position, const glm::vec2& size, float angle); 
	};
}
