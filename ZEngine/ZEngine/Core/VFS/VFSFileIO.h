#pragma once
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <uuid.h>

namespace ZEngine::Core::VFS
{
    // Material identity extraction uses a JSON parser that may temporarily retain one
    // string token. Keep that bounded without imposing a limit on mesh assets.
    inline constexpr uint64_t            MATERIAL_IDENTITY_MAX_FILE_SIZE = 1024ULL * 1024ULL;

    // Read only the embedded identity; material JSON is streamed without an arena or DOM.
    [[nodiscard]] VFSResult<uuids::uuid> ReadEmbeddedAssetUUID(IVFSContext& ctx, const VFSPath& path);

    // Publish using a unique temporary sibling on the same mount/filesystem.
    [[nodiscard]] VFSResult<void>        WriteFileAtomically(IVFSContext& ctx, const VFSPath& path, Containers::ArrayView<const uint8_t> data);
} // namespace ZEngine::Core::VFS
