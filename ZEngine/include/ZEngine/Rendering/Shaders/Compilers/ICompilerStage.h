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

        /**
         * Return information related to the stage.
         * These information are updated during call of Run()
         *
         * @return An information related to the compiler stage
         */
        const Core::StageInformation& GetInformation() const {
            return m_information;
        }
    };
} // namespace ZEngine::Rendering::Shaders::Compilers
