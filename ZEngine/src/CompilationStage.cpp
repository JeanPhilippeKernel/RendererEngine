#include <pch.h>
#include <Rendering/Shaders/Compilers/CompilationStage.h>
#include <Rendering/Shaders/Compilers/LinkageStage.h>
#include <Logging/LoggerDefinition.h>
#include <Core/Coroutine.h>

#include <shaderc/shaderc.hpp>

namespace ZEngine::Rendering::Shaders::Compilers
{

    CompilationStage::CompilationStage()
    {
        m_next_stage = std::make_shared<LinkageStage>();
    }

    CompilationStage::~CompilationStage() {}

    void CompilationStage::Run(std::vector<ShaderInformation>& information_list)
    {
        //std::for_each(std::begin(information_list), std::end(information_list),
        //    [this](ShaderInformation& shader_info)
        //    {
        //        if (shader_info.CompiledOnce)
        //        {
        //            return;
        //        }

        //        shader_info.ShaderId = glCreateShader(shader_info.InternalType);
        //        const GLchar* source = (const GLchar*) shader_info.Source.c_str();
        //        glShaderSource(shader_info.ShaderId, 1, &source, 0);

        //        glCompileShader(shader_info.ShaderId);

        //        GLint compile_status;
        //        glGetShaderiv(shader_info.ShaderId, GL_COMPILE_STATUS, &compile_status);
        //        if (compile_status == GL_FALSE)
        //        {

        //            GLint log_info_length = 0;
        //            glGetShaderiv(shader_info.ShaderId, GL_INFO_LOG_LENGTH, &log_info_length);

        //            std::vector<GLchar> log_message(log_info_length);
        //            glGetShaderInfoLog(shader_info.ShaderId, log_info_length, &log_info_length, &log_message[0]);
        //            glDeleteShader(shader_info.ShaderId);

        //            this->m_information.IsSuccess = this->m_information.IsSuccess && false;
        //            this->m_information.ErrorMessage.append(std::begin(log_message), std::end(log_message));
        //            ZENGINE_CORE_ERROR("------> Failed to compile {} shader", shader_info.Name);
        //        }
        //    });

        //if (!this->m_information.IsSuccess)
        //{
        //    ZENGINE_CORE_ERROR("------> Compilation stage completed with errors");
        //    ZENGINE_CORE_ERROR("------> {}", this->m_information.ErrorMessage);
        //    return;
        //}
    }

    std::future<void> CompilationStage::RunAsync(std::vector<ShaderInformation>& information_list)
    {
        std::unique_lock lock(m_mutex);

        shaderc::Compiler       compiler;
        shaderc::CompileOptions compiler_option;

        compiler_option.SetOptimizationLevel(shaderc_optimization_level_zero);
        //compiler_option.SetOptimizationLevel(shaderc_optimization_level_performance);
        compiler_option.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);

        for (ShaderInformation& shader_info : information_list)
        {
            if (shader_info.CompiledOnce)
            {
                co_return;
            }

            shaderc_shader_kind shader_kind;

            if (shader_info.Type == ShaderType::VERTEX)
            {
                shader_kind = shaderc_glsl_vertex_shader;
            }
            else if (shader_info.Type == ShaderType::FRAGMENT)
            {
                shader_kind = shaderc_glsl_fragment_shader;
            }
            else if (shader_info.Type == ShaderType::GEOMETRY)
            {
                shader_kind = shaderc_glsl_geometry_shader;
            }

            auto compile_status = compiler.CompileGlslToSpv(shader_info.Source, shader_kind, shader_info.Name.c_str(), compiler_option);

            if (compile_status.GetCompilationStatus() == shaderc_compilation_status_success)
            {
                if (!shader_info.BinarySource.empty())
                {
                    shader_info.BinarySource.clear();
                    shader_info.BinarySource.shrink_to_fit();
                }

                std::copy(compile_status.cbegin(), compile_status.cend(), std::back_inserter(shader_info.BinarySource));
            }
            else
            {
                this->m_information.IsSuccess    = this->m_information.IsSuccess && false;
                this->m_information.ErrorMessage = compile_status.GetErrorMessage();
                ZENGINE_CORE_ERROR("------> Failed to compile {} shader", shader_info.Name)
            }
        }

        if (!this->m_information.IsSuccess)
        {
            ZENGINE_CORE_ERROR("------> Compilation stage completed with errors")
            ZENGINE_CORE_ERROR("------> {}", this->m_information.ErrorMessage)
        }
        co_return;
    }
} // namespace ZEngine::Rendering::Shaders::Compilers
