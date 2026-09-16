#pragma once
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/IAssetImporter.h>

namespace ZEngine::Importers
{
    // Imports .hdr equirectangular images, converts them to cubemaps,
    // and writes a .zenvmap cooked artifact. Registered with ImportCoordinator
    // so the standard Enqueue/Tick pipeline handles HDRI assets off the render thread.
    class EnvironmentMapImporter : public IAssetImporter
    {
    public:
        EnvironmentMapImporter()  = default;
        ~EnvironmentMapImporter() = default;

        void                         Initialize(Core::Memory::ArenaAllocator* arena);

        // IAssetImporter
        bool                         CanImport(const char* extension) const override;
        Core::VFS::VFSResult<void>   Import(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta) override;

        /// @brief Validates the source contract before conversion allocates its working buffers.
        [[nodiscard]] static bool    IsSupportedEquirectangularSource(int width, int height, const float* rgba_pixels);
        /// @brief Builds the cache-only VFS path for a stable source asset UUID.
        [[nodiscard]] static bool    BuildArtifactPath(const uuids::uuid& asset_uuid, char* out_path, size_t out_path_size);

        Core::Memory::ArenaAllocator Arena = {};
    };
} // namespace ZEngine::Importers
