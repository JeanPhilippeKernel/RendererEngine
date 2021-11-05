#pragma once
#include <Rendering/Shaders/Compilers/ICompilerStage.h>

namespace ZEngine::Rendering::Shaders::Compilers {

    class LinkageStage : public ICompilerStage {
    public:
        LinkageStage();
        virtual ~LinkageStage();

        virtual void Run(std::vector<ShaderInformation>&) override;
    };
}