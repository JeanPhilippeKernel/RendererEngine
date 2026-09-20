#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/ComponentTypeID.h>
#include <ZEngine/ECS/EntityID.h>
#include <ZEngine/ECS/SceneDiagnostics.h>
#include <ZEngine/ZEngineDef.h>
#include <uuid.h>
#include <cstdint>

namespace ZEngine::ECS
{
    class Scene;

    class SceneValue;
    class SceneValueWriter;
    class SceneBinaryReader;
    class SceneBinaryWriter;

    enum class SceneFieldClass : uint8_t
    {
        Authored,
        RuntimeDerived,
        EditorOnly,
        Forbidden,
    };

    enum class SceneReferenceKind : uint8_t
    {
        Entity,
        Asset,
    };

    struct SceneFieldSchema
    {
        cstring         Key   = nullptr;
        SceneFieldClass Class = SceneFieldClass::Authored;
    };

    struct SceneReference
    {
        SceneReferenceKind Kind = SceneReferenceKind::Entity;
        uuids::uuid        UUID = {};
    };

    // Returning false stops the walk.
    using SceneReferenceVisitorFn = bool (*)(void* visitor_context, const SceneReference& reference);

    struct SceneUUIDRemap
    {
        void* Context                                                               = nullptr;
        bool (*Find)(void* context, const uuids::uuid& old_uuid, uuids::uuid* out_new_uuid) = nullptr;
    };

    struct SceneReferenceFns
    {
        void* Context                                                                                                                        = nullptr;
        bool (*Enumerate)(void* context, const SceneValue& payload, SceneReferenceVisitorFn visitor, void* visitor_context, SceneDiagnostics* diagnostics) = nullptr;
        bool (*Remap)(void* context, const SceneValue& payload, const SceneUUIDRemap& remap, SceneValueWriter* out_payload, SceneDiagnostics* diagnostics) = nullptr;
    };

    struct SceneComponentCodecFns
    {
        void* Context                                                                                                                              = nullptr;
        bool (*Capture)(void* context, EntityID entity, const Scene& scene, SceneValueWriter* out_payload, SceneDiagnostics* diagnostics)           = nullptr;
        bool (*PopulateCandidate)(void* context, EntityID entity, Scene& candidate, const SceneValue& payload, SceneDiagnostics* diagnostics)       = nullptr;
        bool (*EncodeBinary)(void* context, const SceneValue& payload, SceneBinaryWriter* writer, SceneDiagnostics* diagnostics)                    = nullptr;
        bool (*DecodeBinary)(void* context, uint32_t encoded_version, SceneBinaryReader* reader, SceneValueWriter* out_payload, SceneDiagnostics* diagnostics) = nullptr;
    };

    struct SceneComponentSchemaDesc
    {
        cstring                 Key         = nullptr;
        uint32_t                Version     = 0;
        ComponentTypeID         RuntimeType = {};
        const SceneFieldSchema* Fields      = nullptr;
        uint32_t                FieldCount  = 0;
        SceneComponentCodecFns  Codecs      = {};
        SceneReferenceFns       References  = {};
    };

    struct SceneComponentSchema
    {
        cstring                 Key         = nullptr;
        uint32_t                Version     = 0;
        ComponentTypeID         RuntimeType = {};
        const SceneFieldSchema* Fields      = nullptr;
        uint32_t                FieldCount  = 0;
        SceneComponentCodecFns  Codecs      = {};
        SceneReferenceFns       References  = {};
    };

    using SceneSchemaVisitorFn = void (*)(void* ctx, const SceneComponentSchema& schema);

    class SceneComponentSchemaRegistry
    {
    public:
        void                        Initialize(Core::Memory::ArenaAllocator* arena, uint32_t capacity = 64);
        [[nodiscard]] bool          IsInitialized() const;

        // Copies Key and every field key into the registry arena
        [[nodiscard]] bool          Register(const SceneComponentSchemaDesc& schema, SceneDiagnostics* diagnostics);

        const SceneComponentSchema* FindByKey(cstring key) const;
        const SceneComponentSchema* FindByRuntimeType(ComponentTypeID type) const;

        void                        ForEachCanonical(SceneSchemaVisitorFn visitor, void* ctx) const;

        [[nodiscard]] uint32_t      Count() const;

    private:
        cstring                                       CopyKey(cstring key);

        Core::Containers::Array<SceneComponentSchema> m_schemas;
        Core::Containers::Array<uint32_t>             m_canonical;
        Core::Memory::ArenaAllocator*                 m_arena = nullptr;
    };
} // namespace ZEngine::ECS
