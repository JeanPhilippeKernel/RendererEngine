#pragma once
#include <ZEngine/Core/VFS/IVFSBackend.h>

namespace ZEngine::Core::VFS
{
    // One retained provider per initialized backend. No legacy wrapper-pool or
    // allocator borrow may cross this boundary.
    Helpers::Ref<IVFSRootProvider> CreateDiskRootProvider(cstring native_root, VFSBackendCaps caps);
#if defined(_WIN32)
    Helpers::Ref<IVFSRootProvider> CreateWindowsDiskRootProvider(cstring native_root, VFSBackendCaps caps);
#endif
} // namespace ZEngine::Core::VFS
