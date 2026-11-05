#include <ZEngine/Cook/CookTypes.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <cstring>

namespace ZEngine::Cook
{
    namespace
    {
        struct KindDescriptor
        {
            CookAssetKind Kind = CookAssetKind::Unknown;
            cstring       Key  = nullptr;
        };
        constexpr Core::Containers::FixedArray<KindDescriptor, 4> Kinds = {
            {
             {CookAssetKind::EnvironmentMap, "environment-map"},
             {CookAssetKind::Material, "material"},
             {CookAssetKind::Mesh, "mesh"},
             {CookAssetKind::Texture, "texture"},
             }
        };
    } // namespace

    cstring CookTargetKey(CookTarget target) noexcept
    {
        return target == CookTarget::DesktopVulkanLE64 ? "desktop-vulkan-le64" : nullptr;
    }

    cstring CookAssetKindKey(CookAssetKind kind) noexcept
    {
        for (const auto& descriptor : Kinds)
        {
            if (descriptor.Kind == kind)
            {
                return descriptor.Key;
            }
        }
        return nullptr;
    }

    CookAssetKind ParseCookAssetKind(Core::Containers::StringView key) noexcept
    {
        for (const auto& descriptor : Kinds)
        {
            if (key == descriptor.Key)
            {
                return descriptor.Kind;
            }
        }
        return CookAssetKind::Unknown;
    }

    bool IsCookRootKey(Core::Containers::StringView key) noexcept
    {
        if (!key.data() || !key.size() || key.size() > 32 || key.data()[0] < 'a' || key.data()[0] > 'z')
        {
            return false;
        }
        for (size_t i = 1; i < key.size(); ++i)
        {
            const auto value = key.data()[i];
            if ((value < 'a' || value > 'z') && (value < '0' || value > '9') && value != '_' && value != '-')
            {
                return false;
            }
        }
        return true;
    }

    bool IsCookLogicalPath(const Core::VFS::VFSRelativePath& path) noexcept
    {
        if (!path.IsValid() || path.ComponentCount() < 2)
        {
            return false;
        }
        size_t end = 0;
        while (end < path.Length() && path.CStr()[end] != '/')
        {
            ++end;
        }
        return IsCookRootKey({path.CStr(), end});
    }

    bool CookAssetIdentity::IsValid() const noexcept
    {
        return CookAssetKindKey(Kind) && !UUID.is_nil();
    }

    bool CookAssetIdentity::operator<(const CookAssetIdentity& other) const noexcept
    {
        const auto key = CookAssetKindKey(Kind), other_key = CookAssetKindKey(other.Kind);
        // Invalid values sort deterministically for validation, but are never
        // accepted as graph identities or serialized into a manifest.
        const int  order = std::strcmp(key ? key : "", other_key ? other_key : "");
        return order ? order < 0 : UUID < other.UUID;
    }
} // namespace ZEngine::Cook
