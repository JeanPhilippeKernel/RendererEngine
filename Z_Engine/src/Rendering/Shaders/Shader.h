#include <GL/glew.h>
#include <unordered_map>
#include <glm/glm.hpp>

#include "../../Z_EngineDef.h"


namespace Z_Engine::Rendering::Shaders {
	class Z_ENGINE_API Shader {
	public:
		Shader(const char * vertexSrc, const char * fragmentSrc);
		~Shader();

		void Bind() const;
		void Unbind() const;
						   
		void SetUniform(const char* name, int value);
		void SetUniform(const char* name, float value);

		void SetUniform(const char* name, const glm::vec2& value);
		void SetUniform(const char* name, const glm::vec3& value);
		void SetUniform(const char* name, const glm::vec4& value);


		void SetUniform(const char* name, const glm::mat2& value);
		void SetUniform(const char* name, const glm::mat3& value);
		void SetUniform(const char* name, const glm::mat4& value);
											  
	private:
		GLuint m_program{0};
		std::unordered_map<const char *, GLint> m_uniform_location_map;

		GLint GetLocationUniform(const char* name);
	};
}