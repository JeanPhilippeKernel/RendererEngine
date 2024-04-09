#pragma once
#include <Rendering/Shaders/Compilers/ICompilerStage.h>
#include <spirv-tools/libspirv.hpp>
#include <spirv-tools/optimizer.hpp>

namespace ZEngine::Rendering::Shaders::Compilers {

    class ValidationStage : public ICompilerStage {
    public:
        /**
         * Initialize a new ValidationStage instance.
         */
        ValidationStage();
        virtual ~ValidationStage();

        /**
         * Run asynchronously compiler stage
         *
         * @param information Collection of shader information
         */
        virtual std::future<void> RunAsync(ShaderInformation& information) override;
    };
} // namespace ZEngine::Rendering::Shaders::Compilers
