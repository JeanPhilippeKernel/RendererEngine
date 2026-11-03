#include <ZEngine/Rendering/ThumbnailCacheMaintenance.h>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <string_view>

namespace ZEngine::Rendering
{
    namespace
    {
        struct DiskKey
        {
            uint64_t Source = 0, Dependency = 0;
            uint32_t Schema = 0, Renderer = 0;
        };
        template <typename T>
        bool Number(std::string_view value, T& output, int base)
        {
            if (value.empty())
                return false;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), output, base);
            return result.ec == std::errc{} && result.ptr == value.data() + value.size();
        }
        bool Parse(std::string_view name, DiskKey& key)
        {
            if (!name.ends_with(".png"))
                return false;
            name.remove_suffix(4);
            std::string_view parts[4]{};
            uint32_t         count = 0;
            while (!name.empty() && count < 4)
            {
                const auto end = name.find('-');
                parts[count++] = name.substr(0, end);
                if (end == std::string_view::npos)
                {
                    name = {};
                    break;
                }
                name.remove_prefix(end + 1);
                if (name.empty())
                    return false;
            }
            if (!name.empty() || (count != 2 && count != 4) || parts[0].size() != 16 || !Number(parts[0], key.Source, 16))
                return false;
            if (count == 2)
                return Number(parts[1], key.Schema, 10) && key.Schema != 0;
            return parts[1].size() == 16 && Number(parts[1], key.Dependency, 16) && Number(parts[2], key.Schema, 10) && key.Schema != 0 && Number(parts[3], key.Renderer, 10) && key.Renderer != 0;
        }
    } // namespace

    ThumbnailCacheMaintenance::Statistics ThumbnailCacheMaintenance::Cleanup(const char* project_root, uint32_t schema, uint32_t renderer, Resolve resolve, void* context, const std::atomic<bool>* accepting)
    {
        namespace fs = std::filesystem;
        Statistics stats{};
        if (!project_root || !project_root[0] || !schema || !renderer || !resolve)
            return stats;
        const auto      running = [&] { return !accepting || accepting->load(std::memory_order_acquire); };
        const auto      started = fs::file_time_type::clock::now();
        fs::path        root(project_root);
        std::error_code error{};
        // Check every generated ancestor, not just the leaf: an intermediate
        // symlink must not redirect a cleanup pass outside this project's cache.
        for (const char* component : {".zodiacengine", "cache", "thumbnails"})
        {
            root /= component;
            if (!fs::is_directory(fs::symlink_status(root, error)) || error)
                return stats;
        }
        fs::directory_iterator directories(root, error), end;
        if (error)
            ++stats.Errors;
        for (; !error && directories != end && running(); directories.increment(error))
        {
            const auto directory = directories->path();
            const auto id        = uuids::uuid::from_string(directory.filename().string());
            if (!id || id->is_nil() || !fs::is_directory(directories->symlink_status(error)) || error)
            {
                error.clear();
                continue;
            }
            CurrentKey             current{};
            const auto             resolution = resolve(context, *id, &current);
            std::error_code        file_error{};
            fs::directory_iterator files(directory, file_error);
            if (file_error)
                ++stats.Errors;
            for (; !file_error && files != end && running(); files.increment(file_error))
            {
                ++stats.Examined;
                if (!fs::is_regular_file(files->symlink_status(file_error)) || file_error)
                {
                    file_error.clear();
                    continue;
                }
                const auto       path = files->path();
                const auto       text = path.filename().string();
                std::string_view name(text);
                bool             sidecar   = name.ends_with(".png.meta");
                bool             temporary = false;
                if (sidecar)
                    name.remove_suffix(5);
                else if (name.ends_with(".tmp") && name.size() > 41 && name[name.size() - 41] == '.')
                {
                    const auto writer = uuids::uuid::from_string(name.substr(name.size() - 40, 36));
                    if (writer)
                    {
                        name.remove_suffix(41);
                        temporary = true;
                    }
                }
                DiskKey disk{};
                if (!Parse(name, disk))
                    continue;
                const auto modified = fs::last_write_time(path, file_error);
                if (file_error)
                {
                    ++stats.Errors;
                    file_error.clear();
                    continue;
                }
                // New/active writers are never candidates. Crash-left siblings
                // need a full day's grace, not merely a different cache key.
                if (modified >= started || (temporary && started - modified < std::chrono::hours(24)))
                    continue;
                const bool obsolete = sidecar || temporary || disk.Schema < schema || (disk.Schema == schema && disk.Renderer != 0 && disk.Renderer < renderer) || resolution == Resolution::Missing || (resolution == Resolution::SourceOnly && disk.Source != current.Identity.SourceHash) || (resolution == Resolution::Current && disk.Schema == schema && (current.FullyTransparent || disk.Source != current.Identity.SourceHash || disk.Renderer != current.Identity.RendererRevision || disk.Dependency != current.Identity.DependencyHash));
                // Preserve future formats for projects shared with newer editors.
                if (!obsolete || disk.Schema > schema || (disk.Schema == schema && disk.Renderer > renderer))
                    continue;
                const auto bytes = fs::file_size(path, file_error);
                if (!file_error && fs::remove(path, file_error))
                {
                    ++stats.Removed;
                    stats.ReclaimedBytes += bytes;
                }
                if (file_error)
                {
                    ++stats.Errors;
                    file_error.clear();
                }
            }
            if (file_error)
                ++stats.Errors;
            // Non-recursive remove succeeds only for an empty UUID directory.
            // Unknown files, symlinks, and active writer temporaries keep it alive.
            if (running())
            {
                std::error_code ignored{};
                fs::remove(directory, ignored);
            }
        }
        if (error)
            ++stats.Errors;
        return stats;
    }
} // namespace ZEngine::Rendering
