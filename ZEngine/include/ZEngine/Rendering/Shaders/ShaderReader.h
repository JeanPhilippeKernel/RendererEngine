#pragma once
#include <fstream>
#include <unordered_map>
#include <future>
#include <Rendering/Shaders/ShaderInformation.h>

namespace ZEngine::Rendering::Shaders {

    class ShaderReader {
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

        /**
         * Read asynchronously content of shader file
         *
         * @param filename  Path to the shader file
         * @return enum ShaderReaderState that describes the read operation state
         */
        std::future<ShaderOperationResult> ReadAsync(std::string_view filename) = delete;

        /**
         * Get shaders information collected during Reading process
         *
         * @see Read(std::string_view) and ReadAsync(std::string_view) methods
         * @return ShaderInformation
         */
        const std::vector<ShaderInformation>& GetInformations() const;

        /**
         * Get shaders information collected during Reading process
         *
         * @see Read(std::string_view) and ReadAsync(std::string_view) methods
         * @return ShaderInformation
         */
        std::vector<ShaderInformation>& GetInformations();

    private:
        const char*                    m_regex_expression{"#type[\\s][a-zA-Z]+"};
        std::ifstream                  m_filestream{};
        std::vector<ShaderInformation> m_shader_info_collection{};
    };
} // namespace ZEngine::Rendering::Shaders
