#pragma once
#include <string>
#include <Rendering/Shaders/ShaderEnums.h>
#include <glad/glad.h>


namespace ZEngine::Rendering::Shaders {

	struct ShaderInformation {
		GLuint ShaderId;
		GLuint ProgramId;
		GLenum InternalType;
		ShaderType Type;
		std::string Name;
		std::string Source;
	};
}