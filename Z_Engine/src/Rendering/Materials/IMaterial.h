#pragma once
#include <glm/glm.hpp>
#include "../Textures/Texture.h"
#include "../Shaders/Shader.h"
#include "../../Z_EngineDef.h"

namespace Z_Engine::Rendering::Meshes {
	struct IMesh;
}

namespace Z_Engine::Rendering::Materials {

	struct IMaterial 
	{
		IMaterial() : m_texture(nullptr), m_shader(nullptr), m_owner_mesh(nullptr) {} 
		~IMaterial() = default;

	public:

		virtual void SetTexture(const Ref<Textures::Texture>& texture) { m_texture = texture; }
		virtual void SetShader(const Ref<Shaders::Shader>& shader) { m_shader = shader; }
		virtual void SetOwnerMesh(Meshes::IMesh* const mesh) { m_owner_mesh = mesh; }

		virtual Ref<Textures::Texture> GetTexture() const { return m_texture; }
		virtual Ref<Shaders::Shader> GetShader() const { return m_shader; }

		virtual void UpdateUniforms() = 0;

	protected:
		Ref<Textures::Texture>	m_texture;
		Ref<Shaders::Shader>	m_shader;
		Meshes::IMesh*	m_owner_mesh;
	};
}
