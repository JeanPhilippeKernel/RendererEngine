#pragma once
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/Meta/MetaFileData.h>
#include <ZEngine/Core/VFS/VFSError.h>
#include <ZEngine/Core/VFS/VFSPath.h>

namespace ZEngine::Core::VFS
{
    struct MetaFileIO
    {
        // Covers all fixed-capacity fields, including worst-case JSON escaping.
        static constexpr size_t MaxFileSize = 64 * 1024;
        static_assert(MaxFileSize >= 6 * sizeof(MetaFileData) + 4096, "Metadata capacity must cover escaped fields and JSON formatting");

        // Returns the sidecar path for an asset: "/project/mesh.glb" -> "/project/mesh.glb.meta"
        static VFSResult<VFSPath>      MetaPathFor(const VFSPath& asset_path);

        // Reads the .meta sidecar from the VFS. Returns Fail if the file is absent or malformed.
        // Status on the returned MetaFileData is always ImportStatus::Unknown — caller sets it.
        static VFSResult<MetaFileData> Read(IVFSContext& ctx, const VFSPath& asset_path);

        // Serialises data to JSON and writes it atomically (write to .tmp, then rename).
        static VFSResult<void>         Write(IVFSContext& ctx, const VFSPath& asset_path, const MetaFileData& data);

        // High-level entry point for importers and the scanner:
        //   - No .meta or corrupt .meta  -> generate UUID, write, return New
        //   - .meta exists, hash matches -> return UpToDate (no write)
        //   - .meta exists, hash differs -> update hash + timestamp, write, return Stale
        // Valid existing identities are preserved; missing/corrupt cooked sidecars can use the embedded identity.
        // Access and size-limit errors are returned without changing the sidecar.
        static VFSResult<MetaFileData> GetOrCreate(IVFSContext& ctx, const VFSPath& asset_path, const char* importer_name, uint64_t current_hash, bool use_embedded_identity = false);

        // Computes the rapidhash-64 digest of the asset file content.
        static VFSResult<uint64_t>     ComputeHash(IVFSContext& ctx, const VFSPath& asset_path);
    };
} // namespace ZEngine::Core::VFS
