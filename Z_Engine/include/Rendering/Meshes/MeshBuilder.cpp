#pragma once
#include "MeshBuilder.h"
#include "../Geometries/QuadGeometry.h"
#include "../Geometries/SquareGeometry.h"
#include "../Geometries/CubeGeometry.h"

#include "../../dependencies/glm/glm.hpp"
#include "../../dependencies/glm/gtc/matrix_transform.hpp"

#include "../Materials/StandardMaterial.h"

using namespace Z_Engine::Rendering::Textures;
using namespace Z_Engine::Rendering::Geometries;
using namespace Z_Engine::Rendering::Materials;

namespace Z_Engine::Rendering::Meshes {

	Mesh* MeshBuilder::CreateQuad(const glm::vec2& position, const glm::vec2& size, float angle) {
		Mesh*				mesh			= new Mesh{};
		QuadGeometry*		quad_geometry	= new QuadGeometry{};
		Texture*			texture			= CreateTexture(1, 1);
		StandardMaterial*	material		= new StandardMaterial{};
		
		glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), { position.x, position.y, 0.0f }) *
			glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), { size.x, size.y, 0.0f });
		
		quad_geometry->ApplyTransform(transform);
		material->SetTexture(texture);
		
		mesh->SetGeometry(quad_geometry);
		mesh->SetMaterial(material);
		return mesh;
	}

	Mesh* MeshBuilder::CreateQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color, float angle) {
		Mesh*				mesh		= CreateQuad(position, size, angle);
		Texture*			texture		= CreateTexture(1, 1);
		StandardMaterial*	material	= new StandardMaterial{};
		
		texture->SetData(color.x, color.y, color.z, 255.0f);
		material->SetTexture(texture);
		mesh->SetMaterial(material);

		return mesh;
	}


	Mesh* MeshBuilder::CreateQuad(const glm::vec2& position, const glm::vec2& size, float angle, Texture2D* const texture) {
		Texture* internal_texture = dynamic_cast<Texture*>(texture);
		return CreateQuad(position, size, angle,  internal_texture);
	}

	Mesh* MeshBuilder::CreateQuad(const glm::vec2& position, const glm::vec2& size, float angle, Texture* const texture) {
		Mesh*				mesh		= CreateQuad(position, size, angle);
		StandardMaterial*	material	= new StandardMaterial{};
		
		material->SetTexture(texture);
		mesh->SetMaterial(material);

		return mesh;
	}
	

	Mesh* MeshBuilder::CreateQuad(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Texture2D>& texture) {
	   auto internal_texture = std::dynamic_pointer_cast<Texture>(texture);
	   assert(texture != nullptr);
	   return CreateQuad(position, size, angle, internal_texture);
	}

	Mesh* MeshBuilder::CreateQuad(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Texture>& texture) {
		Mesh*				mesh		= CreateQuad(position, size, angle);
		StandardMaterial*	material	=  new StandardMaterial{};
		
		material->SetTexture(texture);
		mesh->SetMaterial(material);

		return mesh;
	}


	Mesh* MeshBuilder::CreateQuad(const glm::vec3& position, const glm::vec2& size, float angle) {
		Mesh*			mesh			= new Mesh{};
		QuadGeometry*	quad_geometry	= new QuadGeometry{};
		
		glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), { position.x, position.y, position.z }) *
			glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), { size.x, size.y, 0.0f });
		
		quad_geometry->ApplyTransform(transform);
		mesh->SetGeometry(quad_geometry);
		return mesh;
	}

	Mesh* MeshBuilder::CreateQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color, float angle) {
		Mesh*				mesh		= CreateQuad(position, size, angle);
		Texture*			texture		= CreateTexture(1, 1);
		StandardMaterial*	material	= new StandardMaterial{};
		
		texture->SetData(color.x, color.y, color.z, 255.f);
		material->SetTexture(texture);
		mesh->SetMaterial(material);

		return mesh;
	}


	Mesh* MeshBuilder::CreateQuad(const glm::vec3& position, const glm::vec2& size, float angle, Texture2D* const texture) {
		Texture* internal_texture = dynamic_cast<Texture*>(texture);
		return CreateQuad(position, size, angle,  internal_texture);
	}

	Mesh* MeshBuilder::CreateQuad(const glm::vec3& position, const glm::vec2& size, float angle, Texture* const texture) {
		Mesh*				mesh		= CreateQuad(position, size, angle);
		StandardMaterial*	material	= new StandardMaterial{};
		
		material->SetTexture(texture);
		mesh->SetMaterial(material);

		return mesh;
	}
	


	Mesh* MeshBuilder::CreateQuad(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture) {
		auto internal_texture = std::dynamic_pointer_cast<Texture>(texture);
		assert(texture != nullptr);
		return CreateQuad(position, size, angle, internal_texture);
	}			
	
	Mesh* MeshBuilder::CreateQuad(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture>& texture) {
		Mesh*				mesh		= CreateQuad(position, size, angle);
		StandardMaterial*	material	= new StandardMaterial{};
		
		material->SetTexture(texture);
		mesh->SetMaterial(material);
		return mesh;
	}

	Mesh* MeshBuilder::CreateQuad(const glm::vec2& position, const glm::vec2& size, float angle, ShaderMaterial* const material) {
		Mesh* mesh = CreateQuad(position, size, angle);
		mesh->SetMaterial(material);
		return mesh;
	} 
	
	
	Mesh* MeshBuilder::CreateQuad(const glm::vec3& position, const glm::vec2& size, float angle, ShaderMaterial* const material) {
		Mesh* mesh = CreateQuad(position, size, angle);
		mesh->SetMaterial(material);
		return mesh;
	}

	Mesh* MeshBuilder::CreateQuad(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<ShaderMaterial>& material) {
		Mesh* mesh = CreateQuad(position, size, angle);
		mesh->SetMaterial(material);
		return mesh;
	} 
	
	
	Mesh* MeshBuilder::CreateQuad(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<ShaderMaterial>& material) {
		Mesh* mesh = CreateQuad(position, size, angle);
		mesh->SetMaterial(material);
		return mesh;
	}

	Mesh* MeshBuilder::CreateSquare(const glm::vec2& position, const glm::vec2& size, float angle) {
		Mesh*				mesh			= new Mesh{};
		SquareGeometry*		quad_geometry	= new SquareGeometry{};
		Texture*			texture			= CreateTexture(1, 1);
		StandardMaterial*	material		= new StandardMaterial{};
		
		glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), { position.x, position.y, 0.0f }) *
			glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), { size.x, size.y, 0.0f });
		
		quad_geometry->ApplyTransform(transform);
		material->SetTexture(texture);
		mesh->SetGeometry(quad_geometry);
		mesh->SetMaterial(material);

		return mesh;
	}

	Mesh* MeshBuilder::CreateSquare(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color, float angle) {
		Mesh*				mesh		= CreateSquare(position, size, angle);
		Texture*			texture		= CreateTexture(1, 1);
		StandardMaterial*	material	= new StandardMaterial{};
		
		texture->SetData(color.x, color.y, color.z, 255.0f);
		material->SetTexture(texture);
		mesh->SetMaterial(material);

		return mesh;
	}


	Mesh* MeshBuilder::CreateSquare(const glm::vec2& position, const glm::vec2& size, float angle, Texture2D* const texture) {
		Texture* internal_texture = dynamic_cast<Texture*>(texture);
		return CreateSquare(position, size, angle,  internal_texture);
	}

	Mesh* MeshBuilder::CreateSquare(const glm::vec2& position, const glm::vec2& size, float angle, Texture* const texture) {
		Mesh*				mesh		= CreateSquare(position, size, angle);
		StandardMaterial*	material	= new StandardMaterial{};
		
		material->SetTexture(texture);
		mesh->SetMaterial(material);

		return mesh;
	}
	

	Mesh* MeshBuilder::CreateSquare(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Texture2D>& texture) {
	   auto internal_texture = std::dynamic_pointer_cast<Texture>(texture);
	   assert(texture != nullptr);
	   return CreateSquare(position, size, angle, internal_texture);
	}

	Mesh* MeshBuilder::CreateSquare(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Texture>& texture) {
		Mesh*				mesh		= CreateSquare(position, size, angle);
		StandardMaterial*	material	=  new StandardMaterial{};
		
		material->SetTexture(texture);
		mesh->SetMaterial(material);

		return mesh;
	}


	Mesh* MeshBuilder::CreateSquare(const glm::vec3& position, const glm::vec2& size, float angle) {
		Mesh*			mesh			= new Mesh{};
		SquareGeometry*	quad_geometry	= new SquareGeometry{};
		
		glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), { position.x, position.y, position.z }) *
			glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), { size.x, size.y, 0.0f });
		
		quad_geometry->ApplyTransform(transform);
		mesh->SetGeometry(quad_geometry);
		return mesh;
	}

	Mesh* MeshBuilder::CreateSquare(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color, float angle) {
		Mesh*				mesh		= CreateSquare(position, size, angle);
		Texture*			texture		= CreateTexture(1, 1);
		StandardMaterial*	material	= new StandardMaterial{};
		
		texture->SetData(color.x, color.y, color.z, 255.f);
		material->SetTexture(texture);
		mesh->SetMaterial(material);

		return mesh;
	}


	Mesh* MeshBuilder::CreateSquare(const glm::vec3& position, const glm::vec2& size, float angle, Texture2D* const texture) {
		Texture* internal_texture = dynamic_cast<Texture*>(texture);
		return CreateSquare(position, size, angle,  internal_texture);
	}

	Mesh* MeshBuilder::CreateSquare(const glm::vec3& position, const glm::vec2& size, float angle, Texture* const texture) {
		Mesh*				mesh		= CreateSquare(position, size, angle);
		StandardMaterial*	material	= new StandardMaterial{};
		
		material->SetTexture(texture);
		mesh->SetMaterial(material);

		return mesh;
	}
	


	Mesh* MeshBuilder::CreateSquare(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture) {
		auto internal_texture = std::dynamic_pointer_cast<Texture>(texture);
		assert(texture != nullptr);
		return CreateSquare(position, size, angle, internal_texture);
	}			
	
	Mesh* MeshBuilder::CreateSquare(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture>& texture) {
		Mesh*				mesh		= CreateSquare(position, size, angle);
		StandardMaterial*	material	= new StandardMaterial{};
		
		material->SetTexture(texture);
		mesh->SetMaterial(material);
		return mesh;
	}

	Mesh* MeshBuilder::CreateSquare(const glm::vec2& position, const glm::vec2& size, float angle, ShaderMaterial* const material) {
		Mesh* mesh = CreateSquare(position, size, angle);
		mesh->SetMaterial(material);
		return mesh;
	} 
	
	
	Mesh* MeshBuilder::CreateSquare(const glm::vec3& position, const glm::vec2& size, float angle, ShaderMaterial* const material) {
		Mesh* mesh = CreateSquare(position, size, angle);
		mesh->SetMaterial(material);
		return mesh;
	}

	Mesh* MeshBuilder::CreateSquare(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<ShaderMaterial>& material) {
		Mesh* mesh = CreateSquare(position, size, angle);
		mesh->SetMaterial(material);
		return mesh;
	} 
	
	
	Mesh* MeshBuilder::CreateSquare(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<ShaderMaterial>& material) {
		Mesh* mesh = CreateSquare(position, size, angle);
		mesh->SetMaterial(material);
		return mesh;
	}



	Mesh* MeshBuilder::CreateCube(const glm::vec2& position, const glm::vec2& size, float angle) {
		Mesh* mesh					= new Mesh{};
		CubeGeometry* quad_geometry = new CubeGeometry{};
		Texture* texture			= CreateTexture(1, 1);
		StandardMaterial* material	= new StandardMaterial{};

		glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), { position.x, position.y, 0.0f }) *
			glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f)) *
			glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

		quad_geometry->ApplyTransform(transform);
		material->SetTexture(texture);

		mesh->SetGeometry(quad_geometry);
		mesh->SetMaterial(material);
		return mesh;
	}

	Mesh* MeshBuilder::CreateCube(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color, float angle) {
		Mesh* mesh					= CreateCube(position, size, angle);
		Texture* texture			= CreateTexture(1, 1);
		StandardMaterial* material	= new StandardMaterial{};

		texture->SetData(color.x, color.y, color.z, 255.0f);
		material->SetTexture(texture);
		mesh->SetMaterial(material);

		return mesh;
	}


	Mesh* MeshBuilder::CreateCube(const glm::vec2& position, const glm::vec2& size, float angle, Texture2D* const texture) {
		Texture* internal_texture = dynamic_cast<Texture*>(texture);
		return CreateCube(position, size, angle, internal_texture);
	}

	Mesh* MeshBuilder::CreateCube(const glm::vec2& position, const glm::vec2& size, float angle, Texture* const texture) {
		Mesh* mesh					= CreateCube(position, size, angle);
		StandardMaterial* material	= new StandardMaterial{};

		material->SetTexture(texture);
		mesh->SetMaterial(material);

		return mesh;
	}


	Mesh* MeshBuilder::CreateCube(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Texture2D>& texture) {
		auto internal_texture = std::dynamic_pointer_cast<Texture>(texture);
		assert(texture != nullptr);
		return CreateCube(position, size, angle, internal_texture);
	}

	Mesh* MeshBuilder::CreateCube(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Texture>& texture) {
		Mesh* mesh					= CreateCube(position, size, angle);
		StandardMaterial* material	= new StandardMaterial{};

		material->SetTexture(texture);
		mesh->SetMaterial(material);

		return mesh;
	}


	Mesh* MeshBuilder::CreateCube(const glm::vec3& position, const glm::vec2& size, float angle) {
		Mesh* mesh					= new Mesh{};
		CubeGeometry* quad_geometry = new CubeGeometry{};

		glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), { position.x, position.y, position.z }) *
			glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f)) *
			glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

		quad_geometry->ApplyTransform(transform);
		mesh->SetGeometry(quad_geometry);
		return mesh;
	}

	Mesh* MeshBuilder::CreateCube(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color, float angle) {
		Mesh* mesh					= CreateCube(position, size, angle);
		Texture* texture			= CreateTexture(1, 1);
		StandardMaterial* material	= new StandardMaterial{};

		texture->SetData(color.x, color.y, color.z, 255.f);
		material->SetTexture(texture);
		mesh->SetMaterial(material);

		return mesh;
	}


	Mesh* MeshBuilder::CreateCube(const glm::vec3& position, const glm::vec2& size, float angle, Texture2D* const texture) {
		Texture* internal_texture = dynamic_cast<Texture*>(texture);
		return CreateCube(position, size, angle, internal_texture);
	}

	Mesh* MeshBuilder::CreateCube(const glm::vec3& position, const glm::vec2& size, float angle, Texture* const texture) {
		Mesh* mesh					= CreateCube(position, size, angle);
		StandardMaterial* material	= new StandardMaterial{};

		material->SetTexture(texture);
		mesh->SetMaterial(material);

		return mesh;
	}



	Mesh* MeshBuilder::CreateCube(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture) {
		auto internal_texture = std::dynamic_pointer_cast<Texture>(texture);
		assert(texture != nullptr);
		return CreateCube(position, size, angle, internal_texture);
	}

	Mesh* MeshBuilder::CreateCube(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture>& texture) {
		Mesh* mesh					= CreateCube(position, size, angle);
		StandardMaterial* material	= new StandardMaterial{};

		material->SetTexture(texture);
		mesh->SetMaterial(material);
		return mesh;
	}

	Mesh* MeshBuilder::CreateCube(const glm::vec2& position, const glm::vec2& size, float angle, ShaderMaterial* const material) {
		Mesh* mesh = CreateCube(position, size, angle);
		mesh->SetMaterial(material);
		return mesh;
	}


	Mesh* MeshBuilder::CreateCube(const glm::vec3& position, const glm::vec2& size, float angle, ShaderMaterial* const material) {
		Mesh* mesh = CreateCube(position, size, angle);
		mesh->SetMaterial(material);
		return mesh;
	}

	Mesh* MeshBuilder::CreateCube(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<ShaderMaterial>& material) {
		Mesh* mesh = CreateCube(position, size, angle);
		mesh->SetMaterial(material);
		return mesh;
	}


	Mesh* MeshBuilder::CreateCube(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<ShaderMaterial>& material) {
		Mesh* mesh = CreateCube(position, size, angle);
		mesh->SetMaterial(material);
		return mesh;
	}
}