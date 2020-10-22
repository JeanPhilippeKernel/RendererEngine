#pragma once
#include "MeshBuilder.h"
#include "../Geometries/QuadGeometry.h"
#include "../Geometries/SquareGeometry.h"

#include "../../dependencies/glm/glm.hpp"
#include "../../dependencies/glm/gtc/matrix_transform.hpp"

using namespace Z_Engine::Rendering::Geometries;

namespace Z_Engine::Rendering::Meshes {

	Mesh* MeshBuilder::CreateQuad(const glm::vec2& position, const glm::vec2& size, float angle) {
	   	
		Mesh* mesh = new Mesh{};
		Ref<QuadGeometry> quad_geometry;
		quad_geometry.reset(new QuadGeometry{});
		
		glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), { position.x, position.y, 0.0f }) *
			glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), { size.x, size.y, 0.0f });
		
		quad_geometry->ApplyTransform(transform);
		
		mesh->SetGeometry(quad_geometry);
		return mesh;
	}

	Mesh* MeshBuilder::CreateSquare(const glm::vec2& position, const glm::vec2& size, float angle) {
		
		Mesh* mesh = new Mesh{};
		Ref<SquareGeometry> square_geometry;
		square_geometry.reset(new SquareGeometry{});
		
		glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), { position.x, position.y, 0.0f }) *
			glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), { size.x, size.y, 0.0f });
		
		square_geometry->ApplyTransform(transform);
		
		mesh->SetGeometry(square_geometry);
		return mesh;
	}
}