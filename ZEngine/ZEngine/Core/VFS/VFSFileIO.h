#pragma once
#include <ZEngine/Core/VFS/IVFSContext.h>

namespace ZEngine::Core::VFS
{
    // Publish using a unique temporary sibling on the same mount/filesystem.
    [[nodiscard]] VFSResult<void> WriteFileAtomically(IVFSContext& ctx, const VFSPath& path, Containers::ArrayView<const uint8_t> data);
} // namespace ZEngine::Core::VFS
