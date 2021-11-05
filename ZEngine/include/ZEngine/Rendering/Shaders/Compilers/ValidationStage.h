#pragma once
#include <Rendering/Shaders/Compilers/ICompilerStage.h>

namespace ZEngine::Rendering::Shaders::Compilers {

    class ValidationStage : public ICompilerStage {
    public:
        ValidationStage();
        virtual ~ValidationStage();

        virtual void Run(std::vector<ShaderInformation>&) override;
    };
}