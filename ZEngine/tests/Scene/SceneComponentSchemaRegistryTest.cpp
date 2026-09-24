#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/SceneComponentSchemaRegistry.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::Core::Memory;
using ZEngine::Helpers::secure_strcmp;

namespace
{
    // Bodies are never called: the registry only stores and validates the pointers.
    bool Capture(void*, EntityID, const Scene&, SceneValueWriter*, SceneDiagnostics*)
    {
        return true;
    }
    bool PopulateCandidate(void*, EntityID, Scene&, const SceneValue&, SceneDiagnostics*)
    {
        return true;
    }
    bool EncodeBinary(void*, const SceneValue&, SceneBinaryWriter*, SceneDiagnostics*)
    {
        return true;
    }
    bool DecodeBinary(void*, uint32_t, SceneBinaryReader*, SceneValueWriter*, SceneDiagnostics*)
    {
        return true;
    }
    bool Enumerate(void*, const SceneValue&, SceneReferenceVisitorFn, void*, SceneDiagnostics*)
    {
        return true;
    }
    bool Remap(void*, const SceneValue&, const SceneUUIDRemap&, SceneValueWriter*, SceneDiagnostics*)
    {
        return true;
    }

    SceneComponentCodecFns FullCodecs()
    {
        SceneComponentCodecFns fns{};
        fns.Capture           = Capture;
        fns.PopulateCandidate = PopulateCandidate;
        fns.EncodeBinary      = EncodeBinary;
        fns.DecodeBinary      = DecodeBinary;
        return fns;
    }

    SceneComponentSchemaDesc Desc(cstring key, ComponentTypeID type, uint32_t version = 1)
    {
        SceneComponentSchemaDesc d{};
        d.Key         = key;
        d.Version     = version;
        d.RuntimeType = type;
        d.Codecs      = FullCodecs();
        return d;
    }

    struct OrderCtx
    {
        char     Keys[16][64] = {};
        uint32_t Count        = 0;
    };

    void CollectKeys(void* ctx, const SceneComponentSchema& schema)
    {
        auto* c = static_cast<OrderCtx*>(ctx);
        if (c->Count < 16)
        {
            snprintf(c->Keys[c->Count], 64, "%s", schema.Key);
            ++c->Count;
        }
    }
} // namespace

class SchemaRegistryFixture : public ::testing::Test
{
protected:
    MemoryManager                m_manager;
    SceneComponentSchemaRegistry m_registry;
    SceneDiagnostics             m_diag;

    void                         SetUp() override
    {
        m_manager.Initialize(ZMega(4), {});
        m_registry.Initialize(&m_manager.MainArena);
    }

    void TearDown() override
    {
        m_manager.Shutdown();
    }
};

TEST_F(SchemaRegistryFixture, IsUnusableUntilInitialized)
{
    SceneComponentSchemaRegistry fresh;
    EXPECT_FALSE(fresh.IsInitialized());

    SceneDiagnostics diag;
    EXPECT_FALSE(fresh.Register(Desc("transform", 1), &diag));
    EXPECT_TRUE(diag.HasErrors());

    EXPECT_TRUE(m_registry.IsInitialized());
}

TEST_F(SchemaRegistryFixture, InitializeIsIdempotent)
{
    ASSERT_TRUE(m_registry.Register(Desc("transform", 1), &m_diag));

    MemoryManager other;
    other.Initialize(ZMega(1), {});
    m_registry.Initialize(&other.MainArena);

    EXPECT_EQ(m_registry.Count(), 1u);
    EXPECT_NE(m_registry.FindByKey("transform"), nullptr);
    other.Shutdown();
}

TEST_F(SchemaRegistryFixture, RegisterThenLookupByKeyAndRuntimeType)
{
    SceneFieldSchema fields[] = {
        {     "position",       SceneFieldClass::Authored},
        {"cached_matrix", SceneFieldClass::RuntimeDerived},
    };

    SceneComponentSchemaDesc d = Desc("transform", 7, 3);
    d.Fields                   = fields;
    d.FieldCount               = 2;
    ASSERT_TRUE(m_registry.Register(d, &m_diag));
    EXPECT_FALSE(m_diag.HasErrors());

    const SceneComponentSchema* by_key  = m_registry.FindByKey("transform");
    const SceneComponentSchema* by_type = m_registry.FindByRuntimeType(7);
    ASSERT_NE(by_key, nullptr);
    EXPECT_EQ(by_key, by_type);
    EXPECT_EQ(by_key->Version, 3u);
    EXPECT_EQ(by_key->FieldCount, 2u);
    EXPECT_EQ(secure_strcmp(by_key->Fields[0].Key, "position"), 0);
    EXPECT_EQ(by_key->Fields[1].Class, SceneFieldClass::RuntimeDerived);

    EXPECT_EQ(m_registry.FindByKey("nope"), nullptr);
    EXPECT_EQ(m_registry.FindByKey(nullptr), nullptr);
    EXPECT_EQ(m_registry.FindByRuntimeType(999), nullptr);
}

// Register copies the strings, so the caller's buffers may die immediately after.
TEST_F(SchemaRegistryFixture, KeysAreCopiedNotBorrowed)
{
    char             key[32]   = "transform";
    char             field[32] = "position";
    SceneFieldSchema fields[]  = {
        {field, SceneFieldClass::Authored}
    };

    SceneComponentSchemaDesc d = Desc(key, 1);
    d.Fields                   = fields;
    d.FieldCount               = 1;
    ASSERT_TRUE(m_registry.Register(d, &m_diag));

    snprintf(key, sizeof(key), "%s", "CLOBBERED");
    snprintf(field, sizeof(field), "%s", "CLOBBERED");

    const SceneComponentSchema* schema = m_registry.FindByKey("transform");
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(secure_strcmp(schema->Key, "transform"), 0);
    EXPECT_EQ(secure_strcmp(schema->Fields[0].Key, "position"), 0);
}

TEST_F(SchemaRegistryFixture, ForEachCanonicalIsLexicographicNotRegistrationOrder)
{
    ASSERT_TRUE(m_registry.Register(Desc("zeta", 1), &m_diag));
    ASSERT_TRUE(m_registry.Register(Desc("alpha", 2), &m_diag));
    ASSERT_TRUE(m_registry.Register(Desc("mesh", 3), &m_diag));
    ASSERT_TRUE(m_registry.Register(Desc("beta", 4), &m_diag));

    OrderCtx ctx{};
    m_registry.ForEachCanonical(CollectKeys, &ctx);

    ASSERT_EQ(ctx.Count, 4u);
    EXPECT_STREQ(ctx.Keys[0], "alpha");
    EXPECT_STREQ(ctx.Keys[1], "beta");
    EXPECT_STREQ(ctx.Keys[2], "mesh");
    EXPECT_STREQ(ctx.Keys[3], "zeta");
}

TEST_F(SchemaRegistryFixture, ForEachCanonicalWithNullVisitorIsANoOp)
{
    ASSERT_TRUE(m_registry.Register(Desc("transform", 1), &m_diag));
    m_registry.ForEachCanonical(nullptr, nullptr);
    SUCCEED();
}

TEST_F(SchemaRegistryFixture, RejectsInvalidKeys)
{
    EXPECT_FALSE(m_registry.Register(Desc(nullptr, 1), &m_diag));
    EXPECT_FALSE(m_registry.Register(Desc("", 2), &m_diag));
    EXPECT_FALSE(m_registry.Register(Desc("has space", 3), &m_diag));
    EXPECT_FALSE(m_registry.Register(Desc("has/slash", 4), &m_diag));
    EXPECT_FALSE(m_registry.Register(Desc("has\"quote", 5), &m_diag));
    EXPECT_EQ(m_registry.Count(), 0u);
    EXPECT_TRUE(m_diag.HasErrors());
}

TEST_F(SchemaRegistryFixture, RejectsVersionZero)
{
    EXPECT_FALSE(m_registry.Register(Desc("transform", 1, 0), &m_diag));
    EXPECT_EQ(m_registry.Count(), 0u);
}

TEST_F(SchemaRegistryFixture, RejectsDuplicateKeyAndDuplicateRuntimeType)
{
    ASSERT_TRUE(m_registry.Register(Desc("transform", 1), &m_diag));

    EXPECT_FALSE(m_registry.Register(Desc("transform", 2), &m_diag)); // duplicate key
    EXPECT_FALSE(m_registry.Register(Desc("other", 1), &m_diag));     // duplicate runtime type
    EXPECT_EQ(m_registry.Count(), 1u);
}

TEST_F(SchemaRegistryFixture, RejectsBadFieldDeclarations)
{
    SceneFieldSchema dupes[] = {
        {"position", SceneFieldClass::Authored},
        {"position", SceneFieldClass::Authored},
    };
    SceneComponentSchemaDesc d = Desc("transform", 1);
    d.Fields                   = dupes;
    d.FieldCount               = 2;
    EXPECT_FALSE(m_registry.Register(d, &m_diag));

    SceneFieldSchema missing[] = {
        {nullptr, SceneFieldClass::Authored}
    };
    d.Fields     = missing;
    d.FieldCount = 1;
    EXPECT_FALSE(m_registry.Register(d, &m_diag));

    d.Fields     = nullptr;
    d.FieldCount = 2; // count without an array
    EXPECT_FALSE(m_registry.Register(d, &m_diag));

    EXPECT_EQ(m_registry.Count(), 0u);
}

TEST_F(SchemaRegistryFixture, RejectsMissingCodecCallbacks)
{
    SceneComponentSchemaDesc d = Desc("transform", 1);
    d.Codecs.Capture           = nullptr;
    EXPECT_FALSE(m_registry.Register(d, &m_diag));

    d                     = Desc("transform", 1);
    d.Codecs.DecodeBinary = nullptr;
    EXPECT_FALSE(m_registry.Register(d, &m_diag));

    EXPECT_EQ(m_registry.Count(), 0u);
}

TEST_F(SchemaRegistryFixture, RejectsHalfOfTheReferenceHookPair)
{
    SceneComponentSchemaDesc d = Desc("transform", 1);
    d.References.Enumerate     = Enumerate; // Remap left null
    EXPECT_FALSE(m_registry.Register(d, &m_diag));

    d                  = Desc("mesh", 2);
    d.References.Remap = Remap; // Enumerate left null
    EXPECT_FALSE(m_registry.Register(d, &m_diag));

    d                      = Desc("light", 3);
    d.References.Enumerate = Enumerate;
    d.References.Remap     = Remap;
    EXPECT_TRUE(m_registry.Register(d, &m_diag)); // both is fine

    EXPECT_TRUE(m_registry.Register(Desc("camera", 4), &m_diag)); // neither is fine
    EXPECT_EQ(m_registry.Count(), 2u);
}

TEST_F(SchemaRegistryFixture, RejectionsAreBoundedAndDoNotCorruptState)
{
    ASSERT_TRUE(m_registry.Register(Desc("transform", 1), &m_diag));

    for (int i = 0; i < 500; ++i)
    {
        EXPECT_FALSE(m_registry.Register(Desc("transform", 1), &m_diag));
    }

    EXPECT_EQ(m_registry.Count(), 1u);
    EXPECT_NE(m_registry.FindByKey("transform"), nullptr);
    EXPECT_LE(m_diag.Count, SceneDiagnostics::MAX_ENTRIES); // bounded, overflow counted
    EXPECT_GT(m_diag.Dropped, 0u);
}

TEST_F(SchemaRegistryFixture, RegisterWithNullDiagnosticsStillRejects)
{
    EXPECT_FALSE(m_registry.Register(Desc(nullptr, 1), nullptr));
    EXPECT_EQ(m_registry.Count(), 0u);
}

TEST_F(SchemaRegistryFixture, RejectsRegistrationPastReservedCapacity)
{
    MemoryManager small;
    small.Initialize(ZMega(1), {});
    SceneComponentSchemaRegistry tight;
    tight.Initialize(&small.MainArena, 2);

    SceneDiagnostics diag;
    EXPECT_TRUE(tight.Register(Desc("a", 1), &diag));
    EXPECT_TRUE(tight.Register(Desc("b", 2), &diag));
    EXPECT_FALSE(tight.Register(Desc("c", 3), &diag)); // would realloc and dangle lookups
    EXPECT_EQ(tight.Count(), 2u);

    small.Shutdown();
}

TEST(SchemaRegistryAllocationTest, ExhaustedArenaLeavesRegistryUninitialized)
{
    MemoryManager small;
    small.Initialize(ZKilo(64), {});

    SceneComponentSchemaRegistry registry;
    registry.Initialize(&small.MainArena, 100000000u); // far beyond the arena

    EXPECT_FALSE(registry.IsInitialized());

    SceneDiagnostics diag;
    EXPECT_FALSE(registry.Register(Desc("transform", 1), &diag));
    EXPECT_TRUE(diag.HasErrors());
    EXPECT_EQ(registry.Count(), 0u);

    small.Shutdown();
}

// A registration that runs out of arena must publish nothing at all.
TEST(SchemaRegistryAllocationTest, ExhaustedArenaDuringRegisterPublishesNoSchema)
{
    MemoryManager small;
    small.Initialize(ZKilo(64), {});

    SceneComponentSchemaRegistry registry;
    registry.Initialize(&small.MainArena, 8);
    ASSERT_TRUE(registry.IsInitialized());

    SceneDiagnostics diag;
    ASSERT_TRUE(registry.Register(Desc("first", 1), &diag));

    SceneComponentSchemaDesc huge      = Desc("second", 2);
    static SceneFieldSchema  fields[3] = {
        {"a", SceneFieldClass::Authored},
        {"b", SceneFieldClass::Authored},
        {"c", SceneFieldClass::Authored},
    };
    huge.Fields     = fields;
    huge.FieldCount = 3;

    for (size_t block = 1024; block >= 1; block /= 2)
    {
        while (small.MainArena.Allocate(block, 16) != nullptr)
        {
        }
        if (block == 1)
        {
            break;
        }
    }

    EXPECT_FALSE(registry.Register(huge, &diag));
    EXPECT_EQ(registry.Count(), 1u);
    EXPECT_EQ(registry.FindByKey("second"), nullptr);
    EXPECT_NE(registry.FindByKey("first"), nullptr); // untouched

    small.Shutdown();
}

TEST_F(SchemaRegistryFixture, RejectsOutOfRangeFieldClass)
{
    SceneFieldSchema bogus[] = {
        {"position", static_cast<SceneFieldClass>(200)},
    };

    SceneComponentSchemaDesc d = Desc("transform", 1);
    d.Fields                   = bogus;
    d.FieldCount               = 1;

    EXPECT_FALSE(m_registry.Register(d, &m_diag));
    EXPECT_TRUE(m_diag.HasErrors());
    EXPECT_EQ(m_registry.Count(), 0u);
}

TEST_F(SchemaRegistryFixture, AcceptsEveryDeclaredFieldClass)
{
    SceneFieldSchema all[] = {
        { "authored",       SceneFieldClass::Authored},
        {  "derived", SceneFieldClass::RuntimeDerived},
        {   "editor",     SceneFieldClass::EditorOnly},
        {"forbidden",      SceneFieldClass::Forbidden},
    };

    SceneComponentSchemaDesc d = Desc("transform", 1);
    d.Fields                   = all;
    d.FieldCount               = 4;

    EXPECT_TRUE(m_registry.Register(d, &m_diag));
    EXPECT_EQ(m_registry.Count(), 1u);
}
