#pragma once
#include <ZEngine/Importers/IAssetImporter.h>

namespace ZEngine::Importers
{
    /// @brief Imports flat 2D raster textures (png/jpg/jpeg/bmp/tga/gif/psd/pic).
    /// @details Does not claim hdr/exr (EnvironmentMapImporter's domain) or ktx/ktx2
    ///          (not decodable by stb_image today).
    class TextureImporter : public IAssetImporter
    {
    public:
        TextureImporter()  = default;
        ~TextureImporter() = default;

        void                         Initialize(Core::Memory::ArenaAllocator* arena);

        // IAssetImporter
        bool                         CanImport(const char* extension) const override;
        Core::VFS::VFSResult<void>   Import(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta) override;

        Core::Memory::ArenaAllocator Arena = {};
    };
} // namespace ZEngine::Importers
