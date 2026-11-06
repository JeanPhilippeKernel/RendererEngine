#pragma once
#include <ZEngine/Cook/CookDigest.h>
#include <ZEngine/Core/VFS/VFSRelativePath.h>

namespace ZEngine::Cook
{
    inline constexpr cstring CookSceneObjectExtension = "zescenebin";

    struct CookObjectDescriptor
    {
        Core::VFS::VFSRelativePath Path                                          = {};
        Digest256                  Digest                                        = {};
        uint64_t                   Size                                          = 0;

        [[nodiscard]] bool         operator==(const CookObjectDescriptor&) const = default;
    };

    // Shared content-addressed path spelling, not format selection or file
    // verification. Callers supply a recognized format's lowercase extension.
    // No temporary string or allocation; failure preserves the destination.
    [[nodiscard]] Core::VFS::VFSResult<void> MakeCookObjectDescriptor(const Digest256&, uint64_t size, Core::Containers::StringView extension, CookObjectDescriptor*);
    [[nodiscard]] bool                       IsCookObjectPath(const Core::VFS::VFSRelativePath&, const Digest256&, uint64_t size, Core::Containers::StringView extension) noexcept;
    [[nodiscard]] inline bool                IsCookObjectDescriptorValid(const CookObjectDescriptor& object, Core::Containers::StringView extension) noexcept
    {
        return IsCookObjectPath(object.Path, object.Digest, object.Size, extension);
    }
} // namespace ZEngine::Cook
