#pragma once
#include <future>
#include <Core/IPipeline.h>
#include <Rendering/Shaders/ShaderInformation.h>

namespace ZEngine::Rendering::Shaders::Compilers
{

    struct ICompilerStage : public Core::IPipelineStage
    {

        /**
         * Initialize a new ICompilerStage instance.
         */
        ICompilerStage()          = default;
        virtual ~ICompilerStage() = default;

        /**
         * Run compiler stage
         *
         * @param information Collection of shader information
         */
        virtual void Run(std::vector<ShaderInformation>& information) = 0;

        /**
         * Run asynchronously compiler stage
         *
         * @param information Collection of shader information
         */
        virtual std::future<void> RunAsync(std::vector<ShaderInformation>& information) = 0;

    protected:
        std::recursive_mutex m_mutex;
    };
} // namespace ZEngine::Rendering::Shaders::Compilers
