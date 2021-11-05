#pragma  once
#include <string>
#include <unordered_map>
#include <ZEngineDef.h>
#include <Maths/Math.h>
#include <Core/IGraphicObject.h>
#include <Rendering/Shaders/Compilers/ShaderCompiler.h>


namespace ZEngine::Rendering::Shaders {
	class Shader : public Core::IGraphicObject {
	public:
		Shader(const char * filename);
		virtual ~Shader();

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

		void SetUniform(const char* name, const Maths::Vector2& value);
		void SetUniform(const char* name, const Maths::Vector3& value);
		void SetUniform(const char* name, const Maths::Vector4& value);


		void SetUniform(const char* name, const Maths::Matrix2& value);
		void SetUniform(const char* name, const Maths::Matrix3& value);
		void SetUniform(const char* name, const Maths::Matrix4& value);
		
		GLuint GetIdentifier() const override;

	private:
		GLint _GetLocationUniform(const char* name);

	private:
		GLuint m_program{0};
		Scope<Compilers::ShaderCompiler> m_compiler;
		std::unordered_map<const char*, GLint> m_uniform_location_map;

		// Inherited via IGraphicObject
	};

	Shader* CreateShader(const char * filename);
}