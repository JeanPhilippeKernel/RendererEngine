#include <Rendering/Shaders/Compilers/LinkageStage.h>
#include <Rendering/Shaders/Compilers/ValidationStage.h>
#include <Logging/LoggerDefinition.h>

namespace ZEngine::Rendering::Shaders::Compilers {

	LinkageStage::LinkageStage()
	{
		m_next_stage = std::make_shared<ValidationStage>();
	}

	LinkageStage::~LinkageStage()
	{
	}

	void LinkageStage::Run(std::vector<ShaderInformation>& information_list) {
		Z_ENGINE_CORE_INFO("------> Linking stage started");

		GLuint shader_program = glCreateProgram();
		std::for_each(std::begin(information_list), std::end(information_list), [&shader_program](const ShaderInformation& info) { glAttachShader(shader_program, info.ShaderId); });
		glLinkProgram(shader_program);

		GLint linking_status;
		glGetProgramiv(shader_program, GL_LINK_STATUS, &linking_status);
		if (linking_status == GL_FALSE) {
			GLint log_info_length = 0;
			glGetProgramiv(shader_program, GL_INFO_LOG_LENGTH, &log_info_length);
			
			std::vector<GLchar> log_message(log_info_length);
			glGetProgramInfoLog(shader_program, log_info_length, &log_info_length, &log_message[0]);
			glDeleteProgram(shader_program);

			this->m_information.IsSuccess = this->m_information.IsSuccess && false;
			this->m_information.ErrorMessage.append(std::begin(log_message), std::end(log_message));
			Z_ENGINE_CORE_ERROR("------> Failed to create shader program {} shader");			
		}

		if (!this->m_information.IsSuccess) {
			Z_ENGINE_CORE_ERROR("------> Linking stage completed with errors");
			Z_ENGINE_CORE_ERROR("------> {}", this->m_information.ErrorMessage);
			return;
		}

		std::for_each(std::begin(information_list), std::end(information_list), [&shader_program](ShaderInformation& info) {
			info.ProgramId = shader_program;
			glDetachShader(shader_program, info.ShaderId);
		});
		Z_ENGINE_CORE_INFO("------> Linking stage succeeded");
	}
}