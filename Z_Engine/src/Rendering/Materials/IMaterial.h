#pragma once
#include <glm/glm.hpp>
#include "../Textures/Texture.h"
#include "../Shaders/Shader.h"
#include "../../Z_EngineDef.h"

namespace Z_Engine::Rendering::Materials {

	struct IMaterial 
	{
		IMaterial() = default; 
		~IMaterial() = default;

		virtual void SetTexture(const Ref<Textures::Texture>& texture) { m_texture = texture; }
		virtual Ref<Textures::Texture> GetTexture() const { return m_texture; }

		//ToDo : Should be renamed or deleted as isn't the responsibility of IMaterial
		//to  assign texture ID
		virtual void UpdateUniforms(int texture_id = 0) = 0;				

	protected:
		Ref<Textures::Texture>	m_texture {nullptr};
	};
}
