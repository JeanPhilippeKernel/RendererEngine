#include <ZEngine/Core/Maths/MathUtils.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneComponentSchemaRegistry.h>
#include <ZEngine/ECS/SceneDocumentBuilder.h>
#include <ZEngine/ECS/YAMLSceneDocumentSerializer.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <cstring>

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Core::VFS;
using ZEngine::Core::Containers::ArrayView;
using ZEngine::Helpers::secure_strcmp;

namespace
{
    constexpr cstring SCENE_UUID = "550e8400-e29b-41d4-a716-446655440000";
    constexpr cstring ROOT_UUID  = "11111111-1111-1111-1111-111111111111";
    constexpr cstring CHILD_UUID = "22222222-2222-2222-2222-222222222222";
    constexpr cstring MESH_UUID  = "33333333-3333-3333-3333-333333333333";
    constexpr cstring SCENE_PATH = "/main.scene.yaml";

    // Test-only components exercising authored, runtime-derived and asset-reference fields.
    struct TestTransform
    {
        float    Position[3]      = {};
        float    Scale[3]         = {1.0f, 1.0f, 1.0f};
        uint32_t RenderInstanceId = 0;
    };

    struct TestMeshRef
    {
        uuids::uuid Mesh = {};
    };

    uuids::uuid U(cstring text)
    {
        uuids::uuid out{};
        EXPECT_TRUE(SceneUUIDFromString(text, &out)) << text;
        return out;
    }

    VFSPath P(cstring raw)
    {
        auto r = VFSPath::Parse(raw);
        EXPECT_TRUE(r.Succeeded()) << raw;
        return r.Value();
    }

    bool DiagHas(const SceneDiagnostics& diag, cstring needle)
    {
        for (uint32_t i = 0; i < diag.Count; ++i)
        {
            if (std::strstr(diag.Entries[i].Message, needle))
            {
                return true;
            }
        }
        return false;
    }

    // ---- codecs ----

    bool NameCapture(void*, EntityID entity, const Scene& scene, SceneValueWriter* out, SceneDiagnostics*)
    {
        SceneValue* map = out->NewMap(1);
        out->Set(map, "value", out->NewString(scene.GetComponent<NameComponent>(entity)->Value));
        out->SetRoot(map);
        return !out->Failed();
    }

    bool UUIDCapture(void*, EntityID entity, const Scene& scene, SceneValueWriter* out, SceneDiagnostics*)
    {
        char text[SCENE_UUID_STRING_LEN + 1];
        SceneUUIDToString(scene.GetComponent<UUIDComponent>(entity)->Value, text);
        SceneValue* map = out->NewMap(1);
        out->Set(map, "value", out->NewString(text));
        out->SetRoot(map);
        return !out->Failed();
    }

    bool ParentCapture(void*, EntityID entity, const Scene& scene, SceneValueWriter* out, SceneDiagnostics*)
    {
        const EntityID       parent = scene.GetComponent<ParentComponent>(entity)->Parent;
        const UUIDComponent* uuid   = scene.IsAlive(parent) ? scene.GetComponent<UUIDComponent>(parent) : nullptr;
        SceneValue*          map    = out->NewMap(1);
        if (uuid)
        {
            char text[SCENE_UUID_STRING_LEN + 1];
            SceneUUIDToString(uuid->Value, text);
            out->Set(map, "parent_uuid", out->NewString(text));
        }
        else
        {
            out->Set(map, "parent_uuid", out->NewNull());
        }
        out->SetRoot(map);
        return !out->Failed();
    }

    bool TransformCapture(void* context, EntityID entity, const Scene& scene, SceneValueWriter* out, SceneDiagnostics*)
    {
        const bool  leak = context && *static_cast<bool*>(context);
        const auto* t    = scene.GetComponent<TestTransform>(entity);
        SceneValue* map  = out->NewMap(3);
        // Deliberately out of schema order; capture must canonicalize.
        out->Set(map, "scale", out->NewFloatArray(t->Scale, 3));
        out->Set(map, "position", out->NewFloatArray(t->Position, 3));
        if (leak)
        {
            out->Set(map, "render_instance_id", out->NewUInt(t->RenderInstanceId));
        }
        out->SetRoot(map);
        return !out->Failed();
    }

    bool MeshCapture(void*, EntityID entity, const Scene& scene, SceneValueWriter* out, SceneDiagnostics*)
    {
        char text[SCENE_UUID_STRING_LEN + 1];
        SceneUUIDToString(scene.GetComponent<TestMeshRef>(entity)->Mesh, text);
        SceneValue* map = out->NewMap(1);
        out->Set(map, "mesh_uuid", out->NewString(text));
        out->SetRoot(map);
        return !out->Failed();
    }

    bool EnumerateUUIDField(const SceneValue& payload, cstring field, SceneReferenceKind kind, SceneReferenceVisitorFn visitor, void* visitor_context)
    {
        const SceneValue* value = payload.Find(field);
        if (!value || value->Kind == SceneValueKind::Null)
        {
            return true;
        }
        SceneReference reference{};
        reference.Kind = kind;
        if (value->Kind != SceneValueKind::String || !SceneUUIDFromString(value->StringValue, &reference.UUID))
        {
            return false;
        }
        visitor(visitor_context, reference);
        return true;
    }

    bool ParentEnumerate(void*, const SceneValue& payload, SceneReferenceVisitorFn visitor, void* visitor_context, SceneDiagnostics*)
    {
        return EnumerateUUIDField(payload, "parent_uuid", SceneReferenceKind::Entity, visitor, visitor_context);
    }

    bool MeshEnumerate(void*, const SceneValue& payload, SceneReferenceVisitorFn visitor, void* visitor_context, SceneDiagnostics*)
    {
        return EnumerateUUIDField(payload, "mesh_uuid", SceneReferenceKind::Asset, visitor, visitor_context);
    }

    bool RemapStub(void*, const SceneValue&, const SceneUUIDRemap&, SceneValueWriter*, SceneDiagnostics*)
    {
        return true;
    }
    bool PopulateStub(void*, EntityID, Scene&, const SceneValue&, SceneDiagnostics*)
    {
        return true;
    }
    bool EncodeStub(void*, const SceneValue&, SceneBinaryWriter*, SceneDiagnostics*)
    {
        return true;
    }
    bool DecodeStub(void*, uint32_t, SceneBinaryReader*, SceneValueWriter*, SceneDiagnostics*)
    {
        return true;
    }

    // Kind-tolerant for integers: "5" always reads back as UInt regardless of the writer's choice.
    bool SameValue(const SceneValue& a, const SceneValue& b)
    {
        const bool a_int = a.Kind == SceneValueKind::SInt || a.Kind == SceneValueKind::UInt;
        const bool b_int = b.Kind == SceneValueKind::SInt || b.Kind == SceneValueKind::UInt;
        if (a_int && b_int)
        {
            const int64_t av = a.Kind == SceneValueKind::SInt ? a.SIntValue : static_cast<int64_t>(a.UIntValue);
            const int64_t bv = b.Kind == SceneValueKind::SInt ? b.SIntValue : static_cast<int64_t>(b.UIntValue);
            return av == bv;
        }
        if (a.Kind != b.Kind)
        {
            return false;
        }
        switch (a.Kind)
        {
            case SceneValueKind::Null:
                return true;
            case SceneValueKind::Bool:
                return a.BoolValue == b.BoolValue;
            case SceneValueKind::Float:
                return static_cast<float>(a.FloatValue) == static_cast<float>(b.FloatValue);
            case SceneValueKind::String:
                return secure_strcmp(a.StringValue, b.StringValue) == 0;
            case SceneValueKind::Sequence:
                if (a.Sequence.size() != b.Sequence.size())
                    return false;
                for (uint32_t i = 0; i < a.Sequence.size(); ++i)
                    if (!SameValue(*a.Sequence[i], *b.Sequence[i]))
                        return false;
                return true;
            case SceneValueKind::Map:
                if (a.Members.size() != b.Members.size())
                    return false;
                for (uint32_t i = 0; i < a.Members.size(); ++i)
                    if (secure_strcmp(a.Members[i].Key, b.Members[i].Key) != 0 || !SameValue(*a.Members[i].Value, *b.Members[i].Value))
                        return false;
                return true;
            default:
                return false;
        }
    }

    // Fails Rename to simulate a crash between temp write and replace.
    struct FailingRenameContext : IVFSContext
    {
        IVFSContext*         Inner = nullptr;

        VFSResult<IVFSFile*> Open(const VFSPath& p, VFSOpenFlags f) override
        {
            return Inner->Open(p, f);
        }
        VFSResult<Core::Containers::Array<VFSDirEntry>> List(const VFSPath& p, ArenaAllocator* a) override
        {
            return Inner->List(p, a);
        }
        VFSResult<VFSFileStat> Stat(const VFSPath& p) override
        {
            return Inner->Stat(p);
        }
        VFSResult<bool> Exists(const VFSPath& p) override
        {
            return Inner->Exists(p);
        }
        VFSResult<void> Mount(IVFSBackend* b, const VFSPath& r, int pr) override
        {
            return Inner->Mount(b, r, pr);
        }
        VFSResult<void> Unmount(const VFSPath& r) override
        {
            return Inner->Unmount(r);
        }
        VFSResult<void> CreateDir(const VFSPath& p) override
        {
            return Inner->CreateDir(p);
        }
        VFSResult<void> Remove(const VFSPath& p) override
        {
            return Inner->Remove(p);
        }
        VFSResult<void> RemoveAll(const VFSPath& p) override
        {
            return Inner->RemoveAll(p);
        }
        VFSResult<void> Rename(const VFSPath&, const VFSPath&) override
        {
            return VFSResult<void>::Fail(VFSError::IOError);
        }
        void Close(IVFSFile* const f) override
        {
            Inner->Close(f);
        }
        void Shutdown() override {}
    };
} // namespace

class SceneDocumentFixture : public ::testing::Test
{
protected:
    MemoryManager                m_manager;
    Scene                        m_scene;
    SceneComponentSchemaRegistry m_registry;
    SceneDocumentBuilder         m_builder;
    YAMLSceneDocumentSerializer  m_serializer;
    VFSMemoryBackend             m_backend;
    VFSContext                   m_vfs;
    SceneDiagnostics             m_diag;
    bool                         m_leak_runtime_field = false;

    void                         SetUp() override
    {
        m_manager.Initialize(ZMega(256), {});
        ArenaAllocator* arena = &m_manager.MainArena;

        m_scene.Initialize(arena);
        m_scene.SetIdentity(U(SCENE_UUID), "MainLevel");

        m_registry.Initialize(arena);
        static const SceneFieldSchema name_fields[] = {
            {"value", SceneFieldClass::Authored}
        };
        static const SceneFieldSchema uuid_fields[] = {
            {"value", SceneFieldClass::Authored}
        };
        static const SceneFieldSchema parent_fields[] = {
            {"parent_uuid", SceneFieldClass::Authored}
        };
        static const SceneFieldSchema transform_fields[] = {
            {          "position",       SceneFieldClass::Authored},
            {             "scale",       SceneFieldClass::Authored},
            {"render_instance_id", SceneFieldClass::RuntimeDerived}
        };
        static const SceneFieldSchema mesh_fields[] = {
            {"mesh_uuid", SceneFieldClass::Authored}
        };

        Register("NameComponent", ComponentTypeOf<NameComponent>(), name_fields, 1, NameCapture, nullptr);
        Register("UUIDComponent", ComponentTypeOf<UUIDComponent>(), uuid_fields, 1, UUIDCapture, nullptr);
        Register("ParentComponent", ComponentTypeOf<ParentComponent>(), parent_fields, 1, ParentCapture, ParentEnumerate);
        Register("TransformComponent", ComponentTypeOf<TestTransform>(), transform_fields, 3, TransformCapture, nullptr, &m_leak_runtime_field);
        Register("MeshComponent", ComponentTypeOf<TestMeshRef>(), mesh_fields, 1, MeshCapture, MeshEnumerate);

        m_builder.Initialize(&m_registry, nullptr);

        m_backend.Initialize(arena);
        m_vfs.Initialize(arena, 4);
        ASSERT_TRUE(m_vfs.Mount(&m_backend, VFSPath::Root(), 0).Succeeded());
    }

    void TearDown() override
    {
        // VFSMemoryBackend's destructor touches arena nodes, so the manager is not shut down here.
        m_vfs.Shutdown();
    }

    void Register(cstring key, ComponentTypeID type, const SceneFieldSchema* fields, uint32_t count, decltype(SceneComponentCodecFns::Capture) capture, decltype(SceneReferenceFns::Enumerate) enumerate, void* context = nullptr)
    {
        SceneComponentSchemaDesc desc{};
        desc.Key                      = key;
        desc.Version                  = 1;
        desc.RuntimeType              = type;
        desc.Fields                   = fields;
        desc.FieldCount               = count;
        desc.Codecs.Context           = context;
        desc.Codecs.Capture           = capture;
        desc.Codecs.PopulateCandidate = PopulateStub;
        desc.Codecs.EncodeBinary      = EncodeStub;
        desc.Codecs.DecodeBinary      = DecodeStub;
        if (enumerate)
        {
            desc.References.Enumerate = enumerate;
            desc.References.Remap     = RemapStub;
        }
        SceneDiagnostics diag;
        ASSERT_TRUE(m_registry.Register(desc, &diag)) << key;
    }

    EntityID Spawn(cstring uuid, cstring name)
    {
        EntityID      e = m_scene.CreateEntity();
        NameComponent n{};
        Helpers::secure_strncpy(n.Value, sizeof(n.Value), name, Helpers::secure_strlen(name));
        m_scene.AddComponent<UUIDComponent>(e, {U(uuid)});
        m_scene.AddComponent<NameComponent>(e, n);
        return e;
    }

    // Child is created first so slot order differs from canonical UUID order.
    void BuildHierarchy()
    {
        EntityID child = Spawn(CHILD_UUID, "Child");
        EntityID root  = Spawn(ROOT_UUID, "Root");
        m_scene.AddComponent<ParentComponent>(child, {root});
        m_scene.AddComponent<TestTransform>(
            child,
            TestTransform{
            {1.0f, 1.5f, -2.25f},
            {0.1f, 1.0f,   3.0f},
            77
        });
        m_scene.AddComponent<TestMeshRef>(child, {U(MESH_UUID)});
        m_scene.AddComponent<ParentComponent>(root, {INVALID_ENTITY});
    }

    VFSResult<void> CaptureAndWrite(SceneDocument* doc, cstring path = SCENE_PATH)
    {
        auto capture = m_builder.Capture(m_scene, nullptr, &m_manager.MainArena, doc, &m_diag);
        if (capture.Failed())
        {
            return capture;
        }
        return m_serializer.WriteAtomic(m_vfs, P(path), *doc, &m_diag);
    }

    cstring ReadText(cstring path)
    {
        auto stat = m_vfs.Stat(P(path));
        if (stat.Failed())
        {
            return nullptr;
        }
        const size_t size = static_cast<size_t>(stat.Value().SizeBytes);
        char*        text = ZPushString(&m_manager.MainArena, size + 1);
        auto         open = m_vfs.Open(P(path), VFSOpenFlags::Read);
        EXPECT_TRUE(open.Succeeded());
        auto read = open.Value()->ReadAll({reinterpret_cast<uint8_t*>(text), size});
        m_vfs.Close(open.Value());
        EXPECT_TRUE(read.Succeeded());
        text[size] = '\0';
        return text;
    }

    void WriteText(cstring path, cstring text)
    {
        ASSERT_TRUE(m_backend.WriteFile(P(path), ArrayView<const uint8_t>(reinterpret_cast<const uint8_t*>(text), std::strlen(text))).Succeeded());
    }

    VFSResult<void> ReadYAML(cstring text, SceneDocument* doc, const SceneSerializationLimits& limits = SceneSerializationLimits::Default())
    {
        WriteText("/input.scene.yaml", text);
        return m_serializer.Read(m_vfs, P("/input.scene.yaml"), limits, &m_manager.MainArena, doc, &m_diag);
    }

    uint32_t RootFileCount()
    {
        auto list = m_vfs.List(VFSPath::Root(), &m_manager.MainArena);
        return list.Succeeded() ? static_cast<uint32_t>(list.Value().size()) : 0;
    }
};

// ---------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------

TEST_F(SceneDocumentFixture, CaptureOrdersEntitiesByUUIDAndComponentsByKey)
{
    BuildHierarchy();
    SceneDocument doc;
    ASSERT_TRUE(m_builder.Capture(m_scene, nullptr, &m_manager.MainArena, &doc, &m_diag).Succeeded()) << m_diag.Entries[0].Message;

    EXPECT_EQ(doc.SchemaVersion, SCENE_DOCUMENT_SCHEMA_VERSION);
    EXPECT_EQ(doc.UUID, U(SCENE_UUID));
    EXPECT_STREQ(doc.Name.c_str(), "MainLevel");
    ASSERT_EQ(doc.Entities.size(), 2u);
    EXPECT_EQ(doc.Entities[0].UUID, U(ROOT_UUID));
    EXPECT_EQ(doc.Entities[1].UUID, U(CHILD_UUID));

    const auto& child = doc.Entities[1].Components;
    ASSERT_EQ(child.size(), 4u);
    EXPECT_STREQ(child[0].SchemaKey, "MeshComponent");
    EXPECT_STREQ(child[1].SchemaKey, "NameComponent");
    EXPECT_STREQ(child[2].SchemaKey, "ParentComponent");
    EXPECT_STREQ(child[3].SchemaKey, "TransformComponent");
}

TEST_F(SceneDocumentFixture, CaptureNeverEmitsUUIDComponentAsAComponent)
{
    BuildHierarchy();
    SceneDocument doc;
    ASSERT_TRUE(m_builder.Capture(m_scene, nullptr, &m_manager.MainArena, &doc, &m_diag).Succeeded());
    for (const auto& entity : doc.Entities)
        for (const auto& component : entity.Components)
            EXPECT_STRNE(component.SchemaKey, "UUIDComponent");
}

TEST_F(SceneDocumentFixture, CaptureCanonicalizesFieldOrderToSchemaOrder)
{
    BuildHierarchy();
    SceneDocument doc;
    ASSERT_TRUE(m_builder.Capture(m_scene, nullptr, &m_manager.MainArena, &doc, &m_diag).Succeeded());
    const SceneValue* transform = doc.Entities[1].Components[3].Payload;
    ASSERT_EQ(transform->Members.size(), 2u);
    EXPECT_STREQ(transform->Members[0].Key, "position");
    EXPECT_STREQ(transform->Members[1].Key, "scale");
}

TEST_F(SceneDocumentFixture, CaptureEncodesParentAsUUIDNotEntityID)
{
    BuildHierarchy();
    SceneDocument doc;
    ASSERT_TRUE(m_builder.Capture(m_scene, nullptr, &m_manager.MainArena, &doc, &m_diag).Succeeded());

    const SceneValue* child_parent = doc.Entities[1].Components[2].Payload->Find("parent_uuid");
    ASSERT_NE(child_parent, nullptr);
    ASSERT_EQ(child_parent->Kind, SceneValueKind::String);
    EXPECT_STREQ(child_parent->StringValue, ROOT_UUID);

    const SceneValue* root_parent = doc.Entities[0].Components[1].Payload->Find("parent_uuid");
    ASSERT_NE(root_parent, nullptr);
    EXPECT_EQ(root_parent->Kind, SceneValueKind::Null);
}

TEST_F(SceneDocumentFixture, CaptureRejectsRuntimeDerivedField)
{
    BuildHierarchy();
    m_leak_runtime_field = true;
    SceneDocument doc;
    EXPECT_TRUE(m_builder.Capture(m_scene, nullptr, &m_manager.MainArena, &doc, &m_diag).Failed());
    EXPECT_TRUE(DiagHas(m_diag, "render_instance_id"));
}

TEST_F(SceneDocumentFixture, CaptureRejectsDuplicateEntityUUID)
{
    Spawn(ROOT_UUID, "A");
    Spawn(ROOT_UUID, "B");
    SceneDocument doc;
    EXPECT_TRUE(m_builder.Capture(m_scene, nullptr, &m_manager.MainArena, &doc, &m_diag).Failed());
    EXPECT_TRUE(DiagHas(m_diag, "duplicate"));
}

TEST_F(SceneDocumentFixture, CaptureRejectsNilSceneUUID)
{
    m_scene.SetIdentity({}, "Untitled");
    SceneDocument doc;
    EXPECT_TRUE(m_builder.Capture(m_scene, nullptr, &m_manager.MainArena, &doc, &m_diag).Failed());
    EXPECT_TRUE(DiagHas(m_diag, "scene: uuid"));
}

TEST_F(SceneDocumentFixture, CaptureSkipsEntitiesWithoutUUIDWithWarning)
{
    Spawn(ROOT_UUID, "Authored");
    m_scene.CreateEntity(); // runtime-only helper
    SceneDocument doc;
    ASSERT_TRUE(m_builder.Capture(m_scene, nullptr, &m_manager.MainArena, &doc, &m_diag).Succeeded());
    EXPECT_EQ(doc.Entities.size(), 1u);
    EXPECT_FALSE(m_diag.HasErrors());
    EXPECT_TRUE(DiagHas(m_diag, "runtime-only"));
}

TEST_F(SceneDocumentFixture, CaptureRefusesToDropOpaqueRecords)
{
    Spawn(ROOT_UUID, "A");
    SceneDocument previous;
    previous.Initialize(&m_manager.MainArena, 1);
    SceneEntityRecord record{};
    record.UUID = U(ROOT_UUID);
    record.Components.init(&m_manager.MainArena, 1);
    SceneComponentRecord opaque{};
    opaque.SchemaKey = "PluginComponent";
    opaque.Version   = 3;
    opaque.IsOpaque  = true;
    record.Components.push(opaque);
    previous.Entities.push(std::move(record));

    SceneDocument doc;
    EXPECT_TRUE(m_builder.Capture(m_scene, &previous, &m_manager.MainArena, &doc, &m_diag).Failed());
    EXPECT_TRUE(DiagHas(m_diag, "unknown components"));
}

TEST_F(SceneDocumentFixture, CaptureRejectsInvalidGrid)
{
    m_scene.GetGridSettings().BaseSpacing = 0.0f;
    SceneDocument doc;
    EXPECT_TRUE(m_builder.Capture(m_scene, nullptr, &m_manager.MainArena, &doc, &m_diag).Failed());
    EXPECT_TRUE(DiagHas(m_diag, "base_spacing"));
}

// ---------------------------------------------------------------------------
// Grid
// ---------------------------------------------------------------------------

TEST(SceneGridSettingsTest, DefaultIsWorldXZPlane)
{
    SceneGridSettings grid;
    EXPECT_TRUE(grid.Enabled);
    EXPECT_EQ(grid.Origin.x, 0.0f);
    EXPECT_EQ(grid.Origin.y, 0.0f);
    EXPECT_EQ(grid.Origin.z, 0.0f);
    EXPECT_EQ(grid.Orientation.x, 0.0f);
    EXPECT_EQ(grid.Orientation.y, 0.0f);
    EXPECT_EQ(grid.Orientation.z, 0.0f);
    EXPECT_EQ(grid.Orientation.w, 1.0f);
    EXPECT_TRUE(CanonicalizeSceneGridSettings(grid, nullptr));
}

TEST(SceneGridSettingsTest, CanonicalizeFlipsNegativeWAndRejectsDegenerateInput)
{
    SceneGridSettings grid;
    grid.Orientation = Core::Maths::Quatf(0.0f, 0.0f, 0.0f, -1.0f);
    ASSERT_TRUE(CanonicalizeSceneGridSettings(grid, nullptr));
    EXPECT_EQ(grid.Orientation.w, 1.0f);

    SceneDiagnostics diag;
    grid.Orientation = Core::Maths::Quatf(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(CanonicalizeSceneGridSettings(grid, &diag));

    grid           = {};
    grid.LineWidth = Core::Maths::INF<float>;
    EXPECT_FALSE(CanonicalizeSceneGridSettings(grid, &diag));

    grid                = {};
    grid.MajorLineEvery = 0;
    EXPECT_FALSE(CanonicalizeSceneGridSettings(grid, &diag));
}

// ---------------------------------------------------------------------------
// YAML round trip and determinism
// ---------------------------------------------------------------------------

TEST_F(SceneDocumentFixture, CanonicalYAMLMatchesGolden)
{
    Spawn(ROOT_UUID, "Root");
    m_scene.AddComponent<TestTransform>(m_scene.CreateEntity(), {}); // no UUID: runtime-only

    SceneDocument doc;
    ASSERT_TRUE(CaptureAndWrite(&doc).Succeeded());

    const char* expected = "scene:\n"
                           "  schema_version: 1\n"
                           "  uuid: 550e8400-e29b-41d4-a716-446655440000\n"
                           "  name: MainLevel\n"
                           "  component_versions:\n"
                           "    NameComponent: 1\n"
                           "  settings:\n"
                           "    grid:\n"
                           "      enabled: true\n"
                           "      origin: [0.0, 0.0, 0.0]\n"
                           "      orientation: [0.0, 0.0, 0.0, 1.0]\n"
                           "      base_spacing: 1.0\n"
                           "      major_line_every: 10\n"
                           "      minor_color: [0.6, 0.6, 0.6, 1.0]\n"
                           "      major_color: [0.3, 0.3, 0.3, 1.0]\n"
                           "      u_axis_color: [0.9, 0.2, 0.2, 1.0]\n"
                           "      v_axis_color: [0.2, 0.4, 1.0, 1.0]\n"
                           "      fade_distance: 500.0\n"
                           "      fade_strength: 0.5\n"
                           "      line_width: 1.5\n"
                           "      max_lod: 5\n"
                           "  entities:\n"
                           "    - uuid: 11111111-1111-1111-1111-111111111111\n"
                           "      components:\n"
                           "        NameComponent:\n"
                           "          value: Root\n";
    EXPECT_STREQ(ReadText(SCENE_PATH), expected);
}

TEST_F(SceneDocumentFixture, FreshArenaRoundTripPreservesSourceData)
{
    BuildHierarchy();
    m_scene.GetGridSettings().Origin         = {0.0f, 2.0f, 0.0f};
    m_scene.GetGridSettings().BaseSpacing    = 0.25f;
    m_scene.GetGridSettings().MajorLineEvery = 4;

    SceneDocument saved;
    ASSERT_TRUE(CaptureAndWrite(&saved).Succeeded());

    // Separate manager stands in for a fresh process: nothing shared with the writer.
    MemoryManager fresh;
    fresh.Initialize(ZMega(16), {});
    SceneDocument    loaded;
    SceneDiagnostics diag;
    ASSERT_TRUE(YAMLSceneDocumentSerializer{}.Read(m_vfs, P(SCENE_PATH), SceneSerializationLimits::Default(), &fresh.MainArena, &loaded, &diag).Succeeded()) << diag.Entries[0].Message;

    EXPECT_EQ(loaded.UUID, saved.UUID);
    EXPECT_STREQ(loaded.Name.c_str(), "MainLevel");
    EXPECT_EQ(loaded.Grid.Origin.y, 2.0f);
    EXPECT_EQ(loaded.Grid.BaseSpacing, 0.25f);
    EXPECT_EQ(loaded.Grid.MajorLineEvery, 4u);
    EXPECT_EQ(loaded.Grid.MinorColor.x, 0.6f);

    ASSERT_EQ(loaded.Entities.size(), saved.Entities.size());
    for (uint32_t e = 0; e < saved.Entities.size(); ++e)
    {
        EXPECT_EQ(loaded.Entities[e].UUID, saved.Entities[e].UUID);
        ASSERT_EQ(loaded.Entities[e].Components.size(), saved.Entities[e].Components.size());
        for (uint32_t c = 0; c < saved.Entities[e].Components.size(); ++c)
        {
            const auto& a = saved.Entities[e].Components[c];
            const auto& b = loaded.Entities[e].Components[c];
            EXPECT_STREQ(a.SchemaKey, b.SchemaKey);
            EXPECT_EQ(a.Version, b.Version);
            EXPECT_FALSE(b.IsOpaque);
            EXPECT_TRUE(SameValue(*a.Payload, *b.Payload)) << a.SchemaKey;
        }
    }

    // Hierarchy and asset references survive and still resolve against the loaded document.
    EXPECT_TRUE(ValidateSceneDocument(loaded, &m_registry, SceneSerializationLimits::Default(), &diag));
    EXPECT_STREQ(loaded.Entities[1].Components[2].Payload->Find("parent_uuid")->StringValue, ROOT_UUID);
    EXPECT_STREQ(loaded.Entities[1].Components[0].Payload->Find("mesh_uuid")->StringValue, MESH_UUID);
    fresh.Shutdown();
}

TEST_F(SceneDocumentFixture, SaveIsByteStableAcrossCaptureAndReload)
{
    BuildHierarchy();
    SceneDocument first;
    SceneDocument second;
    ASSERT_TRUE(CaptureAndWrite(&first, "/a.scene.yaml").Succeeded());
    ASSERT_TRUE(CaptureAndWrite(&second, "/b.scene.yaml").Succeeded());
    cstring a = ReadText("/a.scene.yaml");
    EXPECT_STREQ(a, ReadText("/b.scene.yaml"));

    // Reload and re-save without edits: identical bytes, so a stable diff.
    SceneDocument reloaded;
    ASSERT_TRUE(m_serializer.Read(m_vfs, P("/a.scene.yaml"), SceneSerializationLimits::Default(), &m_manager.MainArena, &reloaded, &m_diag).Succeeded());
    ASSERT_TRUE(m_serializer.WriteAtomic(m_vfs, P("/c.scene.yaml"), reloaded, &m_diag).Succeeded());
    EXPECT_STREQ(a, ReadText("/c.scene.yaml"));
}

TEST_F(SceneDocumentFixture, FloatsUseShortestExactText)
{
    BuildHierarchy();
    SceneDocument doc;
    ASSERT_TRUE(CaptureAndWrite(&doc).Succeeded());
    cstring text = ReadText(SCENE_PATH);
    EXPECT_NE(std::strstr(text, "position: [1.0, 1.5, -2.25]"), nullptr) << text;
    EXPECT_NE(std::strstr(text, "scale: [0.1, 1.0, 3.0]"), nullptr) << text;
}

TEST_F(SceneDocumentFixture, StringsThatLookLikeOtherTypesStayStrings)
{
    for (cstring name : {"123", "true", "null", "1.5", "", "~", "-7", ".inf"})
    {
        Scene scene;
        scene.Initialize(&m_manager.MainArena);
        scene.SetIdentity(U(SCENE_UUID), name);
        EntityID      e = scene.CreateEntity();
        NameComponent n{};
        Helpers::secure_strncpy(n.Value, sizeof(n.Value), name, Helpers::secure_strlen(name));
        scene.AddComponent<UUIDComponent>(e, {U(ROOT_UUID)});
        scene.AddComponent<NameComponent>(e, n);

        SceneDocument doc;
        ASSERT_TRUE(m_builder.Capture(scene, nullptr, &m_manager.MainArena, &doc, &m_diag).Succeeded()) << name;
        ASSERT_TRUE(m_serializer.WriteAtomic(m_vfs, P(SCENE_PATH), doc, &m_diag).Succeeded()) << name;
        SceneDocument loaded;
        ASSERT_TRUE(m_serializer.Read(m_vfs, P(SCENE_PATH), SceneSerializationLimits::Default(), &m_manager.MainArena, &loaded, &m_diag).Succeeded()) << name << ": " << ReadText(SCENE_PATH);
        EXPECT_STREQ(loaded.Name.c_str(), name);
        const SceneValue* value = loaded.Entities[0].Components[0].Payload->Find("value");
        ASSERT_EQ(value->Kind, SceneValueKind::String) << name;
        EXPECT_STREQ(value->StringValue, name);
        scene.Shutdown();
    }
}

// ---------------------------------------------------------------------------
// Atomic write
// ---------------------------------------------------------------------------

TEST_F(SceneDocumentFixture, FailedRenameKeepsPreviousFileAndLeavesNoTemp)
{
    EntityID      entity = Spawn(ROOT_UUID, "Before");
    SceneDocument before;
    ASSERT_TRUE(CaptureAndWrite(&before).Succeeded());
    cstring        original                               = ReadText(SCENE_PATH);
    const uint32_t files                                  = RootFileCount();

    m_scene.GetComponent<NameComponent>(entity)->Value[0] = 'X'; // a real edit
    SceneDocument after;
    ASSERT_TRUE(m_builder.Capture(m_scene, nullptr, &m_manager.MainArena, &after, &m_diag).Succeeded());

    FailingRenameContext failing;
    failing.Inner = &m_vfs;
    EXPECT_TRUE(m_serializer.WriteAtomic(failing, P(SCENE_PATH), after, &m_diag).Failed());
    EXPECT_TRUE(DiagHas(m_diag, "previous file kept"));
    EXPECT_STREQ(ReadText(SCENE_PATH), original);
    EXPECT_EQ(RootFileCount(), files);
}

TEST_F(SceneDocumentFixture, InvalidDocumentNeverTouchesDestination)
{
    Spawn(ROOT_UUID, "Before");
    SceneDocument doc;
    ASSERT_TRUE(CaptureAndWrite(&doc).Succeeded());
    cstring original                                                    = ReadText(SCENE_PATH);

    doc.Entities[0].Components[0].Payload->Members[0].Value->Kind       = SceneValueKind::Float;
    doc.Entities[0].Components[0].Payload->Members[0].Value->FloatValue = Core::Maths::QNAN<double>;
    EXPECT_TRUE(m_serializer.WriteAtomic(m_vfs, P(SCENE_PATH), doc, &m_diag).Failed());
    EXPECT_TRUE(DiagHas(m_diag, "non-finite"));
    EXPECT_STREQ(ReadText(SCENE_PATH), original);
}

// ---------------------------------------------------------------------------
// Read validation
// ---------------------------------------------------------------------------

namespace
{
    constexpr cstring MINIMAL = "scene:\n"
                                "  schema_version: 1\n"
                                "  uuid: 550e8400-e29b-41d4-a716-446655440000\n"
                                "  name: MainLevel\n";
}

TEST_F(SceneDocumentFixture, ReadMinimalDocumentUsesDefaultGrid)
{
    SceneDocument doc;
    ASSERT_TRUE(ReadYAML(MINIMAL, &doc).Succeeded());
    EXPECT_EQ(doc.Entities.size(), 0u);
    EXPECT_EQ(doc.Grid.BaseSpacing, 1.0f);
    EXPECT_EQ(doc.Grid.Orientation.w, 1.0f);
}

TEST_F(SceneDocumentFixture, ReadSortsHandEditedEntityOrder)
{
    SceneDocument doc;
    ASSERT_TRUE(ReadYAML(
                    "scene:\n"
                    "  schema_version: 1\n"
                    "  uuid: 550e8400-e29b-41d4-a716-446655440000\n"
                    "  name: S\n"
                    "  entities:\n"
                    "    - uuid: 22222222-2222-2222-2222-222222222222\n"
                    "    - uuid: 11111111-1111-1111-1111-111111111111\n",
                    &doc)
                    .Succeeded());
    EXPECT_EQ(doc.Entities[0].UUID, U(ROOT_UUID));
}

struct BadYAMLCase
{
    cstring Name;
    cstring Text;
    cstring Expect;
};

class SceneDocumentBadYAML : public SceneDocumentFixture, public ::testing::WithParamInterface<BadYAMLCase>
{
};

TEST_P(SceneDocumentBadYAML, IsRejectedWithDiagnostic)
{
    SceneDocument doc;
    EXPECT_TRUE(ReadYAML(GetParam().Text, &doc).Failed());
    EXPECT_TRUE(m_diag.HasErrors());
    EXPECT_TRUE(DiagHas(m_diag, GetParam().Expect)) << (m_diag.Count ? m_diag.Entries[0].Message : "(no diagnostics)");
}

INSTANTIATE_TEST_SUITE_P(
    Read,
    SceneDocumentBadYAML,
    ::testing::Values(
        BadYAMLCase{"Malformed", "scene: [unclosed\n", "yaml line"},
        BadYAMLCase{"NewerSchema", "scene:\n  schema_version: 2\n  uuid: 550e8400-e29b-41d4-a716-446655440000\n", "schema_version 2"},
        BadYAMLCase{"UnknownSceneField", "scene:\n  schema_version: 1\n  uuid: 550e8400-e29b-41d4-a716-446655440000\n  layers: []\n", "unknown scene field 'layers'"},
        BadYAMLCase{"BracedUUID", "scene:\n  schema_version: 1\n  uuid: '{550e8400-e29b-41d4-a716-446655440000}'\n", "canonical UUID"},
        BadYAMLCase{"NilSceneUUID", "scene:\n  schema_version: 1\n  uuid: 00000000-0000-0000-0000-000000000000\n", "uuid must not be nil"},
        BadYAMLCase{"DuplicateEntity", "scene:\n  schema_version: 1\n  uuid: 550e8400-e29b-41d4-a716-446655440000\n  entities:\n    - uuid: 11111111-1111-1111-1111-111111111111\n    - uuid: 11111111-1111-1111-1111-111111111111\n", "duplicate"},
        BadYAMLCase{"MissingVersion", "scene:\n  schema_version: 1\n  uuid: 550e8400-e29b-41d4-a716-446655440000\n  entities:\n    - uuid: 11111111-1111-1111-1111-111111111111\n      components:\n        NameComponent: {value: A}\n", "component_versions"},
        BadYAMLCase{"NonFiniteFloat", "scene:\n  schema_version: 1\n  uuid: 550e8400-e29b-41d4-a716-446655440000\n  component_versions: {T: 1}\n  entities:\n    - uuid: 11111111-1111-1111-1111-111111111111\n      components:\n        T: {x: .inf}\n", "non-finite"},
        BadYAMLCase{"ZeroSpacing", "scene:\n  schema_version: 1\n  uuid: 550e8400-e29b-41d4-a716-446655440000\n  settings:\n    grid:\n      base_spacing: 0.0\n", "base_spacing"},
        BadYAMLCase{"NonUnitOrientation", "scene:\n  schema_version: 1\n  uuid: 550e8400-e29b-41d4-a716-446655440000\n  settings:\n    grid:\n      orientation: [0.0, 0.0, 0.0, 2.0]\n", "unit quaternion"},
        BadYAMLCase{"DuplicateMapKey", "scene:\n  schema_version: 1\n  uuid: 550e8400-e29b-41d4-a716-446655440000\n  component_versions: {T: 1}\n  entities:\n    - uuid: 11111111-1111-1111-1111-111111111111\n      components:\n        T: {x: 1, x: 2}\n", "duplicate key"},
        BadYAMLCase{"ExplicitTag", "scene:\n  schema_version: 1\n  uuid: 550e8400-e29b-41d4-a716-446655440000\n  component_versions: {T: 1}\n  entities:\n    - uuid: 11111111-1111-1111-1111-111111111111\n      components:\n        T: {x: !custom 1}\n", "explicit tag"}),
    [](const ::testing::TestParamInfo<BadYAMLCase>& info) { return info.param.Name; });

TEST_F(SceneDocumentFixture, ReadEnforcesLimits)
{
    SceneSerializationLimits tight = SceneSerializationLimits::Default();
    SceneDocument            doc;

    tight.MaxSourceBytes = 16;
    EXPECT_EQ(ReadYAML(MINIMAL, &doc, tight).Error(), VFSError::SizeLimitExceeded);

    tight                 = SceneSerializationLimits::Default();
    tight.MaxNestingDepth = 2;
    EXPECT_TRUE(ReadYAML("scene:\n  schema_version: 1\n  uuid: 550e8400-e29b-41d4-a716-446655440000\n  component_versions: {T: 1}\n  entities:\n    - uuid: 11111111-1111-1111-1111-111111111111\n      components:\n        T: {a: {b: {c: 1}}}\n", &doc, tight).Failed());
    EXPECT_TRUE(DiagHas(m_diag, "nesting depth"));

    tight                = SceneSerializationLimits::Default();
    tight.MaxStringBytes = 3;
    EXPECT_TRUE(ReadYAML("scene:\n  schema_version: 1\n  uuid: 550e8400-e29b-41d4-a716-446655440000\n  component_versions: {T: 1}\n  entities:\n    - uuid: 11111111-1111-1111-1111-111111111111\n      components:\n        T: {s: toolong}\n", &doc, tight).Failed());
    EXPECT_TRUE(DiagHas(m_diag, "exceeds 3 bytes"));

    tight             = SceneSerializationLimits::Default();
    tight.MaxEntities = 1;
    EXPECT_TRUE(ReadYAML("scene:\n  schema_version: 1\n  uuid: 550e8400-e29b-41d4-a716-446655440000\n  entities:\n    - uuid: 11111111-1111-1111-1111-111111111111\n    - uuid: 22222222-2222-2222-2222-222222222222\n", &doc, tight).Failed());
}

TEST_F(SceneDocumentFixture, ReadRejectsAliasExpansion)
{
    // Each level doubles the node count; the budget is bounded by the source size.
    SceneDocument doc;
    EXPECT_TRUE(ReadYAML(
                    "scene:\n  schema_version: 1\n  uuid: 550e8400-e29b-41d4-a716-446655440000\n  component_versions: {T: 1}\n  entities:\n    - uuid: 11111111-1111-1111-1111-111111111111\n      components:\n        T:\n"
                    "          a: &a [1, 1, 1, 1, 1, 1, 1, 1]\n"
                    "          b: &b [*a, *a, *a, *a, *a, *a, *a, *a]\n"
                    "          c: &c [*b, *b, *b, *b, *b, *b, *b, *b]\n"
                    "          d: [*c, *c, *c, *c, *c, *c, *c, *c]\n",
                    &doc)
                    .Failed());
    EXPECT_TRUE(DiagHas(m_diag, "alias"));
}

TEST_F(SceneDocumentFixture, ValidationWithSchemasRejectsDanglingEntityReference)
{
    SceneDocument doc;
    ASSERT_TRUE(ReadYAML("scene:\n  schema_version: 1\n  uuid: 550e8400-e29b-41d4-a716-446655440000\n  component_versions: {ParentComponent: 1}\n  entities:\n    - uuid: 11111111-1111-1111-1111-111111111111\n      components:\n        ParentComponent: {parent_uuid: 99999999-9999-9999-9999-999999999999}\n", &doc).Succeeded());
    EXPECT_FALSE(ValidateSceneDocument(doc, &m_registry, SceneSerializationLimits::Default(), &m_diag));
    EXPECT_TRUE(DiagHas(m_diag, "not in the document"));
}

TEST_F(SceneDocumentFixture, ValidationWithSchemasRejectsUnknownComponentUnlessOpaque)
{
    SceneDocument doc;
    ASSERT_TRUE(ReadYAML("scene:\n  schema_version: 1\n  uuid: 550e8400-e29b-41d4-a716-446655440000\n  component_versions: {PluginComponent: 4}\n  entities:\n    - uuid: 11111111-1111-1111-1111-111111111111\n      components:\n        PluginComponent: {x: 1}\n", &doc).Succeeded());
    EXPECT_FALSE(ValidateSceneDocument(doc, &m_registry, SceneSerializationLimits::Default(), &m_diag));
    EXPECT_TRUE(DiagHas(m_diag, "PluginComponent"));

    m_diag.Clear();
    doc.Entities[0].Components[0].IsOpaque = true;
    EXPECT_TRUE(ValidateSceneDocument(doc, &m_registry, SceneSerializationLimits::Default(), &m_diag));
}
