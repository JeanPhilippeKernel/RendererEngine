#pragma once
#include "Mesh.h"
#include "../Textures/Texture2D.h"
#include "../../Z_EngineDef.h"

namespace Z_Engine::Rendering::Meshes {
	
	struct  MeshBuilder {

		MeshBuilder() = delete;
		MeshBuilder(const MeshBuilder&) = delete;

		static Mesh* CreateQuad(const glm::vec2& position, const glm::vec2& size, float angle); 
		static Mesh* CreateQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color, float angle); 
		static Mesh* CreateQuad(const glm::vec2& position, const glm::vec2& size, float angle, Rendering::Textures::Texture* const texture);
		static Mesh* CreateQuad(const glm::vec2& position, const glm::vec2& size, float angle, Rendering::Textures::Texture2D* const texture);
		static Mesh* CreateQuad(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture>& texture);
		static Mesh* CreateQuad(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture);
		static Mesh* CreateQuad(const glm::vec2& position, const glm::vec2& size, float angle, Rendering::Materials::ShaderMaterial* const material); 
		static Mesh* CreateQuad(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Rendering::Materials::ShaderMaterial>& material); 

		static Mesh* CreateQuad(const glm::vec3& position, const glm::vec2& size, float angle); 
		static Mesh* CreateQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color, float angle);
		static Mesh* CreateQuad(const glm::vec3& position, const glm::vec2& size, float angle, Rendering::Textures::Texture* const texture);
		static Mesh* CreateQuad(const glm::vec3& position, const glm::vec2& size, float angle, Rendering::Textures::Texture2D* const texture);
		static Mesh* CreateQuad(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture);			
		static Mesh* CreateQuad(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture>& texture);
		static Mesh* CreateQuad(const glm::vec3& position, const glm::vec2& size, float angle, Rendering::Materials::ShaderMaterial* const material);
		static Mesh* CreateQuad(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Materials::ShaderMaterial>& material);
		




		 static Mesh* CreateSquare(const glm::vec2& position, const glm::vec2& size, float angle); 
	};
}
