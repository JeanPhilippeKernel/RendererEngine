#include <ZEngine/Core/Serialization/JSONReader.h>
#include <ZEngine/Helpers/UTF8.h>
#include <ZEngine/Helpers/UUID.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/MaterialAssetDocument.h>
#include <cmath>
#include <limits>
#include <new>

namespace ZEngine::Importers::AssetCodec
{
    namespace VFS           = Core::VFS;
    namespace Serialization = Core::Serialization;

    namespace
    {
        constexpr cstring TextureSlots[] = {"albedo", "emissive", "normal", "opacity", "specular"};
        static_assert(sizeof(TextureSlots) / sizeof(TextureSlots[0]) == static_cast<size_t>(MaterialTextureSlot::Count));

        struct MaterialVectorField
        {
            Core::Containers::FixedArray<float, 4> MaterialAssetDocument::* Member = nullptr;
        };
        constexpr MaterialVectorField VectorFields[] = {
            {&MaterialAssetDocument::AmbientColor},
            {&MaterialAssetDocument::AlbedoColor},
            {&MaterialAssetDocument::EmissiveColor},
            {&MaterialAssetDocument::RoughnessColor},
            {&MaterialAssetDocument::SpecularColor},
            {&MaterialAssetDocument::Factors},
        };

        template <size_t Capacity>
        bool BoundedText(const char (&bytes)[Capacity], Core::Containers::StringView* output)
        {
            size_t count = 0;
            while (count < Capacity && bytes[count])
            {
                ++count;
            }
            if (count == Capacity || !Helpers::IsValidUTF8(bytes, count))
            {
                return false;
            }
            *output = {bytes, count};
            return true;
        }

        bool ValidTexture(const MaterialTextureReference& texture)
        {
            Core::Containers::StringView path = {};
            if (!BoundedText(texture.Path, &path))
            {
                return false;
            }
            if (texture.UUID.is_nil())
            {
                return path.empty();
            }
            // Imported paths retain a context-leading '/', cooked paths do not.
            // Strictly validate original bytes before any VFSPath conversion.
            const size_t start = !path.empty() && path[0] == '/' ? 1 : 0;
            return VFS::VFSRelativePath::Validate(path.data() + start, path.size() - start).Succeeded();
        }

        template <size_t Count>
        uint32_t FieldIndex(Core::Containers::StringView key, const cstring (&names)[Count])
        {
            for (uint32_t i = 0; i < Count; ++i)
            {
                if (key == names[i])
                {
                    return i;
                }
            }
            return static_cast<uint32_t>(Count);
        }

        VFS::VFSError ReaderError(const Serialization::JSONReader& reader)
        {
            return reader.Error() == VFS::VFSError::OK ? VFS::VFSError::Corrupted : reader.Error();
        }

        template <size_t Capacity>
        bool ReadText(Serialization::JSONReader& reader, char (&bytes)[Capacity], bool nonempty = false)
        {
            Core::Containers::StringView text = {};
            return reader.String(bytes, &text) && (!nonempty || text.size()) && Helpers::IsValidUTF8(text.data(), text.size());
        }

        bool ReadUUID(Serialization::JSONReader& reader, Core::Containers::ArrayView<char> scratch, uuids::uuid* uuid, bool allow_nil)
        {
            Core::Containers::StringView text = {};
            return reader.String(scratch.first(37), &text) && Helpers::ParseCanonicalUUID(text.data(), text.size(), uuid, allow_nil);
        }

        bool ReadVector(Serialization::JSONReader& reader, Core::Containers::FixedArray<float, 4>& vector, bool source_compatible)
        {
            if (!reader.BeginArray())
            {
                return false;
            }
            size_t count = 0;
            while (reader.Element())
            {
                float ignored = 0.f;
                if ((!source_compatible && count == vector.size()) || !reader.Float32(count < vector.size() ? &vector[count] : &ignored))
                {
                    return false;
                }
                ++count;
            }
            return (source_compatible || count == vector.size()) && reader.Error() == VFS::VFSError::OK;
        }

        bool ReadTexture(Serialization::JSONReader& reader, MaterialTextureReference& texture, Core::Containers::ArrayView<char> scratch, bool source_compatible)
        {
            if (!reader.BeginObject())
            {
                return false;
            }
            constexpr cstring            names[] = {"uuid", "path"};
            Core::Containers::StringView key     = {};
            uint32_t                     fields  = 0;
            while (reader.Member(scratch, &key))
            {
                const uint32_t index = FieldIndex(key, names);
                if (source_compatible && index == 2)
                {
                    if (!reader.SkipValue(scratch))
                    {
                        return false;
                    }
                    continue;
                }
                if (index == 2 || (fields & (1u << index)))
                {
                    return false;
                }
                fields |= 1u << index;
                if (!(index == 0 ? ReadUUID(reader, scratch, &texture.UUID, true) : ReadText(reader, texture.Path)))
                {
                    return false;
                }
            }
            if (reader.Error() != VFS::VFSError::OK || (!source_compatible && fields != 3))
            {
                return false;
            }
            return ValidTexture(texture);
        }

        bool ReadTextures(Serialization::JSONReader& reader, MaterialAssetDocument& document, Core::Containers::ArrayView<char> scratch, bool source_compatible)
        {
            if (!reader.BeginObject())
            {
                return false;
            }
            Core::Containers::StringView key    = {};
            uint32_t                     fields = 0;
            while (reader.Member(scratch, &key))
            {
                const uint32_t index = FieldIndex(key, TextureSlots);
                if (source_compatible && index == static_cast<uint32_t>(MaterialTextureSlot::Count))
                {
                    if (!reader.SkipValue(scratch))
                    {
                        return false;
                    }
                    continue;
                }
                if (index == static_cast<uint32_t>(MaterialTextureSlot::Count) || (fields & (1u << index)))
                {
                    return false;
                }
                fields |= 1u << index;
                if (!ReadTexture(reader, document.Textures[index], scratch, source_compatible))
                {
                    return false;
                }
            }
            return reader.Error() == VFS::VFSError::OK && (source_compatible || fields == (1u << static_cast<uint32_t>(MaterialTextureSlot::Count)) - 1);
        }

        bool ReadProvenance(Serialization::JSONReader& reader, MaterialSourceProvenance& provenance, Core::Containers::ArrayView<char> scratch)
        {
            if (!reader.BeginObject())
            {
                return false;
            }
            constexpr cstring            names[] = {"path", "importer", "index"};
            Core::Containers::StringView key     = {};
            uint32_t                     fields  = 0;
            while (reader.Member(scratch.first(16), &key))
            {
                const uint32_t index = FieldIndex(key, names);
                if (index == 3 || (fields & (1u << index)))
                {
                    return false;
                }
                fields |= 1u << index;
                if (index < 2)
                {
                    if (!(index == 0 ? ReadText(reader, provenance.Path) : ReadText(reader, provenance.Importer, true)))
                    {
                        return false;
                    }
                }
                else
                {
                    uint64_t value = 0;
                    if (!reader.UInt64(&value) || value > std::numeric_limits<uint32_t>::max())
                    {
                        return false;
                    }
                    provenance.Index = static_cast<uint32_t>(value);
                }
            }
            return reader.Error() == VFS::VFSError::OK && fields == 7;
        }

        bool WriteVector(Serialization::CanonicalJSONWriter& writer, const Core::Containers::FixedArray<float, 4>& vector)
        {
            if (!writer.Raw("["))
            {
                return false;
            }
            for (size_t i = 0; i < vector.size(); ++i)
            {
                if ((i && !writer.Raw(",")) || !writer.Float32(vector[i]))
                {
                    return false;
                }
            }
            return writer.Raw("]");
        }

        VFS::VFSResult<void> EmitMaterial(const MaterialAssetDocument& document, Core::Containers::ArrayView<const VFS::VFSRelativePath> paths, bool projection, Serialization::JSONWriteFn write, void* context)
        {
            if (!write || (!projection && (paths.size() != document.Textures.size() || !paths.data())))
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
            }
            Core::Containers::StringView name = {};
            if (document.UUID.is_nil() || !BoundedText(document.Name, &name) || document.AlphaMode > Rendering::Materials::AlphaMode::Blend || document.Factors[2] < 0.f)
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            }
            for (const auto& field : VectorFields)
            {
                for (const auto value : document.*field.Member)
                {
                    if (!std::isfinite(value))
                    {
                        return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                    }
                }
            }
            for (size_t i = 0; i < document.Textures.size(); ++i)
            {
                if (!ValidTexture(document.Textures[i]) || (!projection && document.Textures[i].UUID.is_nil() == paths[i].IsValid()))
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                }
            }
            Serialization::CanonicalJSONWriter writer(write, context);
            // Fixed schema order is the unsigned UTF-8 key order, without a DOM
            // or a separately sorted key/value collection.
            const auto                         emit = [&]() {
                if (!writer.Raw("{\"albedo_color\":") || !WriteVector(writer, document.AlbedoColor) || !writer.Raw(",\"alpha_cutoff\":") || !writer.Float32(document.Factors[2]) || !writer.Raw(",\"alpha_mode\":") || !writer.String(Rendering::Materials::AlphaModeName(document.AlphaMode)) || !writer.Raw(",\"ambient_color\":") || !WriteVector(writer, document.AmbientColor) || !writer.Raw(",\"emissive_color\":") || !WriteVector(writer, document.EmissiveColor) || !writer.Raw(",\"factors\":") || !WriteVector(writer, document.Factors) ||
                    !writer.Raw(",\"material_version\":") || !writer.UInt64(MATERIAL_FILE_VERSION) || !writer.Raw(",\"name\":") || !writer.String(name) || !writer.Raw(",\"roughness_color\":") || !WriteVector(writer, document.RoughnessColor) || !writer.Raw(",\"specular_color\":") || !WriteVector(writer, document.SpecularColor) || !writer.Raw(",\"textures\":{"))
                {
                    return false;
                }
                for (size_t i = 0; i < document.Textures.size(); ++i)
                {
                    const auto& texture = document.Textures[i];
                    const auto  path    = projection || texture.UUID.is_nil() ? Core::Containers::StringView{} : paths[i].View();
                    if ((i && !writer.Raw(",")) || !writer.String(TextureSlots[i]) || !writer.Raw(":{\"path\":") || !writer.String(path) || !writer.Raw(",\"uuid\":") || !writer.UUID(texture.UUID) || !writer.Raw("}"))
                    {
                        return false;
                    }
                }
                return writer.Raw("},\"uuid\":") && writer.UUID(document.UUID) && writer.Raw("}\n");
            };
            return emit() ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(writer.Error());
        }
    } // namespace

    cstring MaterialTextureSlotKey(MaterialTextureSlot slot) noexcept
    {
        const auto index = static_cast<size_t>(slot);
        return index < static_cast<size_t>(MaterialTextureSlot::Count) ? TextureSlots[index] : nullptr;
    }

    VFS::VFSResult<const MaterialAssetDocument*> MaterialAssetReader::Read(Core::Containers::ArrayView<const uint8_t> bytes, const uuids::uuid& expected_uuid, uint64_t maximum_file_bytes, MaterialReadPolicy policy)
    {
        if (bytes.size() && !bytes.data())
        {
            return VFS::VFSResult<const MaterialAssetDocument*>::Fail(VFS::VFSError::InvalidPath);
        }
        if (bytes.size() > maximum_file_bytes || bytes.size() > ZMega(1))
        {
            return VFS::VFSResult<const MaterialAssetDocument*>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        if (bytes.size())
        {
            const auto source = reinterpret_cast<uintptr_t>(bytes.data());
            const auto state  = reinterpret_cast<uintptr_t>(this);
            if (source >= state ? source - state < sizeof(*this) : state - source < bytes.size())
            {
                return VFS::VFSResult<const MaterialAssetDocument*>::Fail(VFS::VFSError::InvalidPath);
            }
        }
        // expected_uuid may borrow the document returned by the previous Read.
        m_expected_uuid = expected_uuid;
        m_reader.Reset(bytes);
        auto& reader = m_reader;
        if (!reader.BeginObject())
        {
            return VFS::VFSResult<const MaterialAssetDocument*>::Fail(reader.Error());
        }
        // The first document is already default-initialized by its owner.
        // Reconstruct only on reuse, with no full-document temporary or copy.
        if (m_document_used)
        {
            new (&m_document) MaterialAssetDocument{};
        }
        m_document_used                                = true;
        auto&                        candidate         = m_document;
        constexpr cstring            names[]           = {"material_version", "uuid", "name", "textures", "ambient_color", "albedo_color", "emissive_color", "roughness_color", "specular_color", "factors", "alpha_mode", "alpha_cutoff", "source_material"};
        Core::Containers::StringView key               = {};
        uint32_t                     fields            = 0;
        float                        cutoff            = 0.f;
        const bool                   source_compatible = policy == MaterialReadPolicy::SourceCompatible;
        while (reader.Member(m_key, &key))
        {
            const uint32_t index = FieldIndex(key, names);
            if (source_compatible && (index == 13 || index == 12))
            {
                if (!reader.SkipValue(m_key))
                {
                    return VFS::VFSResult<const MaterialAssetDocument*>::Fail(ReaderError(reader));
                }
                continue;
            }
            if (index == 13 || (fields & (1u << index)))
            {
                return VFS::VFSResult<const MaterialAssetDocument*>::Fail(VFS::VFSError::Corrupted);
            }
            fields     |= 1u << index;
            bool valid  = false;
            if (index >= 4 && index <= 9)
            {
                valid = ReadVector(reader, candidate.*VectorFields[index - 4].Member, source_compatible);
            }
            else
            {
                switch (index)
                {
                    case 0:
                    {
                        uint64_t version = 0;
                        if (!reader.UInt64(&version))
                        {
                            return VFS::VFSResult<const MaterialAssetDocument*>::Fail(ReaderError(reader));
                        }
                        if (version != MATERIAL_FILE_VERSION)
                        {
                            return VFS::VFSResult<const MaterialAssetDocument*>::Fail(VFS::VFSError::Unsupported);
                        }
                        valid = true;
                        break;
                    }
                    case 1:
                        valid = ReadUUID(reader, m_text, &candidate.UUID, false);
                        break;
                    case 2:
                        valid = ReadText(reader, candidate.Name);
                        break;
                    case 3:
                        valid = ReadTextures(reader, candidate, m_key, source_compatible);
                        break;
                    case 10:
                    {
                        Core::Containers::StringView mode = {};
                        valid                             = reader.String(Core::Containers::ArrayView<char>(m_text).first(16), &mode) && (mode == "OPAQUE" || mode == "MASK" || mode == "BLEND" || mode == "LEGACY");
                        if (valid)
                        {
                            candidate.AlphaMode = Rendering::Materials::ParseAlphaMode({mode.data(), mode.size()});
                        }
                        break;
                    }
                    case 11:
                        valid = reader.Float32(&cutoff) && cutoff >= 0.f;
                        break;
                    case 12:
                        valid               = ReadProvenance(reader, candidate.Source, m_text);
                        candidate.HasSource = valid;
                        break;
                    default:
                        break;
                }
            }
            if (!valid)
            {
                return VFS::VFSResult<const MaterialAssetDocument*>::Fail(ReaderError(reader));
            }
        }
        constexpr uint32_t required = (1u << 12) - 1;
        if (!reader.Finish())
        {
            return VFS::VFSResult<const MaterialAssetDocument*>::Fail(reader.Error());
        }
        if ((!source_compatible && ((fields & required) != required || cutoff != candidate.Factors[2] || candidate.UUID.is_nil())) || (!m_expected_uuid.is_nil() && candidate.UUID != m_expected_uuid))
        {
            return VFS::VFSResult<const MaterialAssetDocument*>::Fail(VFS::VFSError::Corrupted);
        }
        if (source_compatible && (fields & (1u << 11)))
        {
            candidate.Factors[2] = cutoff;
        }
        return VFS::VFSResult<const MaterialAssetDocument*>::Ok(&m_document);
    }

    VFS::VFSResult<void> ReadMaterialAssetDocument(Core::Containers::ArrayView<const uint8_t> bytes, MaterialAssetDocument* output, const uuids::uuid& expected_uuid, uint64_t maximum_file_bytes)
    {
        if (!output)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        MaterialAssetReader reader = {};
        const auto          parsed = reader.Read(bytes, expected_uuid, maximum_file_bytes);
        if (parsed.Failed())
        {
            return VFS::VFSResult<void>::Fail(parsed.Error());
        }
        *output = *parsed.Value();
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> WriteCanonicalMaterialAsset(const MaterialAssetDocument& document, Core::Containers::ArrayView<const VFS::VFSRelativePath> paths, Serialization::JSONWriteFn write, void* context)
    {
        return EmitMaterial(document, paths, false, write, context);
    }

    VFS::VFSResult<void> WriteCanonicalMaterialSemanticProjection(const MaterialAssetDocument& document, Serialization::JSONWriteFn write, void* context)
    {
        return EmitMaterial(document, {}, true, write, context);
    }
} // namespace ZEngine::Importers::AssetCodec
