#pragma once
#include <filesystem>
#include <string>

namespace ZEngine::Core
{
    inline constexpr const char* EngineAssetRootOverrideEnvironment = "ZENGINE_ASSET_ROOT";

    struct EngineAssetRootResolution
    {
        std::filesystem::path Root       = {};
        std::string           Diagnostic = {};

        [[nodiscard]] bool    Succeeded() const
        {
            return Diagnostic.empty();
        }
    };

    // Returns the executable's native path, independent of the process working directory.
    [[nodiscard]] std::filesystem::path     GetExecutablePath();

    // Resolves the directory containing packaged engine files. An explicit override
    // takes precedence; otherwise assets live in <executable directory>/ZodiacEngine.
    [[nodiscard]] EngineAssetRootResolution ResolveEngineAssetRoot(const std::filesystem::path& executable_path, const std::filesystem::path& override_root = {});

    // Uses the current executable and the optional ZENGINE_ASSET_ROOT environment
    // override. The override exists for development tools and package tests.
    [[nodiscard]] EngineAssetRootResolution ResolveEngineAssetRoot();

    // Checks that the native package directory exists before it is mounted.
    [[nodiscard]] EngineAssetRootResolution ValidateEngineAssetRoot(const std::filesystem::path& root);

} // namespace ZEngine::Core
