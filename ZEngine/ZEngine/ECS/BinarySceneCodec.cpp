#include <ZEngine/ECS/BinarySceneCodec.h>
#include <ZEngine/ECS/SceneBinaryValue.h>
#include <ZEngine/ECS/SceneCompatibility.h>
#include <ZEngine/ECS/SceneDocumentInternal.h>
#include <ZEngine/ECS/SceneSchemaKey.h>
#include <ZEngine/ECS/SceneSettingsBinary.h>
#include <ZEngine/ECS/SceneValue.h>
#include <miniz.h>
#include <array>
#include <new>

namespace ZEngine::ECS
{
    namespace
    {
        namespace Format = SceneBinaryFormat;
        namespace Detail = SceneDocumentDetail;

        bool Reject(SceneDiagnostics* diagnostics, cstring message)
        {
            SceneDiagnosticError(diagnostics, message);
            return false;
        }
        bool WriteUUID(SceneBinaryWriter& writer, const uuids::uuid& uuid)
        {
            const auto bytes = uuid.as_bytes();
            return writer.Bytes({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
        }
        bool ReadUUID(SceneBinaryReader& reader, uuids::uuid& uuid)
        {
            std::span<const uint8_t> bytes = {};
            if (!reader.Bytes(16, bytes))
                return false;
            uuid = uuids::uuid(bytes.begin(), bytes.end());
            return !uuid.is_nil();
        }

        // Strict UTF-8: reject NUL, overlongs, surrogates and values above U+10FFFF.
        bool ValidText(std::string_view text)
        {
            for (size_t i = 0; i < text.size();)
            {
                const auto first = static_cast<uint8_t>(text[i++]);
                if (!first)
                    return false;
                if (first < 0x80)
                    continue;
                uint32_t value = 0, minimum = 0;
                size_t   count = 0;
                if (first >= 0xc2 && first <= 0xdf)
                {
                    value   = first & 0x1fu;
                    minimum = 0x80;
                    count   = 1;
                }
                else if (first >= 0xe0 && first <= 0xef)
                {
                    value   = first & 0x0fu;
                    minimum = 0x800;
                    count   = 2;
                }
                else if (first >= 0xf0 && first <= 0xf4)
                {
                    value   = first & 0x07u;
                    minimum = 0x10000;
                    count   = 3;
                }
                else
                    return false;
                if (count > text.size() - i)
                    return false;
                for (size_t j = 0; j < count; ++j)
                {
                    const auto next = static_cast<uint8_t>(text[i++]);
                    if ((next & 0xc0u) != 0x80u)
                        return false;
                    value = (value << 6) | (next & 0x3fu);
                }
                if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
                    return false;
            }
            return true;
        }
        bool ValidStrings(const SceneValue& value, const SceneDocumentLimits& limits)
        {
            for (const auto& field : value.Fields())
                if (const auto* text = std::get_if<SceneStringValue>(&field.Value))
                    if ((!text->Data && text->Size) || text->Size > limits.MaxScalarBytes || !ValidText({text->Data ? text->Data : "", text->Size}))
                        return false;
            return true;
        }

        struct Header
        {
            uint32_t    FileBytes       = 0;
            uint32_t    NameBytes       = 0;
            uint32_t    Entities        = 0;
            uint32_t    Components      = 0;
            uuids::uuid UUID            = {};
            size_t      EntityOffset    = 0;
            size_t      ComponentOffset = 0;
            size_t      DataOffset      = 0;
        };
        struct ComponentEntry
        {
            uint32_t KeyOffset     = 0;
            uint32_t KeyBytes      = 0;
            uint32_t Version       = 0;
            uint32_t PayloadOffset = 0;
            uint32_t PayloadBytes  = 0;
        };
        bool ReadEntry(SceneBinaryReader& reader, ComponentEntry& entry)
        {
            return reader.UInt32(entry.KeyOffset) && reader.UInt32(entry.KeyBytes) && reader.UInt32(entry.Version) && reader.UInt32(entry.PayloadOffset) && reader.UInt32(entry.PayloadBytes);
        }
        bool ReadHeader(std::span<const uint8_t> bytes, const SceneDocumentLimits& limits, Header& header)
        {
            if (bytes.size() < Format::HeaderBytes || bytes.size() > limits.MaxFileBytes)
                return false;
            SceneBinaryReader reader(std::as_bytes(bytes.first(Format::HeaderBytes)));
            uint32_t          magic = 0, version = 0, header_bytes = 0, endian = 0, features = 0, document_version = 0, checksum = 0, reserved = 0;
            if (!reader.UInt32(magic) || magic != Format::Magic || !reader.UInt32(version) || version != Format::Version || !reader.UInt32(header_bytes) || header_bytes != Format::HeaderBytes || !reader.UInt32(endian) || endian != Format::LittleEndianMark || !reader.UInt32(features) || features != 0 || !reader.UInt32(document_version) || document_version != SceneDocument::SchemaVersion || !reader.UInt32(header.FileBytes) || header.FileBytes != bytes.size() || !reader.UInt32(checksum) || checksum != Format::Checksum(bytes) || !ReadUUID(reader, header.UUID) ||
                !reader.UInt32(header.NameBytes) || header.NameBytes > SceneDocument::MaxNameBytes || header.NameBytes > limits.MaxScalarBytes || !reader.UInt32(header.Entities) || header.Entities > limits.MaxEntities || header.Entities > limits.MaxCollectionItems || !reader.UInt32(header.Components) || !reader.UInt32(reserved) || reserved || reader.Remaining())
                return false;
            // Widen before multiplication; prove each derived region fits before
            // using any offsets. No pointers from unchecked header values.
            if (uint64_t(header.Components) > uint64_t(header.Entities) * limits.MaxComponentsPerEntity)
                return false;
            header.EntityOffset = Format::HeaderBytes + header.NameBytes + SceneGridBinaryBytes + SceneSkyBinaryBytes;
            if (header.EntityOffset > bytes.size() || header.Entities > (bytes.size() - header.EntityOffset) / Format::EntityBytes)
                return false;
            header.ComponentOffset = header.EntityOffset + size_t(header.Entities) * Format::EntityBytes;
            if (header.Components > (bytes.size() - header.ComponentOffset) / Format::ComponentBytes)
                return false;
            header.DataOffset = header.ComponentOffset + size_t(header.Components) * Format::ComponentBytes;
            return true;
        }
        const SceneComponentSchema* FindSchema(std::string_view key, uint32_t version, const SceneComponentSchemaRegistry& schemas)
        {
            if (!IsValidSceneSchemaKey(key) || key == "UUIDComponent")
                return nullptr;
            std::array<char, 128> terminated = {};
            if (Helpers::secure_strncpy(terminated.data(), terminated.size(), key.data(), key.size()) != Helpers::MEMORY_OP_SUCCESS)
                return nullptr;
            const auto* schema = schemas.FindByKey(terminated.data());
            return schema && schema->Version == version && schema->Codecs.EncodeBinary && schema->Codecs.DecodeBinary && schema->Codecs.EncodeYAML && schema->Codecs.DecodeYAML ? schema : nullptr;
        }
        bool ValidateTables(std::span<const uint8_t> bytes, const Header& header, const SceneComponentSchemaRegistry& schemas, const SceneDocumentLimits& limits)
        {
            SceneBinaryReader entities(std::as_bytes(bytes.subspan(header.EntityOffset, size_t(header.Entities) * Format::EntityBytes)));
            SceneBinaryReader components(std::as_bytes(bytes.subspan(header.ComponentOffset, size_t(header.Components) * Format::ComponentBytes)));
            uuids::uuid       previous        = {};
            uint32_t          component_index = 0;
            size_t            cursor          = header.DataOffset;
            for (uint32_t i = 0; i < header.Entities; ++i)
            {
                uuids::uuid uuid  = {};
                uint32_t    first = 0, count = 0;
                if (!ReadUUID(entities, uuid) || (i && !(previous < uuid)) || !entities.UInt32(first) || first != component_index || !entities.UInt32(count) || count > limits.MaxComponentsPerEntity || count > limits.MaxCollectionItems || count > header.Components - component_index)
                    return false;
                previous                      = uuid;
                std::string_view previous_key = {};
                for (uint32_t j = 0; j < count; ++j)
                {
                    ComponentEntry entry = {};
                    if (!ReadEntry(components, entry) || entry.KeyOffset != cursor || !entry.KeyBytes || entry.KeyBytes > 127 || entry.KeyBytes > limits.MaxScalarBytes || entry.KeyBytes > bytes.size() - cursor)
                        return false;
                    const std::string_view key(reinterpret_cast<const char*>(bytes.data() + cursor), entry.KeyBytes);
                    cursor += entry.KeyBytes;
                    if ((j && !(previous_key < key)) || !FindSchema(key, entry.Version, schemas) || entry.PayloadOffset != cursor || entry.PayloadBytes > bytes.size() - cursor)
                        return false;
                    previous_key  = key;
                    cursor       += entry.PayloadBytes;
                }
                component_index += count;
            }
            // Contiguous canonical regions forbid aliases, gaps, overlapping
            // payloads, unreferenced rows and hidden trailing bytes.
            return component_index == header.Components && cursor == bytes.size() && !entities.Remaining() && !components.Remaining();
        }
    } // namespace

    uint32_t SceneBinaryFormat::Checksum(std::span<const uint8_t> bytes)
    {
        if (bytes.size() < ChecksumOffset + 4)
            return 0;
        constexpr std::array<uint8_t, 4> zero = {};
        auto                             crc  = mz_crc32(0, bytes.data(), ChecksumOffset);
        crc                                   = mz_crc32(crc, zero.data(), zero.size());
        return static_cast<uint32_t>(mz_crc32(crc, bytes.data() + ChecksumOffset + 4, bytes.size() - ChecksumOffset - 4));
    }

    bool BinarySceneCodec::Encode(const SceneDocument& document, const SceneComponentSchemaRegistry& schemas, Core::Memory::ArenaAllocator* output_arena, Core::Containers::Array<uint8_t>* out_bytes, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits)
    {
        if (!out_bytes || !output_arena || !output_arena->m_memory || !schemas.IsInitialized() || !Detail::LimitsValid(limits))
            return Reject(diagnostics, "Binary scene encode requires a writable validated document, schemas, output arena and valid limits.");
        if (!SceneCompatibilityService::PrepareForSave(document, SceneCompatibilityUse::CookedRuntime, diagnostics))
            return false;
        const auto& source = *document.m_impl;
        if (source.Entities.size() > limits.MaxEntities || source.Entities.size() > limits.MaxCollectionItems || source.Settings.Name.size() > limits.MaxScalarBytes || !ValidText(source.Settings.Name))
            return Reject(diagnostics, "Binary scene text or entity count exceeds its limits.");
        std::array<std::byte, SceneGridBinaryBytes> grid = {};
        std::array<std::byte, SceneSkyBinaryBytes>  sky  = {};
        if (!EncodeSceneGridBinary(source.Settings.Grid, {grid.data(), grid.size()}) || !EncodeSceneSkyBinary(source.Settings.Sky, {sky.data(), sky.size()}))
            return Reject(diagnostics, "Binary scene settings are not valid portable authoring values.");
        const size_t entity_offset    = Format::HeaderBytes + source.Settings.Name.size() + grid.size() + sky.size();
        const size_t component_offset = entity_offset + source.Entities.size() * Format::EntityBytes;
        const size_t data_offset      = component_offset + source.Components.size() * Format::ComponentBytes;
        size_t       total            = data_offset;
        if (total > limits.MaxFileBytes)
            return Reject(diagnostics, "Binary scene tables exceed the file-size limit.");
        Core::Containers::FixedArray<SceneValueField, Detail::MaxComponentFields> fields = {};
        for (const auto& entity : source.Entities)
        {
            if (entity.Components.size() > limits.MaxComponentsPerEntity || entity.Components.size() > limits.MaxCollectionItems)
                return Reject(diagnostics, "Binary scene entity exceeds its component limit.");
            for (const auto& component : entity.Components)
            {
                const auto*      schema = FindSchema(component.Key, component.Version, schemas);
                SceneValueWriter value(fields);
                if (!schema || component.Key.size() > limits.MaxScalarBytes || component.Key.size() > limits.MaxFileBytes - total || !schema->Codecs.DecodeYAML(schema->Codecs.Context, component.Version, component.Payload, &value, diagnostics) || !ValidStrings(value.Value(), limits))
                    return Reject(diagnostics, "Binary scene component schema/version or semantic value is unsupported.");
                total        += component.Key.size();
                auto measure  = SceneBinaryWriter::Measure(limits.MaxFileBytes - total);
                if (!schema->Codecs.EncodeBinary(schema->Codecs.Context, value.Value(), &measure, diagnostics))
                    return Reject(diagnostics, "Binary scene component payload exceeds its limits or fails validation.");
                total += measure.Position();
            }
        }
        Core::Memory::ArenaCheckpoint    checkpoint(output_arena);
        Core::Containers::Array<uint8_t> result = {};
        result.m_allocator                      = output_arena;
        if (!result.try_reserve(total))
            return Reject(diagnostics, "Cannot allocate bounded binary scene output.");
        result.m_size           = total;
        const auto        bytes = std::as_writable_bytes(std::span(result.data(), result.size()));
        SceneBinaryWriter header(bytes.first(Format::HeaderBytes));
        if (!header.UInt32(Format::Magic) || !header.UInt32(Format::Version) || !header.UInt32(Format::HeaderBytes) || !header.UInt32(Format::LittleEndianMark) || !header.UInt32(0) || !header.UInt32(SceneDocument::SchemaVersion) || !header.UInt32(static_cast<uint32_t>(total)) || !header.UInt32(0) || !WriteUUID(header, source.Settings.UUID) || !header.UInt32(static_cast<uint32_t>(source.Settings.Name.size())) || !header.UInt32(static_cast<uint32_t>(source.Entities.size())) || !header.UInt32(static_cast<uint32_t>(source.Components.size())) || !header.UInt32(0) ||
            header.Remaining())
            return Reject(diagnostics, "Binary scene header encoding failed.");
        SceneBinaryWriter settings(bytes.subspan(Format::HeaderBytes, entity_offset - Format::HeaderBytes));
        if (!settings.Bytes({reinterpret_cast<const uint8_t*>(source.Settings.Name.data()), source.Settings.Name.size()}) || !settings.Bytes({reinterpret_cast<const uint8_t*>(grid.data()), grid.size()}) || !settings.Bytes({reinterpret_cast<const uint8_t*>(sky.data()), sky.size()}) || settings.Remaining())
            return Reject(diagnostics, "Binary scene settings encoding failed.");
        SceneBinaryWriter entities(bytes.subspan(entity_offset, component_offset - entity_offset));
        SceneBinaryWriter components(bytes.subspan(component_offset, data_offset - component_offset));
        size_t            cursor = data_offset;
        uint32_t          first  = 0;
        for (const auto& entity : source.Entities)
        {
            if (!WriteUUID(entities, entity.UUID) || !entities.UInt32(first) || !entities.UInt32(static_cast<uint32_t>(entity.Components.size())))
                return Reject(diagnostics, "Binary scene entity table encoding failed.");
            first += static_cast<uint32_t>(entity.Components.size());
            for (const auto& component : entity.Components)
            {
                const auto*      schema = FindSchema(component.Key, component.Version, schemas);
                SceneValueWriter value(fields);
                if (!schema || !schema->Codecs.DecodeYAML(schema->Codecs.Context, component.Version, component.Payload, &value, diagnostics) || !ValidStrings(value.Value(), limits) || component.Key.size() > total - cursor)
                    return Reject(diagnostics, "Binary scene schema changed during encoding.");
                const auto        key_offset = static_cast<uint32_t>(cursor);
                SceneBinaryWriter key(bytes.subspan(cursor, component.Key.size()));
                if (!key.Bytes({reinterpret_cast<const uint8_t*>(component.Key.data()), component.Key.size()}))
                    return Reject(diagnostics, "Binary scene key encoding failed.");
                cursor += component.Key.size();
                SceneBinaryWriter payload(bytes.subspan(cursor));
                if (!schema->Codecs.EncodeBinary(schema->Codecs.Context, value.Value(), &payload, diagnostics) || !components.UInt32(key_offset) || !components.UInt32(static_cast<uint32_t>(component.Key.size())) || !components.UInt32(component.Version) || !components.UInt32(static_cast<uint32_t>(cursor)) || !components.UInt32(static_cast<uint32_t>(payload.Position())))
                    return Reject(diagnostics, "Binary scene component encoding failed.");
                cursor += payload.Position();
            }
        }
        if (cursor != total || entities.Remaining() || components.Remaining())
            return Reject(diagnostics, "Binary scene codec changed its measured output size.");
        SceneBinaryWriter checksum(bytes.subspan(Format::ChecksumOffset, 4));
        if (!checksum.UInt32(Format::Checksum({result.data(), result.size()})))
            return Reject(diagnostics, "Binary scene integrity encoding failed.");
        *out_bytes = std::move(result);
        checkpoint.Commit();
        return true;
    }

    bool BinarySceneCodec::Decode(std::span<const uint8_t> bytes, const SceneComponentSchemaRegistry& schemas, SceneDocument* out_document, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits)
    {
        if (!out_document || !schemas.IsInitialized() || !Detail::LimitsValid(limits))
            return Reject(diagnostics, "Binary scene decode requires schemas, an output document and valid limits.");
        Header header = {};
        if (!ReadHeader(bytes, limits, header) || !ValidateTables(bytes, header, schemas, limits))
            return Reject(diagnostics, "Binary scene header, integrity, table bounds/order or schema compatibility is invalid.");
        SceneDocumentSettings  settings = {.UUID = header.UUID};
        const std::string_view name(reinterpret_cast<const char*>(bytes.data() + Format::HeaderBytes), header.NameBytes);
        if (!ValidText(name))
            return Reject(diagnostics, "Binary scene name is not valid non-NUL UTF-8.");
        settings.Name            = name;
        const size_t grid_offset = Format::HeaderBytes + header.NameBytes;
        if (!DecodeSceneGridBinary({reinterpret_cast<const std::byte*>(bytes.data() + grid_offset), SceneGridBinaryBytes}, settings.Grid) || !DecodeSceneSkyBinary({reinterpret_cast<const std::byte*>(bytes.data() + grid_offset + SceneGridBinaryBytes), SceneSkyBinaryBytes}, settings.Sky))
            return Reject(diagnostics, "Binary scene grid/sky settings are invalid or incompatible.");
        SceneDocument candidate = {};
        candidate.m_impl.reset(new (std::nothrow) SceneDocument::Impl());
        if (!candidate.m_impl || !candidate.m_impl->Initialize(limits))
            return Reject(diagnostics, "Cannot allocate bounded binary scene document storage.");
        auto&           storage = candidate.m_impl->Tree;
        SceneYAML::Node root = {}, entity_nodes = {};
        if (!candidate.m_impl->CreateRoot(settings, &root, &entity_nodes, diagnostics))
            return false;
        SceneBinaryReader                                                         entities(std::as_bytes(bytes.subspan(header.EntityOffset, size_t(header.Entities) * Format::EntityBytes)));
        SceneBinaryReader                                                         components(std::as_bytes(bytes.subspan(header.ComponentOffset, size_t(header.Components) * Format::ComponentBytes)));
        Core::Containers::FixedArray<SceneValueField, Detail::MaxComponentFields> fields = {};
        for (uint32_t i = 0; i < header.Entities; ++i)
        {
            uuids::uuid uuid  = {};
            uint32_t    first = 0, count = 0;
            if (!ReadUUID(entities, uuid) || !entities.UInt32(first) || !entities.UInt32(count))
                return Reject(diagnostics, "Binary scene entity table changed during decoding.");
            const auto entity          = storage.Make(SceneYAML::NodeType::Map);
            const auto component_nodes = storage.Make(SceneYAML::NodeType::Map);
            if (!storage.Set(entity, "uuid", storage.UUID(uuid)) || !storage.Set(entity, "components", component_nodes) || !storage.Append(entity_nodes, entity))
                return Reject(diagnostics, "Binary scene entities exceed the decoded-memory budget.");
            for (uint32_t j = 0; j < count; ++j)
            {
                ComponentEntry entry = {};
                if (!ReadEntry(components, entry))
                    return Reject(diagnostics, "Binary scene component table changed during decoding.");
                const std::string_view key(reinterpret_cast<const char*>(bytes.data() + entry.KeyOffset), entry.KeyBytes);
                const auto*            schema = FindSchema(key, entry.Version, schemas);
                SceneBinaryReader      payload(std::as_bytes(bytes.subspan(entry.PayloadOffset, entry.PayloadBytes)));
                SceneValueWriter       value(fields);
                auto                   decoded = storage.Destination();
                if (!schema || !schema->Codecs.DecodeBinary(schema->Codecs.Context, entry.Version, &payload, &value, diagnostics) || payload.Remaining() || !ValidStrings(value.Value(), limits) || !schema->Codecs.EncodeYAML(schema->Codecs.Context, value.Value(), &decoded, diagnostics))
                    return Reject(diagnostics, "Binary scene component payload is invalid, incompatible or not completely consumed.");
                const auto envelope = storage.Make(SceneYAML::NodeType::Map);
                if (!storage.Set(envelope, "schema_version", storage.UInt(entry.Version)) || !storage.Set(envelope, "fields", decoded) || !storage.Set(component_nodes, key, envelope))
                    return Reject(diagnostics, "Binary scene components exceed the decoded-memory budget.");
            }
        }
        uint32_t nodes = 0;
        if (!Detail::CountCapturedNodes(root, limits, nodes) || !candidate.m_impl->Decode(root, schemas, diagnostics, true))
            return Reject(diagnostics, "Binary scene exceeds document limits or fails shared graph validation.");
        *out_document = std::move(candidate);
        return true;
    }
} // namespace ZEngine::ECS
