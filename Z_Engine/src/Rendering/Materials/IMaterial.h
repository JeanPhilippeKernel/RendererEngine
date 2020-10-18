#pragma once
#include "../../dependencies/glm/glm.hpp"
#include "../Textures/Texture.h"
#include "../Shaders/Shader.h"
#include "../../Z_EngineDef.h"

namespace Z_Engine::Rendering::Materials {

	struct IMaterial 
	{
		IMaterial() = default; 
		~IMaterial() = default;

		virtual void SetTexture(const Ref<Textures::Texture>& texture) { 
			m_texture = texture; 
		}

		virtual void SetTextureTilingFactor(float value) {
			m_texture_tiling_factor = value;
		}

		virtual void SetTextureTintColor(const glm::vec4& value) {
			m_texture_tint_color = value;
		}

		virtual const Ref<Textures::Texture>& GetTexture() const { 
			return m_texture; 
		}		

		virtual float GetTextureTilingFactor() {
			return m_texture_tiling_factor;
		}

		virtual glm::vec4 GetTextureTintColor() {
			return m_texture_tint_color;
		}

	protected:
		float m_texture_tiling_factor		{0.0f};
		glm::vec4 m_texture_tint_color		{0.0f, 0.0f, 0.0f, 0.0f};
		Ref<Textures::Texture>	m_texture	{nullptr};
	};
}
