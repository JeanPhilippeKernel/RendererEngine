#pragma once
#include <initializer_list>
#include <unordered_map>
#include <string>
#include <any>

#include "IMaterial.h"


namespace Z_Engine::Rendering::Materials {

	class ShaderMaterial : public IMaterial
	{
	public:
		explicit ShaderMaterial(const Ref<Shaders::Shader>& shader, std::initializer_list<std::string>&& uniforms);
		explicit ShaderMaterial(const char * shader_filename, std::initializer_list<std::string>&& uniforms);
		explicit ShaderMaterial(std::initializer_list<std::string>&& uniforms);

		virtual ~ShaderMaterial() =  default;

		std::any& operator[](const std::string& key) {
			return m_uniform_collection[key];
		} 

		virtual void SetAttributes() = 0;

	protected:
		std::unordered_map<std::string, std::any>	m_uniform_collection {};
	};
}


