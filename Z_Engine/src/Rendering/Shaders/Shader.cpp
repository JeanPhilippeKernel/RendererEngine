#pragma once

#include "Shader.h"
#include <vector>
#include <fstream>
#include <string>

#include "glm/gtc/type_ptr.hpp"

namespace Z_Engine::Rendering::Shaders {
	
	Shader::Shader(const char* vertexSrc, const char* fragmentSrc) {
		
		std::string vertexSource = std::string(vertexSrc); // Get source code for vertex shader.
		std::string fragmentSource = std::string(fragmentSrc); // Get source code for fragment shader.

			// Create an empty vertex shader handle
		GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);

		// Send the vertex shader source code to GL
		// Note that std::string's .c_str is NULL character terminated.
		const GLchar* source = (const GLchar*)vertexSource.c_str();
		glShaderSource(vertexShader, 1, &source, 0);

		// Compile the vertex shader
		glCompileShader(vertexShader);

		GLint isCompiled = 0;
		glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &isCompiled);
		if (isCompiled == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &maxLength);

			// The maxLength includes the NULL character
			std::vector<GLchar> infoLog(maxLength);
			glGetShaderInfoLog(vertexShader, maxLength, &maxLength, &infoLog[0]);

			// We don't need the shader anymore.
			glDeleteShader(vertexShader);

			// Use the infoLog as you see fit.

			// In this simple program, we'll just leave
			return;
		}

		// Create an empty fragment shader handle
		GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);

		// Send the fragment shader source code to GL
		// Note that std::string's .c_str is NULL character terminated.
		source = (const GLchar*)fragmentSource.c_str();
		glShaderSource(fragmentShader, 1, &source, 0);

		// Compile the fragment shader
		glCompileShader(fragmentShader);

		glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &isCompiled);
		if (isCompiled == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &maxLength);

			// The maxLength includes the NULL character
			std::vector<GLchar> infoLog(maxLength);
			glGetShaderInfoLog(fragmentShader, maxLength, &maxLength, &infoLog[0]);

			// We don't need the shader anymore.
			glDeleteShader(fragmentShader);
			// Either of them. Don't leak shaders.
			glDeleteShader(vertexShader);

			// Use the infoLog as you see fit.

			// In this simple program, we'll just leave
			return;
		}

		// Vertex and fragment shaders are successfully compiled.
		// Now time to link them together into a program.
		// Get a program object.
		GLuint program = glCreateProgram();
		m_program = program;

		// Attach our shaders to our program
		glAttachShader(program, vertexShader);
		glAttachShader(program, fragmentShader);

		// Link our program
		glLinkProgram(program);

		// Note the different functions here: glGetProgram* instead of glGetShader*.
		GLint isLinked = 0;
		glGetProgramiv(program, GL_LINK_STATUS, (int*)&isLinked);
		if (isLinked == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

			// The maxLength includes the NULL character
			std::vector<GLchar> infoLog(maxLength);
			glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);

			// We don't need the program anymore.
			glDeleteProgram(program);
			// Don't leak shaders either.
			glDeleteShader(vertexShader);
			glDeleteShader(fragmentShader);

			// Use the infoLog as you see fit.

			// In this simple program, we'll just leave
			return;
		}

		// Always detach shaders after a successful link.
		glDetachShader(program, vertexShader);
		glDetachShader(program, fragmentShader);
	}

	Shader::Shader(const char* filename) {
		
	}

	Shader::~Shader() {
		glDeleteProgram(m_program);
	}

	void Shader::Bind() const {
		glUseProgram(m_program);
	}

	void Shader::Unbind() const {
		glUseProgram(0);
	}

	GLint Shader::GetLocationUniform(const char* name){
		auto it = std::find_if(
			std::begin(m_uniform_location_map), std::end(m_uniform_location_map), 
			[name](const auto& key_pair){ return strcmp(key_pair.first, name) == 0; });
		
		if(it != std::end(m_uniform_location_map))
			return it->second;

		Bind();
		GLint location = glGetUniformLocation(m_program, name);
		if(location != -1) {
			m_uniform_location_map[name] = location;
		}
		return location;
	}


	void Shader::SetUniform(const char* name, int value) {
		Bind();
		auto location = GetLocationUniform(name);
		if(location != -1){
			glUniform1i(location, value);																			   
		}
	}

	void Shader::SetUniform(const char* name, float value) {
		Bind();

		auto location = GetLocationUniform(name);
		if (location != -1) {
			glUniform1f(location, value);
		}
	}

	void Shader::SetUniform(const char* name, const glm::vec2& value) {
		Bind();

		auto location = GetLocationUniform(name);
		if (location != -1) {
			glUniform2d(location, value.x, value.y);
		}
	}
	
	void Shader::SetUniform(const char* name, const glm::vec3& value) {
		Bind();

		auto location = GetLocationUniform(name);
		if (location != -1) {																															    
			glUniform3f(location, value.x, value.y, value.z);
		}
	}

	void Shader::SetUniform(const char* name, const glm::vec4& value) {
		Bind();

		auto location = GetLocationUniform(name);
		if (location != -1) {
			glUniform4f(location, value.x, value.y, value.z, value.w);
		}
	}


	void Shader::SetUniform(const char* name, const glm::mat2& value) {
		Bind();

		auto location = GetLocationUniform(name);
		if (location != -1) {
			glUniformMatrix2fv(location, 1, GL_FALSE, glm::value_ptr(value));
		}
	}

	void Shader::SetUniform(const char* name, const glm::mat3& value) {
		Bind();

		auto location = GetLocationUniform(name);
		if (location != -1) {
			glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(value));
		}
	}
	
	void Shader::SetUniform(const char* name, const glm::mat4& value) {
		Bind();

		auto location = GetLocationUniform(name);
		if (location != -1) {
			glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
		}
	}
}