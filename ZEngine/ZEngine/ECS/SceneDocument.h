#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/SceneDiagnostics.h>
#include <ZEngine/ECS/SceneGridSettings.h>
#include <ZEngine/ECS/SceneValue.h>
#include <ZEngine/ZEngineDef.h>
#include <uuid.h>
#include <cstdint>

namespace ZEngine::ECS
{
    class SceneComponentSchemaRegistry;

    inline constexpr uint32_t SCENE_DOCUMENT_SCHEMA_VERSION = 1;

    struct SceneComponentRecord
    {
        cstring     SchemaKey = nullptr;
        uint32_t    Version   = 0;
        SceneValue* Payload   = nullptr;
        bool        IsOpaque  = false;
    };

    struct SceneEntityRecord
    {
        uuids::uuid                                   UUID       = {};
        Core::Containers::Array<SceneComponentRecord> Components = {};
    };

    // Immutable source snapshot: safe to hand to worker I/O once built.
    struct SceneDocument
    {
        uint32_t                                   SchemaVersion      = 0;
        uint64_t                                   CompatibleFeatures = 0;
        uuids::uuid                                UUID               = {};
        Core::Containers::String                   Name               = {};
        SceneGridSettings                          Grid               = {};
        Core::Containers::Array<SceneEntityRecord> Entities           = {};

        void                                       Initialize(Core::Memory::ArenaAllocator* arena, uint32_t entity_capacity = 64);
    };

    struct SceneSerializationLimits
    {
        uint64_t                        MaxSourceBytes         = 0;
        uint32_t                        MaxEntities            = 0;
        uint32_t                        MaxComponentsPerEntity = 0;
        uint32_t                        MaxStringBytes         = 0;
        uint32_t                        MaxCollectionEntries   = 0;
        uint32_t                        MaxNestingDepth        = 0;

        static SceneSerializationLimits Default();
    };

    inline constexpr uint32_t SCENE_UUID_STRING_LEN = 36;
    bool                      SceneUUIDFromString(cstring text, uuids::uuid* out_uuid);
    void                      SceneUUIDToString(const uuids::uuid& uuid, char* out);

    bool                      CanonicalizeSceneGridSettings(SceneGridSettings& grid, SceneDiagnostics* diagnostics);

    void                      SortSceneDocument(SceneDocument& document);

    bool                      ValidateSceneDocument(const SceneDocument& document, const SceneComponentSchemaRegistry* schemas, const SceneSerializationLimits& limits, SceneDiagnostics* diagnostics);
} // namespace ZEngine::ECS
