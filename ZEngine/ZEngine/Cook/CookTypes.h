#pragma once
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/VFS/VFSRelativePath.h>
#include <uuid.h>

namespace ZEngine::Cook
{
    enum class CookTarget : uint8_t
    {
        Invalid           = 0,
        DesktopVulkanLE64 = 1,
    };

    enum class CookAssetKind : uint8_t
    {
        Unknown        = 0,
        EnvironmentMap = 1,
        Material       = 2,
        Mesh           = 3,
        Texture        = 4,
    };

    [[nodiscard]] cstring       CookTargetKey(CookTarget target) noexcept;
    [[nodiscard]] cstring       CookAssetKindKey(CookAssetKind kind) noexcept;
    [[nodiscard]] CookAssetKind ParseCookAssetKind(Core::Containers::StringView key) noexcept;
    [[nodiscard]] bool          IsCookRootKey(Core::Containers::StringView key) noexcept;
    [[nodiscard]] bool          IsCookLogicalPath(const Core::VFS::VFSRelativePath& path) noexcept;

    struct CookAssetIdentity
    {
        CookAssetKind      Kind = CookAssetKind::Unknown;
        uuids::uuid        UUID = {};

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool operator==(const CookAssetIdentity&) const = default;
        // Canonical wire-key/UUID-byte order, never native enum representation.
        [[nodiscard]] bool operator<(const CookAssetIdentity& other) const noexcept;
    };
} // namespace ZEngine::Cook
