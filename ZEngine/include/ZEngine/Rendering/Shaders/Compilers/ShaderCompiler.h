#pragma once
#include <Core/IPipeline.h>
#include <Rendering/Shaders/ShaderReader.h>
#include <ZEngineDef.h>
#include <future>
#include <mutex>

namespace ZEngine::Rendering::Shaders::Compilers
{
    struct ShaderCompilerResult
    {
        ShaderOperationResult OperationResult;
        ShaderInformation     Information;
    };

    class ShaderCompiler : public Core::IPipelineContext
    {
    public:
        /**
         * Initialize a new ShaderCompiler instance.
         */
        ShaderCompiler();
        ShaderCompiler(std::string_view filename);
        ~ShaderCompiler();

        /**
         * Set the shader source file
         * @param filename Path of the shader file
         */
        void SetSource(std::string_view filename);

        /**
         * Compile shader source code
         * @return Tuple of <ShaderOperationResult, ShaderInformation> that represents status of the compile process (Success or Failure)
         *			and the Shader Information
         */

        std::future<ShaderCompilerResult> CompileAsync();

    private:
        std::string                                                            m_source_file;
        Scope<ShaderReader>                                                    m_reader{nullptr};
        std::recursive_mutex                                                   m_mutex;
        static std::unordered_map<std::string, std::vector<ShaderInformation>> s_already_compiled_shaders_collection;
    };
} // namespace ZEngine::Rendering::Shaders::Compilers
