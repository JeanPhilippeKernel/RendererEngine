#pragma once
#include <Rendering/Shaders/Compilers/ICompilerStage.h>

namespace ZEngine::Rendering::Shaders::Compilers {

    class CompilationStage : public ICompilerStage {
    public:
        CompilationStage();
        virtual ~CompilationStage();

        virtual void Run(std::vector<ShaderInformation>&) override;
    };
}