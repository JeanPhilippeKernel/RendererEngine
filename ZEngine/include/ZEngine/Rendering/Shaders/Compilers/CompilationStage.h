#pragma once
#include <Rendering/Shaders/Compilers/ICompilerStage.h>

namespace ZEngine::Rendering::Shaders::Compilers {

    class CompilationStage : public ICompilerStage {
    public:
        CompilationStage();
        virtual ~CompilationStage();

		/**
		* Run Compiler stage
		* 
		* @param information    Collection of shader information
		*/        
        virtual void Run(std::vector<ShaderInformation>& information) override;
    };
}