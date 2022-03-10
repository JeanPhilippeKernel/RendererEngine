#pragma once
#include <Core/IPipeline.h>
#include <Rendering/Shaders/ShaderInformation.h>

namespace ZEngine::Rendering::Shaders::Compilers {

    struct ICompilerStage : public Core::IPipelineStage {

        /**
         * Initialize a new ICompilerStage instance.
         */
        ICompilerStage()          = default;
        virtual ~ICompilerStage() = default;

        /**
         * Run Compiler stage
         *
         * @param information Collection of shader information
         */
        virtual void Run(std::vector<ShaderInformation>& information) = 0;
    };
} // namespace ZEngine::Rendering::Shaders::Compilers
