#include <ZEngine/Cook/CookAdapter.h>
#include <ZEngine/Cook/CookInputPath.h>
#include <ZEngine/Cook/CookObject.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/UTF8.h>
#include <algorithm>
#include <charconv>
#include <limits>
#include <new>
#include <utility>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        constexpr CookAssetFormat Formats[] = {
            {CookAssetKind::EnvironmentMap,    "zenvmap-v2-le64",    "zenvmap", 1},
            {      CookAssetKind::Material, "zematerial-v1-json", "zematerial", 1},
            {          CookAssetKind::Mesh,     "zemesh-v1-le64",     "zemesh", 1},
            {       CookAssetKind::Texture,          "image-png",        "png", 1},
            {       CookAssetKind::Texture,         "image-jpeg",        "jpg", 1},
            {       CookAssetKind::Texture,          "image-bmp",        "bmp", 1},
            {       CookAssetKind::Texture,          "image-tga",        "tga", 1},
            {       CookAssetKind::Texture,          "image-gif",        "gif", 1},
            {       CookAssetKind::Texture,          "image-psd",        "psd", 1},
            {       CookAssetKind::Texture,          "image-pic",        "pic", 1},
        };

        Core::Containers::StringView CopyDependencyText(char*& cursor, Core::Containers::StringView text)
        {
            if (!text.size())
                return {};
            Helpers::secure_memcpy(cursor, text.size() + 1, text.data(), text.size());
            cursor[text.size()] = '\0';
            const Core::Containers::StringView result(cursor, text.size());
            cursor += text.size() + 1;
            return result;
        }

        VFS::VFSResult<void> DependencyFailure(const CookDependency& dependency, CookDiagnostics* diagnostics, CookDiagnosticCode code, VFS::VFSError error)
        {
            if (auto* entry = diagnostics ? diagnostics->Emplace(CookStage::Discover, code, error) : nullptr)
            {
                entry->Asset = dependency.Identity;
                (void) entry->SetKeys({}, dependency.Slot);
                (void) entry->SetMessage("Dependency identity and original path must match one frozen catalog artifact.");
            }
            return VFS::VFSResult<void>::Fail(CookDiagnosticError(code, error));
        }

        CookDiagnosticCode DependencyErrorCode(VFS::VFSError error)
        {
            switch (error)
            {
                case VFS::VFSError::NotFound:
                    return CookDiagnosticCode::MissingDependency;
                case VFS::VFSError::SizeLimitExceeded:
                    return CookDiagnosticCode::BudgetExceeded;
                case VFS::VFSError::OutOfMemory:
                    return CookDiagnosticCode::AllocationFailed;
                case VFS::VFSError::InvalidPath:
                    return CookDiagnosticCode::InvalidPath;
                case VFS::VFSError::Corrupted:
                    return CookDiagnosticCode::InvalidAsset;
                case VFS::VFSError::AlreadyExists:
                    return CookDiagnosticCode::IdentityConflict;
                case VFS::VFSError::Unsupported:
                    return CookDiagnosticCode::UnsupportedBackend;
                default:
                    return CookDiagnosticCode::IOFailure;
            }
        }
    } // namespace

    bool CookDependencySlotBefore(Core::Containers::StringView a, Core::Containers::StringView b) noexcept
    {
        if (!a.size() || !b.size())
            return a.size() < b.size();
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char left, char right) { return static_cast<uint8_t>(left) < static_cast<uint8_t>(right); });
    }

    bool IsCookDependencySlotValid(CookAssetKind parent, CookAssetKind child, Core::Containers::StringView slot) noexcept
    {
        if (!slot.data())
            return false;
        if (parent == CookAssetKind::Material)
            return child == CookAssetKind::Texture && (slot == "albedo" || slot == "emissive" || slot == "normal" || slot == "opacity" || slot == "specular");
        if (parent != CookAssetKind::Mesh || child != CookAssetKind::Material || slot.size() <= 19 || Core::Containers::StringView(slot.data(), 10) != "submeshes/" || Core::Containers::StringView(slot.data() + slot.size() - 9, 9) != "/material")
            return false;
        const Core::Containers::StringView number(slot.data() + 10, slot.size() - 19);
        if (number.size() > 1 && number[0] == '0')
            return false;
        uint32_t   index  = 0;
        const auto parsed = std::from_chars(number.data(), number.data() + number.size(), index);
        return parsed.ec == std::errc{} && parsed.ptr == number.data() + number.size();
    }

    Core::Containers::ArrayView<const CookAssetFormat> CookAssetFormats() noexcept
    {
        return Formats;
    }

    const CookAssetFormat* FindCookAssetFormat(Core::Containers::StringView key) noexcept
    {
        for (const auto& format : Formats)
            if (key == format.Key)
                return &format;
        return nullptr;
    }

    bool IsCookObjectDescriptorValid(const CookAssetFormat& format, const VFS::VFSRelativePath& path, const Digest256& digest, uint64_t size) noexcept
    {
        const auto* canonical = FindCookAssetFormat(format.Key);
        return canonical && canonical->Kind == format.Kind && canonical->Revision == format.Revision && Core::Containers::StringView(format.Extension) == canonical->Extension && IsCookObjectPath(path, digest, size, canonical->Extension);
    }

    CookAssetInspection::~CookAssetInspection()
    {
        Reset();
    }

    CookAssetInspection::CookAssetInspection(CookAssetInspection&& other) noexcept
    {
        *this = std::move(other);
    }

    CookAssetInspection& CookAssetInspection::operator=(CookAssetInspection&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_allocator    = std::exchange(other.m_allocator, nullptr);
            m_dependencies = std::exchange(other.m_dependencies, nullptr);
            m_count        = std::exchange(other.m_count, 0);
            m_format       = std::exchange(other.m_format, nullptr);
            m_local_key    = std::exchange(other.m_local_key, {});
            m_metadata_key = std::exchange(other.m_metadata_key, {});
        }
        return *this;
    }

    void CookAssetInspection::Reset() noexcept
    {
        if (m_dependencies)
        {
            for (size_t i = 0; i < m_count; ++i)
                m_dependencies[i].~CookDependency();
            m_allocator->Free(m_dependencies);
        }
        m_allocator    = nullptr;
        m_dependencies = nullptr;
        m_count        = 0;
        m_format       = nullptr;
        m_local_key    = {};
        m_metadata_key = {};
    }

    VFS::VFSResult<void> CookAssetInspection::Assign(cstring format_key, const Digest256& local_key, const Digest256& metadata_key, Core::Containers::ArrayView<const CookDependency> dependencies, ICookAllocator& allocator)
    {
        const auto* format = FindCookAssetFormat(format_key);
        if (!format || (dependencies.size() && !dependencies.data()))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        if (dependencies.size() > std::numeric_limits<uint32_t>::max() || dependencies.size() > SIZE_MAX / sizeof(CookDependency))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        size_t bytes = dependencies.size() * sizeof(CookDependency);
        for (size_t i = 0; i < dependencies.size(); ++i)
        {
            const auto& dependency = dependencies[i];
            if (!dependency.Identity.IsValid() || !dependency.Slot.size() || dependency.Slot.size() >= 128 || !Helpers::IsValidUTF8(dependency.Slot.data(), dependency.Slot.size(), false) || (i && !CookDependencySlotBefore(dependencies[i - 1].Slot, dependency.Slot)))
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            if (dependency.SourcePath.size() && ParseCookInputPath(dependency.SourcePath).Failed())
                return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
            const size_t text_bytes = dependency.Slot.size() + 1 + (dependency.SourcePath.size() ? dependency.SourcePath.size() + 1 : 0);
            if (text_bytes > SIZE_MAX - bytes)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            bytes += text_bytes;
        }
        CookAssetInspection pending = {};
        if (bytes)
        {
            const auto allocation = allocator.Allocate(bytes, alignof(CookDependency), CookMemoryCategory::Codecs);
            if (allocation.Failed())
                return VFS::VFSResult<void>::Fail(allocation.Error());
            if (!allocation.Value())
                return VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
            if (reinterpret_cast<uintptr_t>(allocation.Value()) % alignof(CookDependency))
            {
                allocator.Free(allocation.Value());
                return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
            }
            pending.m_allocator    = &allocator;
            pending.m_dependencies = static_cast<CookDependency*>(allocation.Value());
            char* cursor           = reinterpret_cast<char*>(pending.m_dependencies + dependencies.size());
            for (const auto& dependency : dependencies)
            {
                auto* stored       = new (pending.m_dependencies + pending.m_count++) CookDependency{};
                stored->Identity   = dependency.Identity;
                stored->Slot       = CopyDependencyText(cursor, dependency.Slot);
                stored->SourcePath = CopyDependencyText(cursor, dependency.SourcePath);
            }
        }
        pending.m_format       = format->Key;
        pending.m_local_key    = local_key;
        pending.m_metadata_key = metadata_key;
        *this                  = std::move(pending);
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookAdapterRegistry::Initialize(Core::Containers::ArrayView<const CookAdapterFns> entries)
    {
        if (m_initialized || entries.size() != m_entries.size() || !entries.data())
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        Core::Containers::FixedArray<CookAdapterFns, 4> pending = {};
        for (const auto& entry : entries)
        {
            if (!CookAssetKindKey(entry.Kind) || !entry.Revision || !entry.Inspect || !entry.Encode)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
            const auto index = static_cast<size_t>(entry.Kind) - 1;
            if (pending[index].Kind != CookAssetKind::Unknown)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::AlreadyExists);
            pending[index] = entry;
        }
        m_entries     = pending;
        m_initialized = true;
        return VFS::VFSResult<void>::Ok();
    }

    const CookAdapterFns* CookAdapterRegistry::Find(CookAssetKind kind) const noexcept
    {
        const auto value = static_cast<size_t>(kind);
        return m_initialized && value && value <= m_entries.size() ? &m_entries[value - 1] : nullptr;
    }

    VFS::VFSResult<void> ResolveCookDependency(CookInputLease& lease, const CookDependency& dependency, CookAssetCatalogEntry* output, CookDiagnostics* diagnostics)
    {
        if (!output || lease.State() != CookInputState::Active || !dependency.Identity.IsValid() || !dependency.Slot.size() || dependency.Slot.size() >= 128 || !Helpers::IsValidUTF8(dependency.Slot.data(), dependency.Slot.size(), false))
            return DependencyFailure(dependency, diagnostics, CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath);
        VFS::VFSRelativePath logical = {};
        if (dependency.SourcePath.size())
        {
            const auto parsed = ParseCookInputPath(dependency.SourcePath);
            if (parsed.Failed())
                return DependencyFailure(dependency, diagnostics, CookDiagnosticCode::InvalidPath, parsed.Error());
            logical = parsed.Value().Logical;
            if (parsed.Value().Context.IsValid())
            {
                const auto mapped = lease.MapContextPath(parsed.Value().Context, &logical, diagnostics);
                if (mapped.Failed())
                    return DependencyFailure(dependency, diagnostics, DependencyErrorCode(mapped.Error()), mapped.Error());
            }
        }
        CookAssetCatalogEntry pending = {};
        const auto            found   = lease.FindAsset(dependency.Identity, &pending, diagnostics);
        if (found.Failed())
            return DependencyFailure(dependency, diagnostics, DependencyErrorCode(found.Error()), found.Error());
        if (logical.IsValid() && pending.Artifact != logical)
            return DependencyFailure(dependency, diagnostics, CookDiagnosticCode::IdentityConflict, VFS::VFSError::Corrupted);
        *output = pending;
        return VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Cook
