#include <pch.h>
#include <Rendering/Shaders/Compilers/CompilationStage.h>
#include <Rendering/Shaders/Compilers/LinkageStage.h>
#include <Logging/LoggerDefinition.h>

namespace ZEngine::Rendering::Shaders::Compilers {

	CompilationStage::CompilationStage()
	{
		m_next_stage = std::make_shared<LinkageStage>();
	}
	
	CompilationStage::~CompilationStage() 
	{
	}

	void CompilationStage::Run(std::vector<ShaderInformation>& information_list)
	{
		ZENGINE_CORE_INFO("------> Compilation stage started");

		std::for_each(std::begin(information_list), std::end(information_list), [this](ShaderInformation& shader_info) {

			if (shader_info.CompiledOnce) {
				ZENGINE_CORE_WARN("------> This {} shader has already been compiled... compilation stage skipped", shader_info.Name);
				return;
			}

			ZENGINE_CORE_INFO("------> Compiling {} shader", shader_info.Name);

			shader_info.ShaderId = glCreateShader(shader_info.InternalType);
			const GLchar* source = (const GLchar*)shader_info.Source.c_str();
			glShaderSource(shader_info.ShaderId, 1, &source, 0);

			glCompileShader(shader_info.ShaderId);

			GLint compile_status;
			glGetShaderiv(shader_info.ShaderId, GL_COMPILE_STATUS, &compile_status);
			if (compile_status == GL_FALSE) {

				GLint log_info_length = 0;
				glGetShaderiv(shader_info.ShaderId, GL_INFO_LOG_LENGTH, &log_info_length);
				
				std::vector<GLchar> log_message(log_info_length);
				glGetShaderInfoLog(shader_info.ShaderId, log_info_length, &log_info_length, &log_message[0]);
				glDeleteShader(shader_info.ShaderId);

				this->m_information.IsSuccess = this->m_information.IsSuccess && false;
				this->m_information.ErrorMessage.append(std::begin(log_message), std::end(log_message));
				ZENGINE_CORE_ERROR("------> Failed to compile {} shader", shader_info.Name);
			}
		});

		if (!this->m_information.IsSuccess) {
			ZENGINE_CORE_ERROR("------> Compilation stage completed with errors");
			ZENGINE_CORE_ERROR("------> {}", this->m_information.ErrorMessage);
			return;
		}

		ZENGINE_CORE_INFO("------> Compilation stage completed successfully");
	}
}