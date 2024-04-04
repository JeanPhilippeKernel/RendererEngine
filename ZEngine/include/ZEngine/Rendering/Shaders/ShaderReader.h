#pragma once
#include <Rendering/Shaders/ShaderInformation.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <unordered_map>
#include "glslang/Public/ShaderLang.h"

namespace ZEngine::Rendering::Shaders
{

    class ShaderReader
    {
    public:
        /**
         * Initializes a new ShaderReader instance.
         */
        ShaderReader();
        ~ShaderReader();

        /**
         * Read synchronously content of shader file
         *
         * @param filename  Path to the shader file
         * @return enum ShaderReaderState that describes the read operation state
         */
        ShaderOperationResult Read(std::string_view filename);

        static std::vector<uint32_t> ReadAsBinary(std::string_view filename);

        /**
         * Read asynchronously content of shader file
         *
         * @param filename  Path to the shader file
         * @return enum ShaderReaderState that describes the read operation state
         */
        std::future<ShaderOperationResult> ReadAsync(std::string_view filename);

        /**
         * Get shaders information collected during Reading process
         *
         * @see Read(std::string_view) and ReadAsync(std::string_view) methods
         * @return ShaderInformation
         */
        const ShaderInformation& GetInformations() const;

        /**
         * Get shaders information collected during Reading process
         *
         * @see Read(std::string_view) and ReadAsync(std::string_view) methods
         * @return ShaderInformation
         */
        ShaderInformation& GetInformations();
        ShaderType                      GetShaderType(const std::filesystem::path& path);

    private:
        std::ifstream                  m_filestream{};
        ShaderInformation m_shader_info_collection{};
        std::mutex                     m_lock;
    };
} // namespace ZEngine::Rendering::Shaders
