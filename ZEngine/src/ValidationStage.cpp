#include <pch.h>
#include <Core/Coroutine.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Shaders/Compilers/LinkageStage.h>
#include <Rendering/Shaders/Compilers/ValidationStage.h>

namespace ZEngine::Rendering::Shaders::Compilers
{

    ValidationStage::ValidationStage()
    {
        m_next_stage = CreateRef<LinkageStage>();
    }

    ValidationStage::~ValidationStage() {}

    std::future<void> ValidationStage::RunAsync(ShaderInformation& information_list)
    {
        std::unique_lock lock(m_mutex);

        // Optimize SPIR-V (not necessary right now)
        // spvtools::Optimizer optimizer(SPV_ENV_VULKAN_1_3);
        // optimizer.RegisterPassFromFlag("-0s");
        // optimizer.Run(information_list.BinarySource.data(), information_list.BinarySource.size(), &information_list.BinarySource);

        // Validate Optimized SPIR-V
        spvtools::SpirvTools tools(SPV_ENV_UNIVERSAL_1_6);

        // Set the message consumer BEFORE calling Validate()
        tools.SetMessageConsumer([this](spv_message_level_t level, const char* source, const spv_position_t& position, const char* message) {
            m_information.ErrorMessage = "Validation Error (" + std::string(source) + ":" + std::to_string(position.index) + "): " + std::string(message) + "\n";
        });

        bool isValid = tools.Validate(information_list.BinarySource.data(), information_list.BinarySource.size());
        if (!isValid)
        {
            m_information.IsSuccess = false;
        }

        co_return;
    }
} // namespace ZEngine::Rendering::Shaders::Compilers
