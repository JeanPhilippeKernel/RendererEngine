#include <ZEngine/Cook/CookSceneCodec.h>
#include <ZEngine/Core/Serialization/YAMLAllocation.h>
#include <ZEngine/ECS/BinarySceneCodec.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneBinaryValue.h>
#include <ZEngine/ECS/SceneSettingsBinary.h>
#include <ZEngine/ECS/SceneYAMLReader.h>
#include <ZEngine/ECS/SceneYamlValue.h>
#include <cstdio>
#include <string>
#include "CookResourceTestSupport.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    class SceneCookSceneResourceProcessTest : public Tests::ResourceProcessTest
    {
    protected:
        struct NumericComponent
        {
            Core::Containers::FixedArray<uint32_t, 64> Values = {};
            uint32_t                                   Count  = 0;
        };

        // Optional trailing fields permit an exact input-node boundary without
        // padding the source with unknown or unsupported component schemas.
        struct NumericCodec
        {
            Core::Containers::FixedArray<Core::Containers::FixedArray<char, 4>, 64> Keys   = {};
            Core::Containers::FixedArray<ECS::SceneFieldSchema, 64>                 Fields = {};

            NumericCodec()
            {
                for (size_t i = 0; i < Keys.size(); ++i)
                {
                    Keys[i]       = {'v', char('0' + i / 10), char('0' + i % 10), '\0'};
                    Fields[i].Key = Keys[i].data();
                }
            }
            static bool Capture(void* context, ECS::EntityID entity, const ECS::Scene& scene, ECS::SceneValueWriter* output, ECS::SceneDiagnostics*)
            {
                const auto* value = scene.GetComponent<NumericComponent>(entity);
                const auto& codec = *static_cast<NumericCodec*>(context);
                if (!value || !output || value->Count > codec.Keys.size())
                    return false;
                for (uint32_t i = 0; i < value->Count; ++i)
                    if (!output->Add(codec.Keys[i].data(), value->Values[i]))
                        return false;
                return true;
            }
            static bool Populate(void* context, ECS::EntityID entity, ECS::Scene& scene, const ECS::SceneValue& payload, ECS::SceneDiagnostics*)
            {
                const auto&      codec     = *static_cast<NumericCodec*>(context);
                NumericComponent component = {};
                if (payload.Fields().size() > codec.Keys.size())
                    return false;
                for (const auto& field : payload.Fields())
                {
                    const auto* value = std::get_if<uint32_t>(&field.Value);
                    if (!value || Helpers::secure_strcmp(field.Key, codec.Keys[component.Count].data()))
                        return false;
                    component.Values[component.Count++] = *value;
                }
                scene.AddComponent<NumericComponent>(entity, component);
                return true;
            }
            static bool Encode(void* context, const ECS::SceneValue& payload, ECS::SceneBinaryWriter* output, ECS::SceneDiagnostics*)
            {
                const auto& codec = *static_cast<NumericCodec*>(context);
                if (!output || payload.Fields().size() > codec.Keys.size() || !output->Require(4 + 4 * payload.Fields().size()) || !output->UInt32(static_cast<uint32_t>(payload.Fields().size())))
                    return false;
                size_t index = 0;
                for (const auto& field : payload.Fields())
                {
                    const auto* value = std::get_if<uint32_t>(&field.Value);
                    if (!value || Helpers::secure_strcmp(field.Key, codec.Keys[index++].data()) || !output->UInt32(*value))
                        return false;
                }
                return true;
            }
            static bool Decode(void* context, uint32_t version, ECS::SceneBinaryReader* input, ECS::SceneValueWriter* output, ECS::SceneDiagnostics*)
            {
                const auto& codec = *static_cast<NumericCodec*>(context);
                if (version != 1 || !input || !output)
                    return false;
                auto     candidate = *input;
                uint32_t count     = 0;
                if (!candidate.UInt32(count) || count > codec.Keys.size() || candidate.Remaining() != 4 * count)
                    return false;
                for (uint32_t i = 0; i < count; ++i)
                {
                    uint32_t value = 0;
                    if (!candidate.UInt32(value) || !output->Add(codec.Keys[i].data(), value))
                        return false;
                }
                *input = candidate;
                return true;
            }
            static bool EncodeYAML(void*, const ECS::SceneValue& payload, ECS::SceneYAML::Node* output, ECS::SceneDiagnostics* diagnostics)
            {
                return ECS::EncodeSceneValueYAML(payload, output, diagnostics);
            }
            static bool DecodeYAML(void* context, uint32_t version, const ECS::SceneYAML::Node& input, ECS::SceneValueWriter* output, ECS::SceneDiagnostics*)
            {
                const auto& codec = *static_cast<NumericCodec*>(context);
                if (version != 1 || !output || !input.IsMap() || input.size() > codec.Keys.size())
                    return false;
                for (size_t i = 0; i < input.size(); ++i)
                {
                    uint32_t value = 0;
                    if (!ECS::ReadSceneYAMLUInt(ECS::FindSceneYAMLField(input, codec.Keys[i].data()), value) || !output->Add(codec.Keys[i].data(), value))
                        return false;
                }
                return true;
            }
        };

        struct LongStringComponent
        {
            char Value[4097] = {};
        };

        // Real typed string codec with the shared wire/YAML contract, rather
        // than raising NameComponent's independent 127-byte semantic limit.
        struct LongStringCodec
        {
            inline static constexpr ECS::SceneFieldSchema Field = {"value"};

            static const ECS::SceneStringValue*           Value(const ECS::SceneValue& payload)
            {
                const auto* text = payload.Find<ECS::SceneStringValue>("value");
                return payload.Fields().size() == 1 && text && text->Data && text->Size <= 4096 && std::string_view(text->Data, text->Size).find('\0') == std::string_view::npos ? text : nullptr;
            }
            static bool Capture(void*, ECS::EntityID entity, const ECS::Scene& scene, ECS::SceneValueWriter* output, ECS::SceneDiagnostics*)
            {
                const auto* value = scene.GetComponent<LongStringComponent>(entity);
                if (!value || !output)
                    return false;
                const auto end = std::find(std::begin(value->Value), std::end(value->Value), '\0');
                return end != std::end(value->Value) && output->Add("value", ECS::SceneStringValue{value->Value, static_cast<uint32_t>(end - value->Value)});
            }
            static bool Populate(void*, ECS::EntityID entity, ECS::Scene& scene, const ECS::SceneValue& payload, ECS::SceneDiagnostics*)
            {
                const auto* text = Value(payload);
                if (!text)
                    return false;
                LongStringComponent component = {};
                if (Helpers::secure_strncpy(component.Value, sizeof(component.Value), text->Data, text->Size) != Helpers::MEMORY_OP_SUCCESS)
                    return false;
                scene.AddComponent<LongStringComponent>(entity, component);
                return true;
            }
            static bool Encode(void*, const ECS::SceneValue& payload, ECS::SceneBinaryWriter* output, ECS::SceneDiagnostics*)
            {
                const auto* text = Value(payload);
                return text && output && output->Require(4 + text->Size) && output->UInt32(text->Size) && output->Bytes({reinterpret_cast<const uint8_t*>(text->Data), text->Size});
            }
            static bool Decode(void*, uint32_t version, ECS::SceneBinaryReader* input, ECS::SceneValueWriter* output, ECS::SceneDiagnostics*)
            {
                if (version != 1 || !input || !output)
                    return false;
                auto                     candidate = *input;
                uint32_t                 size      = 0;
                std::span<const uint8_t> bytes     = {};
                if (!candidate.UInt32(size) || size > 4096 || !candidate.Bytes(size, bytes) || candidate.Remaining() || std::find(bytes.begin(), bytes.end(), uint8_t(0)) != bytes.end() || !output->Add("value", ECS::SceneStringValue{reinterpret_cast<cstring>(bytes.data()), size}))
                    return false;
                *input = candidate;
                return true;
            }
            static bool EncodeYAML(void*, const ECS::SceneValue& payload, ECS::SceneYAML::Node* output, ECS::SceneDiagnostics* diagnostics)
            {
                return Value(payload) && ECS::EncodeSceneValueYAML(payload, output, diagnostics);
            }
            static bool DecodeYAML(void*, uint32_t version, const ECS::SceneYAML::Node& input, ECS::SceneValueWriter* output, ECS::SceneDiagnostics*)
            {
                const auto node = ECS::FindSceneYAMLField(input, "value");
                if (version != 1 || !output || !input.IsMap() || input.size() != 1 || !node.IsScalar())
                    return false;
                const auto text = node.Scalar();
                return text.size() <= 4096 && text.find('\0') == std::string_view::npos && output->Add("value", ECS::SceneStringValue{text.data(), static_cast<uint32_t>(text.size())});
            }
        };

        // Borrow only the bounded settings prefix. Entity rows are synthesized
        // on demand, including unaligned reads, without a large fixture string.
        struct DenseCapture final : ICookCapturedFile
        {
            inline static constexpr char               Row[]        = "  - uuid: \"20000000-0000-0000-0000-000000000001\"\n    components: {}\n";
            Core::Containers::ArrayView<const uint8_t> Prefix       = {};
            Core::Containers::ArrayView<const uint8_t> Pattern      = {reinterpret_cast<const uint8_t*>(Row), sizeof(Row) - 1};
            Core::Containers::ArrayView<const uint8_t> FinalPattern = {};
            uint32_t                                   Entities     = 0;
            Digest256                                  Key          = {};

            uint64_t                                   Size() const noexcept override
            {
                return Prefix.size() + 1 + uint64_t(Entities - (FinalPattern.empty() ? 0 : 1)) * Pattern.size() + FinalPattern.size();
            }
            Digest256 Digest() const noexcept override
            {
                return Key;
            }
            VFS::VFSResult<size_t> Read(uint64_t offset, Core::Containers::ArrayView<uint8_t> output) const override
            {
                if (offset > Size())
                    return VFS::VFSResult<size_t>::Fail(VFS::VFSError::Corrupted);
                const size_t count = static_cast<size_t>(std::min<uint64_t>(output.size(), Size() - offset));
                size_t       done  = 0;
                if (offset < Prefix.size())
                {
                    const size_t copied = std::min<size_t>(count, Prefix.size() - static_cast<size_t>(offset));
                    (void) Helpers::secure_memcpy(output.data(), output.size(), Prefix.data() + offset, copied);
                    offset += copied;
                    done   += copied;
                }
                if (offset == Prefix.size() && done < count)
                {
                    output[done++] = '\n';
                    ++offset;
                }
                while (done < count)
                {
                    const auto     at       = offset - Prefix.size() - 1;
                    const uint64_t repeated = uint64_t(Entities - (FinalPattern.empty() ? 0 : 1)) * Pattern.size();
                    const bool     final    = !FinalPattern.empty() && at >= repeated;
                    const auto     pattern  = final ? FinalPattern : Pattern;
                    const auto     index    = final ? Entities - 1 : at / Pattern.size();
                    const size_t   within   = final ? at - repeated : at % Pattern.size();
                    const size_t   copied   = std::min(count - done, pattern.size() - within);
                    (void) Helpers::secure_memcpy(output.data() + done, output.size() - done, pattern.data() + within, copied);
                    // Descending source IDs exercise canonical entity ordering.
                    uint64_t         id     = Entities - index;
                    constexpr size_t digits = sizeof("  - uuid: \"20000000-0000-0000-0000-") - 1;
                    for (size_t i = 0; i < 12; ++i, id >>= 4)
                        if (digits + 11 - i >= within && digits + 11 - i - within < copied)
                            output[done + digits + 11 - i - within] = "0123456789abcdef"[id & 15];
                    offset += copied;
                    done   += copied;
                }
                return VFS::VFSResult<size_t>::Ok(count);
            }
            void Configure(uint32_t entities)
            {
                ASSERT_GE(Pattern.size(), sizeof(Row) - 1);
                ASSERT_TRUE(FinalPattern.empty() || entities > 0);
                Entities                                               = entities;
                Core::Containers::FixedArray<uint8_t, ZKilo(64)> bytes = {};
                Core::Hash::SHA256                               hash  = {};
                for (uint64_t offset = 0; offset < Size();)
                {
                    const auto read = Read(offset, bytes);
                    ASSERT_TRUE(read.Succeeded());
                    ASSERT_GT(read.Value(), 0u);
                    ASSERT_TRUE(hash.Update({bytes.data(), read.Value()}).Succeeded());
                    offset += read.Value();
                }
                ASSERT_TRUE(hash.Finalize(&Key).Succeeded());
            }
        };

        // Inspect the complete wire tables without retaining another output
        // copy or decoding another dense tree inside the measured allocation run.
        struct DenseSink final : ICookByteSink
        {
            uint32_t    Entities            = 0;
            uuids::uuid UUID                = {};
            size_t      Calls               = 0;
            size_t      Bytes               = 0;
            Digest256   Digest              = {};
            uint32_t    ComponentsPerEntity = 0;
            uint32_t    ScalarBytes         = 0;
            uint32_t    NumericFields       = 0;
            uint32_t    FirstFields         = 0;

            DenseSink(uint32_t entities, const uuids::uuid& uuid) : Entities(entities), UUID(uuid) {}

            VFS::VFSResult<void> Write(Core::Containers::ArrayView<const uint8_t> bytes) override
            {
                ++Calls;
                const auto word = [&](size_t at) {
                    uint32_t value = 0;
                    for (size_t i = 0; i < 4; ++i)
                        value |= uint32_t(bytes[at + i]) << (8 * i);
                    return value;
                };
                const auto corrupt = VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                if (Calls != 1 || bytes.size() < ECS::SceneBinaryFormat::HeaderBytes)
                    return corrupt;
                const size_t     table            = ECS::SceneBinaryFormat::HeaderBytes + word(48) + ECS::SceneGridBinaryBytes + ECS::SceneSkyBinaryBytes;
                constexpr size_t key_bytes        = sizeof("DenseName00") - 1;
                const size_t     components       = size_t(Entities) * ComponentsPerEntity;
                const size_t     component_table  = table + size_t(Entities) * ECS::SceneBinaryFormat::EntityBytes;
                const size_t     data             = component_table + components * ECS::SceneBinaryFormat::ComponentBytes;
                const size_t     expected_payload = NumericFields ? components * (key_bytes + 4 + 4 * NumericFields) - 4 * (NumericFields - FirstFields) : components * (key_bytes + 4 + ScalarBytes);
                if (word(0) != ECS::SceneBinaryFormat::Magic || word(4) != ECS::SceneBinaryFormat::Version || word(8) != ECS::SceneBinaryFormat::HeaderBytes || word(12) != ECS::SceneBinaryFormat::LittleEndianMark || word(16) || word(20) != ECS::SceneDocument::SchemaVersion || word(24) != bytes.size() || word(28) != ECS::SceneBinaryFormat::Checksum({bytes.data(), bytes.size()}) || word(52) != Entities || word(56) != components || word(60) || data > bytes.size() || bytes.size() - data != expected_payload ||
                    !std::equal(UUID.as_bytes().begin(), UUID.as_bytes().end(), reinterpret_cast<const std::byte*>(bytes.data() + 32)))
                    return corrupt;
                for (uint32_t i = 0; i < Entities; ++i)
                {
                    const size_t row = table + size_t(i) * ECS::SceneBinaryFormat::EntityBytes;
                    for (size_t b = 0; b < 16; ++b)
                    {
                        const uint8_t expected = b == 0 ? 0x20 : b < 10 ? 0 : uint8_t(uint64_t(i + 1) >> ((15 - b) * 8));
                        if (bytes[row + b] != expected)
                            return corrupt;
                    }
                    if (word(row + 16) != i * ComponentsPerEntity || word(row + 20) != ComponentsPerEntity)
                        return corrupt;
                }
                for (size_t i = 0; i < components; ++i)
                {
                    const size_t row = component_table + i * ECS::SceneBinaryFormat::ComponentBytes;
                    if (NumericFields)
                    {
                        const size_t fields  = i ? NumericFields : FirstFields;
                        const size_t key     = data + i * (key_bytes + 4 + 4 * NumericFields) - (i ? 4 * (NumericFields - FirstFields) : 0);
                        const size_t payload = key + key_bytes;
                        if (word(row) != key || word(row + 4) != key_bytes || word(row + 8) != 1 || word(row + 12) != payload || word(row + 16) != 4 + 4 * fields || word(payload) != fields || !std::equal(bytes.begin() + key, bytes.begin() + payload, "DenseFields"))
                            return corrupt;
                        for (size_t field = 0; field < fields; ++field)
                            if (word(payload + 4 + 4 * field) != field)
                                return corrupt;
                        continue;
                    }
                    const size_t key     = data + i * (key_bytes + 4 + ScalarBytes);
                    const size_t payload = key + key_bytes;
                    if (word(row) != key || word(row + 4) != key_bytes || word(row + 8) != 1 || word(row + 12) != payload || word(row + 16) != 4 + ScalarBytes || word(payload) != ScalarBytes)
                        return corrupt;
                    for (size_t b = 0; b < key_bytes; ++b)
                    {
                        const size_t  component = i % ComponentsPerEntity;
                        const uint8_t expected  = b < key_bytes - 2 ? "DenseName"[b] : uint8_t('0' + (b == key_bytes - 2 ? component / 10 : component % 10));
                        if (bytes[key + b] != expected)
                            return corrupt;
                    }
                    if (!std::all_of(bytes.begin() + payload + 4, bytes.begin() + payload + 4 + ScalarBytes, [](uint8_t value) { return value == 'x'; }))
                        return corrupt;
                }
                Core::Hash::SHA256 hash = {};
                if (hash.Update(bytes).Failed() || hash.Finalize(&Digest).Failed())
                    return corrupt;
                Bytes = bytes.size();
                return VFS::VFSResult<void>::Ok();
            }
        };

        struct BinarySink final : ICookByteSink
        {
            Core::Containers::FixedArray<uint8_t, ZKilo(4)> Bytes = {};
            size_t                                          Count = 0;
            size_t                                          Calls = 0;

            VFS::VFSResult<void>                            Write(Core::Containers::ArrayView<const uint8_t> bytes) override
            {
                ++Calls;
                if (bytes.size() > Bytes.size() - Count)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                (void) Helpers::secure_memcpy(Bytes.data() + Count, Bytes.size() - Count, bytes.data(), bytes.size());
                Count += bytes.size();
                return VFS::VFSResult<void>::Ok();
            }
        };

        Core::Memory::ArenaAllocator                     RegistryArena  = {};
        ECS::SceneComponentSchemaRegistry                Schemas        = {};
        Tests::GeneratedResourceCapture                  Capture        = {};
        Core::Containers::FixedArray<uint8_t, ZKilo(4)>  Source         = {};
        size_t                                           SourceBytes    = 0;
        DenseCapture                                     Dense          = {};
        uint32_t                                         EmptyNodes     = 0;
        Core::Containers::FixedArray<uint8_t, ZKilo(32)> ComponentRow   = {};
        Core::Containers::FixedArray<uint8_t, ZKilo(2)>  FinalRow       = {};
        NumericCodec                                     Numeric        = {};
        uint64_t                                         WorkingCeiling = ZMega(96);
        uuids::uuid                                      SceneUUID      = uuids::uuid::from_string("10000000-0000-0000-0000-000000000001").value();

        void                                             SetUp() override
        {
            Limits.MaxWorkingBytes = WorkingCeiling;
            ResourceProcessTest::SetUp();
            ASSERT_FALSE(HasFatalFailure());
            RegistryArena.Initialize(ZMega(4), 0);
            ASSERT_NE(RegistryArena.m_memory, nullptr);
            Schemas.Initialize(&RegistryArena);
            ASSERT_TRUE(ECS::RegisterBuiltInSceneComponentSchemas(Schemas));
            // Only this bounded fixture preparation uses the authoring emitter.
            // The 32 MiB input is generated later, never copied into a fixture.
            ECS::Scene scene = {};
            scene.Initialize(&RegistryArena);
            ECS::SceneDocument document = {};
            ASSERT_TRUE(ECS::SceneDocument::Capture(scene, {.UUID = SceneUUID, .Name = "Encoded cap"}, Schemas, &document, nullptr));
            std::string yaml = {};
            ASSERT_TRUE(document.Emit(&yaml, nullptr));
            ASSERT_LT(yaml.size(), Source.size());
            SourceBytes = yaml.size();
            (void) Helpers::secure_memcpy(Source.data(), Source.size(), yaml.data(), yaml.size());
            const auto entities = yaml.rfind("[]");
            ASSERT_NE(entities, std::string::npos);
            ASSERT_EQ(yaml.find_first_not_of(" \r\n", entities + 2), std::string::npos);
            Dense.Prefix = {Source.data(), entities};
            scene.Shutdown();
            ECS::SceneYAML::Storage storage = {};
            ASSERT_TRUE(storage.Initialize(ZMega(1), 4096));
            auto root = storage.Destination();
            ASSERT_TRUE(ECS::ReadSceneYAML(yaml, Limits.SceneLimits, &root, nullptr));
            const auto mark = storage.Mark();
            EmptyNodes      = static_cast<uint32_t>(mark.Nodes);
            storage.Rollback(mark);
        }

        void Configure(uint64_t size)
        {
            ASSERT_GE(size, SourceBytes);
            Capture.Reset();
            for (size_t offset = 0; offset < SourceBytes;)
            {
                const auto count = std::min<size_t>(512, SourceBytes - offset);
                Capture.Append({Source.data() + offset, count}, count);
                offset += count;
            }
            constexpr uint8_t space = ' ';
            Capture.Append({&space, 1}, size - SourceBytes);
            Capture.HashContents();
            Capture.Reads = Capture.MaxReadEnd = 0;
        }

        CookSceneInput SceneInput() const
        {
            return {&Capture, VFS::VFSRelativePath::Parse("scenes/generated.scene").Value(), &Limits};
        }

        CookSceneInput DenseInput() const
        {
            return {&Dense, VFS::VFSRelativePath::Parse("scenes/dense.scene").Value(), &Limits};
        }

        void ConfigureComponentRow(uint32_t components, uint32_t scalar_bytes)
        {
            size_t     used   = 0;
            const auto append = [&](cstring text) {
                const size_t count = Helpers::secure_strlen(text);
                if (count > ComponentRow.size() - used)
                    return false;
                (void) Helpers::secure_memcpy(ComponentRow.data() + used, ComponentRow.size() - used, text, count);
                used += count;
                return true;
            };
            ASSERT_GT(components, 0u);
            ASSERT_LE(components, 65u);
            ASSERT_TRUE(append("  - uuid: \"20000000-0000-0000-0000-000000000001\"\n    components:\n"));
            for (uint32_t i = components; i > 0; --i)
            {
                char      header[128] = {};
                const int length      = std::snprintf(header, sizeof(header), "      DenseName%02u:\n        schema_version: 1\n        fields: {value: \"", i - 1);
                ASSERT_GT(length, 0);
                ASSERT_LT(static_cast<size_t>(length), sizeof(header));
                ASSERT_TRUE(append(header));
                ASSERT_LE(scalar_bytes, ComponentRow.size() - used);
                std::fill_n(ComponentRow.data() + used, scalar_bytes, uint8_t('x'));
                used += scalar_bytes;
                ASSERT_TRUE(append("\"}\n"));
            }
            Dense.Pattern = {ComponentRow.data(), used};
        }

        void RegisterWireNameSchemas(ECS::SceneComponentSchemaRegistry& registry)
        {
            registry.Initialize(&RegistryArena, 65);
            const auto* name = Schemas.FindByKey("NameComponent");
            ASSERT_NE(name, nullptr);
            // Synthetic wire-only aliases share the real built-in name codec.
            // Runtime slots are unique within this independent registry; these
            // tests do not instantiate or claim coverage of candidate ECS types.
            for (uint32_t i = 0; i < 65; ++i)
            {
                char      key[32] = {};
                const int length  = std::snprintf(key, sizeof(key), "DenseName%02u", i);
                ASSERT_GT(length, 0);
                ASSERT_LT(static_cast<size_t>(length), sizeof(key));
                ASSERT_TRUE(registry.Register({.Key = key, .Version = name->Version, .RuntimeType = i, .Fields = name->Fields, .FieldCount = name->FieldCount, .Codecs = name->Codecs}, nullptr));
            }
        }

        void RegisterLongStringSchema(ECS::SceneComponentSchemaRegistry& registry)
        {
            registry.Initialize(&RegistryArena, 1);
            ASSERT_TRUE(registry.Register(
                {
                .Key = "DenseName00", .Version = 1, .RuntimeType = ECS::ComponentTypeOf<LongStringComponent>(), .Fields = &LongStringCodec::Field, .FieldCount = 1, .Codecs = {.Capture = LongStringCodec::Capture, .PopulateCandidate = LongStringCodec::Populate, .EncodeBinary = LongStringCodec::Encode, .DecodeBinary = LongStringCodec::Decode, .EncodeYAML = LongStringCodec::EncodeYAML, .DecodeYAML = LongStringCodec::DecodeYAML}
            },
                nullptr));
        }

        void RegisterNumericSchema(ECS::SceneComponentSchemaRegistry& registry)
        {
            registry.Initialize(&RegistryArena, 1);
            ASSERT_TRUE(registry.Register(
                {
                .Key = "DenseFields", .Version = 1, .RuntimeType = ECS::ComponentTypeOf<NumericComponent>(), .Fields = Numeric.Fields.data(), .FieldCount = static_cast<uint32_t>(Numeric.Fields.size()), .Codecs = {.Context = &Numeric, .Capture = NumericCodec::Capture, .PopulateCandidate = NumericCodec::Populate, .EncodeBinary = NumericCodec::Encode, .DecodeBinary = NumericCodec::Decode, .EncodeYAML = NumericCodec::EncodeYAML, .DecodeYAML = NumericCodec::DecodeYAML}
            },
                nullptr));
        }

        void ConfigureNumericRow(uint32_t fields, Core::Containers::ArrayView<uint8_t> storage, Core::Containers::ArrayView<const uint8_t>& pattern, bool extra_node = false)
        {
            ASSERT_LE(fields, 65u);
            const int header = std::snprintf(reinterpret_cast<char*>(storage.data()), storage.size(), "  - uuid: \"20000000-0000-0000-0000-000000000001\"\n    components:\n      DenseFields:\n        schema_version: 1\n        fields: {");
            ASSERT_GT(header, 0);
            ASSERT_LT(static_cast<size_t>(header), storage.size());
            size_t used = static_cast<size_t>(header);
            // Descending input fields exercise canonical codec ordering too.
            for (uint32_t i = fields; i > 0; --i)
            {
                // Replacing scalar 0 with [0] adds exactly one YAML node. This
                // malformed typed value is used only for parser-limit coverage.
                const int count = extra_node && i == 1 ? std::snprintf(reinterpret_cast<char*>(storage.data() + used), storage.size() - used, ", v00: [0]") : std::snprintf(reinterpret_cast<char*>(storage.data() + used), storage.size() - used, "%sv%02u: %u", i == fields ? "" : ", ", i - 1, i - 1);
                ASSERT_GT(count, 0);
                ASSERT_LT(static_cast<size_t>(count), storage.size() - used);
                used += static_cast<size_t>(count);
            }
            ASSERT_GE(storage.size() - used, 2u);
            storage[used++] = '}';
            storage[used++] = '\n';
            pattern         = {storage.data(), used};
        }

        VFS::VFSResult<void> Encode(BinarySink& sink, CookSceneOutput& output)
        {
            CookSceneCodec codec(Budget, Schemas);
            return codec.Encode(SceneInput(), SceneUUID, CookTarget::DesktopVulkanLE64, sink, &output, nullptr);
        }

        void CheckDenseWorkingBudgets(cstring label, const ECS::SceneComponentSchemaRegistry& registry, uint32_t components, uint32_t scalar_bytes, uint32_t numeric_fields = 0, uint32_t first_fields = 0)
        {
            for (const bool encoding : {false, true})
            {
                SCOPED_TRACE(encoding);
                const auto execute = [&](DenseSink& sink, CookSceneOutput& output) {
                    CookSceneCodec codec(Budget, registry);
                    if (encoding)
                        return codec.Encode(DenseInput(), SceneUUID, CookTarget::DesktopVulkanLE64, sink, &output, nullptr);
                    ECS::SceneDocument document = {};
                    const auto         read     = codec.Read(DenseInput(), &document, nullptr);
                    if (read.Succeeded())
                    {
                        EXPECT_EQ(document.EntityCount(), Dense.Entities);
                        EXPECT_EQ(document.Settings()->UUID, SceneUUID);
                    }
                    return read;
                };
                ResetBudget(WorkingCeiling);
                DenseSink first(Dense.Entities, SceneUUID);
                first.ComponentsPerEntity = components;
                first.ScalarBytes         = scalar_bytes;
                first.NumericFields       = numeric_fields;
                first.FirstFields         = first_fields;
                CookSceneOutput canonical = {};
                ASSERT_TRUE(execute(first, canonical).Succeeded());
                const auto peak = Budget.Statistics().PeakBytes;
                ExpectDrained();
                if (encoding)
                {
                    EXPECT_EQ(first.Calls, 1u);
                    EXPECT_EQ(canonical.Bytes, first.Bytes);
                    EXPECT_EQ(canonical.Digest, first.Digest);
                }
                ResetBudget(peak - 1);
                DenseSink refused(Dense.Entities, SceneUUID);
                refused.ComponentsPerEntity = components;
                refused.ScalarBytes         = scalar_bytes;
                refused.NumericFields       = numeric_fields;
                refused.FirstFields         = first_fields;
                CookSceneOutput output      = {.UUID = SceneUUID, .Digest = Dense.Key, .Bytes = 77};
                EXPECT_EQ(execute(refused, output).Error(), VFS::VFSError::SizeLimitExceeded);
                EXPECT_EQ(refused.Calls, 0u);
                EXPECT_EQ(output.UUID, SceneUUID);
                EXPECT_EQ(output.Digest, Dense.Key);
                EXPECT_EQ(output.Bytes, 77u);
                ExpectDrained();
                ResetBudget(peak);
                DenseSink exact(Dense.Entities, SceneUUID);
                exact.ComponentsPerEntity = components;
                exact.ScalarBytes         = scalar_bytes;
                exact.NumericFields       = numeric_fields;
                exact.FirstFields         = first_fields;
                ASSERT_TRUE(execute(exact, output).Succeeded());
                EXPECT_EQ(Budget.Statistics().PeakBytes, peak);
                if (encoding)
                {
                    EXPECT_EQ(output.Bytes, canonical.Bytes);
                    EXPECT_EQ(output.Digest, canonical.Digest);
                    EXPECT_EQ(exact.Digest, first.Digest);
                }
                ExpectDrained();
                std::printf("%s scene %s: %u entities, %u components each, %u-byte strings, %u numeric fields, %llu source bytes, %zu binary bytes, %llu modeled working bytes, digest=%s\n", label, encoding ? "encode" : "read", Dense.Entities, components, scalar_bytes, numeric_fields, static_cast<unsigned long long>(Dense.Size()), first.Bytes, static_cast<unsigned long long>(peak), canonical.Digest.Hex().data());
            }
        }

        void ExpectDenseRefusal(const ECS::SceneComponentSchemaRegistry& registry, VFS::VFSError error, CookDiagnosticCode code)
        {
            Configure(SourceBytes);
            ASSERT_FALSE(HasFatalFailure());
            CookSceneCodec     owner(Budget, Schemas);
            ECS::SceneDocument previous = {};
            ASSERT_TRUE(owner.Read(SceneInput(), &previous, nullptr).Succeeded());
            const auto* settings = previous.Settings();
            const auto  live     = Budget.Statistics().LiveBytes;
            for (const bool encoding : {false, true})
            {
                SCOPED_TRACE(encoding);
                CookSceneCodec  codec(Budget, registry);
                CookDiagnostics diagnostics = {};
                DenseSink       sink(Dense.Entities, SceneUUID);
                CookSceneOutput output = {.UUID = SceneUUID, .Digest = Dense.Key, .Bytes = 77};
                const auto      result = encoding ? codec.Encode(DenseInput(), SceneUUID, CookTarget::DesktopVulkanLE64, sink, &output, &diagnostics) : codec.Read(DenseInput(), &previous, &diagnostics);
                EXPECT_EQ(result.Error(), error);
                ASSERT_GT(diagnostics.Count, 0u);
                EXPECT_EQ(diagnostics.Entries[0].Code, code);
                EXPECT_EQ(diagnostics.Entries[0].Stage, encoding ? CookStage::Encode : CookStage::Discover);
                EXPECT_EQ(previous.Settings(), settings);
                EXPECT_EQ(previous.Settings()->UUID, SceneUUID);
                EXPECT_EQ(previous.EntityCount(), 0u);
                EXPECT_EQ(sink.Calls, 0u);
                EXPECT_EQ(output.UUID, SceneUUID);
                EXPECT_EQ(output.Digest, Dense.Key);
                EXPECT_EQ(output.Bytes, 77u);
                EXPECT_EQ(Budget.Statistics().LiveBytes, live);
            }
        }

        void ExpectBinary(const BinarySink& actual, const BinarySink& expected, const CookSceneOutput& output)
        {
            ASSERT_EQ(actual.Count, expected.Count);
            EXPECT_TRUE(std::equal(actual.Bytes.begin(), actual.Bytes.begin() + actual.Count, expected.Bytes.begin()));
            EXPECT_EQ(actual.Calls, 1u);
            EXPECT_EQ(output.Bytes, actual.Count);
            ECS::SceneDocument decoded = {};
            ASSERT_TRUE(ECS::BinarySceneCodec::Decode({actual.Bytes.data(), actual.Count}, Schemas, &decoded, nullptr));
            ASSERT_NE(decoded.Settings(), nullptr);
            EXPECT_EQ(decoded.Settings()->UUID, SceneUUID);
            EXPECT_EQ(decoded.EntityCount(), 0u);
        }
    };

    class SceneCookNodeResourceProcessTest : public SceneCookSceneResourceProcessTest
    {
    protected:
        void SetUp() override
        {
            // Explicit stress-fixture backing, not a change to any production
            // preset or the smaller existing resource fixtures.
            WorkingCeiling = ZMega(192);
            SceneCookSceneResourceProcessTest::SetUp();
        }

        void ConfigureNodeCap(ECS::SceneComponentSchemaRegistry& registry)
        {
            ASSERT_EQ(EmptyNodes, 175u);
            ASSERT_EQ(Limits.SceneLimits.MaxNodes, 1048576u);
            ConfigureNumericRow(64, ComponentRow, Dense.Pattern);
            ConfigureNumericRow(26, FinalRow, Dense.FinalPattern);
            Dense.Configure(7543);
            ASSERT_EQ(uint64_t(EmptyNodes) + 7542 * (11 + 2 * 64) + (11 + 2 * 26), Limits.SceneLimits.MaxNodes);
            RegisterNumericSchema(registry);
        }
    };

    TEST_F(SceneCookNodeResourceProcessTest, DefaultDecodedTreeBudgetRefusesExactNodeCapWithoutChangingPriorState)
    {
        ECS::SceneComponentSchemaRegistry registry = {};
        ConfigureNodeCap(registry);
        ASSERT_FALSE(HasFatalFailure());
        ASSERT_EQ(Limits.SceneLimits.MaxDecodedBytes, ZMega(128));
        ExpectDenseRefusal(registry, VFS::VFSError::SizeLimitExceeded, CookDiagnosticCode::BudgetExceeded);
    }

    TEST_F(SceneCookNodeResourceProcessTest, ParserAcceptsExactDefaultNodeCapAndRollsBackLowerCapAndOneExtraNode)
    {
        ECS::SceneComponentSchemaRegistry registry = {};
        ConfigureNodeCap(registry);
        ASSERT_FALSE(HasFatalFailure());
        {
            CookWorkspace source   = {};
            const auto    capacity = static_cast<size_t>(Dense.Size()) + 2;
            ASSERT_TRUE(source.Initialize(Budget, capacity, CookMemoryCategory::Codecs).Succeeded());
            auto* bytes = static_cast<uint8_t*>(source.Arena()->AllocateNoZero(capacity, 1));
            ASSERT_NE(bytes, nullptr);
            for (uint32_t failure = 0; failure < 3; ++failure)
            {
                SCOPED_TRACE(failure);
                if (failure == 2)
                {
                    ConfigureNumericRow(26, FinalRow, Dense.FinalPattern, true);
                    Dense.Configure(7543);
                    ASSERT_FALSE(HasFatalFailure());
                    ASSERT_EQ(Dense.Size(), capacity);
                }
                const auto read_source = Dense.Read(0, {bytes, static_cast<size_t>(Dense.Size())});
                ASSERT_TRUE(read_source.Succeeded());
                ASSERT_EQ(read_source.Value(), Dense.Size());
                auto limits      = Limits.SceneLimits;
                limits.MaxNodes -= failure == 1 ? 1 : 0;
                CookCodecAllocationRoute                 route(Budget);
                Core::Serialization::YAMLAllocationScope scope(route);
                ECS::SceneYAML::Storage                  storage = {};
                ASSERT_TRUE(storage.Initialize(limits.MaxDecodedBytes, 2 * Limits.SceneLimits.MaxNodes, limits.MaxCollectionItems, limits.MaxScalarBytes, &route));
                auto                  root        = storage.Destination();
                const auto            before      = storage.Mark();
                ECS::SceneDiagnostics diagnostics = {};
                const bool            read        = ECS::ReadSceneYAML({reinterpret_cast<cstring>(bytes), static_cast<size_t>(Dense.Size())}, limits, &root, &diagnostics);
                EXPECT_EQ(read, failure == 0);
                EXPECT_EQ(route.Error(), VFS::VFSError::OK);
                const auto after = storage.Mark();
                if (failure)
                {
                    EXPECT_TRUE(diagnostics.HasError(ECS::SceneDiagnosticCode::LimitExceeded));
                    EXPECT_EQ(after.Nodes, before.Nodes);
                    EXPECT_EQ(after.Links, before.Links);
                    EXPECT_EQ(after.NodeData, before.NodeData);
                    EXPECT_EQ(after.LinkData, before.LinkData);
                    EXPECT_FALSE(root.IsDefined());
                }
                else
                {
                    EXPECT_EQ(after.Nodes, Limits.SceneLimits.MaxNodes);
                    EXPECT_EQ(root["scene"]["entities"].size(), Dense.Entities);
                    std::printf("Exact node-cap parser: %zu nodes, %llu source bytes, %llu modeled working bytes\n", after.Nodes, static_cast<unsigned long long>(Dense.Size()), static_cast<unsigned long long>(Budget.Statistics().PeakBytes));
                }
            }
        }
        ExpectDrained();
    }

    TEST_F(SceneCookNodeResourceProcessTest, FullFieldDensityHonorsExactWorkingBudgets)
    {
        ConfigureNumericRow(64, ComponentRow, Dense.Pattern);
        Dense.Configure(2048);
        ECS::SceneComponentSchemaRegistry registry = {};
        RegisterNumericSchema(registry);
        ASSERT_FALSE(HasFatalFailure());
        CheckDenseWorkingBudgets("Field-cap", registry, 1, 0, 64, 64);
    }

    TEST_F(SceneCookNodeResourceProcessTest, OneExtraFieldRefusesBeforeSinkWrites)
    {
        ECS::SceneComponentSchemaRegistry registry = {};
        RegisterNumericSchema(registry);
        ASSERT_FALSE(HasFatalFailure());
        ConfigureNumericRow(65, ComponentRow, Dense.Pattern);
        Dense.Configure(1);
        ExpectDenseRefusal(registry, VFS::VFSError::Corrupted, CookDiagnosticCode::InvalidScene);
    }

    TEST_F(SceneCookSceneResourceProcessTest, ActualScalarCapHonorsExactWorkingBudgetsWithRegisteredStringValues)
    {
        ASSERT_EQ(Limits.SceneLimits.MaxScalarBytes, 4096u);
        ConfigureComponentRow(1, Limits.SceneLimits.MaxScalarBytes);
        Dense.Configure(2048);
        ECS::SceneComponentSchemaRegistry registry = {};
        RegisterLongStringSchema(registry);
        ASSERT_FALSE(HasFatalFailure());
        CheckDenseWorkingBudgets("Scalar-cap", registry, 1, Limits.SceneLimits.MaxScalarBytes);
    }

    TEST_F(SceneCookSceneResourceProcessTest, LowerScalarCapAndOneExtraScalarBytePreservePriorDocumentsAndResults)
    {
        ECS::SceneComponentSchemaRegistry registry = {};
        RegisterLongStringSchema(registry);
        ASSERT_FALSE(HasFatalFailure());
        for (const bool over_cap : {false, true})
        {
            SCOPED_TRACE(over_cap);
            ResetBudget(ZMega(96));
            ConfigureComponentRow(1, over_cap ? 4097 : 4096);
            Dense.Configure(2048);
            ASSERT_FALSE(HasFatalFailure());
            Limits.SceneLimits.MaxScalarBytes = over_cap ? 4096 : 4095;
            ExpectDenseRefusal(registry, VFS::VFSError::SizeLimitExceeded, CookDiagnosticCode::BudgetExceeded);
            Limits.SceneLimits.MaxScalarBytes = 4096;
            ExpectDrained();
        }
    }

    TEST_F(SceneCookSceneResourceProcessTest, FullComponentCapAndBuiltInNameValueCapHonorExactWorkingBudgets)
    {
        ASSERT_EQ(Limits.SceneLimits.MaxComponentsPerEntity, 64u);
        constexpr uint32_t scalar_bytes = sizeof(ECS::Components::NameComponent{}.Value) - 1;
        ConfigureComponentRow(64, scalar_bytes);
        Dense.Configure(512);
        ECS::SceneComponentSchemaRegistry registry = {};
        RegisterWireNameSchemas(registry);
        ASSERT_FALSE(HasFatalFailure());
        CheckDenseWorkingBudgets("Component-cap", registry, 64, scalar_bytes);
    }

    TEST_F(SceneCookSceneResourceProcessTest, ComponentCapAndNameSemanticFailuresPreservePriorDocumentsAndResults)
    {
        const auto                        original = Limits.SceneLimits;
        ECS::SceneComponentSchemaRegistry registry = {};
        RegisterWireNameSchemas(registry);
        ASSERT_FALSE(HasFatalFailure());
        for (uint32_t failure = 0; failure < 4; ++failure)
        {
            SCOPED_TRACE(failure);
            ResetBudget(ZMega(96));
            ConfigureComponentRow(failure == 1 ? 65 : 64, failure == 3 ? 128 : 127);
            Dense.Configure(512);
            ASSERT_FALSE(HasFatalFailure());
            if (failure == 0)
                --Limits.SceneLimits.MaxComponentsPerEntity;
            else if (failure == 2)
                Limits.SceneLimits.MaxScalarBytes = 126;
            ExpectDenseRefusal(registry, failure == 3 ? VFS::VFSError::Corrupted : VFS::VFSError::SizeLimitExceeded, failure == 3 ? CookDiagnosticCode::InvalidScene : CookDiagnosticCode::BudgetExceeded);
            Limits.SceneLimits = original;
            ExpectDrained();
        }
    }

    TEST_F(SceneCookSceneResourceProcessTest, ActualEntityCapReadsAndEncodesAtExactPrincipalWorkingBudgets)
    {
        ASSERT_EQ(Limits.SceneLimits.MaxEntities, 65536u);
        Dense.Configure(Limits.SceneLimits.MaxEntities);
        ASSERT_FALSE(HasFatalFailure());
        CheckDenseWorkingBudgets("Entity-cap", Schemas, 0, 0);
    }

    TEST_F(SceneCookSceneResourceProcessTest, DenseSemanticLimitFailuresPreservePreviousDocumentsAndRefuseSinkWrites)
    {
        const auto original = Limits.SceneLimits;
        for (uint32_t failure = 0; failure < 5; ++failure)
        {
            SCOPED_TRACE(failure);
            ResetBudget(ZMega(96));
            Configure(SourceBytes);
            ASSERT_FALSE(HasFatalFailure());
            {
                // Keep the previous document's route alive, independent of the
                // fresh attempt's sticky refusal state.
                CookSceneCodec     owner(Budget, Schemas);
                ECS::SceneDocument previous = {};
                ASSERT_TRUE(owner.Read(SceneInput(), &previous, nullptr).Succeeded());
                const auto* settings = previous.Settings();
                const auto  live     = Budget.Statistics().LiveBytes;
                Dense.Configure(original.MaxEntities + (failure == 4 ? 1 : 0));
                ASSERT_FALSE(HasFatalFailure());
                if (failure == 0)
                    --Limits.SceneLimits.MaxEntities;
                else if (failure == 1)
                    --Limits.SceneLimits.MaxCollectionItems;
                else if (failure == 2)
                    Limits.SceneLimits.MaxNodes = EmptyNodes + 5 * Dense.Entities - 1;
                else if (failure == 3)
                    Limits.SceneLimits.MaxDecodedBytes = ZKilo(64);
                for (const bool encoding : {false, true})
                {
                    SCOPED_TRACE(encoding);
                    CookSceneCodec  codec(Budget, Schemas);
                    CookDiagnostics diagnostics = {};
                    DenseSink       sink(Dense.Entities, SceneUUID);
                    CookSceneOutput output = {.UUID = SceneUUID, .Digest = Dense.Key, .Bytes = 77};
                    const auto      result = encoding ? codec.Encode(DenseInput(), SceneUUID, CookTarget::DesktopVulkanLE64, sink, &output, &diagnostics) : codec.Read(DenseInput(), &previous, &diagnostics);
                    EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
                    ASSERT_GT(diagnostics.Count, 0u);
                    EXPECT_EQ(diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
                    EXPECT_EQ(diagnostics.Entries[0].Stage, encoding ? CookStage::Encode : CookStage::Discover);
                    EXPECT_EQ(previous.Settings(), settings);
                    EXPECT_EQ(previous.Settings()->UUID, SceneUUID);
                    EXPECT_EQ(previous.EntityCount(), 0u);
                    EXPECT_EQ(sink.Calls, 0u);
                    EXPECT_EQ(output.UUID, SceneUUID);
                    EXPECT_EQ(output.Digest, Dense.Key);
                    EXPECT_EQ(output.Bytes, 77u);
                    EXPECT_EQ(Budget.Statistics().LiveBytes, live);
                }
            }
            Limits.SceneLimits = original;
            ExpectDrained();
        }
    }

    TEST_F(SceneCookSceneResourceProcessTest, DenseInputAcceptsTheExactYAMLNodeBoundary)
    {
        Dense.Configure(Limits.SceneLimits.MaxEntities);
        ASSERT_FALSE(HasFatalFailure());
        // Each row contributes a map, two map keys, UUID scalar and components
        // map. The original empty sequence node is retained by the dense input.
        Limits.SceneLimits.MaxNodes = EmptyNodes + 5 * Dense.Entities;
        CookSceneOutput output      = {};
        {
            CookSceneCodec     codec(Budget, Schemas);
            ECS::SceneDocument document = {};
            ASSERT_TRUE(codec.Read(DenseInput(), &document, nullptr).Succeeded());
            EXPECT_EQ(document.EntityCount(), Dense.Entities);
        }
        ExpectDrained();
        {
            CookSceneCodec codec(Budget, Schemas);
            DenseSink      sink(Dense.Entities, SceneUUID);
            ASSERT_TRUE(codec.Encode(DenseInput(), SceneUUID, CookTarget::DesktopVulkanLE64, sink, &output, nullptr).Succeeded());
            EXPECT_EQ(output.Bytes, sink.Bytes);
            EXPECT_EQ(output.Digest, sink.Digest);
        }
        ExpectDrained();
        std::printf("Dense scene: %u exact input YAML nodes for %u entities\n", Limits.SceneLimits.MaxNodes, Dense.Entities);
    }

    TEST_F(SceneCookSceneResourceProcessTest, ActualSourceCapHonorsExactWorkingBudgetsAndEmitsTheSameSmallBinary)
    {
        ASSERT_EQ(Limits.SceneLimits.MaxFileBytes, ZMega(32));
        Configure(SourceBytes);
        ASSERT_FALSE(HasFatalFailure());
        BinarySink      expected  = {};
        CookSceneOutput canonical = {};
        ASSERT_TRUE(Encode(expected, canonical).Succeeded());
        ExpectDrained();
        Configure(Limits.SceneLimits.MaxFileBytes);
        ASSERT_FALSE(HasFatalFailure());
        for (const bool encoding : {false, true})
        {
            SCOPED_TRACE(encoding);
            const auto execute = [&](BinarySink& sink, CookSceneOutput& output) {
                if (encoding)
                    return Encode(sink, output);
                CookSceneCodec     codec(Budget, Schemas);
                ECS::SceneDocument document = {};
                const auto         read     = codec.Read(SceneInput(), &document, nullptr);
                if (read.Succeeded())
                {
                    EXPECT_EQ(document.Settings()->UUID, SceneUUID);
                    EXPECT_EQ(document.EntityCount(), 0u);
                }
                return read;
            };
            ResetBudget(ZMega(96));
            BinarySink      first  = {};
            CookSceneOutput output = canonical;
            ASSERT_TRUE(execute(first, output).Succeeded());
            ExpectDrained();
            const auto peak = Budget.Statistics().PeakBytes;
            EXPECT_GT(peak, Capture.Size());
            EXPECT_LT(peak, Capture.Size() + ZMega(1));
            if (encoding)
                ExpectBinary(first, expected, output);
            ResetBudget(peak - 1);
            BinarySink refused = {};
            output             = canonical;
            EXPECT_EQ(execute(refused, output).Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(refused.Calls, 0u);
            EXPECT_EQ(output.UUID, canonical.UUID);
            EXPECT_EQ(output.Digest, canonical.Digest);
            EXPECT_EQ(output.Bytes, canonical.Bytes);
            ExpectDrained();
            ResetBudget(peak);
            BinarySink exact = {};
            ASSERT_TRUE(execute(exact, output).Succeeded());
            EXPECT_EQ(Budget.Statistics().PeakBytes, peak);
            if (encoding)
                ExpectBinary(exact, expected, output);
            ExpectDrained();
            std::printf("Source-cap scene %s: %llu input bytes, %zu binary bytes, %llu modeled working bytes\n", encoding ? "encode" : "read", static_cast<unsigned long long>(Capture.Size()), expected.Count, static_cast<unsigned long long>(peak));
        }
    }

    TEST_F(SceneCookSceneResourceProcessTest, LowerSourceCapsAndLateReadDigestOrSyntaxFailuresPreservePriorDocumentsAndDrainStorage)
    {
        for (uint32_t failure = 0; failure < 5; ++failure)
        {
            SCOPED_TRACE(failure);
            ResetBudget(ZMega(96));
            Configure(SourceBytes);
            ASSERT_FALSE(HasFatalFailure());
            {
                CookSceneCodec     codec(Budget, Schemas);
                ECS::SceneDocument previous = {};
                ASSERT_TRUE(codec.Read(SceneInput(), &previous, nullptr).Succeeded());
                const auto* settings = previous.Settings();
                Configure(ZMega(32) + (failure == 1 ? 1 : 0));
                ASSERT_FALSE(HasFatalFailure());
                const auto allocations = Native.AllocationCount();
                auto       error       = VFS::VFSError::SizeLimitExceeded;
                if (failure == 0)
                    --Limits.SceneLimits.MaxFileBytes;
                else if (failure == 2)
                {
                    Capture.Chunk      = 997;
                    Capture.FailOffset = Capture.Size() - 1;
                    error              = VFS::VFSError::PermissionDenied;
                }
                else if (failure == 3)
                {
                    Capture.Key.Bytes[0] ^= 1;
                    error                 = VFS::VFSError::Corrupted;
                }
                else if (failure == 4)
                {
                    Capture.PatchOffset = Capture.Size() - 4;
                    Capture.Patch       = {'\n', '!', '!', 0};
                    // Rehash the malformed source: this must reach YAML syntax
                    // validation, not stop at the source-receipt check.
                    Capture.HashContents();
                    Capture.Reads = Capture.MaxReadEnd = 0;
                    error                              = VFS::VFSError::Corrupted;
                }
                EXPECT_EQ(codec.Read(SceneInput(), &previous, nullptr).Error(), error);
                EXPECT_EQ(previous.Settings(), settings);
                EXPECT_EQ(previous.Settings()->UUID, SceneUUID);
                EXPECT_EQ(previous.EntityCount(), 0u);
                BinarySink      sink   = {};
                CookSceneOutput output = {.UUID = SceneUUID, .Bytes = 77};
                EXPECT_EQ(Encode(sink, output).Error(), error);
                EXPECT_EQ(sink.Calls, 0u);
                EXPECT_EQ(output.UUID, SceneUUID);
                EXPECT_EQ(output.Bytes, 77u);
                if (failure < 2)
                {
                    EXPECT_EQ(Native.AllocationCount(), allocations);
                    EXPECT_EQ(Capture.Reads, 0u);
                }
                else if (failure == 2)
                    EXPECT_GT(Capture.MaxReadEnd, Capture.Size() - 997);
            }
            Limits.SceneLimits.MaxFileBytes = ZMega(32);
            ExpectDrained();
        }
    }
} // namespace ZEngine::Cook
