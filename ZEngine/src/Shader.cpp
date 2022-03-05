#include <pch.h>
#include <Rendering/Shaders/Shader.h>
#include <Logging/LoggerDefinition.h>
#include <Core/Coroutine.h>

namespace ZEngine::Rendering::Shaders {

    Shader* CreateShader(const char* filename) {
        return new Shader(filename);
    }
} // namespace ZEngine::Rendering::Shaders

namespace ZEngine::Rendering::Shaders {

    Shader::Shader(const char* filename, bool defer_program_creation) : m_filename(filename) {
        if (!defer_program_creation) {
            CreateProgram();
        }
    }

    Shader::~Shader() {
        Unbind();
        glDeleteProgram(m_program);
    }

    bool Shader::IsActive() const {
        GLint state = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &state);
        return (state == m_program);
    }

    void Shader::CreateProgram() {
        m_compiler                = std::make_unique<Compilers::ShaderCompiler>(m_filename);
        const auto compile_result = m_compiler->Compile();

        if (std::get<0>(compile_result) == ShaderOperationResult::SUCCESS) {
            m_program = std::get<1>(compile_result);
        }
    }

    void Shader::Bind() const {
        if (!IsActive()) {
            glUseProgram(m_program);
        }
    }

    void Shader::Unbind() const {
        glUseProgram(0);
    }

    GLuint Shader::GetIdentifier() const {
        return m_program;
    }

    GLint Shader::_GetLocationUniform(const char* name) {
        const auto it = std::find_if(std::begin(m_uniform_location_map), std::end(m_uniform_location_map), [&](const auto& kv) { return strcmp(kv.first, name) == 0; });

        if (it != std::end(m_uniform_location_map))
            return it->second;

        GLint location = glGetUniformLocation(m_program, name);
        if (location == -1) {
            ZENGINE_CORE_WARN("Error while finding uniform location : Name : {0}", name);
            return -1;
        }

        m_uniform_location_map[name] = location;
        return location;
    }

    void Shader::SetUniform(const char* name, int value) {
        if (IsActive()) {
            auto location = _GetLocationUniform(name);
            if (location != -1) {
                glUniform1i(location, value);
            }
        }
    }

    void Shader::SetUniform(const char* name, int value_one, int value_two) {
        if (IsActive()) {
            auto location = _GetLocationUniform(name);
            if (location != -1) {
                int arr[2] = {value_one, value_two};
                glUniform1iv(location, 2, arr);
            }
        }
    }

    void Shader::SetUniform(const char* name, int* arr, unsigned int size) {
        if (IsActive()) {
            auto location = _GetLocationUniform(name);
            if (location != -1) {
                glUniform1iv(location, size, arr);
            }
        }
    }

    void Shader::SetUniform(const char* name, int value_one, int value_two, int value_three) {
        if (IsActive()) {
            auto location = _GetLocationUniform(name);
            if (location != -1) {
                int arr[3] = {value_one, value_two, value_three};
                glUniform1iv(location, 3, arr);
            }
        }
    }

    void Shader::SetUniform(const char* name, int value_one, int value_two, int value_three, int value_four) {
        if (IsActive()) {
            auto location = _GetLocationUniform(name);
            if (location != -1) {
                int arr[4] = {value_one, value_two, value_three, value_four};
                glUniform1iv(location, 4, arr);
            }
        }
    }

    void Shader::SetUniform(const char* name, float value) {
        if (IsActive()) {
            auto location = _GetLocationUniform(name);
            if (location != -1) {
                glUniform1f(location, value);
            }
        }
    }

    void Shader::SetUniform(const char* name, float value_one, float value_two) {
        if (IsActive()) {
            auto location = _GetLocationUniform(name);
            if (location != -1) {
                float arr[2] = {value_one, value_two};
                glUniform1fv(location, 2, arr);
            }
        }
    }

    void Shader::SetUniform(const char* name, float value_one, float value_two, float value_three) {
        if (IsActive()) {
            auto location = _GetLocationUniform(name);
            if (location != -1) {
                float arr[3] = {value_one, value_two, value_three};
                glUniform1fv(location, 3, arr);
            }
        }
    }

    void Shader::SetUniform(const char* name, float value_one, float value_two, float value_three, float value_four) {
        if (IsActive()) {
            auto location = _GetLocationUniform(name);
            if (location != -1) {
                float arr[4] = {value_one, value_two, value_three, value_four};
                glUniform1fv(location, 4, arr);
            }
        }
    }

    void Shader::SetUniform(const char* name, const Maths::Vector2& value) {
        if (IsActive()) {
            auto location = _GetLocationUniform(name);
            if (location != -1) {
                glUniform2d(location, value.x, value.y);
            }
        }
    }

    void Shader::SetUniform(const char* name, const Maths::Vector3& value) {

        if (IsActive()) {
            auto location = _GetLocationUniform(name);
            if (location != -1) {
                glUniform3f(location, value.x, value.y, value.z);
            }
        }
    }

    void Shader::SetUniform(const char* name, const Maths::Vector4& value) {
        if (IsActive()) {
            auto location = _GetLocationUniform(name);
            if (location != -1) {
                glUniform4f(location, value.x, value.y, value.z, value.w);
            }
        }
    }

    void Shader::SetUniform(const char* name, const Maths::Matrix2& value) {
        if (IsActive()) {
            auto location = _GetLocationUniform(name);
            if (location != -1) {
                glUniformMatrix2fv(location, 1, GL_FALSE, Maths::value_ptr(value));
            }
        }
    }

    void Shader::SetUniform(const char* name, const Maths::Matrix3& value) {
        if (IsActive()) {
            auto location = _GetLocationUniform(name);
            if (location != -1) {
                glUniformMatrix3fv(location, 1, GL_FALSE, Maths::value_ptr(value));
            }
        }
    }

    void Shader::SetUniform(const char* name, const Maths::Matrix4& value) {
        if (IsActive()) {
            auto location = _GetLocationUniform(name);
            if (location != -1) {
                glUniformMatrix4fv(location, 1, GL_FALSE, Maths::value_ptr(value));
            }
        }
    }
} // namespace ZEngine::Rendering::Shaders
