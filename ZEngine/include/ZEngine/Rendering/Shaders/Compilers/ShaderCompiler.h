#pragma once
#include <ZEngineDef.h>
#include <future>
#include <tuple>
#include <Rendering/Shaders/Compilers/ICompilerStage.h>
#include <Rendering/Shaders/ShaderReader.h>

namespace ZEngine::Rendering::Shaders::Compilers {
	struct ICompilerStage;
	
	class ShaderCompiler : public std::enable_shared_from_this<ShaderCompiler> {
	public:
		ShaderCompiler();
		ShaderCompiler(std::string_view filename);
		~ShaderCompiler();

		void SetSource(std::string_view filename);

		void UpdateStage(const Ref<ICompilerStage>& stage);
		void UpdateStage(ICompilerStage* const stage);

		std::tuple<ShaderOperationResult, GLuint> Compile();
		std::future<std::tuple<ShaderOperationResult, GLuint>> CompileAsync() = delete;

	private:
		bool					m_running_stages{ true };
		std::string				m_source_file;
		Ref<ICompilerStage>		m_stage;
		Scope<ShaderReader>		m_reader;
	};
}