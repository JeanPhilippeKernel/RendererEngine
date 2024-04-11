#pragma once
#include <Rendering/Shaders/Compilers/ICompilerStage.h>
#include <fmt/format.h>
#include <string_view>

namespace ZEngine::Rendering::Shaders::Compilers
{

    class ShaderFileGenerator : public ICompilerStage
    {
    public:
        /**
         * Initialize a new ShaderFileGenerator instance.
         */
        ShaderFileGenerator();
        virtual ~ShaderFileGenerator();

        /**
         * Run Compiler stage
         *
         * @param information Collection of shader information
         */
        std::string OutputName(ShaderInformation& information_list);

        /**
         * Run asynchronously compiler stage
         *
         * @param information Collection of shader information
         */
        virtual std::future<void> RunAsync(ShaderInformation& information) override;

    private:
        std::string_view outputDirectory = "Shaders/Cache/";
    };
} // namespace ZEngine::Rendering::Shaders::Compilers
