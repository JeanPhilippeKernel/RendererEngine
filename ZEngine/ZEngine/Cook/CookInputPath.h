#pragma once
#include <ZEngine/Cook/CookTypes.h>

namespace ZEngine::Cook
{
    struct CookInputPath
    {
        Core::VFS::VFSRelativePath Logical = {};
        Core::VFS::VFSPath         Context = {};
    };

    // Persisted input locations only, never native provenance. Validate the
    // original spelling before constructing a normalized context VFSPath.
    // Exactly one of Logical/Context is valid on success. Resolving Context
    // still requires the active input owner's unambiguous root mapping.
    [[nodiscard]] Core::VFS::VFSResult<CookInputPath> ParseCookInputPath(Core::Containers::StringView);
} // namespace ZEngine::Cook
