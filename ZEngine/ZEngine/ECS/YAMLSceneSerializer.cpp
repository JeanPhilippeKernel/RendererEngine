#include <ZEngine/ECS/YAMLSceneSerializer.h>
#ifdef ZENGINE_EDITOR
#include <ZEngine/ECS/ComponentSerializerRegistry.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/ZEngineDef.h>

using namespace ZEngine::Core::VFS;

namespace ZEngine::ECS
{
    namespace
    {
        bool EndsWithUUIDKey(cstring key)
        {
            constexpr size_t kSuffixLen = 5;
            const size_t     len        = Helpers::secure_strlen(key);
            return len >= kSuffixLen && Helpers::secure_strcmp(key + (len - kSuffixLen), "_uuid") == 0;
        }

        void UUIDToChars(const uuids::uuid& id, char (&out)[37])
        {
            Helpers::secure_memset(out, 0, sizeof(out), sizeof(out));
            uuids::to_string<char>(id, out);
        }

        void WarnIfAssetMissing(const uuids::uuid& id)
        {
            auto* manager = Managers::AssetManager::Instance();
            if (!manager || !manager->Registry)
            {
                return;
            }
            if (!manager->Registry->FindByUUID(id))
            {
                char text[37];
                UUIDToChars(id, text);
                ZENGINE_CORE_WARN("YAMLSceneSerializer: asset {} is not registered; using placeholder", text);
            }
        }

        VFSResult<void> WriteAll(IVFSContext& ctx, const VFSPath& path, cstring data, size_t size)
        {
            auto opened = ctx.Open(path, VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Truncate);
            if (!opened.Succeeded())
            {
                return VFSResult<void>::Fail(opened.Error());
            }

            IVFSFile* file    = opened.Value();
            auto      written = file->Write(Core::Containers::ArrayView<const uint8_t>(reinterpret_cast<const uint8_t*>(data), size), 0);
            auto      closed  = file->Close();

            if (!written.Succeeded())
            {
                return VFSResult<void>::Fail(written.Error());
            }
            if (!closed.Succeeded())
            {
                return VFSResult<void>::Fail(closed.Error());
            }
            return VFSResult<void>::Ok();
        }
    } // namespace

    void YAMLSceneSerializer::Initialize(Scene* scene, Core::Memory::ArenaAllocator* arena)
    {
        m_scene = scene;
        m_arena = arena;
    }

    VFSResult<void> YAMLSceneSerializer::ValidateAssetRefs(const YAML::Node& entity_node)
    {
        if (!entity_node.IsDefined() || entity_node.IsNull())
        {
            return VFSResult<void>::Ok();
        }

        if (entity_node.IsSequence())
        {
            for (const auto& child : entity_node)
            {
                auto r = ValidateAssetRefs(child);
                if (!r.Succeeded())
                {
                    return r;
                }
            }
            return VFSResult<void>::Ok();
        }

        if (!entity_node.IsMap())
        {
            return VFSResult<void>::Ok();
        }

        for (const auto& kv : entity_node)
        {
            if (!kv.first.IsScalar())
            {
                continue;
            }
            cstring key = kv.first.Scalar().c_str();

            if (EndsWithUUIDKey(key) && kv.second.IsScalar())
            {
                cstring value  = kv.second.Scalar().c_str();
                auto    parsed = uuids::uuid::from_string(value);
                if (!parsed.has_value())
                {
                    ZENGINE_CORE_ERROR("YAMLSceneSerializer: '{}' must be a UUID, found '{}'", key, value);
                    return VFSResult<void>::Fail(VFSError::Corrupted);
                }
                WarnIfAssetMissing(parsed.value());
                continue;
            }

            auto r = ValidateAssetRefs(kv.second);
            if (!r.Succeeded())
            {
                return r;
            }
        }
        return VFSResult<void>::Ok();
    }

    VFSResult<void> YAMLSceneSerializer::Serialize(IVFSContext& ctx, const VFSPath& path, const SceneSnapshot& scene)
    {
        if (!m_scene)
        {
            ZENGINE_CORE_ERROR("YAMLSceneSerializer::Serialize: no Scene set — call Initialize() first")
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }

        const auto& serializers = ComponentSerializerRegistry::Get();
        const auto& reflection  = ComponentReflectionRegistry::Get();

        YAML::Node  entities(YAML::NodeType::Sequence);

        for (uint32_t i = 0; i < scene.Entities.size(); ++i)
        {
            const EntityID id = scene.Entities[i];
            if (!m_scene->IsAlive(id))
            {
                continue;
            }

            YAML::Node entity_node(YAML::NodeType::Map);
            entity_node["id"] = id.Index;

            if (const auto* nc = m_scene->GetComponent<Components::NameComponent>(id))
            {
                entity_node["name"] = nc->Value;
            }

            YAML::Node components(YAML::NodeType::Map);

            // Dispatch is generic: no component type is named here.
            struct WriteCtx
            {
                Scene*                             ScenePtr;
                EntityID                           Id;
                YAML::Node*                        Out;
                const ComponentReflectionRegistry* Reflection;
            } write_ctx{m_scene, id, &components, &reflection};

            serializers.ForEach(
                [](void* raw_ctx, ComponentTypeID type_id, const ComponentSerializeFns& fns) {
                    auto* c = static_cast<WriteCtx*>(raw_ctx);
                    if (!fns.SerializeYAML || !c->ScenePtr->GetComponentRaw(c->Id, type_id))
                    {
                        return;
                    }

                    const ComponentMeta* meta = c->Reflection->Lookup(type_id);
                    if (!meta || !meta->TypeName)
                    {
                        return;
                    }

                    YAML::Node component_node(YAML::NodeType::Map);
                    fns.SerializeYAML(fns.Context, c->Id, *c->ScenePtr, component_node);
                    (*c->Out)[meta->TypeName] = component_node;
                },
                &write_ctx);

            entity_node["components"] = components;
            entities.push_back(entity_node);
        }

        YAML::Node scene_node(YAML::NodeType::Map);
        char       uuid_text[37];
        UUIDToChars(scene.SceneUUID, uuid_text);
        scene_node["uuid"]     = uuid_text;
        scene_node["name"]     = scene.Name.empty() ? "" : scene.Name.c_str();
        scene_node["entities"] = entities;

        YAML::Node root(YAML::NodeType::Map);
        root["scene"] = scene_node;

        YAML::Emitter emitter;
        emitter.SetIndent(2);
        emitter << YAML::Block << root;
        if (!emitter.good())
        {
            ZENGINE_CORE_ERROR("YAMLSceneSerializer::Serialize: emitter failed: {}", emitter.GetLastError())
            return VFSResult<void>::Fail(VFSError::IOError);
        }

        return WriteAll(ctx, path, emitter.c_str(), static_cast<size_t>(emitter.size()));
    }

    VFSResult<void> YAMLSceneSerializer::Deserialize(IVFSContext& ctx, const VFSPath& path, SceneSnapshot& out_scene)
    {
        if (!m_scene || !m_arena)
        {
            ZENGINE_CORE_ERROR("YAMLSceneSerializer::Deserialize: no Scene/arena set — call Initialize() first")
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }

        auto opened = ctx.Open(path, VFSOpenFlags::Read);
        if (!opened.Succeeded())
        {
            return VFSResult<void>::Fail(opened.Error());
        }

        IVFSFile* file = opened.Value();
        auto      size = file->Size();
        if (!size.Succeeded())
        {
            (void) file->Close();
            return VFSResult<void>::Fail(size.Error());
        }

        // +1 so the buffer is a valid C string for YAML::Load.
        const size_t byte_count = static_cast<size_t>(size.Value());
        auto*        text       = ZPushString(m_arena, byte_count + 1);
        Helpers::secure_memset(text, 0, byte_count + 1, byte_count + 1);

        if (byte_count > 0)
        {
            auto read = file->ReadAll(Core::Containers::ArrayView<uint8_t>(reinterpret_cast<uint8_t*>(text), byte_count));
            if (!read.Succeeded())
            {
                (void) file->Close();
                return VFSResult<void>::Fail(read.Error());
            }
        }
        (void) file->Close();

        // yaml-cpp's YAML::Load has no non-throwing parse API — this is the single
        // mandatory exception boundary for the third-party library.
        YAML::Node root;
        try
        {
            root = YAML::Load(text);
        }
        catch (const YAML::Exception& e)
        {
            ZENGINE_CORE_ERROR("YAMLSceneSerializer::Deserialize: parse failed: {}", e.what())
            return VFSResult<void>::Fail(VFSError::Corrupted);
        }

        const YAML::Node scene_node = root["scene"];
        if (!scene_node.IsDefined() || !scene_node.IsMap())
        {
            ZENGINE_CORE_ERROR("YAMLSceneSerializer::Deserialize: missing top-level 'scene' map")
            return VFSResult<void>::Fail(VFSError::Corrupted);
        }

        const YAML::Node entities = scene_node["entities"];

        // Validate every asset reference before mutating the Scene
        if (entities.IsDefined() && entities.IsSequence())
        {
            for (const auto& entity_node : entities)
            {
                auto valid = ValidateAssetRefs(entity_node);
                if (!valid.Succeeded())
                {
                    return valid;
                }
            }
        }

        if (const YAML::Node uuid_node = scene_node["uuid"]; uuid_node.IsScalar())
        {
            if (auto parsed = uuids::uuid::from_string(uuid_node.Scalar()); parsed.has_value())
            {
                out_scene.SceneUUID = parsed.value();
            }
        }
        if (const YAML::Node name_node = scene_node["name"]; name_node.IsScalar())
        {
            out_scene.Name.init(m_arena, name_node.Scalar().c_str());
        }

        const uint32_t entity_count = entities.IsDefined() && entities.IsSequence() ? static_cast<uint32_t>(entities.size()) : 0;
        out_scene.Entities.init(m_arena, entity_count ? entity_count : 1);

        const auto& serializers = ComponentSerializerRegistry::Get();
        const auto& reflection  = ComponentReflectionRegistry::Get();

        if (!entities.IsDefined() || !entities.IsSequence())
        {
            return VFSResult<void>::Ok(); // a scene with no entities is valid
        }

        for (const auto& entity_node : entities)
        {
            const EntityID id = m_scene->CreateEntity();
            out_scene.Entities.push(id);

            const YAML::Node components = entity_node["components"];
            if (!components.IsDefined() || !components.IsMap())
            {
                continue;
            }

            for (const auto& kv : components)
            {
                if (!kv.first.IsScalar())
                {
                    continue;
                }

                cstring              type_name = kv.first.Scalar().c_str();
                const ComponentMeta* meta      = reflection.LookupByName(type_name);
                if (!meta)
                {
                    ZENGINE_CORE_WARN("YAMLSceneSerializer: unknown component '{}', skipping", type_name);
                    continue;
                }

                const ComponentSerializeFns* fns = serializers.Lookup(meta->TypeID);
                if (!fns || !fns->DeserializeYAML)
                {
                    ZENGINE_CORE_WARN("YAMLSceneSerializer: '{}' has no YAML deserializer, skipping", type_name);
                    continue;
                }

                // The component must exist before the callback writes into it.
                // DeserializeYAML callbacks must use safe yaml-cpp APIs (check IsScalar()
                // before reading, use as<T>(default) overloads) — no exception handling here.
                m_scene->AddComponentRaw(id, meta->TypeID);
                fns->DeserializeYAML(fns->Context, id, *m_scene, kv.second);
            }
        }

        return VFSResult<void>::Ok();
    }
} // namespace ZEngine::ECS
#endif // ZENGINE_EDITOR
