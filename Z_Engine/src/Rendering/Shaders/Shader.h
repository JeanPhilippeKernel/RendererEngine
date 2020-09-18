#pragma  once
#include <string>

#include <GL/glew.h>
#include <unordered_map>
#include <glm/glm.hpp>

#include "../../Z_EngineDef.h"


namespace Z_Engine::Rendering::Shaders {
	class Shader {
	public:
		Shader(const char * vertexSrc, const char * fragmentSrc);
		Shader(const char * filename);
		~Shader();

		void Bind() const;
		void Unbind() const;
						   

		void SetUniform(const char* name, int value);
		void SetUniform(const char* name, int value_one, int value_two);
		void SetUniform(const char* name, int value_one, int value_two, int value_three);
		void SetUniform(const char* name, int value_one, int value_two, int value_three, int value_four);

		void SetUniform(const char* name, float value);
		void SetUniform(const char* name, float value_one, float value_two);
		void SetUniform(const char* name, float value_one, float value_two, float value_three);
		void SetUniform(const char* name, float value_one, float value_two, float value_three, float value_four);

		void SetUniform(const char* name, const glm::vec2& value);
		void SetUniform(const char* name, const glm::vec3& value);
		void SetUniform(const char* name, const glm::vec4& value);


		void SetUniform(const char* name, const glm::mat2& value);
		void SetUniform(const char* name, const glm::mat3& value);
		void SetUniform(const char* name, const glm::mat4& value);
		
	private:
		GLint _GetLocationUniform(const char* name);
		void _Compile();
		void _Read(const char* filename);


	private:
		GLuint m_program{0};
		std::unordered_map<const char*, GLint> m_uniform_location_map;
		std::unordered_map<GLenum, std::string> m_shader_source_map;
	};


	Shader* CreateShader(const char * vertexSrc, const char * fragmentSrc);
	Shader* CreateShader(const char * filename);
}