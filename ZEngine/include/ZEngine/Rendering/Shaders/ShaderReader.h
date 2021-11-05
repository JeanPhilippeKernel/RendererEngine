#pragma once
#include <fstream>
#include <unordered_map>
#include <future>
#include <glad/glad.h>
#include <Rendering/Shaders/ShaderInformation.h>

namespace ZEngine::Rendering::Shaders {

	class ShaderReader {
	public:
		ShaderReader();
		~ShaderReader();

		/**
		* Reading content of shader file
		* 
		* @param filename :  Path to the shader file
		* @return enum ShaderReaderState that describes the read operation state
		*/
		ShaderOperationResult Read(std::string_view filename);
		
		/**
		* Reading asynchronously content of shader file
		*
		* @param filename :  Path to the shader file
		* @return enum ShaderReaderState that describes the read operation state
		*/
		std::future<ShaderOperationResult> ReadAsync(std::string_view filename) = delete;

		/**
		* Get shaders information collected during Reading process
		* 
		* @see Read(std::string_view) and ReadAsync(std::string_view) methods
		* @return ShaderInformation	object
		*/
		const std::vector<ShaderInformation>& GetInformations() const;
		std::vector<ShaderInformation>& GetInformations();

	private:
		const char*								m_regex_expression{ "#type[\\s][a-zA-Z]+" };
		std::ifstream							m_filestream{};
		std::vector<ShaderInformation>			m_shader_info_collection{};
	};
}