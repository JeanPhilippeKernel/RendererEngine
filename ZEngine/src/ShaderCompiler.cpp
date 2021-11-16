#include <pch.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Shaders/Compilers/ShaderCompiler.h>
#include <Rendering/Shaders/Compilers/CompilationStage.h>

namespace ZEngine::Rendering::Shaders::Compilers {
	
	ShaderCompiler::ShaderCompiler() {
		m_reader = std::make_unique<ShaderReader>();
		m_stage  = std::make_unique<CompilationStage>();
		m_stage->SetContext(this);
	}

	ShaderCompiler::ShaderCompiler(std::string_view filename)
		: m_source_file(filename)
	{
		m_reader = std::make_unique<ShaderReader>();
		m_stage = std::make_unique<CompilationStage>();
		m_stage->SetContext(this);
	}

	ShaderCompiler::~ShaderCompiler() 
	{
	}

	void ShaderCompiler::SetSource(std::string_view filename) {
		m_source_file = filename.data();
	}

	void ShaderCompiler::UpdateStage(const Ref<ICompilerStage>& stage) {
		m_stage = stage;
		m_stage->SetContext(this);
	}
	
	void ShaderCompiler::UpdateStage(ICompilerStage* const stage) {
		m_stage.reset(stage);
		m_stage->SetContext(this);
	}

	std::tuple<ShaderOperationResult, GLuint> ShaderCompiler::Compile() {
		bool compile_process_succeeded{ true };

		ZENGINE_CORE_INFO("====== Compilation process started ======");

		const ShaderOperationResult read_operation = m_reader->Read(m_source_file);
		if (read_operation == ShaderOperationResult::FAILURE) {
			ZENGINE_CORE_CRITICAL("Compilation process stopped");
			return std::make_tuple(ShaderOperationResult::FAILURE, 0);
		}

		auto shader_information = m_reader->GetInformations();
		if (shader_information.empty()) {
			ZENGINE_CORE_CRITICAL("Information collected while reading shader file are incorrect or not enough to continue compilation process");
			ZENGINE_CORE_CRITICAL("Compilation process stopped");
			return std::make_tuple(ShaderOperationResult::FAILURE, 0);
		}

		while (m_running_stages) {
			
			m_stage->Run(shader_information);
			const auto stage_info = m_stage->GetInformation();
			compile_process_succeeded = compile_process_succeeded && stage_info.IsSuccess;

			if (stage_info.IsSuccess && m_stage->HasNext()) {
				m_stage->Next();
			}
			else {
				m_running_stages = false;
			}
		}

		if (!compile_process_succeeded) {
			ZENGINE_CORE_CRITICAL("Compilation process weren't able to create a valid Shader Program");
			return std::make_tuple(ShaderOperationResult::FAILURE, 0);
		}

		ZENGINE_CORE_INFO("====== Compilation process succeeded ======");

		const auto& first = std::begin(shader_information);
		return std::make_tuple(ShaderOperationResult::SUCCESS, first->ProgramId);
	}
}