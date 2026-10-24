#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneDocument.h>
#include <ZEngine/ECS/SceneSettingsYAML.h>
#include <ZEngine/ECS/SceneYAMLReader.h>
#include <ZEngine/ECS/SceneYamlValue.h>
#include <algorithm>
#include <limits>
#include <new>
#include <string_view>

using ZEngine::Core::Containers::Array;
using ZEngine::Core::Containers::ArrayView;
using ZEngine::Core::Containers::FixedArray;

namespace ZEngine::ECS
{
    namespace
    {
        constexpr size_t MaxComponentFields = 64;

        bool             Reject(SceneDiagnostics* diagnostics, cstring message)
        {
            SceneDiagnosticError(diagnostics, message);
            return false;
        }

        bool LimitsValid(const SceneDocumentLimits& limits)
        {
            return limits.MaxFileBytes > 0 && limits.MaxFileBytes <= 32 * 1024 * 1024 && limits.MaxEntities <= EntityRegistry::MAX_ENTITIES && limits.MaxComponentsPerEntity > 0 && limits.MaxComponentsPerEntity <= ARCHETYPE_MASK_CAPACITY && limits.MaxNodes > 0 && limits.MaxNodes <= 1048576 && limits.MaxDepth > 0 && limits.MaxDepth <= 32 && limits.MaxCollectionItems > 0 && limits.MaxCollectionItems <= 65536 && limits.MaxScalarBytes > 0 && limits.MaxScalarBytes <= 4096 && limits.MaxDecodedBytes > 0 && limits.MaxDecodedBytes <= ZMega(256);
        }

        bool CountCapturedNodes(const SceneYAML::Node& node, const SceneDocumentLimits& limits, uint32_t& count, uint32_t depth = 1)
        {
            if (++count > limits.MaxNodes)
                return false;
            if (node.IsScalar())
                return node.Scalar().size() <= limits.MaxScalarBytes && node.Scalar().find('\0') == std::string::npos;
            if (node.IsNull())
                return true;
            if (depth > limits.MaxDepth || node.size() > limits.MaxCollectionItems)
                return false;
            if (node.IsSequence())
            {
                for (const auto& child : node.Sequence())
                    if (!CountCapturedNodes(child, limits, count, depth + 1))
                        return false;
            }
            else if (node.IsMap())
            {
                for (const auto& entry : node.Mapping())
                    if (entry.Key.size() > limits.MaxScalarBytes || entry.Key.find('\0') != std::string_view::npos || ++count > limits.MaxNodes || !CountCapturedNodes(entry.Value, limits, count, depth + 1))
                        return false;
            }
            else
                return false;
            return true;
        }

        struct ComponentRecord
        {
            std::string_view Key       = {};
            uint32_t         Version   = 0;
            SceneYAML::Node  Payload   = {};
            bool             Supported = false;
        };
        struct EntityRecord
        {
            uuids::uuid                UUID       = {};
            ArrayView<ComponentRecord> Components = {};
        };

        bool ReadUUID(const SceneYAML::Node& node, uuids::uuid& uuid)
        {
            if (!node.IsScalar() || node.Scalar().size() != 36)
                return false;
            const auto parsed = uuids::uuid::from_string(node.Scalar());
            if (!parsed || parsed->is_nil())
                return false;
            uuid = *parsed;
            return true;
        }

        bool ReadVersion(const SceneYAML::Node& node, uint32_t& version)
        {
            return ReadSceneYAMLUInt(node, version) && version != 0;
        }

        size_t FindEntity(ArrayView<const EntityRecord> entities, const uuids::uuid& uuid)
        {
            if (entities.empty())
                return 0;
            const auto found = std::lower_bound(entities.begin(), entities.end(), uuid, [](const auto& entity, const auto& id) { return entity.UUID < id; });
            return found != entities.end() && found->UUID == uuid ? static_cast<size_t>(found - entities.begin()) : entities.size();
        }

        const ComponentRecord* FindComponent(const EntityRecord& entity, cstring key)
        {
            if (entity.Components.empty())
                return nullptr;
            const auto found = std::lower_bound(entity.Components.begin(), entity.Components.end(), key, [](const auto& component, cstring value) { return component.Key < value; });
            return found != entity.Components.end() && found->Key == key ? &*found : nullptr;
        }

        bool ValidateGraph(ArrayView<const EntityRecord> entities, const SceneDocumentSettings& settings, const SceneComponentSchemaRegistry& schemas, Core::Memory::ArenaAllocator& arena, SceneDiagnostics* diagnostics)
        {
            using namespace Core::Maths;
            const size_t none = entities.size();
            struct ScratchLease
            {
                Core::Memory::ArenaTemp Mark = {};
                ~ScratchLease()
                {
                    Core::Memory::EndTempArena(Mark);
                }
            } scratch{Core::Memory::BeginTempArena(&arena)};
            struct GraphNode
            {
                size_t  Parent = 0;
                Mat4f   World  = Identity<Mat4f>();
                uint8_t Color  = 0;
            };
            Array<GraphNode> graph = {};
            Array<size_t>    path  = {};
            graph.m_allocator = path.m_allocator = &arena;
            if (!graph.try_reserve(none) || !path.try_reserve(none))
                return Reject(diagnostics, "Scene graph validation exceeds its decoded-memory budget.");
            for (size_t i = 0; i < none; ++i)
                graph.push(GraphNode{.Parent = none});
            // Local matrices become world matrices in parent-first order below. Organizational
            // entities use identity locally and therefore carry the nearest authored parent frame.
            FixedArray<SceneValueField, MaxComponentFields> fields = {};
            struct ReferenceContext
            {
                ArrayView<const EntityRecord> Entities = {};
            } context{entities};
            for (size_t i = 0; i < entities.size(); ++i)
            {
                for (const auto& component : entities[i].Components)
                {
                    if (!component.Supported)
                        continue;
                    const auto*      schema = schemas.FindByKey(component.Key.data());
                    SceneValueWriter writer(fields);
                    if (!schema || !schema->Codecs.DecodeYAML || !schema->Codecs.DecodeYAML(schema->Codecs.Context, component.Version, component.Payload, &writer, diagnostics))
                        return false;
                    if (schema->References.Enumerate && !schema->References.Enumerate(
                                                            schema->References.Context,
                                                            writer.Value(),
                                                            [](void* ctx, const SceneReference& ref) {
                                                                const auto list = static_cast<ReferenceContext*>(ctx)->Entities;
                                                                return ref.Kind == SceneReferenceKind::Asset || FindEntity(list, ref.UUID) != list.size();
                                                            },
                                                            &context,
                                                            diagnostics))
                        return Reject(diagnostics, "Scene contains an unresolved entity UUID reference.");
                    if (component.Key == "ParentComponent")
                    {
                        const auto* uuid = writer.Value().Find<uuids::uuid>("parent_uuid");
                        if (!uuid)
                            return Reject(diagnostics, "Parent component does not provide its stable parent UUID.");
                        if (!uuid->is_nil())
                            graph[i].Parent = FindEntity(entities, *uuid);
                    }
                    if (component.Key == "TransformComponent")
                    {
                        const auto* position = writer.Value().Find<Vec3f>("position");
                        const auto* rotation = writer.Value().Find<Vec3f>("rotation");
                        const auto* scale    = writer.Value().Find<Vec3f>("scale");
                        if (!position || !rotation || !scale)
                            return Reject(diagnostics, "Transform component does not provide semantic local TRS.");
                        graph[i].World = ComposeTransformMatrix(*position, *rotation, *scale);
                    }
                }
            }
            // One parent per entity: three-color iterative walks visit each edge once, no recursion.
            for (size_t i = 0; i < entities.size(); ++i)
            {
                path.clear();
                auto walk = i;
                while (walk != none && graph[walk].Color == 0)
                {
                    graph[walk].Color = 1;
                    path.push(walk);
                    walk = graph[walk].Parent;
                }
                if (walk != none && graph[walk].Color == 1)
                    return Reject(diagnostics, "Scene hierarchy contains a parent cycle.");
                for (size_t next = path.size(); next > 0; --next)
                {
                    walk = path[next - 1];
                    if (graph[walk].Parent != none)
                        graph[walk].World = graph[graph[walk].Parent].World * graph[walk].World;
                    for (uint32_t row = 0; row < 4; ++row)
                        for (uint32_t column = 0; column < 4; ++column)
                            if (!std::isfinite(graph[walk].World(row, column)))
                                return Reject(diagnostics, "Authored hierarchy produces a non-finite derived world transform.");
                    graph[walk].Color = 2;
                }
            }
            if (!settings.Sky.PrimaryCelestialLight.is_nil())
            {
                const auto  index     = FindEntity(entities, settings.Sky.PrimaryCelestialLight);
                const auto* light     = index != none ? FindComponent(entities[index], "LightComponent") : nullptr;
                const auto* transform = index != none ? FindComponent(entities[index], "TransformComponent") : nullptr;
                if (!light || !transform)
                    return Reject(diagnostics, "Primary celestial light UUID does not identify a transformed light.");
                uint32_t light_type = 0;
                if (light->Supported && (!ReadSceneYAMLUInt(FindSceneYAMLField(light->Payload, "type"), light_type) || light_type != static_cast<uint32_t>(Components::LightComponent::Type::Directional)))
                    return Reject(diagnostics, "Primary celestial light must be directional.");
            }
            return true;
        }
    } // namespace

    struct SceneDocument::Impl
    {
        SceneYAML::Storage     Tree       = {};
        SceneDocumentSettings  Settings   = {};
        Array<EntityRecord>    Entities   = {};
        Array<ComponentRecord> Components = {};
        SceneDocumentLimits    Limits     = {};
        bool                   ReadOnly   = false;

        bool                   Initialize(const SceneDocumentLimits& limits)
        {
            Limits = limits;
            // Canonicalized component payloads share the same bounded arena as source nodes.
            // Source-node limits remain enforced separately by the reader/capture traversal.
            if (!Tree.Initialize(limits.MaxDecodedBytes, 2 * limits.MaxNodes, limits.MaxCollectionItems, limits.MaxScalarBytes))
                return false;
            Entities.m_allocator = Components.m_allocator = Tree.Allocator();
            return true;
        }

        bool Decode(const SceneYAML::Node& root, const SceneComponentSchemaRegistry& schemas, SceneDiagnostics* diagnostics, bool canonical_payloads = false)
        {
            if (!schemas.IsInitialized() || !root.IsMap() || root.size() != 1)
                return Reject(diagnostics, "Scene document requires an initialized schema registry and a scene root.");
            const auto scene   = FindSceneYAMLField(root, "scene");
            uint32_t   version = 0;
            if (!scene.IsMap() || scene.size() != 5 || !ReadVersion(FindSceneYAMLField(scene, "schema_version"), version) || version != SchemaVersion || !ReadUUID(FindSceneYAMLField(scene, "uuid"), Settings.UUID) || !FindSceneYAMLField(scene, "name").IsScalar() || FindSceneYAMLField(scene, "name").Scalar().size() > MaxNameBytes)
                return Reject(diagnostics, "Unsupported scene document version, identity, name or fields.");
            Settings.Name       = FindSceneYAMLField(scene, "name").Scalar();
            const auto settings = FindSceneYAMLField(scene, "settings");
            if (!settings.IsMap() || settings.size() != 2 || !DecodeSceneGridYAML(FindSceneYAMLField(settings, "grid"), &Settings.Grid, diagnostics) || !DecodeSceneSkyYAML(FindSceneYAMLField(settings, "sky"), &Settings.Sky, diagnostics))
                return Reject(diagnostics, "Scene settings are malformed or unsupported.");
            const auto entities = FindSceneYAMLField(scene, "entities");
            if (!entities.IsSequence() || entities.size() > Limits.MaxEntities)
                return Reject(diagnostics, "Scene entity collection exceeds its limit or is not a sequence.");
            size_t component_count = 0;
            for (const auto item : entities.Sequence())
            {
                const auto components = FindSceneYAMLField(item, "components");
                if (!components.IsMap() || components.size() > Limits.MaxComponentsPerEntity)
                    return Reject(diagnostics, "Scene component collection exceeds its limit or is not a map.");
                component_count += components.size();
            }
            // Exact reservation before taking any views. Neither array grows after this point.
            if (!Entities.try_reserve(entities.size()) || !Components.try_reserve(component_count))
                return Reject(diagnostics, "Scene records exceed their decoded-memory budget.");
            FixedArray<SceneValueField, MaxComponentFields> fields = {};
            for (const auto& item : entities.Sequence())
            {
                EntityRecord entity = {};
                if (!item.IsMap() || item.size() != 2 || !ReadUUID(FindSceneYAMLField(item, "uuid"), entity.UUID))
                    return Reject(diagnostics, "Scene entity needs one non-null UUID and a component map.");
                const auto components = FindSceneYAMLField(item, "components");
                if (!components.IsMap() || components.size() > Limits.MaxComponentsPerEntity)
                    return Reject(diagnostics, "Scene component collection exceeds its limit or is not a map.");
                const auto first_component = Components.size();
                for (const auto& entry : components.Mapping())
                {
                    ComponentRecord component = {};
                    if (entry.Key.empty() || entry.Key.size() >= 128 || entry.Key == "UUIDComponent" || !entry.Value.IsMap() || entry.Value.size() != 2 || !ReadVersion(FindSceneYAMLField(entry.Value, "schema_version"), component.Version) || !FindSceneYAMLField(entry.Value, "fields").IsMap() || FindSceneYAMLField(entry.Value, "fields").size() > MaxComponentFields)
                        return Reject(diagnostics, "Malformed component envelope or duplicate authored UUID representation.");
                    component.Key       = entry.Key; // NUL-terminated scalar text owned by Tree.
                    const auto* schema  = schemas.FindByKey(component.Key.data());
                    component.Supported = schema && schema->Version == component.Version && schema->Codecs.DecodeYAML && schema->Codecs.EncodeYAML;
                    if (component.Supported)
                    {
                        SceneValueWriter writer(fields);
                        if (!schema->Codecs.DecodeYAML(schema->Codecs.Context, component.Version, FindSceneYAMLField(entry.Value, "fields"), &writer, diagnostics))
                            return false;
                        component.Payload = Tree.Destination();
                        if (canonical_payloads)
                            component.Payload = FindSceneYAMLField(entry.Value, "fields");
                        else if (!schema->Codecs.EncodeYAML(schema->Codecs.Context, writer.Value(), &component.Payload, diagnostics))
                            return false;
                    }
                    else
                    {
                        ReadOnly = true;
                        component.Payload.reset(FindSceneYAMLField(entry.Value, "fields"));
                    }
                    if (!Components.try_push(component))
                        return Reject(diagnostics, "Scene component storage exceeds its decoded-memory budget.");
                }
                entity.Components = {components.size() == 0 ? nullptr : Components.data() + first_component, components.size()};
                if (entity.Components.size() > 1)
                    std::sort(entity.Components.begin(), entity.Components.end(), [](const auto& a, const auto& b) { return a.Key < b.Key; });
                for (size_t i = 1; i < entity.Components.size(); ++i)
                    if (entity.Components[i - 1].Key == entity.Components[i].Key)
                        return Reject(diagnostics, "Scene entity contains duplicate component keys.");
                if (!Entities.try_push(entity))
                    return Reject(diagnostics, "Scene entity storage exceeds its decoded-memory budget.");
            }
            if (Entities.size() > 1)
                std::sort(Entities.begin(), Entities.end(), [](const auto& a, const auto& b) { return a.UUID < b.UUID; });
            for (size_t i = 1; i < Entities.size(); ++i)
                if (Entities[i - 1].UUID == Entities[i].UUID)
                    return Reject(diagnostics, "Scene contains duplicate entity UUIDs.");
            if (!ValidateGraph(Entities, Settings, schemas, *Tree.Allocator(), diagnostics))
                return false;
            if (ReadOnly && diagnostics)
                diagnostics->Add(SceneDiagnosticSeverity::Warning, "Unknown component schema/version retained for inspection. Document is read-only and cannot be saved.");
            Tree.Freeze();
            return true;
        }
    };

    SceneDocument::SceneDocument()                                                  = default;
    SceneDocument::~SceneDocument()                                                 = default;
    SceneDocument::SceneDocument(SceneDocument&&) noexcept                          = default;
    SceneDocument&               SceneDocument::operator=(SceneDocument&&) noexcept = default;

    const SceneDocumentSettings* SceneDocument::Settings() const
    {
        return m_impl ? &m_impl->Settings : nullptr;
    }
    uint32_t SceneDocument::EntityCount() const
    {
        return m_impl ? static_cast<uint32_t>(m_impl->Entities.size()) : 0;
    }
    bool SceneDocument::ReadOnly() const
    {
        return !m_impl || m_impl->ReadOnly;
    }

    bool SceneDocument::Parse(cstring source, size_t size, const SceneComponentSchemaRegistry& schemas, SceneDocument* out, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits)
    {
        if (!out || !source || size == 0 || !LimitsValid(limits) || size > limits.MaxFileBytes || std::string_view(source, size).find('\0') != std::string_view::npos)
            return Reject(diagnostics, "Scene source is empty or exceeds configured input limits.");
        {
            SceneDocument candidate;
            candidate.m_impl.reset(new (std::nothrow) Impl());
            if (!candidate.m_impl || !candidate.m_impl->Initialize(limits))
                return Reject(diagnostics, "Cannot allocate bounded scene document storage.");
            auto root = candidate.m_impl->Tree.Destination();
            if (!ReadSceneYAML({source, size}, limits, &root, diagnostics))
                return false;
            if (!candidate.m_impl->Decode(root, schemas, diagnostics))
                return false;
            *out = std::move(candidate);
            return true;
        }
    }

    bool SceneDocument::Capture(const Scene& scene, const SceneDocumentSettings& settings, const SceneComponentSchemaRegistry& schemas, SceneDocument* out, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits)
    {
        if (!out || !scene.GetArena() || !schemas.IsInitialized() || !LimitsValid(limits) || scene.AliveCount() > limits.MaxEntities || scene.AliveCount() > limits.MaxCollectionItems || settings.UUID.is_nil() || settings.Name.size() > MaxNameBytes || settings.Name.find('\0') != std::string::npos)
            return Reject(diagnostics, "Scene capture requires initialized state, authored identity and bounded settings.");
        {
            SceneDocument candidate;
            candidate.m_impl.reset(new (std::nothrow) Impl());
            if (!candidate.m_impl || !candidate.m_impl->Initialize(limits))
                return Reject(diagnostics, "Cannot allocate bounded scene capture storage.");
            auto&      storage       = candidate.m_impl->Tree;
            const auto root          = storage.Make(SceneYAML::NodeType::Map);
            const auto node          = storage.Make(SceneYAML::NodeType::Map);
            const auto configuration = storage.Make(SceneYAML::NodeType::Map);
            const auto entities      = storage.Make(SceneYAML::NodeType::Sequence);
            auto       grid          = storage.Destination();
            auto       sky           = storage.Destination();
            if (!EncodeSceneGridYAML(settings.Grid, &grid, diagnostics) || !EncodeSceneSkyYAML(settings.Sky, &sky, diagnostics))
                return false;
            if (!storage.Set(root, "scene", node) || !storage.Set(node, "schema_version", storage.UInt(SchemaVersion)) || !storage.Set(node, "uuid", storage.UUID(settings.UUID)) || !storage.Set(node, "name", storage.Scalar(settings.Name)) || !storage.Set(configuration, "grid", grid) || !storage.Set(configuration, "sky", sky) || !storage.Set(node, "settings", configuration) || !storage.Set(node, "entities", entities))
                return Reject(diagnostics, "Captured scene settings exceed the decoded-memory budget.");
            uint32_t nodes = 0;
            if (!CountCapturedNodes(root, limits, nodes))
                return Reject(diagnostics, "Captured scene settings exceed document tree limits.");
            struct CaptureContext
            {
                const Scene*        Source      = nullptr;
                SceneYAML::Storage* Storage     = nullptr;
                EntityID            Entity      = {};
                SceneYAML::Node     Components  = {};
                ArchetypeMask       Remaining   = 0;
                SceneDiagnostics*   Diagnostics = nullptr;
                uint32_t            Count       = 0;
                uint32_t            Limit       = 0;
                bool                Valid       = true;
            } context  = {.Source = &scene, .Storage = &storage, .Diagnostics = diagnostics, .Limit = limits.MaxComponentsPerEntity};
            bool valid = true;
            scene.ForEachEntity([&](EntityID entity) {
                if (!valid)
                    return;
                const auto* uuid = scene.GetComponent<Components::UUIDComponent>(entity);
                if (!uuid || uuid->Value.is_nil())
                {
                    valid = Reject(diagnostics, "Every captured entity requires a non-null authored UUID.");
                    return;
                }
                const auto record = storage.Make(SceneYAML::NodeType::Map);
                if (!storage.Set(record, "uuid", storage.UUID(uuid->Value)))
                {
                    valid = Reject(diagnostics, "Captured entity exceeds the decoded-memory budget.");
                    return;
                }
                context.Entity     = entity;
                context.Components = storage.Make(SceneYAML::NodeType::Map);
                context.Remaining  = scene.GetMask(entity) & ~MaskBit(ComponentTypeOf<Components::UUIDComponent>());
                context.Count      = 0;
                schemas.ForEachCanonical(
                    [](void* ptr, const SceneComponentSchema& schema) {
                        auto& ctx = *static_cast<CaptureContext*>(ptr);
                        if (!ctx.Valid || schema.RuntimeType >= ARCHETYPE_MASK_CAPACITY || !(ctx.Remaining & MaskBit(schema.RuntimeType)))
                            return;
                        if (++ctx.Count > ctx.Limit || !schema.Codecs.EncodeYAML)
                        {
                            ctx.Valid = false;
                            return;
                        }
                        FixedArray<SceneValueField, MaxComponentFields> fields = {};
                        SceneValueWriter                                writer(fields);
                        auto                                            payload = ctx.Storage->Destination();
                        if (!schema.Codecs.Capture(schema.Codecs.Context, ctx.Entity, *ctx.Source, &writer, ctx.Diagnostics) || !schema.Codecs.EncodeYAML(schema.Codecs.Context, writer.Value(), &payload, ctx.Diagnostics))
                        {
                            ctx.Valid = false;
                            return;
                        }
                        const auto envelope = ctx.Storage->Make(SceneYAML::NodeType::Map);
                        if (!ctx.Storage->Set(envelope, "schema_version", ctx.Storage->UInt(schema.Version)) || !ctx.Storage->Set(envelope, "fields", payload) || !ctx.Storage->Set(ctx.Components, schema.Key, envelope))
                        {
                            ctx.Valid = false;
                            return;
                        }
                        ctx.Remaining &= ~MaskBit(schema.RuntimeType);
                    },
                    &context);
                if (!context.Valid || context.Remaining)
                {
                    valid = Reject(diagnostics, "Cannot save an unsupported runtime component or an incomplete component capture.");
                    return;
                }
                if (!storage.Set(record, "components", context.Components))
                {
                    valid = Reject(diagnostics, "Captured components exceed the decoded-memory budget.");
                    return;
                }
                if (!CountCapturedNodes(record, limits, nodes, 4))
                {
                    valid = Reject(diagnostics, "Captured entity exceeds document tree limits.");
                    return;
                }
                if (!storage.Append(entities, record))
                    valid = Reject(diagnostics, "Captured entities exceed the document budget.");
            });
            if (!valid)
                return false;
            // Root has five schema fields; keep exactly one explicit document contract.
            if (!candidate.m_impl->Decode(root, schemas, diagnostics, true))
                return false;
            *out = std::move(candidate);
            return true;
        }
    }

    bool SceneDocument::Emit(std::string* out, SceneDiagnostics* diagnostics) const
    {
        if (!out || !m_impl || m_impl->ReadOnly)
            return Reject(diagnostics, "Read-only/empty scene document cannot be saved; unsupported data would be lost.");
        {
            SceneYAML::Storage scratch;
            if (!scratch.Initialize(ZMega(1)))
                return Reject(diagnostics, "Cannot allocate scene settings emission scratch.");
            auto grid = scratch.Destination();
            auto sky  = scratch.Destination();
            if (!EncodeSceneGridYAML(m_impl->Settings.Grid, &grid, diagnostics) || !EncodeSceneSkyYAML(m_impl->Settings.Sky, &sky, diagnostics))
                return false;
            SceneYAML::Emitter emitter(m_impl->Limits.MaxFileBytes);
            // Stream immutable payloads directly, without constructing or retaining another tree.
            emitter << SceneYAML::BeginMap << SceneYAML::Key << "scene" << SceneYAML::Value << SceneYAML::BeginMap << SceneYAML::Key << "schema_version" << SceneYAML::Value << SchemaVersion << SceneYAML::Key << "uuid" << SceneYAML::Value << uuids::to_string(m_impl->Settings.UUID) << SceneYAML::Key << "name" << SceneYAML::Value << m_impl->Settings.Name << SceneYAML::Key << "settings" << SceneYAML::Value << SceneYAML::BeginMap << SceneYAML::Key << "grid" << SceneYAML::Value << grid << SceneYAML::Key << "sky" << SceneYAML::Value << sky << SceneYAML::EndMap
                    << SceneYAML::Key << "entities" << SceneYAML::Value << SceneYAML::BeginSeq;
            for (const auto& entity : m_impl->Entities)
            {
                emitter << SceneYAML::BeginMap << SceneYAML::Key << "uuid" << SceneYAML::Value << uuids::to_string(entity.UUID) << SceneYAML::Key << "components" << SceneYAML::Value << SceneYAML::BeginMap;
                for (const auto& component : entity.Components)
                {
                    emitter << SceneYAML::Key << component.Key << SceneYAML::Value << SceneYAML::BeginMap << SceneYAML::Key << "schema_version" << SceneYAML::Value << component.Version << SceneYAML::Key << "fields" << SceneYAML::Value << component.Payload << SceneYAML::EndMap;
                }
                emitter << SceneYAML::EndMap << SceneYAML::EndMap;
            }
            emitter << SceneYAML::EndSeq << SceneYAML::EndMap << SceneYAML::EndMap;
            if (!emitter.good())
                return Reject(diagnostics, "Scene YAML emission failed.");
            if (!emitter.Output(out))
                return Reject(diagnostics, "Scene emission failed or exceeds its file-size limit.");
            return true;
        }
    }

    Core::VFS::VFSResult<void> SceneDocument::Save(Core::VFS::IVFSContext& vfs, const Core::VFS::VFSPath& path, SceneDiagnostics* diagnostics) const
    {
        std::string source;
        if (!Emit(&source, diagnostics))
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Corrupted);
        {
            auto result = Core::VFS::WriteFileAtomically(vfs, path, {reinterpret_cast<const uint8_t*>(source.data()), source.size()});
            if (result.Failed())
                SceneDiagnosticError(diagnostics, "Scene save failed; the saved checkpoint must not advance.");
            return result;
        }
    }

    bool SceneDocument::Read(Core::VFS::IVFSContext& vfs, const Core::VFS::VFSPath& path, const SceneComponentSchemaRegistry& schemas, SceneDocument* out, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits)
    {
        if (!out || !LimitsValid(limits))
            return Reject(diagnostics, "Invalid scene read destination or limits.");
        auto open = vfs.Open(path, Core::VFS::VFSOpenFlags::Read);
        if (open.Failed())
            return Reject(diagnostics, "Cannot open scene source through VFS.");
        struct FileLease
        {
            Core::VFS::IVFSContext& Context;
            Core::VFS::IVFSFile*    File = nullptr;
            ~FileLease()
            {
                if (File)
                    Context.Close(File);
            }
        } lease{vfs, open.Value()};
        {
            const auto size = lease.File->Size();
            if (size.Failed() || size.Value() == 0 || size.Value() > limits.MaxFileBytes)
                return Reject(diagnostics, "Cannot read an empty, oversized or inaccessible scene source.");
            std::string source(static_cast<size_t>(size.Value()), '\0');
            const auto  read = lease.File->ReadAll({reinterpret_cast<uint8_t*>(source.data()), source.size()});
            if (read.Failed() || read.Value() != source.size())
                return Reject(diagnostics, "Scene source read was incomplete.");
            return Parse(source.data(), source.size(), schemas, out, diagnostics, limits);
        }
    }

    bool SceneDocument::EnumerateComponentReferences(ComponentTypeID type, const SceneComponentSchemaRegistry& schemas, SceneReferenceVisitorFn visitor, void* context, SceneDiagnostics* diagnostics) const
    {
        if (!m_impl || !visitor || !schemas.IsInitialized())
            return Reject(diagnostics, "Scene reference enumeration requires a document, schemas and visitor.");
        const auto* schema = schemas.FindByRuntimeType(type);
        if (!schema || !schema->References.Enumerate || !schema->Codecs.DecodeYAML)
            return Reject(diagnostics, "Scene reference enumeration requires a supported component reference codec.");
        FixedArray<SceneValueField, MaxComponentFields> fields = {};
        for (const auto& entity : m_impl->Entities)
        {
            const auto* component = FindComponent(entity, schema->Key);
            if (!component || !component->Supported)
                continue;
            if (component->Version != schema->Version)
                return Reject(diagnostics, "Scene reference codec changed since document validation.");
            SceneValueWriter writer(fields);
            if (!schema->Codecs.DecodeYAML(schema->Codecs.Context, component->Version, component->Payload, &writer, diagnostics) || !schema->References.Enumerate(schema->References.Context, writer.Value(), visitor, context, diagnostics))
                return false;
        }
        return true;
    }

    bool SceneDocument::PopulateCandidate(Scene& candidate, const SceneComponentSchemaRegistry& schemas, SceneDiagnostics* diagnostics) const
    {
        if (!m_impl || !candidate.GetArena() || candidate.AliveCount() || !schemas.IsInitialized())
            return Reject(diagnostics, "Scene load needs an empty, initialized, unpublished candidate.");
        // Legacy component stores assume their arena growth succeeds. Reject an undersized
        // candidate before creating anything; use codec-provided conservative allocation bounds.
        const auto* arena = candidate.GetArena();
        if (arena->m_current_offset > arena->m_total_size)
            return Reject(diagnostics, "Candidate arena has an invalid allocation budget.");
        const size_t available = arena->m_total_size - arena->m_current_offset;
        const auto   count     = static_cast<uint32_t>(m_impl->Entities.size());
        size_t       required  = ComponentStorage<Components::UUIDComponent>::AllocationUpperBound(count, EntityRegistry::MAX_ENTITIES);
        if (count)
        {
            size_t capacity = 256;
            while (capacity < count)
                capacity *= 2;
            required += 2 * capacity * (sizeof(SceneHierarchy::Node) + sizeof(uint32_t)) + 64;
        }
        if (required > available)
            return Reject(diagnostics, "Candidate arena cannot retain the complete scene; no entities were created.");
        FixedArray<uint32_t, ARCHETYPE_MASK_CAPACITY> counts = {};
        for (const auto& record : m_impl->Entities)
            for (const auto& component : record.Components)
                if (component.Supported)
                {
                    const auto* schema = schemas.FindByKey(component.Key.data());
                    if (!schema || schema->Version != component.Version || schema->RuntimeType >= counts.size() || !schema->Codecs.CandidateMemoryBound)
                        return Reject(diagnostics, "Candidate codec is missing its safe allocation bound or changed schema.");
                    ++counts[schema->RuntimeType];
                }
        for (uint32_t type = 0; type < counts.size(); ++type)
            if (counts[type])
            {
                const auto* schema = schemas.FindByRuntimeType(type);
                const auto  bytes  = schema->Codecs.CandidateMemoryBound(counts[type]);
                if (!bytes || bytes > available - required)
                    return Reject(diagnostics, "Candidate arena cannot retain complete component storage; no entities were created.");
                required += bytes;
            }
        {
            struct ResolverContext
            {
                ArrayView<const EntityRecord> Records = {};
                Array<EntityID>               IDs     = {};
            };
            // Separate scratch: component creation must never be rewound with resolver memory.
            Core::Memory::ArenaAllocator scratch;
            scratch.Initialize(std::max<size_t>(4096, count * sizeof(EntityID)), 0, "SceneDocument/CandidateResolver");
            ResolverContext context = {.Records = m_impl->Entities};
            context.IDs.m_allocator = &scratch;
            if (!scratch.m_memory || !context.IDs.try_reserve(count))
                return Reject(diagnostics, "Cannot allocate candidate entity resolution scratch.");
            const SceneEntityResolver resolver = {&context, [](void* ptr, const uuids::uuid& uuid) {
                                                      const auto& ctx   = *static_cast<ResolverContext*>(ptr);
                                                      const auto  index = FindEntity(ctx.Records, uuid);
                                                      return index != ctx.Records.size() ? ctx.IDs[index] : INVALID_ENTITY;
                                                  }};
            for (const auto& record : m_impl->Entities)
            {
                const auto entity = candidate.CreateEntity();
                if (!entity.IsValid())
                    return Reject(diagnostics, "Candidate entity capacity exhausted; discard this unpublished candidate.");
                candidate.AddComponent<Components::UUIDComponent>(entity, {record.UUID});
                if (!context.IDs.try_push(entity))
                    return Reject(diagnostics, "Candidate entity resolution storage exhausted.");
            }
            FixedArray<SceneValueField, MaxComponentFields> fields = {};
            for (uint32_t phase = 0; phase < 2; ++phase)
            {
                for (size_t i = 0; i < m_impl->Entities.size(); ++i)
                {
                    for (const auto& component : m_impl->Entities[i].Components)
                    {
                        if (!component.Supported)
                            continue; // The owner must enforce ReadOnly() for this inspected document.
                        const auto* schema = schemas.FindByKey(component.Key.data());
                        if (!schema || schema->Version != component.Version || !schema->Codecs.DecodeYAML)
                            return Reject(diagnostics, "Scene schemas changed before candidate reconstruction.");
                        if (phase == 1 && !schema->Codecs.ResolveCandidateReferences)
                            continue;
                        SceneValueWriter writer(fields);
                        if (!schema->Codecs.DecodeYAML(schema->Codecs.Context, component.Version, component.Payload, &writer, diagnostics))
                            return false;
                        if (phase == 0 && !schema->Codecs.PopulateCandidate(schema->Codecs.Context, context.IDs[i], candidate, writer.Value(), diagnostics))
                            return false;
                        if (phase == 1 && !schema->Codecs.ResolveCandidateReferences(schema->Codecs.Context, context.IDs[i], candidate, writer.Value(), resolver, diagnostics))
                            return false;
                    }
                }
            }
            candidate.SynchronizeHierarchy();
            for (const auto& node : candidate.Hierarchy().Nodes())
                if (!node.TopologyValid || !node.FrameValid)
                    return Reject(diagnostics, "Candidate hierarchy or derived transform is not safely editable.");
            return true;
        }
    }
} // namespace ZEngine::ECS
