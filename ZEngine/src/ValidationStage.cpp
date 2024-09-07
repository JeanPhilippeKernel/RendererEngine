#include <pch.h>
#include <Core/Coroutine.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Shaders/Compilers/ShaderFileGenerator.h>
#include <Rendering/Shaders/Compilers/ValidationStage.h>

namespace ZEngine::Rendering::Shaders::Compilers
{

    ValidationStage::ValidationStage()
    {
        m_next_stage = CreateRef<ShaderFileGenerator>();
    }

    ValidationStage::~ValidationStage() {}

    std::future<void> ValidationStage::RunAsync(ShaderInformation& information_list)
    {
        std::unique_lock     lock(m_mutex);
        spvtools::SpirvTools tools(SPV_ENV_UNIVERSAL_1_6);

        tools.SetMessageConsumer([this](spv_message_level_t level, const char* source, const spv_position_t& position, const char* message) {
            m_information.ErrorMessage = "Validation Error (" + std::string(source) + ":" + std::to_string(position.index) + "): " + std::string(message) + "\n";
        });

        // First validation pass on the original SPIR-V code
        bool isValidInitial = tools.Validate(information_list.BinarySource.data(), information_list.BinarySource.size());
        if (!isValidInitial)
        {
            m_information.IsSuccess = false;
            co_return;
        }

        // Optimization phase
        spvtools::Optimizer optimizer(SPV_ENV_UNIVERSAL_1_6);
        optimizer.RegisterPassFromFlag("-0s");
        optimizer.Run(information_list.BinarySource.data(), information_list.BinarySource.size(), &information_list.BinarySource);

        // Second validation pass on the optimized SPIR-V code
        bool isValidPostOptimization = tools.Validate(information_list.BinarySource.data(), information_list.BinarySource.size());
        if (!isValidPostOptimization)
        {
            m_information = {false, "Post-optimization " + m_information.ErrorMessage};
        }

        co_return;
    }

} // namespace ZEngine::Rendering::Shaders::Compilers
