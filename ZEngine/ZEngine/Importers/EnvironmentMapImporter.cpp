#include <Core/Coroutine.h>
#include <EnvironmentMapImporter.h>
#include <Helpers/ThreadPool.h>
#include <fmt/format.h>

// stb_image implementation is defined once in AsyncResourceLoader.cpp.
#include <stb/stb_image.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Buffers;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Importers
{
    std::future<void> EnvironmentMapImporter::ImportAsync(const char* filename, const ImportConfiguration& cfg)
    {
        ThreadPoolHelper::Submit([this, path = std::string(filename), cfg] {
            std::unique_lock l(m_mutex);

            Arena.Clear();
            auto                thread_local_arena = &Arena;
            ImportConfiguration config             = {};

            config.OutputWorkingSpacePath.init(thread_local_arena, cfg.OutputWorkingSpacePath.c_str());
            config.OutputAssetsPath.init(thread_local_arena, cfg.OutputAssetsPath.c_str());
            config.AssetName.init(thread_local_arena, cfg.AssetName.c_str());
            config.OutputAssetFile.init(thread_local_arena, cfg.OutputAssetFile.c_str());

            m_is_importing.store(true, std::memory_order_release);

            REPORT_LOG(Context, fmt::format("Decoding environment map: {}", path))

            int width = 0, height = 0, channel = 0;
            stbi_set_flip_vertically_on_load(1);
            const float* image_data = stbi_loadf(path.c_str(), &width, &height, &channel, 4);

            if (!image_data)
            {
                if (m_error_callback)
                {
                    m_error_callback(Context, fmt::format("Failed to decode '{}': {}", path, stbi_failure_reason()));
                }
                m_is_importing.store(false, std::memory_order_release);
                return;
            }

            if (m_progress_callback)
            {
                m_progress_callback(Context, 0.33f);
            }

            Bitmap equirect = {width, height, 4, BitmapFormat::FLOAT, image_data};
            stbi_image_free(const_cast<float*>(image_data));

            REPORT_LOG(Context, "Converting equirectangular map to cubemap faces...")

            Bitmap vertical_cross = Bitmap::EquirectangularMapToVerticalCross(equirect);
            Bitmap cubemap        = Bitmap::VerticalCrossToCubemap(vertical_cross);

            if (m_progress_callback)
            {
                m_progress_callback(Context, 0.75f);
            }

            REPORT_LOG(Context, fmt::format("Saving .zenvmap to: {}", config.OutputAssetsPath.c_str()))

            auto output = IAssetImporter::SerializeEnvironmentMapFile(cubemap, config);

            m_is_importing.store(false, std::memory_order_release);

            if (output.Type == AssetFileType::ENVIRONMENT_MAP)
            {
                if (m_progress_callback)
                {
                    m_progress_callback(Context, 1.0f);
                }

                Array<AssetImporterOutput> outputs = {};
                outputs.init(thread_local_arena, 1);
                outputs.push(output);

                if (m_complete_callback)
                {
                    m_complete_callback(Context, ArrayView{outputs});
                }
            }
            else
            {
                if (m_error_callback)
                {
                    m_error_callback(Context, fmt::format("Failed to write .zenvmap for '{}'", path));
                }
            }
        });

        co_return;
    }

} // namespace ZEngine::Importers
