#pragma  once
#include <string>
#include <unordered_map>


#include "../../Z_EngineDef.h"
#include "../../dependencies/glm/glm.hpp"
#include "../../dependencies/glew/include/GL/glew.h"

#include "../../Core/IGraphicObject.h"


namespace Z_Engine::Rendering::Shaders {
	class Shader : public Core::IGraphicObject {
	public:
		Shader(const char * vertexSrc, const char * fragmentSrc);
		Shader(const char * filename);
		~Shader();

		bool IsActive() const;
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


		void SetUniform(const char* name, int* arr, unsigned int size);

		void SetUniform(const char* name, const glm::vec2& value);
		void SetUniform(const char* name, const glm::vec3& value);
		void SetUniform(const char* name, const glm::vec4& value);


		void SetUniform(const char* name, const glm::mat2& value);
		void SetUniform(const char* name, const glm::mat3& value);
		void SetUniform(const char* name, const glm::mat4& value);
		
		GLuint GetIdentifier() const override;

	private:
		GLint _GetLocationUniform(const char* name);
		void _Compile();
		void _Read(const char* filename);


	private:
		GLuint m_program{0};
		std::unordered_map<const char*, GLint> m_uniform_location_map;
		std::unordered_map<GLenum, std::string> m_shader_source_map;

		// Inherited via IGraphicObject
	};


	Shader* CreateShader(const char * vertexSrc, const char * fragmentSrc);
	Shader* CreateShader(const char * filename);
}