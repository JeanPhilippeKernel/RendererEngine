#pragma once
#include <Rendering/Shaders/Compilers/ICompilerStage.h>
#include <Rendering/Shaders/ShaderIncluder.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

namespace ZEngine::Rendering::Shaders::Compilers
{
    class CompilationStage : public ICompilerStage
    {
    public:
        /**
         * Initialize a new CompilationStage instance.
         */
        CompilationStage();
        virtual ~CompilationStage();

        /**
         * Run asynchronously compiler stage
         *
         * @param information Collection of shader information
         */
        virtual std::future<void> RunAsync(ShaderInformation& information) override;

        EShLanguage GetEShLanguage(const ShaderType type);

        void SetShaderRules(glslang::TShader& shader, ShaderInformation& information_list, TBuiltInResource& Resources);

    };

    class GlslangInitializer
    {
    public:
        GlslangInitializer() : initSuccess(glslang::InitializeProcess()) {}
        ~GlslangInitializer()
        {
            if (initSuccess)
            {
                glslang::FinalizeProcess();
            }
        }

        GlslangInitializer(const GlslangInitializer&)            = delete;
        GlslangInitializer& operator=(const GlslangInitializer&) = delete;

        GlslangInitializer(GlslangInitializer&&) noexcept            = default;
        GlslangInitializer& operator=(GlslangInitializer&&) noexcept = default;

        bool isSuccess() const
        {
            return initSuccess;
        }

    private:
        bool initSuccess;
    };

} // namespace ZEngine::Rendering::Shaders::Compilers
