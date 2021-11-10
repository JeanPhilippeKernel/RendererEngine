#include <ZEngine/pch.h>
#include <Rendering/Shaders/Compilers/ValidationStage.h>
#include <Logging/LoggerDefinition.h>

namespace ZEngine::Rendering::Shaders::Compilers {

	ValidationStage::ValidationStage()
	{
	}

	ValidationStage::~ValidationStage()
	{
	}

	void ValidationStage::Run(std::vector<ShaderInformation>& information_list) {
		Z_ENGINE_CORE_INFO("------> Validation stage started");

		// We assume that all ShaderInformation object have the same ProgramId
		const auto& first = information_list.at(0);
		const GLuint shader_program = first.ProgramId;
		glValidateProgram(shader_program);

		GLint validate_status;
		glGetProgramiv(shader_program, GL_VALIDATE_STATUS, &validate_status);
		if (validate_status == GL_FALSE) {

			GLint log_info_length = 0;
			glGetProgramiv(shader_program, GL_INFO_LOG_LENGTH, &log_info_length);

			std::vector<GLchar> log_message(log_info_length);
			glGetProgramInfoLog(shader_program, log_info_length, &log_info_length, &log_message[0]);

			this->m_information.IsSuccess = this->m_information.IsSuccess && false;
			this->m_information.ErrorMessage.append(std::begin(log_message), std::end(log_message));
			Z_ENGINE_CORE_ERROR("------> Shader Program is invalid");
		}

		if (!this->m_information.IsSuccess) {
			Z_ENGINE_CORE_ERROR("------> Validation stage completed with errors");
			Z_ENGINE_CORE_ERROR("------> {}", this->m_information.ErrorMessage);
			return;
		}

		Z_ENGINE_CORE_INFO("------> Validation stage succeeded");
	}
}