#include <pch.h>
#include <Core/Coroutine.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Shaders/Compilers/LinkageStage.h>
#include <Rendering/Shaders/Compilers/ValidationStage.h>

namespace ZEngine::Rendering::Shaders::Compilers
{

    LinkageStage::LinkageStage()
    {
#ifdef __APPLE__
        // we dont perform validation stage on macOs for the moment as it considers generated shader program as invalid
#else
        m_next_stage = CreateRef<ValidationStage>();
#endif
    }

    LinkageStage::~LinkageStage()
    {
        // check if output dir exists if not create the folder, - works
        if (!std::filesystem::exists(std::filesystem::path(outputDirectory)))
        {
            if (!std::filesystem::create_directories(std::filesystem::path(outputDirectory)))
            {
                m_information = {false, "Failed to create output directory "};
            }
        }
    }

    std::string LinkageStage::outputname(ShaderInformation& information_list)
    {
        std::filesystem::path file_path;
        if (information_list.Type == ShaderType::VERTEX)
            file_path = std::filesystem::path(outputDirectory) / fmt::format("{}_vertex.spv", information_list.Name);
        if (information_list.Type == ShaderType::FRAGMENT)
            file_path = std::filesystem::path(outputDirectory) / fmt::format("{}_fragment.spv", information_list.Name);

        return file_path.string();
    }


    std::future<void> LinkageStage::RunAsync(ShaderInformation& information_list)
    {
        std::string output_file = outputname(information_list);
        std::ofstream out(output_file, std::ios::out | std::ios::binary);
        if (!out.is_open() || !out)
        { 
            m_information = {false, "Failed to open spriv file: " + output_file};
            co_return;
        }
        out.write(reinterpret_cast<const char*>(information_list.BinarySource.data()), information_list.BinarySource.size() * sizeof(uint32_t));

        co_return;
    }
} // namespace ZEngine::Rendering::Shaders::Compilers
