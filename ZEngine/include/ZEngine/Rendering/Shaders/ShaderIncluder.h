#include <Rendering/Shaders/ShaderReader.h>
#include <ZEngineDef.h>
#include <glslang/Public/ShaderLang.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace ZEngine::Rendering::Shaders
{

    class ShaderIncluder : public glslang::TShader::Includer
    {
    public:
        ShaderIncluder() {}

        IncludeResult* includeLocal(const char* headerName, const char* includerName, size_t inclusionDepth) override
        {
            std::filesystem::path fullPath = std::filesystem::path(basePath) / headerName;
            const auto            cacheKey = fullPath.string();

            auto it = cache.find(cacheKey);
            if (it != cache.end())
            {
                return new IncludeResult(cacheKey, it->second.data(), it->second.size(), nullptr);
            }

            std::string content = readFileContent(fullPath);
            cache[cacheKey]     = content;

            return new IncludeResult(cacheKey, content.data(), content.size(), nullptr);
        }

        void releaseInclude(IncludeResult* result) override
        {
            delete result;
        }

    private:
        std::string_view                             basePath = "Shaders/";
        std::unordered_map<std::string, std::string> cache;

        std::string readFileContent(const std::filesystem::path& filePath) const
        {
            std::ifstream fileStream(filePath, std::ios::in | std::ios::binary);
            if (!fileStream)
            {
                ZENGINE_CORE_ERROR("Failed to open shader file: {}", filePath.string());
            }
            return std::string((std::istreambuf_iterator<char>(fileStream)), std::istreambuf_iterator<char>());
        }
    };
} // namespace ZEngine::Rendering::Shaders