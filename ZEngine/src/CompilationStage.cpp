#pragma once
#include <pch.h>
#include <Core/Coroutine.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Shaders/Compilers/CompilationStage.h>
#include <Rendering/Shaders/Compilers/ValidationStage.h>

namespace ZEngine::Rendering::Shaders::Compilers
{
    const TBuiltInResource DefaultTBuiltInResource = {
        /* .MaxLights = */ 32,
        /* .MaxClipPlanes = */ 6,
        /* .MaxTextureUnits = */ 32,
        /* .MaxTextureCoords = */ 32,
        /* .MaxVertexAttribs = */ 64,
        /* .MaxVertexUniformComponents = */ 4096,
    };

    const TBuiltInResource* GetDefaultResources()
    {
        return &DefaultTBuiltInResource;
    }

    CompilationStage::CompilationStage()
    {
        m_next_stage = CreateRef<ValidationStage>();
    }

    CompilationStage::~CompilationStage() {}

    EShLanguage CompilationStage::GetEShLanguage(const ShaderType type)
    {
        if (type == ShaderType::VERTEX)
            return EShLangVertex;
        if (type == ShaderType::FRAGMENT)
            return EShLangFragment;
        return EShLangGeometry;
    }

    void CompilationStage::SetShaderRules(glslang::TShader& shader, ShaderInformation& information_list, TBuiltInResource& Resources)
    {
        // Language-specific rules for parsing the shader
        int                               ClientInputSemanticsVersion = 460;
        glslang::EShTargetClientVersion   VulkanVersion               = glslang::EShTargetVulkan_1_3;
        glslang::EShTargetLanguageVersion TargetVersion               = glslang::EShTargetSpv_1_6;
        shader.setEnvInput(glslang::EShSourceGlsl, GetEShLanguage(information_list.Type), glslang::EShClientVulkan, ClientInputSemanticsVersion);
        shader.setEnvClient(glslang::EShClientVulkan, VulkanVersion);
        shader.setEnvTarget(glslang::EShTargetSpv, TargetVersion);

        Resources.maxDrawBuffers                 = 8;
        Resources.limits.generalUniformIndexing  = true;
        Resources.limits.generalSamplerIndexing  = true;
        Resources.limits.generalVariableIndexing = true;
    }

    std::future<void> CompilationStage::RunAsync(ShaderInformation& information_list)
    {
        std::unique_lock   lock(m_mutex);
        GlslangInitializer glslangInit;
        if (!glslangInit.isSuccess())
        {
            m_information = {false, "Failed to initialize glslang"};
            co_return;
        }
        information_list.BinarySource = std::vector<uint32_t>();
        glslang::TShader shader(GetEShLanguage(information_list.Type));

        TBuiltInResource Resources = DefaultTBuiltInResource;
        SetShaderRules(shader, information_list, Resources);
        EShMessages messages = (EShMessages) (EShMsgSpvRules | EShMsgVulkanRules);

        const char* shaderSource = information_list.Source.c_str();
        shader.setStrings(&shaderSource, 1);

        ShaderIncluder includefiles;

        if (!shader.parse(&Resources, 100, false, messages, includefiles))
        {
            m_information = {false, shader.getInfoLog()};
            co_return;
        }

        glslang::TProgram program;
        program.addShader(&shader);
        if (!program.link(messages))
        {
            m_information = {false, program.getInfoLog()};
            co_return;
        }

        glslang::GlslangToSpv(*program.getIntermediate(GetEShLanguage(information_list.Type)), information_list.BinarySource);
        co_return;
    }
} // namespace ZEngine::Rendering::Shaders::Compilers
