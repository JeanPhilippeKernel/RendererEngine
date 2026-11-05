#include <ZEngine/Core/Serialization/JSONReader.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Helpers/UTF8.h>
#include <ZEngine/Helpers/UUID.h>

namespace ZEngine::Core::VFS
{
    namespace
    {
        template <size_t Capacity>
        bool ReadMetaText(Serialization::JSONReader& reader, char (&buffer)[Capacity], bool nonempty = false)
        {
            Containers::StringView text = {};
            return reader.String(buffer, &text) && (!nonempty || text.size()) && Helpers::IsValidUTF8(text.data(), text.size());
        }

        VFSResult<void> ReadSettings(Serialization::JSONReader& reader, MetaFileData& data)
        {
            if (!reader.BeginArray())
            {
                return VFSResult<void>::Fail(reader.Error());
            }
            while (reader.Element())
            {
                if (data.SettingsCount == META_MAX_SETTINGS)
                {
                    return VFSResult<void>::Fail(VFSError::SizeLimitExceeded);
                }
                auto& setting = data.Settings[data.SettingsCount];
                if (!reader.BeginObject())
                {
                    return VFSResult<void>::Fail(reader.Error());
                }
                uint32_t               fields         = 0;
                char                   key_buffer[16] = {};
                Containers::StringView key            = {};
                while (reader.Member(key_buffer, &key))
                {
                    const uint32_t field = key == "key" ? 1 : key == "value" ? 2 : 0;
                    if (!field || (fields & field))
                    {
                        return VFSResult<void>::Fail(VFSError::Corrupted);
                    }
                    fields |= field;
                    if (!(field == 1 ? ReadMetaText(reader, setting.Key) : ReadMetaText(reader, setting.Value)))
                    {
                        return VFSResult<void>::Fail(reader.Error() == VFSError::OK ? VFSError::Corrupted : reader.Error());
                    }
                }
                if (reader.Error() != VFSError::OK)
                {
                    return VFSResult<void>::Fail(reader.Error());
                }
                if (fields != 3)
                {
                    return VFSResult<void>::Fail(VFSError::Corrupted);
                }
                // At most 32 fixed-capacity settings. Avoid a second allocation/
                // hash table for this small bounded duplicate check.
                for (uint32_t i = 0; i < data.SettingsCount; ++i)
                {
                    if (Helpers::secure_strcmp(data.Settings[i].Key, setting.Key) == 0)
                    {
                        return VFSResult<void>::Fail(VFSError::Corrupted);
                    }
                }
                ++data.SettingsCount;
            }
            return reader.Error() == VFSError::OK ? VFSResult<void>::Ok() : VFSResult<void>::Fail(reader.Error());
        }
    } // namespace

    VFSResult<void> MetaFileIO::ParseStrict(Containers::ArrayView<const uint8_t> bytes, StrictMetaFileData* output)
    {
        if (!output || (bytes.size() && !bytes.data()))
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        if (bytes.size() > MaxFileSize)
        {
            return VFSResult<void>::Fail(VFSError::SizeLimitExceeded);
        }
        StrictMetaFileData        candidate = {};
        Serialization::JSONReader reader(bytes);
        if (!reader.BeginObject())
        {
            return VFSResult<void>::Fail(reader.Error());
        }
        constexpr cstring      names[]        = {"uuid", "importer", "settings", "source_path", "source_hash", "import_time_ns", "artifact_path"};
        char                   key_buffer[32] = {};
        Containers::StringView key            = {};
        while (reader.Member(key_buffer, &key))
        {
            uint32_t index = 0;
            for (; index < sizeof(names) / sizeof(names[0]); ++index)
            {
                if (key == names[index])
                {
                    break;
                }
            }
            if (index == sizeof(names) / sizeof(names[0]))
            {
                return VFSResult<void>::Fail(VFSError::Corrupted);
            }
            const uint32_t field = uint32_t(1) << index;
            if (candidate.Fields & field)
            {
                return VFSResult<void>::Fail(VFSError::Corrupted);
            }
            candidate.Fields |= field;
            bool valid        = false;
            switch (static_cast<MetaFileField>(field))
            {
                case MetaFileField::UUID:
                {
                    char                   text[37] = {};
                    Containers::StringView uuid     = {};
                    valid                           = reader.String(text, &uuid) && Helpers::ParseCanonicalUUID(uuid.data(), uuid.size(), &candidate.Data.AssetUUID);
                    break;
                }
                case MetaFileField::Importer:
                    valid = ReadMetaText(reader, candidate.Data.ImporterName, true);
                    break;
                case MetaFileField::Settings:
                {
                    const auto settings = ReadSettings(reader, candidate.Data);
                    if (settings.Failed())
                    {
                        return settings;
                    }
                    valid = true;
                    break;
                }
                case MetaFileField::SourcePath:
                    valid = ReadMetaText(reader, candidate.Data.SourcePath);
                    break;
                case MetaFileField::SourceHash:
                    valid = reader.UInt64(&candidate.Data.SourceHash);
                    break;
                case MetaFileField::ImportTimeNs:
                    valid = reader.Int64(&candidate.Data.LastImportTimeNs);
                    break;
                case MetaFileField::ArtifactPath:
                    valid = ReadMetaText(reader, candidate.Data.ArtifactPath);
                    break;
                default:
                    break;
            }
            if (!valid)
            {
                return VFSResult<void>::Fail(reader.Error() == VFSError::OK ? VFSError::Corrupted : reader.Error());
            }
        }
        if (!reader.Finish())
        {
            return VFSResult<void>::Fail(reader.Error());
        }
        constexpr uint32_t required = static_cast<uint32_t>(MetaFileField::UUID) | static_cast<uint32_t>(MetaFileField::Importer) | static_cast<uint32_t>(MetaFileField::Settings);
        if ((candidate.Fields & required) != required)
        {
            return VFSResult<void>::Fail(VFSError::Corrupted);
        }
        *output = candidate;
        return VFSResult<void>::Ok();
    }
} // namespace ZEngine::Core::VFS
