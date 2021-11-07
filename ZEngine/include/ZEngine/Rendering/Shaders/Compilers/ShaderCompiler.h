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

		/**
		* Initialize a new ShaderCompiler instance.
		*/
		ShaderCompiler();
		ShaderCompiler(std::string_view filename);
		~ShaderCompiler();

		/**
		* Set the shader source file 
		* @param filename Path of the shader file
		*/     
		void SetSource(std::string_view filename);

		/**
		* Update the current compiler stage
		* @param stage Compiler stage 
		*/     
		void UpdateStage(const Ref<ICompilerStage>& stage);
		
		/**
		* Update the current compiler stage
		* @param stage Compiler stage 
		*/     		
		void UpdateStage(ICompilerStage* const stage);
		
		/**
		* Compile shader source code
		* @return Tuple of <ShaderOperationResult, GLuint> that represents status of the compile process (Success or Failure)
		*			and the identifier of generated shader program (0 in case of any errors)
		*/     
		std::tuple<ShaderOperationResult, GLuint> Compile();
		
		/**
		* Compile shader source code
		* @return Tuple of <ShaderOperationResult, GLuint> that represents status of the compile process (Success or Failure)
		*			and the identifier of generated shader program (0 in case of any errors)
		*/     		
		std::future<std::tuple<ShaderOperationResult, GLuint>> CompileAsync() = delete;

	private:
		bool					m_running_stages{ true };
		std::string				m_source_file;
		Ref<ICompilerStage>		m_stage{ nullptr };
		Scope<ShaderReader>		m_reader{ nullptr };
	};
}