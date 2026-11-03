#pragma once
#include <ZEngine/ECS/SceneDocument.h>
#include <span>

namespace ZEngine::ECS
{
    namespace SceneBinaryFormat
    {
        // Wire v1: eight u32 words (magic, version, header bytes, endian, flags,
        // document version, file bytes, CRC), UUID[16], four u32 words (name
        // bytes, entity count, component count, reserved zero). Then UTF-8 name,
        // fixed grid/sky blocks, entity table, component table, contiguous data.
        // Entity rows: UUID[16], first component u32, component count u32.
        // Component rows: key offset/bytes, version, payload offset/bytes (u32).
        // Offsets are absolute. All integers are little endian; no padding.
        inline constexpr uint32_t Magic            = 0x4353455a; // ASCII ZESC, little endian
        inline constexpr uint32_t Version          = 1;
        inline constexpr uint32_t LittleEndianMark = 0x01020304;
        inline constexpr uint32_t HeaderBytes      = 64;
        inline constexpr uint32_t EntityBytes      = 24;
        inline constexpr uint32_t ComponentBytes   = 20;
        inline constexpr uint32_t ChecksumOffset   = 28;
        // CRC-32/IEEE over the artifact with its checksum word zeroed.
        // Integrity detection only, not authentication of untrusted content.
        [[nodiscard]] uint32_t    Checksum(std::span<const uint8_t> bytes);
    } // namespace SceneBinaryFormat

    /// Portable registered-codec container. No I/O, asset resolution or live-world
    /// publication. Unsupported/opaque schemas fail explicitly, never skip.
    class BinarySceneCodec
    {
    public:
        /// Failure preserves out_bytes and rewinds tentative output-arena storage.
        /// Applies file, entity, component, collection and scalar limits. Tree
        /// node/depth and decoded-memory budgets apply to Decode, not Encode.
        [[nodiscard]] static bool Encode(const SceneDocument& document, const SceneComponentSchemaRegistry& schemas, Core::Memory::ArenaAllocator* output_arena, Core::Containers::Array<uint8_t>* out_bytes, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits = {});
        /// Success owns every value independently of bytes and schemas. Failure
        /// preserves out_document; no candidate ECS entities are created.
        [[nodiscard]] static bool Decode(std::span<const uint8_t> bytes, const SceneComponentSchemaRegistry& schemas, SceneDocument* out_document, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits = {});
    };
} // namespace ZEngine::ECS
