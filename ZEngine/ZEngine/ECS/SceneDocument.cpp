#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Containers/HeapSort.h>
#include <ZEngine/Core/Serialization/YAMLAllocation.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneCompatibility.h>
#include <ZEngine/ECS/SceneDocument.h>
#include <ZEngine/ECS/SceneDocumentInternal.h>
#include <ZEngine/ECS/SceneSettingsYAML.h>
#include <ZEngine/ECS/SceneYAMLReader.h>
#include <ZEngine/ECS/SceneYamlValue.h>
#include <algorithm>
#include <limits>
#include <new>
#include <optional>
#include <string_view>

using ZEngine::Core::Containers::Array;
using ZEngine::Core::Containers::ArrayView;
using ZEngine::Core::Containers::FixedArray;
using ZEngine::ECS::SceneDocumentDetail::ComponentRecord;
using ZEngine::ECS::SceneDocumentDetail::CountCapturedNodes;
using ZEngine::ECS::SceneDocumentDetail::EntityRecord;
using ZEngine::ECS::SceneDocumentDetail::LimitsValid;
using ZEngine::ECS::SceneDocumentDetail::MaxComponentFields;
using ZEngine::ECS::SceneDocumentDetail::ValidateGraph;

namespace ZEngine::ECS
{
    namespace SceneDocumentDetail
    {
        bool Reject(SceneDiagnostics* diagnostics, cstring message, SceneDiagnosticCode code = SceneDiagnosticCode::InvalidData)
        {
            SceneDiagnosticError(diagnostics, message, code);
            return false;
        }

        template <typename T, typename Before>
        bool SortRecords(ArrayView<T> records, Core::Memory::IWorkingMemoryBudget* budget, const Before& before, SceneDiagnostics* diagnostics)
        {
            if (records.size() < 2)
                return true;
            Core::Memory::WorkingMemoryCharge fixed = {};
            if (!fixed.TryAcquire(budget, sizeof(Core::Containers::HeapSortState<T>) + sizeof(fixed)))
                return Reject(diagnostics, "Scene ordering exceeds its fixed working-memory budget.", SceneDiagnosticCode::LimitExceeded);
            Core::Containers::HeapSortState<T> state = {};
            Core::Containers::HeapSort(records, state, before);
            return true;
        }

        bool LimitsValid(const SceneDocumentLimits& limits)
        {
            return limits.MaxFileBytes > 0 && limits.MaxFileBytes <= 32 * 1024 * 1024 && limits.MaxEntities <= EntityRegistry::MAX_ENTITIES && limits.MaxComponentsPerEntity > 0 && limits.MaxComponentsPerEntity <= ARCHETYPE_MASK_CAPACITY && limits.MaxNodes > 0 && limits.MaxNodes <= 1048576 && limits.MaxDepth > 0 && limits.MaxDepth <= 32 && limits.MaxCollectionItems > 0 && limits.MaxCollectionItems <= 65536 && limits.MaxScalarBytes > 0 && limits.MaxScalarBytes <= 4096 && limits.MaxDecodedBytes > 0 && limits.MaxDecodedBytes <= ZMega(256);
        }

        bool CountCapturedNodes(const SceneYAML::Node& node, const SceneDocumentLimits& limits, uint32_t& count, uint32_t depth, SceneDiagnosticCode* failure, bool unique_keys)
        {
            const auto fail = [failure](SceneDiagnosticCode code) {
                if (failure)
                    *failure = code;
                return false;
            };
            Core::Memory::WorkingMemoryCharge fixed = {};
            if (!fixed.TryAcquire(node.Owner() ? node.Owner()->WorkingMemoryBudget() : Core::Serialization::YAMLWorkingMemoryBudget(), SceneTreeWalkWorkingBytes()))
                return fail(SceneDiagnosticCode::LimitExceeded);
            SceneTreeWalkScratch walk = {};
            walk.Current              = node;
            for (;;)
            {
                if (count == UINT32_MAX || ++count > limits.MaxNodes)
                    return fail(SceneDiagnosticCode::LimitExceeded);
                if (walk.Current.IsScalar())
                {
                    if (walk.Current.Scalar().size() > limits.MaxScalarBytes)
                        return fail(SceneDiagnosticCode::LimitExceeded);
                    if (walk.Current.Scalar().find('\0') != std::string_view::npos)
                        return fail(SceneDiagnosticCode::InvalidData);
                }
                else if (!walk.Current.IsNull())
                {
                    if (!walk.Current.IsMap() && !walk.Current.IsSequence())
                        return fail(SceneDiagnosticCode::InvalidData);
                    // Subtraction keeps even adversarial initial depths from
                    // wrapping. Scalar/null leaves do not consume a frame.
                    if (depth > limits.MaxDepth || walk.Active > limits.MaxDepth - depth || walk.Current.size() > limits.MaxCollectionItems || walk.Active == walk.Parents.size())
                        return fail(SceneDiagnosticCode::LimitExceeded);
                    if (unique_keys && walk.Current.IsMap() && !walk.Current.Owner()->UniqueKeys(walk.Current))
                        return fail(walk.Current.Owner()->Failure());
                    auto& parent     = walk.Parents[walk.Active++];
                    parent.Container = walk.Current;
                    parent.Field     = walk.Current.Mapping().begin();
                    parent.Item      = walk.Current.Sequence().begin();
                }
                // Resume the nearest unfinished parent; each link is followed
                // once. Node[index] would repeatedly rescan wide collections.
                while (walk.Active)
                {
                    auto& parent = walk.Parents[walk.Active - 1];
                    if (parent.Container.IsMap())
                    {
                        if (parent.Field != parent.Container.Mapping().end())
                        {
                            const auto entry = *parent.Field;
                            ++parent.Field;
                            if (entry.Key.size() > limits.MaxScalarBytes || count == UINT32_MAX || ++count > limits.MaxNodes)
                                return fail(SceneDiagnosticCode::LimitExceeded);
                            if (entry.Key.find('\0') != std::string_view::npos)
                                return fail(SceneDiagnosticCode::InvalidData);
                            walk.Current = entry.Value;
                            break;
                        }
                    }
                    else if (parent.Item != parent.Container.Sequence().end())
                    {
                        walk.Current = *parent.Item;
                        ++parent.Item;
                        break;
                    }
                    --walk.Active;
                }
                if (!walk.Active)
                    return true;
            }
        }

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

        bool EnumerateRecordReferences(const ComponentRecord& component, const SceneComponentSchema& schema, ArrayView<SceneValueField> fields, SceneReferenceVisitorFn visitor, void* context, SceneDiagnostics* diagnostics)
        {
            if (component.Version != schema.Version)
                return Reject(diagnostics, "Scene reference codec changed since document validation.");
            if (!schema.Codecs.DecodeYAML || !schema.References.Enumerate)
                return Reject(diagnostics, "Scene reference enumeration requires a supported component reference codec.");
            SceneValueWriter writer(fields);
            return schema.Codecs.DecodeYAML(schema.Codecs.Context, component.Version, component.Payload, &writer, diagnostics) && schema.References.Enumerate(schema.References.Context, writer.Value(), visitor, context, diagnostics);
        }

        bool ValidateGraph(ArrayView<const EntityRecord> entities, const SceneDocumentSettingsView& settings, const SceneComponentSchemaRegistry& schemas, Core::Memory::ArenaAllocator& arena, SceneFieldScratch& fields, Core::Memory::IWorkingMemoryBudget* budget, SceneDiagnostics* diagnostics)
        {
            const size_t none = entities.size();
            struct ScratchLease
            {
                Core::Memory::ArenaTemp Mark = {};
                ~ScratchLease()
                {
                    Core::Memory::EndTempArena(Mark);
                }
            };
            struct GraphNode
            {
                size_t             Parent = 0;
                Core::Maths::Mat4f World  = Core::Maths::Identity<Core::Maths::Mat4f>();
                uint8_t            Color  = 0;
            };
            struct ReferenceContext
            {
                ArrayView<const EntityRecord> Entities = {};
            };
            Core::Memory::WorkingMemoryCharge fixed = {};
            if (!fixed.TryAcquire(budget, sizeof(ScratchLease) + sizeof(Array<GraphNode>) + sizeof(Array<size_t>) + sizeof(GraphNode) + sizeof(ReferenceContext) + sizeof(fixed)))
                return Reject(diagnostics, "Scene graph validation exceeds its fixed working-memory budget.", SceneDiagnosticCode::LimitExceeded);
            ScratchLease     scratch{Core::Memory::BeginTempArena(&arena)};
            Array<GraphNode> graph = {};
            Array<size_t>    path  = {};
            graph.m_allocator = path.m_allocator = &arena;
            if (!graph.try_reserve(none) || !path.try_reserve(none))
                return Reject(diagnostics, "Scene graph validation exceeds its decoded-memory budget.", SceneDiagnosticCode::LimitExceeded);
            for (size_t i = 0; i < none; ++i)
                graph.push(GraphNode{.Parent = none});
            // Local matrices become world matrices in parent-first order below. Organizational
            // entities use identity locally and therefore carry the nearest authored parent frame.
            ReferenceContext context{entities};
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
                        const auto* position = writer.Value().Find<Core::Maths::Vec3f>("position");
                        const auto* rotation = writer.Value().Find<Core::Maths::Vec3f>("rotation");
                        const auto* scale    = writer.Value().Find<Core::Maths::Vec3f>("scale");
                        if (!position || !rotation || !scale)
                            return Reject(diagnostics, "Transform component does not provide semantic local TRS.");
                        graph[i].World = Core::Maths::ComposeTransformMatrix(*position, *rotation, *scale);
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
    } // namespace SceneDocumentDetail

    bool SceneDocument::Impl::Initialize(const SceneDocumentLimits& limits, void* tree_memory, Core::Memory::IAllocationRoute* allocation_route)
    {
        Limits                 = limits;
        // Canonicalized component payloads share the same bounded arena as source nodes.
        // Source-node limits remain enforced separately by the reader/capture traversal.
        const bool initialized = tree_memory && allocation_route ? Tree.InitializeGrowingBorrowed(tree_memory, SceneYAML::Storage::InitialRoutedCapacity(limits.MaxDecodedBytes), limits.MaxDecodedBytes, *allocation_route, 2 * limits.MaxNodes, limits.MaxCollectionItems, limits.MaxScalarBytes) : Tree.Initialize(limits.MaxDecodedBytes, 2 * limits.MaxNodes, limits.MaxCollectionItems, limits.MaxScalarBytes);
        if (!initialized)
            return false;
        Entities.m_allocator = Components.m_allocator = Tree.Allocator();
        return true;
    }

    bool SceneDocument::Impl::CreateRoot(const SceneDocumentSettingsView& settings, SceneYAML::Node* root, SceneYAML::Node* entities, SceneDiagnostics* diagnostics)
    {
        auto& storage            = Tree;
        *root                    = storage.Make(SceneYAML::NodeType::Map);
        *entities                = storage.Make(SceneYAML::NodeType::Sequence);
        const auto node          = storage.Make(SceneYAML::NodeType::Map);
        const auto configuration = storage.Make(SceneYAML::NodeType::Map);
        auto       grid          = storage.Destination();
        auto       sky           = storage.Destination();
        if (!EncodeSceneGridYAML(settings.Grid, &grid, diagnostics) || !EncodeSceneSkyYAML(settings.Sky, &sky, diagnostics))
            return false;
        if (!storage.Set(*root, "scene", node) || !storage.Set(node, "schema_version", storage.UInt(SchemaVersion)) || !storage.Set(node, "uuid", storage.UUID(settings.UUID)) || !storage.Set(node, "name", storage.Scalar(settings.Name)) || !storage.Set(configuration, "grid", grid) || !storage.Set(configuration, "sky", sky) || !storage.Set(node, "settings", configuration) || !storage.Set(node, "entities", *entities))
            return SceneDocumentDetail::Reject(diagnostics, "Scene settings exceed the decoded-memory budget.");
        return true;
    }

    bool SceneDocument::Impl::Decode(const SceneYAML::Node& root, const SceneComponentSchemaRegistry& schemas, SceneDiagnostics* diagnostics, bool canonical_payloads)
    {
        Core::Memory::WorkingMemoryCharge fixed = {};
        if (!fixed.TryAcquire(Tree.WorkingMemoryBudget(), SceneDocumentDetail::SceneFieldScratchWorkingBytes()))
            return SceneDocumentDetail::Reject(diagnostics, "Scene field decoding exceeds its fixed working-memory budget.", SceneDiagnosticCode::LimitExceeded);
        SceneDocumentDetail::SceneFieldScratch fields = {};
        return Decode(root, schemas, fields, diagnostics, canonical_payloads);
    }

    bool SceneDocument::Impl::Decode(const SceneYAML::Node& root, const SceneComponentSchemaRegistry& schemas, SceneDocumentDetail::SceneFieldScratch& fields, SceneDiagnostics* diagnostics, bool canonical_payloads)
    {
        if (!schemas.IsInitialized() || !root.IsMap() || root.size() != 1)
            return SceneDocumentDetail::Reject(diagnostics, "Scene document requires an initialized schema registry and a scene root.");
        const auto scene   = FindSceneYAMLField(root, "scene");
        uint32_t   version = 0;
        if (!scene.IsMap() || scene.size() != 5 || !SceneDocumentDetail::ReadVersion(FindSceneYAMLField(scene, "schema_version"), version) || version != SchemaVersion || !SceneDocumentDetail::ReadUUID(FindSceneYAMLField(scene, "uuid"), Settings.UUID) || !FindSceneYAMLField(scene, "name").IsScalar() || FindSceneYAMLField(scene, "name").Scalar().size() > MaxNameBytes)
            return SceneDocumentDetail::Reject(diagnostics, "Unsupported scene document version, identity, name or fields.");
        Settings.Name       = FindSceneYAMLField(scene, "name").Scalar();
        const auto settings = FindSceneYAMLField(scene, "settings");
        if (!settings.IsMap() || settings.size() != 2 || !DecodeSceneGridYAML(FindSceneYAMLField(settings, "grid"), &Settings.Grid, diagnostics) || !DecodeSceneSkyYAML(FindSceneYAMLField(settings, "sky"), &Settings.Sky, diagnostics))
            return SceneDocumentDetail::Reject(diagnostics, "Scene settings are malformed or unsupported.");
        const auto entities = FindSceneYAMLField(scene, "entities");
        if (!entities.IsSequence())
            return SceneDocumentDetail::Reject(diagnostics, "Scene entity collection is not a sequence.");
        if (entities.size() > Limits.MaxEntities)
            return SceneDocumentDetail::Reject(diagnostics, "Scene entity collection exceeds its limit.", SceneDiagnosticCode::LimitExceeded);
        size_t component_count = 0;
        for (const auto item : entities.Sequence())
        {
            const auto components = FindSceneYAMLField(item, "components");
            if (!components.IsMap())
                return SceneDocumentDetail::Reject(diagnostics, "Scene component collection is not a map.");
            if (components.size() > Limits.MaxComponentsPerEntity)
                return SceneDocumentDetail::Reject(diagnostics, "Scene component collection exceeds its limit.", SceneDiagnosticCode::LimitExceeded);
            component_count += components.size();
        }
        // Exact reservation before taking any views. Neither array grows after this point.
        if (!Entities.try_reserve(entities.size()) || !Components.try_reserve(component_count))
            return SceneDocumentDetail::Reject(diagnostics, "Scene records exceed their decoded-memory budget.", SceneDiagnosticCode::LimitExceeded);
        for (const auto& item : entities.Sequence())
        {
            EntityRecord entity = {};
            if (!item.IsMap() || item.size() != 2 || !SceneDocumentDetail::ReadUUID(FindSceneYAMLField(item, "uuid"), entity.UUID))
                return SceneDocumentDetail::Reject(diagnostics, "Scene entity needs one non-null UUID and a component map.");
            const auto components      = FindSceneYAMLField(item, "components");
            const auto first_component = Components.size();
            for (const auto& entry : components.Mapping())
            {
                ComponentRecord component = {};
                if (entry.Key.empty() || entry.Key.size() >= 128 || entry.Key == "UUIDComponent" || !entry.Value.IsMap() || entry.Value.size() != 2 || !SceneDocumentDetail::ReadVersion(FindSceneYAMLField(entry.Value, "schema_version"), component.Version) || !FindSceneYAMLField(entry.Value, "fields").IsMap() || FindSceneYAMLField(entry.Value, "fields").size() > MaxComponentFields)
                    return SceneDocumentDetail::Reject(diagnostics, "Malformed component envelope or duplicate authored UUID representation.");
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
                    return SceneDocumentDetail::Reject(diagnostics, "Scene component storage exceeds its decoded-memory budget.", SceneDiagnosticCode::LimitExceeded);
            }
            entity.Components = {components.size() == 0 ? nullptr : Components.data() + first_component, components.size()};
            if (!SceneDocumentDetail::SortRecords(entity.Components, Tree.WorkingMemoryBudget(), [](const auto& a, const auto& b) { return a.Key < b.Key; }, diagnostics))
                return false;
            for (size_t i = 1; i < entity.Components.size(); ++i)
                if (entity.Components[i - 1].Key == entity.Components[i].Key)
                    return SceneDocumentDetail::Reject(diagnostics, "Scene entity contains duplicate component keys.");
            if (!Entities.try_push(entity))
                return SceneDocumentDetail::Reject(diagnostics, "Scene entity storage exceeds its decoded-memory budget.", SceneDiagnosticCode::LimitExceeded);
        }
        if (!SceneDocumentDetail::SortRecords<EntityRecord>(Entities, Tree.WorkingMemoryBudget(), [](const auto& a, const auto& b) { return a.UUID < b.UUID; }, diagnostics))
            return false;
        for (size_t i = 1; i < Entities.size(); ++i)
            if (Entities[i - 1].UUID == Entities[i].UUID)
                return SceneDocumentDetail::Reject(diagnostics, "Scene contains duplicate entity UUIDs.");
        if (!ValidateGraph(Entities, Settings, schemas, *Tree.Allocator(), fields, Tree.WorkingMemoryBudget(), diagnostics))
            return false;
        if (ReadOnly && diagnostics)
            diagnostics->Add(SceneDiagnosticSeverity::Warning, "Unknown component schema/version retained for inspection. Document is read-only and cannot be saved.");
        Tree.Freeze();
        return true;
    }
    SceneDocument::SceneDocument()                                    = default;
    SceneDocument::~SceneDocument()                                   = default;
    SceneDocument::SceneDocument(SceneDocument&&) noexcept            = default;
    SceneDocument& SceneDocument::operator=(SceneDocument&&) noexcept = default;

    void           SceneDocument::ImplDeleter::operator()(Impl* pointer) const noexcept
    {
        if (!pointer)
            return;
        if (Route)
        {
            pointer->~Impl();
            Route->Free(pointer);
        }
        else
            delete pointer;
    }

    bool SceneDocument::Initialize(const SceneDocumentLimits& limits, Core::Memory::IAllocationRoute* allocation_route)
    {
        if (m_impl || !LimitsValid(limits))
            return false;
        void* tree_memory = nullptr;
        if (allocation_route)
        {
            constexpr size_t alignment = alignof(std::max_align_t);
            constexpr size_t offset    = (sizeof(Impl) + alignment - 1) & ~(alignment - 1);
            static_assert(alignof(Impl) <= alignment);
            const size_t initial_bytes = SceneYAML::Storage::InitialRoutedCapacity(limits.MaxDecodedBytes);
            if (initial_bytes > std::numeric_limits<size_t>::max() - offset)
                return false;
            void* memory = allocation_route->Allocate(offset + initial_bytes);
            if (!memory)
                return false;
            m_impl      = {new (memory) Impl{}, ImplDeleter{allocation_route}};
            tree_memory = static_cast<uint8_t*>(memory) + offset;
        }
        else
            m_impl.reset(new (std::nothrow) Impl{});
        return m_impl && m_impl->Initialize(limits, tree_memory, allocation_route);
    }

    const SceneDocumentSettingsView* SceneDocument::Settings() const
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

    bool SceneDocument::Parse(cstring source, size_t size, const SceneComponentSchemaRegistry& schemas, SceneDocument* out, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits, const SceneMigrationRegistry* migrations, Core::Memory::IAllocationRoute* allocation_route)
    {
        if (!out || !source || !schemas.IsInitialized() || !LimitsValid(limits))
            return SceneDocumentDetail::Reject(diagnostics, "Scene parsing requires source, destination, schemas and valid limits.", SceneDiagnosticCode::InvalidArgument);
        if (size > limits.MaxFileBytes)
            return SceneDocumentDetail::Reject(diagnostics, "Scene source exceeds configured input limits.", SceneDiagnosticCode::LimitExceeded);
        if (size == 0 || std::string_view(source, size).find('\0') != std::string_view::npos)
            return SceneDocumentDetail::Reject(diagnostics, "Scene source is empty or contains embedded NUL bytes.");
        {
            SceneDocument candidate;
            if (!candidate.Initialize(limits, allocation_route))
                return SceneDocumentDetail::Reject(diagnostics, "Cannot allocate bounded scene document storage.", SceneDiagnosticCode::AllocationFailed);
            std::optional<Core::Serialization::YAMLAllocationScope> allocation_scope = {};
            if (allocation_route)
                allocation_scope.emplace(*allocation_route);
            auto root = candidate.m_impl->Tree.Destination();
            if (!ReadSceneYAML({source, size}, limits, &root, diagnostics))
                return false;
            if (!SceneCompatibilityService::PrepareForLoad(&root, schemas, migrations, diagnostics, limits, allocation_route) || !candidate.m_impl->Decode(root, schemas, diagnostics))
            {
                if (!candidate.m_impl->Tree.Good())
                    SceneDiagnosticError(diagnostics, "Scene value storage failed.", candidate.m_impl->Tree.Failure());
                return false;
            }
            *out = std::move(candidate);
            return true;
        }
    }

    bool SceneDocument::Capture(const Scene& scene, const SceneDocumentSettingsView& settings, const SceneComponentSchemaRegistry& schemas, SceneDocument* out, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits)
    {
        if (!out || !scene.GetArena() || !schemas.IsInitialized() || !LimitsValid(limits) || scene.AliveCount() > limits.MaxEntities || scene.AliveCount() > limits.MaxCollectionItems || settings.UUID.is_nil() || settings.Name.size() > MaxNameBytes || settings.Name.find('\0') != std::string::npos)
            return SceneDocumentDetail::Reject(diagnostics, "Scene capture requires initialized state, authored identity and bounded settings.");
        {
            SceneDocument candidate;
            if (!candidate.Initialize(limits))
                return SceneDocumentDetail::Reject(diagnostics, "Cannot allocate bounded scene capture storage.");
            auto&                             storage = candidate.m_impl->Tree;
            Core::Memory::WorkingMemoryCharge fixed   = {};
            if (!fixed.TryAcquire(storage.WorkingMemoryBudget(), SceneDocumentDetail::SceneFieldScratchWorkingBytes()))
                return SceneDocumentDetail::Reject(diagnostics, "Scene capture exceeds its fixed field-memory budget.", SceneDiagnosticCode::LimitExceeded);
            SceneDocumentDetail::SceneFieldScratch fields = {};
            SceneYAML::Node                        root = {}, entities = {};
            if (!candidate.m_impl->CreateRoot(settings, &root, &entities, diagnostics))
                return false;
            uint32_t nodes = 0;
            if (!CountCapturedNodes(root, limits, nodes))
                return SceneDocumentDetail::Reject(diagnostics, "Captured scene settings exceed document tree limits.");
            struct CaptureContext
            {
                const Scene*               Source      = nullptr;
                SceneYAML::Storage*        Storage     = nullptr;
                EntityID                   Entity      = {};
                SceneYAML::Node            Components  = {};
                ArchetypeMask              Remaining   = 0;
                SceneDiagnostics*          Diagnostics = nullptr;
                uint32_t                   Count       = 0;
                uint32_t                   Limit       = 0;
                bool                       Valid       = true;
                ArrayView<SceneValueField> Fields      = {};
            } context  = {.Source = &scene, .Storage = &storage, .Diagnostics = diagnostics, .Limit = limits.MaxComponentsPerEntity, .Fields = fields};
            bool valid = true;
            scene.ForEachEntity([&](EntityID entity) {
                if (!valid)
                    return;
                const auto* uuid = scene.GetComponent<Components::UUIDComponent>(entity);
                if (!uuid || uuid->Value.is_nil())
                {
                    valid = SceneDocumentDetail::Reject(diagnostics, "Every captured entity requires a non-null authored UUID.");
                    return;
                }
                const auto record = storage.Make(SceneYAML::NodeType::Map);
                if (!storage.Set(record, "uuid", storage.UUID(uuid->Value)))
                {
                    valid = SceneDocumentDetail::Reject(diagnostics, "Captured entity exceeds the decoded-memory budget.");
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
                        SceneValueWriter writer(ctx.Fields);
                        auto             payload = ctx.Storage->Destination();
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
                    valid = SceneDocumentDetail::Reject(diagnostics, "Cannot save an unsupported runtime component or an incomplete component capture.");
                    return;
                }
                if (!storage.Set(record, "components", context.Components))
                {
                    valid = SceneDocumentDetail::Reject(diagnostics, "Captured components exceed the decoded-memory budget.");
                    return;
                }
                if (!CountCapturedNodes(record, limits, nodes, 4))
                {
                    valid = SceneDocumentDetail::Reject(diagnostics, "Captured entity exceeds document tree limits.");
                    return;
                }
                if (!storage.Append(entities, record))
                    valid = SceneDocumentDetail::Reject(diagnostics, "Captured entities exceed the document budget.");
            });
            if (!valid)
                return false;
            // Root has five schema fields; keep exactly one explicit document contract.
            if (!candidate.m_impl->Decode(root, schemas, fields, diagnostics, true))
                return false;
            *out = std::move(candidate);
            return true;
        }
    }

    bool SceneDocument::Emit(std::string* out, SceneDiagnostics* diagnostics) const
    {
        if (!out || !SceneCompatibilityService::PrepareForSave(*this, SceneCompatibilityUse::EditorSource, diagnostics))
            return SceneDocumentDetail::Reject(diagnostics, "Scene source emission rejected by compatibility policy or invalid output.");
        {
            SceneYAML::Storage scratch;
            if (!scratch.Initialize(ZMega(1)))
                return SceneDocumentDetail::Reject(diagnostics, "Cannot allocate scene settings emission scratch.");
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
                return SceneDocumentDetail::Reject(diagnostics, "Scene YAML emission failed.");
            if (!emitter.Output(out))
                return SceneDocumentDetail::Reject(diagnostics, "Scene emission failed or exceeds its file-size limit.");
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

    bool SceneDocument::Read(Core::VFS::IVFSContext& vfs, const Core::VFS::VFSPath& path, const SceneComponentSchemaRegistry& schemas, SceneDocument* out, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits, const SceneMigrationRegistry* migrations)
    {
        if (!out || !LimitsValid(limits))
            return SceneDocumentDetail::Reject(diagnostics, "Invalid scene read destination or limits.");
        auto open = vfs.Open(path, Core::VFS::VFSOpenFlags::Read);
        if (open.Failed())
            return SceneDocumentDetail::Reject(diagnostics, "Cannot open scene source through VFS.");
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
                return SceneDocumentDetail::Reject(diagnostics, "Cannot read an empty, oversized or inaccessible scene source.");
            std::string source(static_cast<size_t>(size.Value()), '\0');
            const auto  read = lease.File->ReadAll({reinterpret_cast<uint8_t*>(source.data()), source.size()});
            if (read.Failed() || read.Value() != source.size())
                return SceneDocumentDetail::Reject(diagnostics, "Scene source read was incomplete.");
            return Parse(source.data(), source.size(), schemas, out, diagnostics, limits, migrations);
        }
    }

    bool SceneDocument::EnumerateComponentReferences(ComponentTypeID type, const SceneComponentSchemaRegistry& schemas, SceneReferenceVisitorFn visitor, void* context, SceneDiagnostics* diagnostics) const
    {
        if (!m_impl || !visitor || !schemas.IsInitialized())
            return SceneDocumentDetail::Reject(diagnostics, "Scene reference enumeration requires a document, schemas and visitor.");
        const auto* schema = schemas.FindByRuntimeType(type);
        if (!schema || !schema->References.Enumerate || !schema->Codecs.DecodeYAML)
            return SceneDocumentDetail::Reject(diagnostics, "Scene reference enumeration requires a supported component reference codec.");
        Core::Memory::WorkingMemoryCharge fixed = {};
        if (!fixed.TryAcquire(m_impl->Tree.WorkingMemoryBudget(), SceneDocumentDetail::SceneFieldScratchWorkingBytes()))
            return SceneDocumentDetail::Reject(diagnostics, "Scene references exceed their fixed field-memory budget.", SceneDiagnosticCode::LimitExceeded);
        SceneDocumentDetail::SceneFieldScratch fields = {};
        for (const auto& entity : m_impl->Entities)
        {
            const auto* component = SceneDocumentDetail::FindComponent(entity, schema->Key);
            if (!component || !component->Supported)
                continue;
            if (!SceneDocumentDetail::EnumerateRecordReferences(*component, *schema, fields, visitor, context, diagnostics))
                return false;
        }
        return true;
    }

    bool SceneDocument::EnumerateReferences(const SceneComponentSchemaRegistry& schemas, SceneReferenceOccurrenceVisitorFn visitor, void* context, SceneDiagnostics* diagnostics) const
    {
        if (!m_impl || m_impl->ReadOnly || !visitor || !schemas.IsInitialized())
            return SceneDocumentDetail::Reject(diagnostics, "Scene reference enumeration requires a fully supported document, schemas and visitor.");
        struct VisitorContext
        {
            SceneReferenceOccurrence          Occurrence = {};
            SceneReferenceOccurrenceVisitorFn Visitor    = nullptr;
            void*                             Context    = nullptr;
        };
        Core::Memory::WorkingMemoryCharge fixed = {};
        if (!fixed.TryAcquire(m_impl->Tree.WorkingMemoryBudget(), SceneDocumentDetail::SceneFieldScratchWorkingBytes() + sizeof(VisitorContext)))
            return SceneDocumentDetail::Reject(diagnostics, "Scene references exceed their fixed field-memory budget.", SceneDiagnosticCode::LimitExceeded);
        VisitorContext forwarding = {.Visitor = visitor, .Context = context};
        const auto     forward    = [](void* opaque, const SceneReference& reference) {
            auto& state                = *static_cast<VisitorContext*>(opaque);
            state.Occurrence.Reference = reference;
            return state.Visitor(state.Context, state.Occurrence);
        };
        SceneDocumentDetail::SceneFieldScratch fields = {};
        for (const auto& entity : m_impl->Entities)
        {
            forwarding.Occurrence.EntityUUID = entity.UUID;
            for (const auto& component : entity.Components)
            {
                const auto* schema = schemas.FindByKey(component.Key.data());
                if (!component.Supported || !schema || schema->Version != component.Version)
                    return SceneDocumentDetail::Reject(diagnostics, "Scene reference schema changed since document validation.");
                if (!schema->References.Enumerate)
                    continue;
                forwarding.Occurrence.SchemaKey = schema->Key;
                if (!SceneDocumentDetail::EnumerateRecordReferences(component, *schema, fields, forward, &forwarding, diagnostics))
                    return false;
            }
        }
        const auto& environment = m_impl->Settings.Sky.EnvironmentMap;
        if (!environment.is_nil())
        {
            const SceneReferenceOccurrence occurrence = {
                {SceneReferenceKind::Asset, environment, "sky.environment", "environment-map"},
                {},
                "SceneSettings"
            };
            if (!visitor(context, occurrence))
                return false;
        }
        return true;
    }

    bool SceneDocument::PopulateCandidate(Scene& candidate, const SceneComponentSchemaRegistry& schemas, SceneDiagnostics* diagnostics) const
    {
        if (!m_impl || !candidate.GetArena() || candidate.AliveCount() || !schemas.IsInitialized())
            return SceneDocumentDetail::Reject(diagnostics, "Scene load needs an empty, initialized, unpublished candidate.");
        // Legacy component stores assume their arena growth succeeds. Reject an undersized
        // candidate before creating anything; use codec-provided conservative allocation bounds.
        const auto* arena = candidate.GetArena();
        if (arena->m_current_offset > arena->m_total_size)
            return SceneDocumentDetail::Reject(diagnostics, "Candidate arena has an invalid allocation budget.");
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
            return SceneDocumentDetail::Reject(diagnostics, "Candidate arena cannot retain the complete scene; no entities were created.");
        FixedArray<uint32_t, ARCHETYPE_MASK_CAPACITY> counts = {};
        for (const auto& record : m_impl->Entities)
            for (const auto& component : record.Components)
                if (component.Supported)
                {
                    const auto* schema = schemas.FindByKey(component.Key.data());
                    if (!schema || schema->Version != component.Version || schema->RuntimeType >= counts.size() || !schema->Codecs.CandidateMemoryBound)
                        return SceneDocumentDetail::Reject(diagnostics, "Candidate codec is missing its safe allocation bound or changed schema.");
                    ++counts[schema->RuntimeType];
                }
        for (uint32_t type = 0; type < counts.size(); ++type)
            if (counts[type])
            {
                const auto* schema = schemas.FindByRuntimeType(type);
                const auto  bytes  = schema->Codecs.CandidateMemoryBound(counts[type]);
                if (!bytes || bytes > available - required)
                    return SceneDocumentDetail::Reject(diagnostics, "Candidate arena cannot retain complete component storage; no entities were created.");
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
                return SceneDocumentDetail::Reject(diagnostics, "Cannot allocate candidate entity resolution scratch.");
            const SceneEntityResolver resolver = {&context, [](void* ptr, const uuids::uuid& uuid) {
                                                      const auto& ctx   = *static_cast<ResolverContext*>(ptr);
                                                      const auto  index = SceneDocumentDetail::FindEntity(ctx.Records, uuid);
                                                      return index != ctx.Records.size() ? ctx.IDs[index] : INVALID_ENTITY;
                                                  }};
            for (const auto& record : m_impl->Entities)
            {
                const auto entity = candidate.CreateEntity();
                if (!entity.IsValid())
                    return SceneDocumentDetail::Reject(diagnostics, "Candidate entity capacity exhausted; discard this unpublished candidate.");
                candidate.AddComponent<Components::UUIDComponent>(entity, {record.UUID});
                if (!context.IDs.try_push(entity))
                    return SceneDocumentDetail::Reject(diagnostics, "Candidate entity resolution storage exhausted.");
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
                            return SceneDocumentDetail::Reject(diagnostics, "Scene schemas changed before candidate reconstruction.");
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
                    return SceneDocumentDetail::Reject(diagnostics, "Candidate hierarchy or derived transform is not safely editable.");
            return true;
        }
    }
} // namespace ZEngine::ECS
