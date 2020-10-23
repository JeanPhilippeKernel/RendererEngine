#pragma once
#include <string>
#include <typeinfo>

#include "../Textures/Texture.h"
#include "../Shaders/Shader.h"
#include "../../dependencies/glm/glm.hpp"
#include "../../Z_EngineDef.h"

namespace Z_Engine::Rendering::Materials {

	struct IMaterial 
	{
	public:
		explicit IMaterial() {
			m_material_name = typeid(*this).name();
		} 

		explicit IMaterial(const Ref<Textures::Texture>& texture) 
			: m_texture(texture)
		{
			m_material_name = typeid(*this).name();
		}

		explicit IMaterial(const Ref<Shaders::Shader>& shader) 
			: m_shader(shader)
		{
			m_shader->Bind();
			m_material_name = typeid(*this).name();
		}
		
		explicit IMaterial(const Ref<Textures::Texture>& texture, const Ref<Shaders::Shader>& shader) 
			: m_texture(texture), m_shader(shader)
		{
			m_material_name = typeid(*this).name();
			m_shader->Bind();
		}

		explicit IMaterial(Ref<Textures::Texture>&& texture, Ref<Shaders::Shader>&& shader)
			: m_texture(texture), m_shader(shader)
		{
			m_material_name = typeid(*this).name();
			m_shader->Bind();
		}

		virtual ~IMaterial() = default;

		virtual void SetTexture(const Ref<Textures::Texture>& texture) { 
			m_texture = texture; 
		}

		virtual void SetTexture(Textures::Texture* const texture) { 
			m_texture.reset(texture); 
		}

		virtual void SetShader(Shaders::Shader* const shader) { 
			m_shader.reset(shader); 
		}

		virtual void SetShader(const Ref<Shaders::Shader>& shader) { 
			m_shader = shader; 
		}

		virtual const std::string& GetName() const {
			return m_material_name;
		}

		virtual const Ref<Textures::Texture>& GetTexture() const { 
			return m_texture; 
		}	

		virtual const Ref<Shaders::Shader>& GetShader() const {
			return m_shader;
		}

	protected:
		std::string				m_material_name{};

		Ref<Textures::Texture>	m_texture	{nullptr};
		Ref<Shaders::Shader>	m_shader	{nullptr};
	};
}
