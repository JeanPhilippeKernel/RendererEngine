#include <pch.h>
#include <Rendering/Shaders/Compilers/LinkageStage.h>
#include <Rendering/Shaders/Compilers/ValidationStage.h>
#include <Logging/LoggerDefinition.h>

namespace ZEngine::Rendering::Shaders::Compilers {

    LinkageStage::LinkageStage() {
#ifdef __APPLE__
        // we dont perform validation stage on macOs for the moment as it considers generated shader program as invalid
#else
        m_next_stage = std::make_shared<ValidationStage>();
#endif
    }

    LinkageStage::~LinkageStage() {}

    void LinkageStage::Run(std::vector<ShaderInformation>& information_list) {
        ZENGINE_CORE_INFO("------> Linking stage started");

        GLuint shader_program = glCreateProgram();
        std::for_each(
            std::begin(information_list), std::end(information_list), [&shader_program](const ShaderInformation& info) { glAttachShader(shader_program, info.ShaderId); });
        glLinkProgram(shader_program);

        GLint linking_status;
        glGetProgramiv(shader_program, GL_LINK_STATUS, &linking_status);
        if (linking_status == GL_FALSE) {
            GLint log_info_length = 0;
            glGetProgramiv(shader_program, GL_INFO_LOG_LENGTH, &log_info_length);

            std::vector<GLchar> log_message(log_info_length);
            if (log_info_length > 0) {
                glGetProgramInfoLog(shader_program, log_info_length, &log_info_length, &log_message[0]);
            }
            glDeleteProgram(shader_program);

            this->m_information.IsSuccess = this->m_information.IsSuccess && false;
            this->m_information.ErrorMessage.append(std::begin(log_message), std::end(log_message));
            ZENGINE_CORE_ERROR("------> Failed to create shader program {} shader");
        }

        if (!this->m_information.IsSuccess) {
            ZENGINE_CORE_ERROR("------> Linking stage completed with errors");
            ZENGINE_CORE_ERROR("------> {}", this->m_information.ErrorMessage);
            return;
        }

        std::for_each(std::begin(information_list), std::end(information_list), [&shader_program](ShaderInformation& info) {
            info.ProgramId = shader_program;
            glDetachShader(shader_program, info.ShaderId);
        });
        ZENGINE_CORE_INFO("------> Linking stage succeeded");
    }
} // namespace ZEngine::Rendering::Shaders::Compilers
