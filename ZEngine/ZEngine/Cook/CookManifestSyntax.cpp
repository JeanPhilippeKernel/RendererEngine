#include <ZEngine/Cook/CookManifestInternal.h>
#include <ZEngine/Core/Serialization/JSONReader.h>
#include <algorithm>
#include <charconv>
#include <new>

namespace ZEngine::Cook::Detail
{
    namespace VFS = Core::VFS;

    namespace
    {
        using StringView = Core::Containers::StringView;
        using ValueKind  = Core::Serialization::JSONValueKind;

        struct KeyEntry
        {
            uint32_t Object = 0;
            uint32_t Offset = 0;
            uint32_t Bytes  = 0;
        };

        enum class Role : uint8_t
        {
            None = 0,
            Root,
            Assets,
            Asset,
        };

        enum class Field : uint8_t
        {
            None = 0,
            Version,
            Recipe,
            Target,
            Format,
            AdapterRevision,
            Kind,
        };

        struct Probe
        {
            uint32_t Version               = 0;
            uint32_t Recipe                = 0;
            bool     InvalidNumericVersion = false;
            bool     UnsupportedFormat     = false;
            bool     UnsupportedTarget     = false;
            bool     UnsupportedRecipe     = false;
        };

        struct AssetProbe
        {
            const CookAssetFormat* Format              = nullptr;
            CookAssetKind          Kind                = CookAssetKind::Unknown;
            uint64_t               Revision            = 0;
            bool                   HasUnsignedRevision = false;
            bool                   RevisionOverflow    = false;
        };

        struct ContainerFrame
        {
            Role     Scope   = Role::None;
            bool     IsArray = false;
            uint32_t Object  = 0;
        };

        // Iterative semantic traversal. All container state is inline and part
        // of the principal sizeof(SyntaxPass) charge, not recursive call frames.
        // Borrow the typed reader's one decode buffer, not another 4 KiB array.
        class SyntaxPass
        {
        public:
            explicit SyntaxPass(Core::Containers::ArrayView<const uint8_t> bytes, CookTarget target, Core::Containers::ArrayView<char> buffer) : Reader(bytes), Target(target), Buffer(buffer) {}

            bool Run()
            {
                Role  role  = Role::Root;
                Field field = Field::None;
                for (;;)
                {
                    if (!Value(role, field))
                    {
                        return false;
                    }
                    bool next = false;
                    while (Depth)
                    {
                        const auto& frame = Frames[Depth - 1];
                        if (frame.IsArray)
                        {
                            if (Reader.Element())
                            {
                                role  = frame.Scope == Role::Assets ? Role::Asset : Role::None;
                                field = Field::None;
                                next  = true;
                                break;
                            }
                        }
                        else
                        {
                            StringView key = {};
                            if (Reader.Member(Buffer, &key))
                            {
                                if (!RecordKey(frame.Object, key))
                                {
                                    return false;
                                }
                                field = Property(frame.Scope, key);
                                role  = frame.Scope == Role::Root && key == "assets" ? Role::Assets : Role::None;
                                next  = true;
                                break;
                            }
                            if (frame.Scope == Role::Asset)
                            {
                                FinishAsset();
                            }
                        }
                        if (Reader.Error() != VFS::VFSError::OK)
                        {
                            return false;
                        }
                        --Depth;
                    }
                    if (!next)
                    {
                        return Reader.Finish();
                    }
                }
            }

            void Index(Core::Containers::ArrayView<const uint8_t> bytes, KeyEntry* keys, char* text, uint32_t key_capacity, uint32_t text_capacity)
            {
                Reader.Reset(bytes);
                Keys         = keys;
                Text         = text;
                KeyCapacity  = key_capacity;
                TextCapacity = text_capacity;
                KeyCount = TextBytes = 0;
                Header               = {};
                Asset                = {};
                Depth                = 0;
            }

            Core::Serialization::JSONReader Reader;
            CookTarget                      Target       = CookTarget::Invalid;
            KeyEntry*                       Keys         = nullptr;
            char*                           Text         = nullptr;
            uint32_t                        KeyCount     = 0;
            uint32_t                        TextBytes    = 0;
            uint32_t                        KeyCapacity  = 0;
            uint32_t                        TextCapacity = 0;
            Probe                           Header       = {};
            CookSortState<KeyEntry>         Sort         = {};

        private:
            static Field Property(Role role, StringView key)
            {
                if (role == Role::Root)
                {
                    return key == "manifest_version" ? Field::Version : key == "recipe_revision" ? Field::Recipe : key == "target" ? Field::Target : Field::None;
                }
                if (role == Role::Asset)
                {
                    return key == "format" ? Field::Format : key == "adapter_revision" ? Field::AdapterRevision : key == "kind" ? Field::Kind : Field::None;
                }
                return Field::None;
            }

            bool RecordKey(uint32_t object, StringView key)
            {
                if (KeyCount == UINT32_MAX || key.size() > UINT32_MAX - TextBytes || (Keys && (KeyCount == KeyCapacity || key.size() > TextCapacity - TextBytes)))
                {
                    return false;
                }
                if (Keys)
                {
                    new (Keys + KeyCount) KeyEntry{object, TextBytes, static_cast<uint32_t>(key.size())};
                    if (key.size())
                    {
                        Helpers::secure_memcpy(Text + TextBytes, key.size(), key.data(), key.size());
                    }
                }
                ++KeyCount;
                TextBytes += static_cast<uint32_t>(key.size());
                return true;
            }

            void FinishAsset()
            {
                if (!Asset.HasUnsignedRevision)
                {
                    return;
                }
                uint32_t supported = Asset.Format ? Asset.Format->Revision : 0;
                if (!Asset.Format)
                {
                    for (const auto& format : CookAssetFormats())
                    {
                        if (Asset.Kind == CookAssetKind::Unknown || format.Kind == Asset.Kind)
                        {
                            supported = std::max(supported, format.Revision);
                        }
                    }
                }
                Header.UnsupportedFormat |= Asset.RevisionOverflow || Asset.Revision > supported;
            }

            bool Value(Role role, Field field)
            {
                const auto kind = Reader.PeekValueKind();
                if (kind == ValueKind::Object)
                {
                    if (role == Role::Asset)
                    {
                        Asset = {};
                    }
                    const auto object = static_cast<uint32_t>(Reader.Offset());
                    if (!Reader.BeginObject() || Depth == Frames.size())
                    {
                        return false;
                    }
                    Frames[Depth++] = {role, false, object};
                    return true;
                }
                if (kind == ValueKind::Array)
                {
                    if (!Reader.BeginArray() || Depth == Frames.size())
                    {
                        return false;
                    }
                    Frames[Depth++] = {role, true, 0};
                    return true;
                }
                if (kind == ValueKind::String)
                {
                    StringView text = {};
                    if (!Reader.String(Buffer, &text))
                    {
                        return false;
                    }
                    if (field == Field::Target && text != CookTargetKey(Target))
                    {
                        Header.UnsupportedTarget = true;
                    }
                    if (field == Field::Format)
                    {
                        Asset.Format              = FindCookAssetFormat(text);
                        Header.UnsupportedFormat |= !Asset.Format;
                    }
                    if (field == Field::Kind)
                    {
                        Asset.Kind                = ParseCookAssetKind(text);
                        Header.UnsupportedFormat |= Asset.Kind == CookAssetKind::Unknown;
                    }
                    return true;
                }
                if (kind == ValueKind::Number)
                {
                    StringView text = {};
                    if (!Reader.Number(&text))
                    {
                        return false;
                    }
                    uint64_t   number           = 0;
                    const auto parsed           = std::from_chars(text.data(), text.data() + text.size(), number);
                    const bool valid            = parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && number <= UINT32_MAX;
                    bool       unsigned_integer = true;
                    for (char byte : text)
                    {
                        unsigned_integer &= byte >= '0' && byte <= '9';
                    }
                    // Oversized unsigned version integers are future versions,
                    // not permission to rebuild by overflowing a conversion.
                    const bool overflow = unsigned_integer && parsed.ec == std::errc::result_out_of_range;
                    if (field == Field::Version)
                    {
                        Header.Version = valid ? static_cast<uint32_t>(number) : 0;
                        for (char byte : text)
                        {
                            Header.InvalidNumericVersion |= byte == '.' || byte == 'e' || byte == 'E';
                        }
                    }
                    if (field == Field::Recipe)
                    {
                        Header.Recipe             = valid ? static_cast<uint32_t>(number) : 0;
                        Header.UnsupportedRecipe |= overflow || (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && number > CookRecipeRevision);
                    }
                    if (field == Field::AdapterRevision)
                    {
                        Asset.Revision            = number;
                        Asset.HasUnsignedRevision = overflow || (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size());
                        Asset.RevisionOverflow    = overflow;
                    }
                    return true;
                }
                if (kind == ValueKind::Boolean)
                {
                    bool ignored = false;
                    return Reader.Boolean(&ignored);
                }
                return kind == ValueKind::Null && Reader.Null();
            }

            Core::Containers::ArrayView<char>                Buffer = {};
            AssetProbe                                       Asset  = {};
            Core::Containers::FixedArray<ContainerFrame, 16> Frames = {};
            uint32_t                                         Depth  = 0;
        };
    } // namespace

    CookDiagnosticCode ManifestResourceCode(VFS::VFSError error) noexcept
    {
        switch (error)
        {
            case VFS::VFSError::SizeLimitExceeded:
                return CookDiagnosticCode::BudgetExceeded;
            case VFS::VFSError::Unsupported:
                return CookDiagnosticCode::UnsupportedBackend;
            case VFS::VFSError::OutOfMemory:
                return CookDiagnosticCode::AllocationFailed;
            case VFS::VFSError::InvalidPath:
                return CookDiagnosticCode::InvalidRequest;
            default:
                return CookDiagnosticCode::IOFailure;
        }
    }

    VFS::VFSError ManifestDiagnostic(CookDiagnostics* diagnostics, CookDiagnosticCode code, VFS::VFSError error, cstring message, CookSeverity severity)
    {
        if (auto* entry = diagnostics ? diagnostics->Emplace(CookStage::Setup, code, error, severity) : nullptr)
        {
            (void) entry->SetMessage(message);
        }
        return error;
    }

    VFS::VFSResult<CookManifestSyntaxHeader> CheckCookManifestSyntax(Core::Containers::ArrayView<const uint8_t> bytes, CookTarget target, ICookAllocator& allocator, CookDiagnostics* diagnostics, Core::Containers::ArrayView<char> buffer)
    {
        const auto fail = [&](CookDiagnosticCode code, VFS::VFSError error, cstring message) { return VFS::VFSResult<CookManifestSyntaxHeader>::Fail(ManifestDiagnostic(diagnostics, code, error, message)); };
        if (!buffer.data() || buffer.size() < 4097)
        {
            return fail(CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Manifest syntax checking requires the reader's complete string decode buffer.");
        }
        CookMemoryCharge fixed   = {};
        const auto       charged = allocator.TryCharge(sizeof(SyntaxPass) + sizeof(fixed), CookMemoryCategory::StagingManifest, &fixed);
        if (charged.Failed())
        {
            return fail(ManifestResourceCode(charged.Error()), charged.Error(), "Manifest syntax state exceeds the working budget or has no supported charge contract.");
        }
        SyntaxPass pass(bytes, target, buffer.first(4097));
        if (!pass.Run())
        {
            const auto error = pass.Reader.Error() == VFS::VFSError::SizeLimitExceeded ? VFS::VFSError::SizeLimitExceeded : VFS::VFSError::Corrupted;
            return fail(error == VFS::VFSError::SizeLimitExceeded ? CookDiagnosticCode::BudgetExceeded : CookDiagnosticCode::InvalidManifest, error, "Manifest JSON syntax, UTF-8 or bounded string/depth validation failed.");
        }
        const uint64_t required = uint64_t(pass.KeyCount) * sizeof(KeyEntry) + pass.TextBytes;
        if (required > SIZE_MAX)
        {
            return fail(CookDiagnosticCode::BudgetExceeded, VFS::VFSError::SizeLimitExceeded, "Manifest key index exceeds addressable memory.");
        }
        CookWorkspace workspace = {};
        if (required)
        {
            const auto allocation = workspace.Initialize(allocator, static_cast<size_t>(required), CookMemoryCategory::StagingManifest);
            if (allocation.Failed())
            {
                return fail(ManifestResourceCode(allocation.Error()), allocation.Error(), "Manifest key-index allocation failed.");
            }
            auto* block = static_cast<uint8_t*>(workspace.Arena()->AllocateNoZero(static_cast<size_t>(required), alignof(KeyEntry)));
            if (!block)
            {
                return fail(CookDiagnosticCode::BudgetExceeded, VFS::VFSError::SizeLimitExceeded, "Manifest key-index workspace is insufficient.");
            }
            const uint32_t key_count  = pass.KeyCount;
            const uint32_t text_bytes = pass.TextBytes;
            // Restart token state while reusing the one fixed decode buffer.
            pass.Index(bytes, reinterpret_cast<KeyEntry*>(block), reinterpret_cast<char*>(block + size_t(key_count) * sizeof(KeyEntry)), key_count, text_bytes);
            if (!pass.Run() || pass.KeyCount != key_count || pass.TextBytes != text_bytes)
            {
                return fail(CookDiagnosticCode::InvalidManifest, VFS::VFSError::Corrupted, "Manifest changed during syntax validation.");
            }
            const auto key = [&](const KeyEntry& entry) { return StringView(pass.Text + entry.Offset, entry.Bytes); };
            HeapSort<KeyEntry>({pass.Keys, key_count}, pass.Sort, [&](const KeyEntry& left, const KeyEntry& right) { return left.Object == right.Object ? CookDependencySlotBefore(key(left), key(right)) : left.Object < right.Object; });
            for (uint32_t i = 1; i < key_count; ++i)
            {
                if (pass.Keys[i - 1].Object == pass.Keys[i].Object && key(pass.Keys[i - 1]) == key(pass.Keys[i]))
                {
                    return fail(CookDiagnosticCode::InvalidManifest, VFS::VFSError::Corrupted, "Manifest contains duplicate decoded JSON keys.");
                }
            }
        }
        if (pass.Header.InvalidNumericVersion)
        {
            return fail(CookDiagnosticCode::InvalidManifest, VFS::VFSError::Corrupted, "Manifest version must use the exact unsigned integer JSON spelling.");
        }
        if (pass.Header.Version != CookManifestVersion)
        {
            return fail(CookDiagnosticCode::UnsupportedVersion, VFS::VFSError::Unsupported, "Manifest version is absent, invalid or unsupported.");
        }
        if (pass.Header.UnsupportedTarget)
        {
            return fail(CookDiagnosticCode::UnsupportedTarget, VFS::VFSError::Unsupported, "Manifest target does not match this cook.");
        }
        if (pass.Header.UnsupportedRecipe || pass.Header.UnsupportedFormat)
        {
            return fail(CookDiagnosticCode::UnsupportedVersion, VFS::VFSError::Unsupported, "Manifest requires a newer recipe or an unknown adapter/kind/format.");
        }
        return VFS::VFSResult<CookManifestSyntaxHeader>::Ok({pass.Header.Recipe});
    }
} // namespace ZEngine::Cook::Detail
