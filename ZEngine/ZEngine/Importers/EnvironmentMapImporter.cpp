#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/EnvironmentMapImporter.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/ZEngineDef.h>
#include <uuid.h>
#include <cmath>
#include <cstdio>

// stb_image implementation is defined once in RenderResourceManager.cpp.
#include <stb/stb_image.h>

using namespace ZEngine::Rendering::Buffers;

namespace ZEngine::Importers
{
    namespace
    {
        void AddImportSetting(Core::VFS::MetaFileData& meta, const char* key, const char* value)
        {
            if (meta.SettingsCount >= Core::VFS::META_MAX_SETTINGS)
                return;
            Core::VFS::MetaKeyValuePair& setting = meta.Settings[meta.SettingsCount++];
            std::snprintf(setting.Key, sizeof(setting.Key), "%s", key);
            std::snprintf(setting.Value, sizeof(setting.Value), "%s", value);
        }
    } // namespace

    void EnvironmentMapImporter::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        arena->CreateSubArena(ZMega(32), &Arena);
    }

    bool EnvironmentMapImporter::CanImport(const char* extension) const
    {
        if (!extension)
            return false;
        // stb_image does not decode EXR. Do not advertise it until an EXR-capable
        // importer is implemented and covered by the same artifact contract.
        return Helpers::secure_strcmp(extension, "hdr") == 0;
    }

    bool EnvironmentMapImporter::IsSupportedEquirectangularSource(int width, int height, const float* rgba_pixels)
    {
        if (!rgba_pixels || width <= 0 || height <= 0 || width != height * 2 || width % 4 != 0)
            return false;

        const int face_size = width / 4;
        if (face_size <= 0 || face_size > static_cast<int>(AssetCodec::ENVIRONMENT_MAP_MAX_FACE_SIZE))
            return false;

        const size_t component_count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        for (size_t index = 0; index < component_count; ++index)
            if (!std::isfinite(rgba_pixels[index]) || rgba_pixels[index] < 0.0f)
                return false;
        return true;
    }

    bool EnvironmentMapImporter::BuildArtifactPath(const uuids::uuid& asset_uuid, char* out_path, size_t out_path_size)
    {
        if (asset_uuid.is_nil() || !out_path || out_path_size == 0)
            return false;
        const int written = std::snprintf(out_path, out_path_size, "/_cache/envmaps/%s.zenvmap", uuids::to_string(asset_uuid).c_str());
        return written > 0 && static_cast<size_t>(written) < out_path_size;
    }

    Core::VFS::VFSResult<void> EnvironmentMapImporter::Import(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta)
    {
        // stb_image works on the filesystem, while the source identity and cooked
        // artifact remain VFS paths. Resolve the source relative to its workspace.
        char        native[MAX_FILE_PATH_COUNT] = {};
        const char* working_space               = Managers::AssetManager::Instance() ? Managers::AssetManager::Instance()->CurrentWorkingSpacePath : "";
        if (working_space && working_space[0] != '\0')
            path.ResolveNative(working_space, native, sizeof(native));
        else
            path.ToNative(native, sizeof(native));

        int          width = 0, height = 0, channel = 0;
        const float* image_data = stbi_loadf(native, &width, &height, &channel, STBI_rgb_alpha);
        if (!image_data)
        {
            ZENGINE_CORE_ERROR("EnvironmentMapImporter: failed to load '{}': {}", native, stbi_failure_reason())
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::IOError);
        }

        if (!IsSupportedEquirectangularSource(width, height, image_data))
        {
            stbi_image_free(const_cast<float*>(image_data));
            ZENGINE_CORE_ERROR("EnvironmentMapImporter: '{}' must be a finite, non-negative 2:1 HDR equirectangular image with a face size no larger than {}", native, AssetCodec::ENVIRONMENT_MAP_MAX_FACE_SIZE)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }

        Core::Memory::TLSFSlab* slab     = Helpers::GetWorkerSlab();
        Bitmap                  equirect = Bitmap::FromData(width, height, 1, STBI_rgb_alpha, BitmapFormat::Float, BitmapType::Texture2D, image_data);
        stbi_image_free(const_cast<float*>(image_data));

        Bitmap cubemap                           = BitmapConvert::EquirectToCubemap(equirect, slab);

        // The cache is UUID keyed, regenerable, and never stored in scene data.
        char   vfs_path_buf[MAX_FILE_PATH_COUNT] = {};
        if (!BuildArtifactPath(meta.AssetUUID, vfs_path_buf, sizeof(vfs_path_buf)))
        {
            ZENGINE_CORE_ERROR("EnvironmentMapImporter: cannot build a cache path for '{}'", native)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }

        auto out_path_result = Core::VFS::VFSPath::Parse(vfs_path_buf);
        if (!out_path_result.Succeeded())
        {
            ZENGINE_CORE_ERROR("EnvironmentMapImporter: invalid output path '{}'", vfs_path_buf)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }

        // Ensure the cache directory exists
        auto       cache_dir               = Core::VFS::VFSPath::Parse("/_cache/envmaps").Value();
        const auto create_directory_result = ctx.CreateDir(cache_dir);
        if (create_directory_result.Failed() && create_directory_result.Error() != Core::VFS::VFSError::AlreadyExists)
        {
            ZENGINE_CORE_ERROR("EnvironmentMapImporter: cannot create the cache directory for '{}'", native)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::IOError);
        }

        const AssetCodec::EnvironmentMapCookMetadata cook_metadata = {.SourceHash = meta.SourceHash};
        auto                                         write_result  = AssetCodec::SerializeEnvironmentMapFileVFS(ctx, out_path_result.Value(), cubemap, cook_metadata);
        if (write_result.Failed())
        {
            ZENGINE_CORE_ERROR("EnvironmentMapImporter: failed to write .zenvmap for '{}'", native)
            return write_result;
        }

        // Keep the artifact metadata beside the stable source UUID. The render
        // path reads the cooked path and validates this source hash before upload.
        Core::VFS::MetaFileData cooked_meta = meta;
        std::snprintf(cooked_meta.ImporterName, sizeof(cooked_meta.ImporterName), "%s", "EnvironmentMapImporter");
        std::snprintf(cooked_meta.SourcePath, sizeof(cooked_meta.SourcePath), "%s", native);
        std::snprintf(cooked_meta.ArtifactPath, sizeof(cooked_meta.ArtifactPath), "%s", vfs_path_buf);
        cooked_meta.SettingsCount = 0;
        AddImportSetting(cooked_meta, "artifact_version", "2");
        AddImportSetting(cooked_meta, "pixel_format", "rgba32f");
        AddImportSetting(cooked_meta, "color_space", "linear_scene");
        AddImportSetting(cooked_meta, "orientation", "renderer_canonical_v1");
        AddImportSetting(cooked_meta, "mip_policy", "generate_on_gpu");
        AddImportSetting(cooked_meta, "exposure", "1.0");
        if (Core::VFS::MetaFileIO::Write(ctx, path, cooked_meta).Failed())
        {
            ZENGINE_CORE_ERROR("EnvironmentMapImporter: failed to write metadata for '{}'", native)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::IOError);
        }

        ZENGINE_CORE_INFO("EnvironmentMapImporter: cooked '{}' → '{}'", native, vfs_path_buf)
        return Core::VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Importers
