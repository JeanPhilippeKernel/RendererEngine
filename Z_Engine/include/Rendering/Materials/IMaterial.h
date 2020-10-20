#pragma once
#include <string>

#include "../Textures/Texture.h"
#include "../Shaders/Shader.h"
#include "../../dependencies/glm/glm.hpp"
#include "../../Z_EngineDef.h"

namespace Z_Engine::Rendering::Materials {

	struct IMaterial 
	{
	public:
		explicit IMaterial() = default; 
		explicit IMaterial(const Ref<Textures::Texture>& texture) 
			: m_texture(texture)
		{
		}

		explicit IMaterial(const Ref<Shaders::Shader>& shader) 
			: m_shader(shader)
		{
		}
		
		explicit IMaterial(const Ref<Textures::Texture>& texture, const Ref<Shaders::Shader>& shader) 
			: m_texture(texture), m_shader(shader)
		{
		}

		explicit IMaterial(Ref<Textures::Texture>&& texture, Ref<Shaders::Shader>&& shader)
			: m_texture(texture), m_shader(shader)
		{
		}

		virtual ~IMaterial() = default;

		virtual void SetTexture(const Ref<Textures::Texture>& texture) { 
			m_texture = texture; 
		}

		virtual void SetShader(const Ref<Shaders::Shader>& shader) { 
			m_shader = shader; 
		}

		virtual const Ref<Textures::Texture>& GetTexture() const { 
			return m_texture; 
		}	

		virtual const Ref<Shaders::Shader>& GetShader() const {
			return m_shader;
		}

		virtual void SetTextureTilingFactor(float value) {
			m_texture_tiling_factor = value;
		}

		virtual void SetTextureTintColor(const glm::vec4& value) {
			m_texture_tint_color = value;
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
		
		Ref<Textures::Texture>		m_texture	{nullptr};
		Ref<Shaders::Shader>		m_shader	{nullptr};

	};
}
