#include <pch.h>
#include <Rendering/Shaders/Compilers/CompilationStage.h>
#include <Rendering/Shaders/Compilers/LinkageStage.h>
#include <Logging/LoggerDefinition.h>
#include <Core/Coroutine.h>

namespace ZEngine::Rendering::Shaders::Compilers
{

    CompilationStage::CompilationStage()
    {
        m_next_stage = CreateRef<LinkageStage>();
    }

    CompilationStage::~CompilationStage() {}

    void CompilationStage::Run(std::vector<ShaderInformation>& information_list)
    {
    }

    std::future<void> CompilationStage::RunAsync(std::vector<ShaderInformation>& information_list)
    {
        co_return;
    }
} // namespace ZEngine::Rendering::Shaders::Compilers
