#include <ZEngine/Core/EngineAssetRoot.h>
#include <cstdlib>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace ZEngine::Core
{
    namespace
    {
        constexpr const char* kEngineAssetDirectory = "ZodiacEngine";

        std::filesystem::path NormalizePath(const std::filesystem::path& path)
        {
            std::error_code       error;
            std::filesystem::path absolute = std::filesystem::absolute(path, error);
            return error ? path.lexically_normal() : absolute.lexically_normal();
        }
    } // namespace

    std::filesystem::path GetExecutablePath()
    {
#if defined(_WIN32)
        std::vector<wchar_t> buffer(MAX_PATH);
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
                return {};
            if (length < buffer.size())
                return std::filesystem::path(std::wstring(buffer.data(), length));
            buffer.resize(buffer.size() * 2);
        }
#elif defined(__APPLE__)
        uint32_t size = 0;
        if (_NSGetExecutablePath(nullptr, &size) != -1 || size == 0)
            return {};

        std::vector<char> buffer(size);
        if (_NSGetExecutablePath(buffer.data(), &size) != 0)
            return {};
        return std::filesystem::path(buffer.data());
#elif defined(__linux__)
        std::vector<char> buffer(1024);
        for (;;)
        {
            const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size());
            if (length < 0)
                return {};
            if (static_cast<size_t>(length) < buffer.size())
                return std::filesystem::path(std::string(buffer.data(), static_cast<size_t>(length)));
            buffer.resize(buffer.size() * 2);
        }
#else
        return {};
#endif
    }

    EngineAssetRootResolution ResolveEngineAssetRoot(const std::filesystem::path& executable_path, const std::filesystem::path& override_root)
    {
        if (!override_root.empty())
            return {.Root = NormalizePath(override_root)};

        if (executable_path.empty())
            return {.Diagnostic = "Unable to determine the executable path while resolving the engine asset package. Set ZENGINE_ASSET_ROOT to the ZodiacEngine directory."};

        return {.Root = NormalizePath(executable_path.parent_path() / kEngineAssetDirectory)};
    }

    EngineAssetRootResolution ResolveEngineAssetRoot()
    {
        const char* const override_root = std::getenv(EngineAssetRootOverrideEnvironment);
        return ResolveEngineAssetRoot(GetExecutablePath(), override_root && override_root[0] != '\0' ? std::filesystem::path(override_root) : std::filesystem::path{});
    }

    EngineAssetRootResolution ValidateEngineAssetRoot(const std::filesystem::path& root)
    {
        if (root.empty())
            return {.Diagnostic = "Engine asset root is empty"};

        EngineAssetRootResolution result = {.Root = NormalizePath(root)};
        std::error_code           error;
        if (!std::filesystem::is_directory(result.Root, error))
        {
            result.Diagnostic = "Engine asset root is missing or not a directory: " + result.Root.string();
        }
        return result;
    }

} // namespace ZEngine::Core
