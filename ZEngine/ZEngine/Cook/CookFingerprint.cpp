#include <ZEngine/Cook/CookFingerprint.h>
#include <ZEngine/Cook/CookMemory.h>
#include <ZEngine/Core/Serialization/CanonicalJSONWriter.h>
#include <ZEngine/Core/VFS/Meta/MetaFileData.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/UTF8.h>
#include <ZEngine/Importers/MaterialAssetDocument.h>
#include <cstring>
#include <limits>

namespace ZEngine::Cook
{
    namespace
    {
        Core::VFS::VFSResult<void> HashCanonicalBytes(void* context, Core::Containers::ArrayView<const uint8_t> bytes)
        {
            return static_cast<Core::Hash::SHA256*>(context)->Update(bytes);
        }

        template <size_t Capacity>
        bool ValidMetadataText(const char (&bytes)[Capacity])
        {
            const auto* end = static_cast<cstring>(std::memchr(bytes, '\0', Capacity));
            return end && Helpers::IsValidUTF8(bytes, static_cast<size_t>(end - bytes));
        }
    } // namespace

    Core::VFS::VFSResult<void> ComputeCookMetadataKey(ICookAllocator& allocator, const Core::VFS::StrictMetaFileData& metadata, Digest256* output)
    {
        if (!output)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        const auto&        data     = metadata.Data;
        constexpr uint32_t required = static_cast<uint32_t>(Core::VFS::MetaFileField::UUID) | static_cast<uint32_t>(Core::VFS::MetaFileField::Importer) | static_cast<uint32_t>(Core::VFS::MetaFileField::Settings);
        if ((metadata.Fields & required) != required || data.AssetUUID.is_nil() || !ValidMetadataText(data.ImporterName) || !data.ImporterName[0])
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Corrupted);
        }
        if (data.SettingsCount > Core::VFS::META_MAX_SETTINGS)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        }
        for (uint32_t i = 0; i < data.SettingsCount; ++i)
        {
            if (!ValidMetadataText(data.Settings[i].Key) || !ValidMetadataText(data.Settings[i].Value))
            {
                return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Corrupted);
            }
        }
        CookMemoryCharge fixed   = {};
        const auto       charged = allocator.TryCharge(sizeof(Core::Containers::FixedArray<uint8_t, Core::VFS::META_MAX_SETTINGS>) + Core::Hash::SHA256::FixedWorkingBytes() + sizeof(Core::Serialization::CanonicalJSONWriter) + sizeof(fixed), CookMemoryCategory::Codecs, &fixed);
        if (charged.Failed())
        {
            return charged;
        }
        Core::Containers::FixedArray<uint8_t, Core::VFS::META_MAX_SETTINGS> order = {};
        static_assert(Core::VFS::META_MAX_SETTINGS <= uint32_t(UINT8_MAX) + 1);
        // At most 32 keys: insert indices and reject equal keys in one pass.
        // Keep the caller's records untouched and avoid recursive sort helpers;
        // the worst case is 496 comparisons/shifts, with no extra buffer.
        for (uint32_t i = 0; i < data.SettingsCount; ++i)
        {
            uint32_t position = i;
            while (position)
            {
                const auto compared = Helpers::secure_strcmp(data.Settings[order[position - 1]].Key, data.Settings[i].Key);
                if (!compared)
                {
                    return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Corrupted);
                }
                if (compared < 0)
                {
                    break;
                }
                order[position] = order[position - 1];
                --position;
            }
            order[position] = static_cast<uint8_t>(i);
        }
        Core::Hash::SHA256                       hash = {};
        Core::Serialization::CanonicalJSONWriter writer(HashCanonicalBytes, &hash);
        if (!writer.Raw("{\"importer\":") || !writer.String(data.ImporterName) || !writer.Raw(",\"settings\":{"))
        {
            return Core::VFS::VFSResult<void>::Fail(writer.Error());
        }
        for (uint32_t i = 0; i < data.SettingsCount; ++i)
        {
            const auto& setting = data.Settings[order[i]];
            if ((i && !writer.Raw(",")) || !writer.String(setting.Key) || !writer.Raw(":") || !writer.String(setting.Value))
            {
                return Core::VFS::VFSResult<void>::Fail(writer.Error());
            }
        }
        if (!writer.Raw("}}\n"))
        {
            return Core::VFS::VFSResult<void>::Fail(writer.Error());
        }
        return hash.Finalize(output);
    }

    Core::VFS::VFSResult<void> ComputeCookMaterialContentKey(ICookAllocator& allocator, const Importers::AssetCodec::MaterialAssetDocument& material, Digest256* output)
    {
        if (!output)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        CookMemoryCharge fixed   = {};
        const auto       charged = allocator.TryCharge(Core::Hash::SHA256::FixedWorkingBytes() + sizeof(Core::Serialization::CanonicalJSONWriter) + sizeof(fixed), CookMemoryCategory::Codecs, &fixed);
        if (charged.Failed())
        {
            return charged;
        }
        Core::Hash::SHA256 hash    = {};
        const auto         emitted = Importers::AssetCodec::WriteCanonicalMaterialSemanticProjection(material, HashCanonicalBytes, &hash);
        return emitted.Failed() ? emitted : hash.Finalize(output);
    }

    void CookFingerprint::Reset() noexcept
    {
        m_hash.Reset();
        m_error = Core::VFS::VFSError::OK;
    }

    Core::VFS::VFSResult<void> CookFingerprint::Append(Core::Containers::ArrayView<const uint8_t> bytes) noexcept
    {
        if (m_error == Core::VFS::VFSError::OK)
        {
            m_error = m_hash.Update(bytes).Error();
        }
        return m_error == Core::VFS::VFSError::OK ? Core::VFS::VFSResult<void>::Ok() : Core::VFS::VFSResult<void>::Fail(m_error);
    }

    Core::VFS::VFSResult<void> CookFingerprint::UnsignedLE(uint64_t value, size_t bytes) noexcept
    {
        for (size_t i = 0; i < bytes; ++i)
        {
            m_encoded[i] = static_cast<uint8_t>(value >> (i * 8));
        }
        return Append({m_encoded.data(), bytes});
    }

    Core::VFS::VFSResult<void> CookFingerprint::UInt32(uint32_t value) noexcept
    {
        return UnsignedLE(value, sizeof(value));
    }

    Core::VFS::VFSResult<void> CookFingerprint::UInt64(uint64_t value) noexcept
    {
        return UnsignedLE(value, sizeof(value));
    }

    Core::VFS::VFSResult<void> CookFingerprint::String(Core::Containers::StringView value) noexcept
    {
        if (m_error != Core::VFS::VFSError::OK)
        {
            return Core::VFS::VFSResult<void>::Fail(m_error);
        }
        if (value.size() > std::numeric_limits<uint32_t>::max())
        {
            m_error = Core::VFS::VFSError::SizeLimitExceeded;
        }
        else if (value.size() && !value.data())
        {
            m_error = Core::VFS::VFSError::InvalidPath;
        }
        if (m_error != Core::VFS::VFSError::OK)
        {
            return Core::VFS::VFSResult<void>::Fail(m_error);
        }
        const auto prefix = UInt32(static_cast<uint32_t>(value.size()));
        return prefix.Failed() ? prefix : Append({reinterpret_cast<const uint8_t*>(value.data()), value.size()});
    }

    Core::VFS::VFSResult<void> CookFingerprint::Digest(const Digest256& value) noexcept
    {
        return Append(value.Bytes);
    }

    Core::VFS::VFSResult<void> CookFingerprint::UUID(const uuids::uuid& value) noexcept
    {
        const auto bytes = value.as_bytes();
        return Append({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
    }

    Core::VFS::VFSResult<void> CookFingerprint::Finalize(Digest256* output) noexcept
    {
        if (m_error == Core::VFS::VFSError::OK)
        {
            m_error = m_hash.Finalize(output).Error();
        }
        return m_error == Core::VFS::VFSError::OK ? Core::VFS::VFSResult<void>::Ok() : Core::VFS::VFSResult<void>::Fail(m_error);
    }
} // namespace ZEngine::Cook
