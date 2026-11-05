#include <ZEngine/Cook/CookDependencyGraph.h>
#include <ZEngine/Cook/CookFingerprint.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Cook/CookSort.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        Digest256 Pattern(uint8_t value)
        {
            Digest256 digest = {};
            std::fill(digest.Bytes.begin(), digest.Bytes.end(), value);
            return digest;
        }

        CookAssetIdentity RecipeIdentity(uint32_t id)
        {
            char text[37] = {};
            std::snprintf(text, sizeof(text), "00000000-0000-0000-0000-%012x", id);
            constexpr CookAssetKind kinds[] = {CookAssetKind::Unknown, CookAssetKind::Texture, CookAssetKind::Material, CookAssetKind::Mesh, CookAssetKind::EnvironmentMap};
            return {id <= 4 ? kinds[id] : CookAssetKind::Unknown, uuids::uuid::from_string(text).value()};
        }

        ECS::SceneComponentCodecFns UncalledCodecs(bool alternate = false)
        {
            ECS::SceneComponentCodecFns codecs = {};
            codecs.Capture                     = [](void*, ECS::EntityID, const ECS::Scene&, ECS::SceneValueWriter*, ECS::SceneDiagnostics*) { return true; };
            if (alternate)
            {
                codecs.Capture = [](void*, ECS::EntityID, const ECS::Scene&, ECS::SceneValueWriter*, ECS::SceneDiagnostics*) { return false; };
            }
            codecs.PopulateCandidate = [](void*, ECS::EntityID, ECS::Scene&, const ECS::SceneValue&, ECS::SceneDiagnostics*) { return true; };
            codecs.EncodeBinary      = [](void*, const ECS::SceneValue&, ECS::SceneBinaryWriter*, ECS::SceneDiagnostics*) { return true; };
            codecs.DecodeBinary      = [](void*, uint32_t, ECS::SceneBinaryReader*, ECS::SceneValueWriter*, ECS::SceneDiagnostics*) { return true; };
            return codecs;
        }
    } // namespace

    TEST(CookObjectTest, ExactContentAddressedAssetAndScenePathsShareOneSpellingRule)
    {
        const auto           digest = Pattern(0x31);
        CookObjectDescriptor object = {};
        ASSERT_TRUE(MakeCookObjectDescriptor(digest, 101, "png", &object).Succeeded());
        EXPECT_STREQ(object.Path.CStr(), "objects/3131313131313131313131313131313131313131313131313131313131313131.png");
        EXPECT_EQ(object.Digest, digest);
        EXPECT_EQ(object.Size, 101u);
        EXPECT_TRUE(IsCookObjectDescriptorValid(object, "png"));
        EXPECT_TRUE(IsCookObjectDescriptorValid(*FindCookAssetFormat("image-png"), object.Path, object.Digest, object.Size));
        EXPECT_FALSE(IsCookObjectDescriptorValid(*FindCookAssetFormat("image-jpeg"), object.Path, object.Digest, object.Size));
        EXPECT_FALSE(IsCookObjectDescriptorValid(object, "jpg"));
        EXPECT_FALSE(IsCookObjectPath(object.Path, Pattern(0x32), object.Size, "png"));
        ASSERT_TRUE(MakeCookObjectDescriptor(digest, 200, CookSceneObjectExtension, &object).Succeeded());
        EXPECT_TRUE(IsCookObjectDescriptorValid(object, CookSceneObjectExtension));
        EXPECT_FALSE(IsCookObjectDescriptorValid(object, "png"));
        char changed[256] = {};
        ASSERT_EQ(Helpers::secure_strcpy(changed, sizeof(changed), object.Path.CStr()), Helpers::MEMORY_OP_SUCCESS);
        changed[0]      = 'O';
        const auto path = VFS::VFSRelativePath::Parse(changed);
        ASSERT_TRUE(path.Succeeded());
        EXPECT_FALSE(IsCookObjectPath(path.Value(), digest, object.Size, CookSceneObjectExtension));
    }

    TEST(CookObjectTest, InvalidExtensionsAndSizesNeverPublishPartialDescriptors)
    {
        CookObjectDescriptor object = {};
        ASSERT_TRUE(MakeCookObjectDescriptor(Pattern(1), 20, "png", &object).Succeeded());
        const auto original       = object;
        const char embedded_nul[] = {'p', '\0', 'g'};
        for (const Core::Containers::StringView extension : {
             Core::Containers::StringView{},
             Core::Containers::StringView{nullptr, 1},
             Core::Containers::StringView{"PNG"},
             Core::Containers::StringView{"../png"},
             Core::Containers::StringView{".png"},
             Core::Containers::StringView{"p/g"},
             Core::Containers::StringView{"p\\g"},
             Core::Containers::StringView{"abcdefghijklmnopq"},
             Core::Containers::StringView{embedded_nul, sizeof(embedded_nul)}
        })
        {
            EXPECT_EQ(MakeCookObjectDescriptor(Pattern(2), 30, extension, &object).Error(), VFS::VFSError::InvalidPath);
            EXPECT_EQ(object.Path, original.Path);
            EXPECT_EQ(object.Digest, original.Digest);
            EXPECT_EQ(object.Size, original.Size);
        }
        for (const uint64_t size : {uint64_t{0}, uint64_t{ZGiga(16)} + 1, UINT64_MAX})
        {
            EXPECT_EQ(MakeCookObjectDescriptor(Pattern(2), size, "png", &object).Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(object.Path, original.Path);
            EXPECT_EQ(object.Digest, original.Digest);
            EXPECT_EQ(object.Size, original.Size);
        }
        EXPECT_EQ(MakeCookObjectDescriptor({}, 1, "png", nullptr).Error(), VFS::VFSError::InvalidPath);
        ASSERT_TRUE(MakeCookObjectDescriptor({}, ZGiga(16), "abcdefghijklmnop", &object).Succeeded());
        EXPECT_TRUE(IsCookObjectDescriptorValid(object, "abcdefghijklmnop"));
    }

    class CookSchemaFingerprintTest : public testing::Test
    {
    protected:
        Tests::FixedAllocator<ZKilo(4), 1> Allocator = {};
        Core::Memory::MemoryManager        Memory    = {};
        ECS::SceneComponentSchemaRegistry  First     = {};
        ECS::SceneComponentSchemaRegistry  Second    = {};
        void                               SetUp() override
        {
            Memory.Initialize(ZMega(2), {});
            First.Initialize(&Memory.MainArena);
            Second.Initialize(&Memory.MainArena);
        }

        void Register(ECS::SceneComponentSchemaRegistry& registry, bool reverse = false, uint32_t runtime = 1, uint32_t version = 3, ECS::SceneFieldClass field_class = ECS::SceneFieldClass::EditorOnly, bool extra_field = false)
        {
            ECS::SceneFieldSchema fields[] = {
                {"z",      ECS::SceneFieldClass::Forbidden},
                {"a",       ECS::SceneFieldClass::Authored},
                {"m",                          field_class},
                {"b", ECS::SceneFieldClass::RuntimeDerived}
            };
            const uint32_t count = extra_field ? 4 : 3;
            if (reverse)
            {
                std::reverse(fields, fields + count);
            }
            const ECS::SceneComponentSchemaDesc alpha = {.Key = "Alpha", .Version = version, .RuntimeType = runtime, .Fields = fields, .FieldCount = count, .Codecs = UncalledCodecs(reverse)};
            const ECS::SceneComponentSchemaDesc zeta  = {.Key = "Zeta", .Version = 7, .RuntimeType = runtime + 1, .Codecs = UncalledCodecs(reverse)};
            ASSERT_TRUE(registry.Register(reverse ? alpha : zeta, nullptr));
            ASSERT_TRUE(registry.Register(reverse ? zeta : alpha, nullptr));
        }
    };

    TEST_F(CookSchemaFingerprintTest, SettingsMatchTheExactContractIncludingFinalLF)
    {
        const auto json = CookSettingsJSON();
        EXPECT_EQ(json, Core::Containers::StringView("{}\n", 3));
        Digest256 digest = {};
        ASSERT_TRUE(ComputeCookSettingsKey(CookTarget::DesktopVulkanLE64, &digest).Succeeded());
        Core::Hash::SHA256 reference = {};
        Digest256          computed  = {};
        ASSERT_TRUE(reference.Update({reinterpret_cast<const uint8_t*>(json.data()), json.size()}).Succeeded());
        ASSERT_TRUE(reference.Finalize(&computed).Succeeded());
        EXPECT_EQ(digest, computed);
        EXPECT_STREQ(digest.Hex().data(), "ca3d163bab055381827226140568f3bef7eaac187cebd76878e0b63e9e442356");
        const auto previous = digest;
        EXPECT_EQ(ComputeCookSettingsKey(CookTarget::Invalid, &digest).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(digest, previous);
        EXPECT_EQ(ComputeCookSettingsKey(CookTarget::DesktopVulkanLE64, nullptr).Error(), VFS::VFSError::InvalidPath);
    }

    TEST_F(CookSchemaFingerprintTest, FullSchemaKeyMatchesIndependentTypedRubyGoldenAndIgnoresRegistrationRuntimeAndCallbackOrder)
    {
        Register(First);
        Register(Second, true, 500);
        ASSERT_EQ(CookSchemaKeyWorkspaceWords(First).Value(), 3u);
        uint32_t   workspace[3] = {};
        Digest256  first = {}, second = {};
        const auto offset = Memory.MainArena.m_current_offset;
        ASSERT_TRUE(ComputeCookSchemaKey(Allocator, First, workspace, &first).Succeeded());
        ASSERT_TRUE(ComputeCookSchemaKey(Allocator, Second, workspace, &second).Succeeded());
        EXPECT_EQ(first, second);
        EXPECT_EQ(Memory.MainArena.m_current_offset, offset);
        // Independent Ruby Digest::SHA256 over explicit V-packed u32 words and
        // byte-count-prefixed strings; schemas Alpha/Zeta, fields a/m/z.
        EXPECT_STREQ(first.Hex().data(), "8474c7b495d387203afdd63caa935cacd9fe250a7385fd5d8b97201225fd9d9a");
        EXPECT_STREQ(First.FindByKey("Alpha")->Fields[0].Key, "z");
        EXPECT_STREQ(Second.FindByKey("Alpha")->Fields[0].Key, "m");
    }

    TEST_F(CookSchemaFingerprintTest, EveryRegisteredFieldClassAndVersionParticipates)
    {
        Register(First);
        uint32_t  workspace[4] = {};
        Digest256 original     = {};
        ASSERT_TRUE(ComputeCookSchemaKey(Allocator, First, workspace, &original).Succeeded());
        for (uint32_t variation = 0; variation < 3; ++variation)
        {
            ECS::SceneComponentSchemaRegistry registry = {};
            registry.Initialize(&Memory.MainArena);
            Register(registry, false, 1, variation == 0 ? 4 : 3, variation == 1 ? ECS::SceneFieldClass::RuntimeDerived : ECS::SceneFieldClass::EditorOnly, variation == 2);
            Digest256 changed = {};
            ASSERT_TRUE(ComputeCookSchemaKey(Allocator, registry, workspace, &changed).Succeeded());
            EXPECT_NE(changed, original);
        }
    }

    TEST_F(CookSchemaFingerprintTest, WorkspaceTracksLargestObservedFieldTableWithoutAHiddenSixtyFourFieldCap)
    {
        Core::Containers::FixedArray<ECS::SceneFieldSchema, 257>                  fields = {};
        Core::Containers::FixedArray<Core::Containers::FixedArray<char, 16>, 257> keys   = {};
        for (uint32_t i = 0; i < fields.size(); ++i)
        {
            std::snprintf(keys[i].data(), keys[i].size(), "field%03u", 256 - i);
            fields[i] = {keys[i].data(), ECS::SceneFieldClass::Authored};
        }
        ASSERT_TRUE(First.Register({.Key = "Large", .Version = 1, .RuntimeType = 1, .Fields = fields.data(), .FieldCount = static_cast<uint32_t>(fields.size()), .Codecs = UncalledCodecs()}, nullptr));
        const auto required = CookSchemaKeyWorkspaceWords(First);
        ASSERT_TRUE(required.Succeeded());
        EXPECT_EQ(required.Value(), fields.size());
        Core::Containers::FixedArray<uint32_t, 257> workspace = {};
        Digest256                                   output    = Pattern(1);
        const auto                                  before    = output;
        EXPECT_EQ(ComputeCookSchemaKey(Allocator, First, {workspace.data(), workspace.size() - 1}, &output).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(output, before);
        const auto offset = Memory.MainArena.m_current_offset;
        ASSERT_TRUE(ComputeCookSchemaKey(Allocator, First, workspace, &output).Succeeded());
        EXPECT_EQ(Memory.MainArena.m_current_offset, offset);
        EXPECT_STREQ(First.FindByKey("Large")->Fields[0].Key, "field256");
        std::reverse(fields.begin(), fields.end());
        ASSERT_TRUE(Second.Register({.Key = "Large", .Version = 1, .RuntimeType = 500, .Fields = fields.data(), .FieldCount = static_cast<uint32_t>(fields.size()), .Codecs = UncalledCodecs(true)}, nullptr));
        Digest256 reordered = {};
        ASSERT_TRUE(ComputeCookSchemaKey(Allocator, Second, workspace, &reordered).Succeeded());
        EXPECT_EQ(output, reordered);
        for (uint32_t i = 0; i < workspace.size(); ++i)
        {
            EXPECT_EQ(workspace[i], i);
        }
    }

    TEST_F(CookSchemaFingerprintTest, InvalidSetupAndWorkspacePreserveRecipeAndDigestOutputs)
    {
        ECS::SceneComponentSchemaRegistry uninitialized = {};
        EXPECT_EQ(CookSchemaKeyWorkspaceWords(uninitialized).Error(), VFS::VFSError::InvalidPath);
        Register(First);
        Digest256 output = Pattern(1);
        EXPECT_EQ(ComputeCookSchemaKey(Allocator, uninitialized, {}, &output).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(ComputeCookSchemaKey(Allocator, First, {nullptr, 3}, &output).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(ComputeCookSchemaKey(Allocator, First, {}, &output).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(output, Pattern(1));
        uint32_t       workspace[3] = {};
        CookRecipeKeys recipe       = {.Revision = 99, .Target = CookTarget::Invalid, .SchemaKey = Pattern(2), .SettingsKey = Pattern(3)};
        EXPECT_EQ(ComputeCookRecipeKeys(Allocator, First, CookTarget::DesktopVulkanLE64, {}, &recipe).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(recipe.Revision, 99u);
        EXPECT_EQ(recipe.SchemaKey, Pattern(2));
        EXPECT_EQ(recipe.SettingsKey, Pattern(3));
        EXPECT_EQ(ComputeCookRecipeKeys(Allocator, First, CookTarget::Invalid, workspace, &recipe).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(recipe.Revision, 99u);
        ASSERT_TRUE(ComputeCookRecipeKeys(Allocator, First, CookTarget::DesktopVulkanLE64, workspace, &recipe).Succeeded());
        EXPECT_EQ(recipe.Revision, CookRecipeRevision);
        EXPECT_EQ(recipe.Target, CookTarget::DesktopVulkanLE64);
        EXPECT_STREQ(recipe.SchemaKey.Hex().data(), "8474c7b495d387203afdd63caa935cacd9fe250a7385fd5d8b97201225fd9d9a");
        EXPECT_STREQ(recipe.SettingsKey.Hex().data(), "ca3d163bab055381827226140568f3bef7eaac187cebd76878e0b63e9e442356");
    }

    TEST_F(CookSchemaFingerprintTest, SchemaAndRecipeReserveOneExactHashContextAndPreserveAllOutputsOnRefusal)
    {
        Register(First);
        uint32_t workspace[3] = {};
        for (const bool recipe : {false, true})
        {
            Tests::FixedStateAllocator budget   = {};
            const auto                 compute  = [&](CookRecipeKeys* output) { return recipe ? ComputeCookRecipeKeys(budget, First, CookTarget::DesktopVulkanLE64, workspace, output) : ComputeCookSchemaKey(budget, First, workspace, &output->SchemaKey); };
            CookRecipeKeys             expected = {};
            ASSERT_TRUE(compute(&expected).Succeeded());
            const auto fixed = budget.Bytes;
            EXPECT_EQ(budget.ChargeCalls, 1u);
            EXPECT_EQ(budget.Category, CookMemoryCategory::CatalogGraph);
            EXPECT_GE(fixed, CookFingerprint::FixedWorkingBytes() + sizeof(Detail::CookSortState<uint32_t>));
            EXPECT_EQ(budget.Statistics().PeakBytes, fixed);
            EXPECT_EQ(budget.Statistics().LiveBytes, 0u);
            const auto offset = Memory.MainArena.m_current_offset;
            for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
            {
                budget.Failure        = error;
                CookRecipeKeys output = {.Revision = 99, .Target = CookTarget::Invalid, .SchemaKey = Pattern(1), .SettingsKey = Pattern(2)};
                const auto     calls  = budget.ChargeCalls;
                EXPECT_EQ(compute(&output).Error(), error);
                EXPECT_EQ(budget.ChargeCalls, calls + 1);
                EXPECT_EQ(output.Revision, 99u);
                EXPECT_EQ(output.Target, CookTarget::Invalid);
                EXPECT_EQ(output.SchemaKey, Pattern(1));
                EXPECT_EQ(output.SettingsKey, Pattern(2));
                EXPECT_EQ(budget.Statistics().LiveBytes, 0u);
            }
            budget.Failure = VFS::VFSError::OK;
            for (const auto available : {fixed - 1, fixed})
            {
                ASSERT_TRUE(budget.Ledger.Initialize(available).Succeeded());
                CookRecipeKeys output = {.SchemaKey = Pattern(1)};
                const auto     result = compute(&output);
                EXPECT_EQ(result.Succeeded(), available == fixed);
                EXPECT_EQ(output.SchemaKey, available == fixed ? expected.SchemaKey : Pattern(1));
                if (result.Failed())
                {
                    EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
                }
                EXPECT_EQ(budget.Statistics().LiveBytes, 0u);
            }
            EXPECT_EQ(Memory.MainArena.m_current_offset, offset);
            EXPECT_EQ(budget.Allocations, 0u);
        }
    }

    TEST_F(CookSchemaFingerprintTest, InvalidSchemaAndRecipeSetupDoesNotQueryTheBudget)
    {
        Register(First);
        Tests::FixedStateAllocator budget = {};
        budget.Failure                    = VFS::VFSError::Unsupported;
        Digest256      output             = {};
        CookRecipeKeys recipe             = {};
        uint32_t       workspace[3]       = {};
        EXPECT_EQ(ComputeCookSchemaKey(budget, First, workspace, nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(ComputeCookSchemaKey(budget, First, {nullptr, 3}, &output).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(ComputeCookSchemaKey(budget, First, {}, &output).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(ComputeCookRecipeKeys(budget, First, CookTarget::Invalid, workspace, &recipe).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(ComputeCookRecipeKeys(budget, First, CookTarget::DesktopVulkanLE64, {}, &recipe).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(budget.ChargeCalls, 0u);
        EXPECT_EQ(budget.Allocations, 0u);
    }

    TEST_F(CookSchemaFingerprintTest, InitializedEmptySchemaSetRequiresNoSortingStorage)
    {
        EXPECT_EQ(CookSchemaKeyWorkspaceWords(First).Value(), 0u);
        Digest256 output = {};
        ASSERT_TRUE(ComputeCookSchemaKey(Allocator, First, {}, &output).Succeeded());
        CookRecipeKeys recipe = {};
        ASSERT_TRUE(ComputeCookRecipeKeys(Allocator, First, CookTarget::DesktopVulkanLE64, {}, &recipe).Succeeded());
        EXPECT_EQ(recipe.SchemaKey, output);
    }

    class CookRecipeFingerprintTest : public testing::Test
    {
    protected:
        Tests::FixedAllocator<ZKilo(256), 1>             Parent    = {};
        CookScratchAllocator                             Allocator = {};
        CookDependencyGraph                              Graph     = {};
        CookLimits                                       Limits    = {.MaxWorkingBytes = ZKilo(256)};
        CookRecipeKeys                                   Recipe    = {.Target = CookTarget::DesktopVulkanLE64, .SchemaKey = Pattern(0x77)};
        Core::Containers::FixedArray<CookAssetResult, 4> Results   = {};
        Core::Containers::FixedArray<uint32_t, 8>        Workspace = {};
        uint32_t                                         MainScene = CookGraphInvalidIndex;
        uint32_t                                         SkyScene  = CookGraphInvalidIndex;

        void                                             SetUp() override
        {
            ASSERT_TRUE(Parent.Ledger.Initialize(ZKilo(512)).Succeeded());
            ASSERT_TRUE(Allocator.Initialize(Parent, ZKilo(256), CookMemoryCategory::CatalogGraph).Succeeded());
            ASSERT_TRUE(ComputeCookSettingsKey(Recipe.Target, &Recipe.SettingsKey).Succeeded());
            Build();
        }
        void TearDown() override
        {
            Graph.Reset();
            EXPECT_EQ(Allocator.OutstandingAllocations(), 0u);
            Allocator.Release();
            EXPECT_EQ(Parent.Allocations, 1u);
            EXPECT_EQ(Parent.Frees, 1u);
        }
        void Inspect(uint32_t id, cstring format, Core::Containers::ArrayView<const CookDependency> edges = {}, bool changed_local = false)
        {
            CookAssetInspection inspection = {};
            ASSERT_TRUE(inspection.Assign(format, Pattern(static_cast<uint8_t>(0x10 + id + (changed_local ? 1 : 0))), Pattern(static_cast<uint8_t>(0x20 + id)), edges, Allocator).Succeeded());
            ASSERT_TRUE(Graph.SetInspection(Graph.Find(RecipeIdentity(id)), 1, inspection).Succeeded());
        }
        void Build(bool reverse = false, bool moved = false, bool changed_local = false, bool jpeg = false, bool one_slot = false)
        {
            Graph.Reset();
            Results = decltype(Results)();
            ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
            const auto add_scene = [&](bool sky) {
                const auto path = VFS::VFSRelativePath::Parse(sky ? (moved ? "scenes/moved-sky.yaml" : "scenes/sky.yaml") : (moved ? "scenes/moved-main.yaml" : "scenes/main.yaml"));
                return Graph.AddScene(RecipeIdentity(sky ? 101 : 100).UUID, path.Value());
            };
            if (reverse)
            {
                SkyScene  = add_scene(true).Value();
                MainScene = add_scene(false).Value();
            }
            else
            {
                MainScene = add_scene(false).Value();
                SkyScene  = add_scene(true).Value();
            }
            const auto root = [&](uint32_t scene, uint32_t id) { return Graph.AddRoot(scene, RecipeIdentity(id), {}, "Test", "asset"); };
            if (reverse)
            {
                ASSERT_TRUE(root(SkyScene, 4).Succeeded());
            }
            ASSERT_TRUE(root(MainScene, 3).Succeeded());
            if (!reverse)
            {
                ASSERT_TRUE(root(SkyScene, 4).Succeeded());
            }
            if (moved)
            {
                ASSERT_TRUE(root(MainScene, 3).Succeeded()); // Occurrence provenance/dedup is not semantic.
            }
            ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
            const CookDependency mesh_edge = {RecipeIdentity(2), "submeshes/0/material", moved ? "assets/moved.zematerial" : "assets/material.zematerial"};
            Inspect(3, "zemesh-v1-le64", {&mesh_edge, 1});
            const CookDependency material_edges[] = {
                {RecipeIdentity(1), "albedo", moved ? "assets/moved.png" : "assets/a.png"},
                {RecipeIdentity(1), "normal", moved ? "assets/moved.png" : "assets/a.png"}
            };
            Inspect(2, "zematerial-v1-json", {material_edges, one_slot ? 1u : 2u});
            Inspect(1, jpeg ? "image-jpeg" : "image-png", {}, changed_local);
            Inspect(4, "zenvmap-v2-le64");
            ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
            FillResults();
        }
        void FillResults()
        {
            for (uint32_t index : Graph.LeafFirstNodes())
            {
                auto& result = Results[index];
                ASSERT_TRUE(ComputeCookAssetInputKey(Allocator, Graph, index, Recipe, Results, &result.InputKey).Succeeded());
                if (result.Object.Size)
                {
                    continue;
                }
                const auto& node = *Graph.Node(index);
                const auto  id   = static_cast<uint8_t>(node.Identity.UUID.as_bytes()[15]);
                ASSERT_TRUE(MakeCookObjectDescriptor(Pattern(static_cast<uint8_t>(0x30 + id)), 100 + id, FindCookAssetFormat(node.Format)->Extension, &result.Object).Succeeded());
            }
        }
        CookAssetResult& Result(uint32_t id)
        {
            return Results[Graph.Find(RecipeIdentity(id))];
        }
        Digest256 SceneKey(uint32_t scene, Digest256 source = Pattern(0x88))
        {
            Digest256 key = {};
            EXPECT_TRUE(ComputeCookSceneInputKey(Allocator, Graph, scene, source, Recipe, Results, Workspace, &key).Succeeded());
            return key;
        }
        Digest256 GraphKey()
        {
            Digest256 key = {};
            EXPECT_TRUE(ComputeCookGraphKey(Allocator, Graph, Results, &key).Succeeded());
            return key;
        }
        Core::Containers::FixedArray<Digest256, 4> Keys()
        {
            Core::Containers::FixedArray<Digest256, 4> keys = {};
            for (uint32_t id = 1; id <= 4; ++id)
            {
                keys[id - 1] = Result(id).InputKey;
            }
            return keys;
        }
    };

    TEST_F(CookRecipeFingerprintTest, AssetGraphAndSceneKeysReserveOneExactHashAndPreserveDigestAndClosureOnRefusal)
    {
        for (uint32_t operation = 0; operation < 3; ++operation)
        {
            SCOPED_TRACE(operation);
            Tests::FixedStateAllocator                  budget   = {};
            Core::Containers::ArrayView<const uint32_t> closure  = {};
            const auto                                  compute  = [&](Digest256* output) { return operation == 0 ? ComputeCookAssetInputKey(budget, Graph, Graph.Find(RecipeIdentity(2)), Recipe, Results, output) : operation == 1 ? ComputeCookGraphKey(budget, Graph, Results, output) : ComputeCookSceneInputKey(budget, Graph, MainScene, Pattern(0x88), Recipe, Results, Workspace, output, &closure); };
            Digest256                                   expected = {};
            ASSERT_TRUE(compute(&expected).Succeeded());
            const auto fixed = budget.Bytes;
            EXPECT_EQ(budget.ChargeCalls, 1u);
            EXPECT_EQ(budget.Category, CookMemoryCategory::CatalogGraph);
            EXPECT_EQ(fixed, CookFingerprint::FixedWorkingBytes() + sizeof(CookMemoryCharge) + (operation == 2 ? sizeof(Digest256) + sizeof(closure) : 0));
            EXPECT_EQ(budget.Statistics().PeakBytes, fixed);
            EXPECT_EQ(budget.Statistics().LiveBytes, 0u);
            for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
            {
                budget.Failure              = error;
                Digest256 output            = Pattern(1);
                closure                     = {Workspace.data(), 1};
                const auto before_workspace = Workspace;
                const auto calls            = budget.ChargeCalls;
                EXPECT_EQ(compute(&output).Error(), error);
                EXPECT_EQ(budget.ChargeCalls, calls + 1);
                EXPECT_EQ(output, Pattern(1));
                EXPECT_EQ(closure.data(), Workspace.data());
                EXPECT_EQ(closure.size(), 1u);
                EXPECT_EQ(Workspace, before_workspace);
                EXPECT_EQ(budget.Statistics().LiveBytes, 0u);
            }
            budget.Failure = VFS::VFSError::OK;
            for (const auto available : {fixed - 1, fixed})
            {
                ASSERT_TRUE(budget.Ledger.Initialize(available).Succeeded());
                Digest256  output = Pattern(1);
                const auto result = compute(&output);
                EXPECT_EQ(result.Succeeded(), available == fixed);
                EXPECT_EQ(output, available == fixed ? expected : Pattern(1));
                if (result.Failed())
                {
                    EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
                }
                EXPECT_EQ(budget.Statistics().LiveBytes, 0u);
            }
            EXPECT_EQ(budget.Allocations, 0u);
        }
    }

    TEST_F(CookRecipeFingerprintTest, AssetGraphAndPerSceneKeysMatchIndependentTypedRubyGoldens)
    {
        // Independent Ruby reference used explicit [n].pack("V"/"Q<"), RFC
        // UUID bytes, repeated-byte digests, and canonical contract field order.
        EXPECT_STREQ(Result(1).InputKey.Hex().data(), "ad1a0d041b5b5fa18faa5b1d607dd13af569dcb10d5563a22da3ea5302eddda0");
        EXPECT_STREQ(Result(2).InputKey.Hex().data(), "2d2ca6f3c3b5ec32a60d6a6687f917eb3afb5df1ac44363bf9af52de5b13e2e5");
        EXPECT_STREQ(Result(3).InputKey.Hex().data(), "68cc3a04caf9572d5dc8b1cf55865f721b0421f70ad38a64a30a92104befea75");
        EXPECT_STREQ(Result(4).InputKey.Hex().data(), "8f181cd02064d21fd1aa0dbec2061da8f52c59bcc0c3b20979afbf36f857cc38");
        EXPECT_STREQ(GraphKey().Hex().data(), "4e94c730433a1fab23dce17b9138ac87a79ae46c6fe7311c91e33fc7bb101110");
        EXPECT_STREQ(SceneKey(MainScene).Hex().data(), "6739b126df69b27573b81875da75a9fdc00db5f9677fcfe2aefac8bb90cc88f3");
        EXPECT_STREQ(SceneKey(SkyScene).Hex().data(), "0398db688a8ee4a6039824ca1be7ef4b85dbecc7014ebf06bf13f4d2fb8faaa1");
    }

    TEST_F(CookRecipeFingerprintTest, OrderLocationsDuplicateOccurrencesAndExecutionBudgetsDoNotChangeKeys)
    {
        const auto keys         = Keys();
        const auto graph        = GraphKey();
        const auto main         = SceneKey(MainScene);
        const auto sky          = SceneKey(SkyScene);
        Limits.MaxAssets        = 4;
        Limits.MaxScenes        = 2;
        Limits.MaxEdges         = 3;
        Limits.MaxWorkingBytes *= 2;
        Build(true, true);
        EXPECT_EQ(Keys(), keys);
        EXPECT_EQ(GraphKey(), graph);
        EXPECT_EQ(SceneKey(MainScene), main);
        EXPECT_EQ(SceneKey(SkyScene), sky);
        const auto live         = Allocator.OutstandingAllocations();
        const auto parent_calls = Parent.Allocations;
        FillResults();
        EXPECT_EQ(GraphKey(), graph);
        EXPECT_EQ(SceneKey(MainScene), main);
        EXPECT_EQ(Allocator.OutstandingAllocations(), live);
        EXPECT_EQ(Parent.Allocations, parent_calls);
    }

    TEST_F(CookRecipeFingerprintTest, TextureContentInvalidatesItsAncestorsButNotAnUnrelatedScenesClosure)
    {
        const auto keys  = Keys();
        const auto graph = GraphKey();
        const auto main  = SceneKey(MainScene);
        const auto sky   = SceneKey(SkyScene);
        Build(false, false, true);
        for (uint32_t id : {1u, 2u, 3u})
        {
            EXPECT_NE(Result(id).InputKey, keys[id - 1]);
        }
        EXPECT_EQ(Result(4).InputKey, keys[3]);
        EXPECT_NE(GraphKey(), graph);
        EXPECT_NE(SceneKey(MainScene), main);
        EXPECT_EQ(SceneKey(SkyScene), sky);
    }

    TEST_F(CookRecipeFingerprintTest, ChildInputDigestSizeAndFormatAndEachDistinctSlotParticipateInParentKeys)
    {
        const auto original      = Result(2).InputKey;
        const auto texture       = Result(1);
        auto&      child         = Result(1);
        Digest256  changed       = {};
        child.InputKey.Bytes[0] ^= 1;
        ASSERT_TRUE(ComputeCookAssetInputKey(Allocator, Graph, Graph.Find(RecipeIdentity(2)), Recipe, Results, &changed).Succeeded());
        EXPECT_NE(changed, original);
        child = texture;
        ++child.Object.Size;
        ASSERT_TRUE(ComputeCookAssetInputKey(Allocator, Graph, Graph.Find(RecipeIdentity(2)), Recipe, Results, &changed).Succeeded());
        EXPECT_NE(changed, original);
        child = texture;
        ASSERT_TRUE(MakeCookObjectDescriptor(Pattern(0x70), texture.Object.Size, "png", &child.Object).Succeeded());
        ASSERT_TRUE(ComputeCookAssetInputKey(Allocator, Graph, Graph.Find(RecipeIdentity(2)), Recipe, Results, &changed).Succeeded());
        EXPECT_NE(changed, original);
        Build(false, false, false, true);
        EXPECT_NE(Result(2).InputKey, original);
        Build(false, false, false, false, true);
        EXPECT_NE(Result(2).InputKey, original); // normal remains a separate slot even with the same child.
    }

    TEST_F(CookRecipeFingerprintTest, SchemaChangesOnlySceneKeysWhileRecipeAndSettingsInvalidateAssetsToo)
    {
        const auto keys            = Keys();
        const auto scene           = SceneKey(MainScene);
        Recipe.SchemaKey.Bytes[0] ^= 1;
        FillResults();
        EXPECT_EQ(Keys(), keys);
        EXPECT_NE(SceneKey(MainScene), scene);
        Recipe.SchemaKey.Bytes[0] ^= 1;
        ++Recipe.Revision;
        FillResults();
        for (uint32_t id = 1; id <= 4; ++id)
        {
            EXPECT_NE(Result(id).InputKey, keys[id - 1]);
        }
        EXPECT_NE(SceneKey(MainScene), scene);
        --Recipe.Revision;
        Recipe.SettingsKey.Bytes[0] ^= 1;
        FillResults();
        for (uint32_t id = 1; id <= 4; ++id)
        {
            EXPECT_NE(Result(id).InputKey, keys[id - 1]);
        }
        EXPECT_NE(SceneKey(MainScene), scene);
    }

    TEST_F(CookRecipeFingerprintTest, ExactSourceDigestParticipatesWithoutChangingAssetKeys)
    {
        const auto keys = Keys();
        EXPECT_NE(SceneKey(MainScene), SceneKey(MainScene, Pattern(0x89)));
        EXPECT_EQ(Keys(), keys);
    }

    TEST_F(CookRecipeFingerprintTest, MissingOrCorruptChildOutputsAreNotHashableAndFailurePreservesDestination)
    {
        const auto index  = Graph.Find(RecipeIdentity(2));
        const auto child  = Result(1);
        Digest256  output = Pattern(1);
        Result(1).Object  = {};
        EXPECT_EQ(ComputeCookAssetInputKey(Allocator, Graph, index, Recipe, Results, &output).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(output, Pattern(1));
        EXPECT_EQ(ComputeCookGraphKey(Allocator, Graph, Results, &output).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(ComputeCookSceneInputKey(Allocator, Graph, MainScene, {}, Recipe, Results, Workspace, &output).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(output, Pattern(1));
        Result(1)                         = child;
        Result(1).Object.Digest.Bytes[0] ^= 1;
        EXPECT_EQ(ComputeCookAssetInputKey(Allocator, Graph, index, Recipe, Results, &output).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(ComputeCookGraphKey(Allocator, Graph, Results, &output).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(output, Pattern(1));
        Result(1)        = child;
        Result(4).Object = {};
        EXPECT_EQ(ComputeCookGraphKey(Allocator, Graph, Results, &output).Error(), VFS::VFSError::NotFound);
        EXPECT_TRUE(ComputeCookSceneInputKey(Allocator, Graph, MainScene, {}, Recipe, Results, Workspace, &output).Succeeded());
    }

    TEST_F(CookRecipeFingerprintTest, OptionalSceneClosureBorrowsTheExistingWorkspaceAndFailurePreservesTheView)
    {
        const uint32_t                              sentinel = 123;
        Core::Containers::ArrayView<const uint32_t> closure  = {&sentinel, 1};
        Digest256                                   output   = Pattern(1);
        const auto                                  child    = Result(1);
        Result(1).Object                                     = {};
        EXPECT_EQ(ComputeCookSceneInputKey(Allocator, Graph, MainScene, {}, Recipe, Results, Workspace, &output, &closure).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(output, Pattern(1));
        EXPECT_EQ(closure.data(), &sentinel);
        EXPECT_EQ(closure.size(), 1u);
        Result(1) = child;
        ASSERT_TRUE(ComputeCookSceneInputKey(Allocator, Graph, MainScene, {}, Recipe, Results, Workspace, &output, &closure).Succeeded());
        ASSERT_EQ(closure.size(), 3u);
        EXPECT_GE(closure.data(), Workspace.data());
        EXPECT_LT(closure.data(), Workspace.data() + Workspace.size());
        EXPECT_EQ(Graph.Node(closure[0])->Identity, RecipeIdentity(2));
        EXPECT_EQ(Graph.Node(closure[1])->Identity, RecipeIdentity(3));
        EXPECT_EQ(Graph.Node(closure[2])->Identity, RecipeIdentity(1));
    }

    TEST_F(CookRecipeFingerprintTest, InvalidGraphRecipeTableAndClosureWorkspaceNeverExposePartialKeys)
    {
        Digest256  output = Pattern(1);
        const auto index  = Graph.Find(RecipeIdentity(2));
        EXPECT_EQ(ComputeCookAssetInputKey(Allocator, Graph, CookGraphInvalidIndex, Recipe, Results, &output).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(ComputeCookAssetInputKey(Allocator, Graph, index, Recipe, {Results.data(), 3}, &output).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(ComputeCookGraphKey(Allocator, Graph, {nullptr, 4}, &output).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(ComputeCookSceneInputKey(Allocator, Graph, MainScene, {}, Recipe, Results, {Workspace.data(), 7}, &output).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(ComputeCookSceneInputKey(Allocator, Graph, MainScene, {}, Recipe, Results, {nullptr, 8}, &output).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(ComputeCookSceneInputKey(Allocator, Graph, 2, {}, Recipe, Results, Workspace, &output).Error(), VFS::VFSError::InvalidPath);
        Recipe.Revision = 0;
        EXPECT_EQ(ComputeCookAssetInputKey(Allocator, Graph, index, Recipe, Results, &output).Error(), VFS::VFSError::InvalidPath);
        Recipe.Revision = 1;
        Recipe.Target   = CookTarget::Invalid;
        EXPECT_EQ(ComputeCookAssetInputKey(Allocator, Graph, index, Recipe, Results, &output).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(ComputeCookSceneInputKey(Allocator, Graph, MainScene, {}, Recipe, Results, Workspace, &output).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(output, Pattern(1));
        Graph.Reset();
        EXPECT_EQ(ComputeCookGraphKey(Allocator, Graph, {}, &output).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(output, Pattern(1));
    }

    TEST_F(CookRecipeFingerprintTest, SceneWithoutAssetsUsesTheEmptyGraphDomainAndNeedsNoClosureAllocation)
    {
        Graph.Reset();
        ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
        const auto scene = Graph.AddScene(RecipeIdentity(100).UUID, VFS::VFSRelativePath::Parse("scenes/empty.yaml").Value());
        ASSERT_TRUE(scene.Succeeded());
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
        Digest256 graph = {}, output = {};
        ASSERT_TRUE(ComputeCookGraphKey(Allocator, Graph, {}, &graph).Succeeded());
        ASSERT_TRUE(ComputeCookSceneInputKey(Allocator, Graph, scene.Value(), Pattern(0x88), Recipe, {}, {}, &output).Succeeded());
        EXPECT_NE(graph, output);
    }
} // namespace ZEngine::Cook
