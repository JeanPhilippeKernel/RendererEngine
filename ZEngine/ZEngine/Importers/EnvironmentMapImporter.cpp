#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/EnvironmentMapImporter.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/ZEngineDef.h>
#include <fmt/format.h>
#include <uuid.h>
#include <filesystem>

// stb_image implementation is defined once in RenderResourceManager.cpp.
#include <stb/stb_image.h>

using namespace ZEngine::Rendering::Buffers;

namespace ZEngine::Importers
{
    void EnvironmentMapImporter::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        arena->CreateSubArena(ZMega(32), &Arena);
    }

    bool EnvironmentMapImporter::CanImport(const char* extension) const
    {
        if (!extension)
            return false;
        return Helpers::secure_strcmp(extension, "hdr") == 0 || Helpers::secure_strcmp(extension, "exr") == 0;
    }

    Core::VFS::VFSResult<void> EnvironmentMapImporter::Import(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta)
    {
        // Resolve to native path — stb_image works on the filesystem, not the VFS.
        char native[MAX_FILE_PATH_COUNT] = {};
        path.ToNative(native, sizeof(native));

        int          width = 0, height = 0, channel = 0;
        const float* image_data = stbi_loadf(native, &width, &height, &channel, 4);
        if (!image_data)
        {
            ZENGINE_CORE_ERROR("EnvironmentMapImporter: failed to load '{}': {}", native, stbi_failure_reason())
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::IOError);
        }

        Core::Memory::TLSFSlab* slab = Helpers::GetWorkerSlab();
        Bitmap                  equirect(width, height, 4, BitmapFormat::FLOAT, image_data, slab);
        stbi_image_free(const_cast<float*>(image_data));

        Bitmap vertical_cross                    = Bitmap::EquirectangularMapToVerticalCross(equirect, slab);
        Bitmap cubemap                           = Bitmap::VerticalCrossToCubemap(vertical_cross, slab);

        // Write to project://_cache/envmaps/<uuid>.zenvmap via VFS.
        // Keyed by UUID — regenerable, gitignored, transparent to game code.
        char   vfs_path_buf[MAX_FILE_PATH_COUNT] = {};
        std::snprintf(vfs_path_buf, sizeof(vfs_path_buf), "/_cache/envmaps/%s.zenvmap", uuids::to_string(meta.AssetUUID).c_str());

        auto out_path_result = Core::VFS::VFSPath::Parse(vfs_path_buf);
        if (!out_path_result.Succeeded())
        {
            ZENGINE_CORE_ERROR("EnvironmentMapImporter: invalid output path '{}'", vfs_path_buf)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }

        // Ensure the cache directory exists
        auto cache_dir = Core::VFS::VFSPath::Parse("/_cache/envmaps").Value();
        ctx.CreateDir(cache_dir); // no-op if already exists

        auto write_result = AssetCodec::SerializeEnvironmentMapFileVFS(ctx, out_path_result.Value(), cubemap);
        if (write_result.Failed())
        {
            ZENGINE_CORE_ERROR("EnvironmentMapImporter: failed to write .zenvmap for '{}'", native)
            return write_result;
        }

        ZENGINE_CORE_INFO("EnvironmentMapImporter: cooked '{}' → '{}'", native, vfs_path_buf)
        return Core::VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Importers
