#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneComponentSchemaRegistry.h>
#include <ZEngine/ECS/SceneDocumentBuilder.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <cstdio>

using ZEngine::Core::VFS::VFSError;
using ZEngine::Core::VFS::VFSResult;

namespace ZEngine::ECS
{
    namespace
    {
        struct EntityCapture
        {
            const Scene*                  SourceScene = nullptr;
            EntityID                      Entity      = INVALID_ENTITY;
            SceneEntityRecord*            Record      = nullptr;
            Core::Memory::ArenaAllocator* Arena       = nullptr;
            SceneDiagnostics*             Diagnostics = nullptr;
            ComponentTypeID               UUIDType    = 0;
            bool                          Ok          = true;
            bool                          OutOfMemory = false;
        };

        // Reorders payload members to the schema's declared field order
        bool OrderPayloadFields(SceneValue* payload, const SceneComponentSchema& schema, Core::Memory::ArenaAllocator* arena)
        {
            if (schema.FieldCount == 0 || payload->Kind != SceneValueKind::Map || payload->Members.size() < 2)
            {
                return true;
            }

            const uint32_t                            count = static_cast<uint32_t>(payload->Members.size());
            Core::Containers::Array<SceneValueMember> ordered;
            ordered.init(arena, count);
            if (!ordered.data())
            {
                return false;
            }

            for (uint32_t f = 0; f < schema.FieldCount; ++f)
            {
                for (uint32_t m = 0; m < count; ++m)
                {
                    if (Helpers::secure_strcmp(payload->Members[m].Key, schema.Fields[f].Key) == 0)
                    {
                        ordered.push(payload->Members[m]);
                        break;
                    }
                }
            }
            for (uint32_t m = 0; m < count; ++m)
            {
                bool declared = false;
                for (uint32_t f = 0; f < schema.FieldCount && !declared; ++f)
                {
                    declared = Helpers::secure_strcmp(payload->Members[m].Key, schema.Fields[f].Key) == 0;
                }
                if (!declared)
                {
                    ordered.push(payload->Members[m]);
                }
            }
            payload->Members = std::move(ordered);
            return true;
        }

        void CaptureComponent(void* context, const SceneComponentSchema& schema)
        {
            auto* capture = static_cast<EntityCapture*>(context);
            if (!capture->Ok || schema.RuntimeType == capture->UUIDType)
            {
                return;
            }
            if (!capture->SourceScene->GetComponentRaw(capture->Entity, schema.RuntimeType))
            {
                return;
            }

            SceneValueWriter writer;
            writer.Initialize(capture->Arena);
            if (!schema.Codecs.Capture(schema.Codecs.Context, capture->Entity, *capture->SourceScene, &writer, capture->Diagnostics))
            {
                char message[SceneDiagnostics::MAX_MESSAGE_LEN];
                snprintf(message, sizeof(message), "capture: '%s' codec failed", schema.Key);
                SceneDiagnosticError(capture->Diagnostics, message);
                capture->Ok = false;
                return;
            }

            SceneComponentRecord record{};
            record.SchemaKey = writer.CopyString(schema.Key);
            record.Version   = schema.Version;
            record.Payload   = writer.Root();
            if (!writer.Failed() && !record.Payload)
            {
                char message[SceneDiagnostics::MAX_MESSAGE_LEN];
                snprintf(message, sizeof(message), "capture: '%s' codec produced no payload", schema.Key);
                SceneDiagnosticError(capture->Diagnostics, message);
                capture->Ok = false;
                return;
            }
            if (writer.Failed() || !record.SchemaKey || !OrderPayloadFields(record.Payload, schema, capture->Arena))
            {
                capture->OutOfMemory = true;
                capture->Ok          = false;
                return;
            }

            auto& components = capture->Record->Components;
            if (components.size() == components.capacity())
            {
                capture->OutOfMemory = true; // sized to the registry count up front
                capture->Ok          = false;
                return;
            }
            components.push(record);
        }

        bool HasOpaqueRecords(const SceneDocument& document)
        {
            for (const auto& entity : document.Entities)
            {
                for (const auto& component : entity.Components)
                {
                    if (component.IsOpaque)
                    {
                        return true;
                    }
                }
            }
            return false;
        }
    } // namespace

    void SceneDocumentBuilder::Initialize(const SceneComponentSchemaRegistry* schemas, const SceneCompatibilityService* compatibility)
    {
        m_schemas       = schemas;
        m_compatibility = compatibility;
    }

    VFSResult<void> SceneDocumentBuilder::Capture(const Scene& scene, const SceneDocument* previously_loaded_document, Core::Memory::ArenaAllocator* document_arena, SceneDocument* out_document, SceneDiagnostics* diagnostics) const
    {
        if (!m_schemas || !m_schemas->IsInitialized() || !document_arena || !out_document)
        {
            SceneDiagnosticError(diagnostics, "capture: builder is not initialized or arguments are null");
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }

        if (previously_loaded_document && HasOpaqueRecords(*previously_loaded_document))
        {
            SceneDiagnosticError(diagnostics, "capture: source has unknown components and no preservation policy is available; save rejected");
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }

        out_document->Initialize(document_arena, scene.AliveCount() ? scene.AliveCount() : 1);
        if (!out_document->Entities.data())
        {
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        }
        out_document->UUID = scene.GetUUID();
        out_document->Name.init(document_arena, scene.GetName());
        out_document->Grid = scene.GetGridSettings();
        if (!CanonicalizeSceneGridSettings(out_document->Grid, diagnostics))
        {
            return VFSResult<void>::Fail(VFSError::Corrupted);
        }

        const ComponentTypeID uuid_type     = ComponentTypeOf<Components::UUIDComponent>();
        const uint32_t        schema_count  = m_schemas->Count();
        uint32_t              without_uuid  = 0;
        bool                  ok            = true;
        bool                  out_of_memory = false;

        scene.ForEachEntity([&](EntityID entity) {
            if (!ok)
            {
                return;
            }
            const auto* uuid = static_cast<const Components::UUIDComponent*>(scene.GetComponentRaw(entity, uuid_type));
            if (!uuid)
            {
                ++without_uuid;
                return;
            }

            SceneEntityRecord record{};
            record.UUID = uuid->Value;
            record.Components.init(document_arena, schema_count ? schema_count : 1);
            if (!record.Components.data())
            {
                ok            = false;
                out_of_memory = true;
                return;
            }

            EntityCapture capture{};
            capture.SourceScene = &scene;
            capture.Entity      = entity;
            capture.Record      = &record;
            capture.Arena       = document_arena;
            capture.Diagnostics = diagnostics;
            capture.UUIDType    = uuid_type;
            m_schemas->ForEachCanonical(CaptureComponent, &capture);

            ok            = capture.Ok;
            out_of_memory = capture.OutOfMemory;
            if (ok)
            {
                out_document->Entities.push(std::move(record));
            }
        });

        if (out_of_memory)
        {
            SceneDiagnosticError(diagnostics, "capture: document arena exhausted");
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        }
        if (!ok)
        {
            return VFSResult<void>::Fail(VFSError::Corrupted);
        }
        if (without_uuid > 0)
        {
            char message[SceneDiagnostics::MAX_MESSAGE_LEN];
            snprintf(message, sizeof(message), "capture: %u entities without UUIDComponent were treated as runtime-only", without_uuid);
            if (diagnostics)
            {
                diagnostics->Add(SceneDiagnosticSeverity::Warning, message);
            }
        }

        SortSceneDocument(*out_document);
        if (!ValidateSceneDocument(*out_document, m_schemas, SceneSerializationLimits::Default(), diagnostics))
        {
            return VFSResult<void>::Fail(VFSError::Corrupted);
        }
        return VFSResult<void>::Ok();
    }
} // namespace ZEngine::ECS
