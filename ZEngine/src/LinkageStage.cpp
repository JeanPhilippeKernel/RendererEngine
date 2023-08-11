#include <pch.h>
#include <Rendering/Shaders/Compilers/LinkageStage.h>
#include <Rendering/Shaders/Compilers/ValidationStage.h>
#include <Logging/LoggerDefinition.h>
#include <Core/Coroutine.h>

namespace ZEngine::Rendering::Shaders::Compilers
{

    LinkageStage::LinkageStage()
    {
#ifdef __APPLE__
        // we dont perform validation stage on macOs for the moment as it considers generated shader program as invalid
#else
        m_next_stage = std::make_shared<ValidationStage>();
#endif
    }

    LinkageStage::~LinkageStage() {}

    void LinkageStage::Run(std::vector<ShaderInformation>& information_list) {}

    std::future<void> LinkageStage::RunAsync(std::vector<ShaderInformation>& information_list)
    {

        co_return;
    }
} // namespace ZEngine::Rendering::Shaders::Compilers
