#include <ZEngine/Importers/TextureImporter.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/ZEngineDef.h>

// stb_image implementation is defined once in RenderResourceManager.cpp.
#include <stb/stb_image.h>

namespace ZEngine::Importers
{
    void TextureImporter::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        arena->CreateSubArena(ZKilo(512), &Arena);
    }

    bool TextureImporter::CanImport(const char* extension) const
    {
        if (!extension)
            return false;
        return Helpers::secure_strcmp(extension, "png") == 0 || Helpers::secure_strcmp(extension, "jpg") == 0 || Helpers::secure_strcmp(extension, "jpeg") == 0 || Helpers::secure_strcmp(extension, "bmp") == 0 || Helpers::secure_strcmp(extension, "tga") == 0 || Helpers::secure_strcmp(extension, "gif") == 0 || Helpers::secure_strcmp(extension, "psd") == 0 || Helpers::secure_strcmp(extension, "pic") == 0;
    }

    Core::VFS::VFSResult<void> TextureImporter::Import(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta)
    {
        (void) ctx;

        // Resolve to native path — stbi_info works on the filesystem, not the VFS.
        char native[MAX_FILE_PATH_COUNT] = {};
        path.ToNative(native, sizeof(native));

        int w = 0, h = 0, ch = 0;
        if (!stbi_info(native, &w, &h, &ch))
        {
            ZENGINE_CORE_ERROR("TextureImporter: failed to probe '{}': {}", native, stbi_failure_reason())
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::IOError);
        }

        // AssetManager::IngestTexture resolves against CurrentWorkingSpacePath itself, so it
        // takes the project-relative VFS path, not the native one probed above.
        Core::Containers::String vfs_path = {};
        vfs_path.init(&Arena, path.CStr());
        Managers::AssetManager::IngestTexture(meta.AssetUUID, vfs_path);

        // IngestTexture copies the path into its own arena before returning, so vfs_path
        // doesn't need to survive past this point — reclaim it so Arena is reused, not
        // consumed, across repeated Import() calls (matches AssimpImporter/GltfImporter).
        Arena.Clear();
        return Core::VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Importers
