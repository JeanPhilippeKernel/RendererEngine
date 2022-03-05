#pragma once
#include <string>
#include <unordered_map>
#include <ZEngineDef.h>
#include <Maths/Math.h>
#include <Core/IGraphicObject.h>
#include <Rendering/Shaders/Compilers/ShaderCompiler.h>

namespace ZEngine::Rendering::Shaders {
    class Shader : public Core::IGraphicObject {
    public:
        /**
         * Initialize a new Shader instance.
         */
        Shader(const char* filename, bool defer_program_creation = false);
        virtual ~Shader();

        /**
         * Check if the shader program is active
         * @return True of False
         */
        bool IsActive() const;

        /**
         * Compile and create shader program
         */
        void CreateProgram();

        /**
         * Make active and uses the shader program
         */
        void Bind() const;

        /**
         * Make unactive and unuses the shader program
         */
        void Unbind() const;

        /**
         * Set a shader uniform value
         *
         * @param name  Uniform name
         * @param value Integer value
         */
        void SetUniform(const char* name, int value);

        /**
         * Set a shader uniform value
         *
         * @param name 			Uniform name
         * @param value_one 	Integer value
         * @param value_two 	Integer value
         */
        void SetUniform(const char* name, int value_one, int value_two);

        /**
         * Set a shader uniform value
         *
         * @param name 			Uniform name
         * @param value_one 	Integer value
         * @param value_two 	Integer value
         * @param value_three 	Integer value
         */
        void SetUniform(const char* name, int value_one, int value_two, int value_three);

        /**
         * Set a shader uniform value
         *
         * @param name 			Uniform name
         * @param value_one 	Integer value
         * @param value_two 	Integer value
         * @param value_three 	Integer value
         * @param value_four 	Integer value
         */
        void SetUniform(const char* name, int value_one, int value_two, int value_three, int value_four);

        /**
         * Set a shader uniform value
         *
         * @param name 			Uniform name
         * @param value_one 	Float value
         */
        void SetUniform(const char* name, float value);

        /**
         * Set a shader uniform value
         *
         * @param name 			Uniform name
         * @param value_one 	Float value
         * @param value_two 	Float value
         */
        void SetUniform(const char* name, float value_one, float value_two);

        /**
         * Set a shader uniform value
         *
         * @param name 			Uniform name
         * @param value_one 	Float value
         * @param value_two 	Float value
         * @param value_three 	Float value
         */
        void SetUniform(const char* name, float value_one, float value_two, float value_three);

        /**
         * Set a shader uniform value
         *
         * @param name 			Uniform name
         * @param value_one 	Float value
         * @param value_two 	Float value
         * @param value_three 	Float value
         * @param value_four 	Float value
         */
        void SetUniform(const char* name, float value_one, float value_two, float value_three, float value_four);

        /**
         * Set a shader uniform value
         *
         * @param name	Uniform name
         * @param arr	Integer array pointer
         * @param size	Array size
         */
        void SetUniform(const char* name, int* arr, unsigned int size);

        /**
         * Set a shader uniform value
         *
         * @param name	Uniform name
         * @param value	Vector2 value
         */
        void SetUniform(const char* name, const Maths::Vector2& value);

        /**
         * Set a shader uniform value
         *
         * @param name	Uniform name
         * @param value	Vector3 value
         */
        void SetUniform(const char* name, const Maths::Vector3& value);

        /**
         * Set a shader uniform value
         *
         * @param name	Uniform name
         * @param value	Vector4 value
         */
        void SetUniform(const char* name, const Maths::Vector4& value);

        /**
         * Set a shader uniform value
         *
         * @param name	Uniform name
         * @param value	Matrix2 value
         */
        void SetUniform(const char* name, const Maths::Matrix2& value);

        /**
         * Set a shader uniform value
         *
         * @param name	Uniform name
         * @param value	Matrix3 value
         */
        void SetUniform(const char* name, const Maths::Matrix3& value);

        /**
         * Set a shader uniform value
         *
         * @param name	Uniform name
         * @param value	Matrix4 value
         */
        void SetUniform(const char* name, const Maths::Matrix4& value);

        /**
         * Get shader program identifier
         *
         * @return Shader program identifier
         */
        GLuint GetIdentifier() const override;

    private:
        GLint _GetLocationUniform(const char* name);

    private:
        GLuint                                 m_program{0};
        std::string                            m_filename;
        Scope<Compilers::ShaderCompiler>       m_compiler;
        std::unordered_map<const char*, GLint> m_uniform_location_map;
    };

    Shader* CreateShader(const char* filename);
} // namespace ZEngine::Rendering::Shaders
