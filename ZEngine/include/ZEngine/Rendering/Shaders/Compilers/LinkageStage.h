#pragma once
#include <Rendering/Shaders/Compilers/ICompilerStage.h>
#include <fmt/format.h>

namespace ZEngine::Rendering::Shaders::Compilers
{

    class LinkageStage : public ICompilerStage
    {
    public:
        /**
         * Initialize a new LinkageStage instance.
         */
        LinkageStage();
        virtual ~LinkageStage();

        /**
         * Run Compiler stage
         *
         * @param information Collection of shader information
         */
        std::string outputname(ShaderInformation& information_list);

        /**
         * Run asynchronously compiler stage
         *
         * @param information Collection of shader information
         */
        virtual std::future<void> RunAsync(ShaderInformation& information) override;

    private:
        const char* outputDirectory = "Shaders/Cache/";
    };
} // namespace ZEngine::Rendering::Shaders::Compilers
