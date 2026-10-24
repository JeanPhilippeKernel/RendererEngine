#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Importers/AssetTypes.h>
#include <gtest/gtest.h>
#include <array>
#include <string>

using ZEngine::Core::Containers::String;
using ZEngine::Core::Memory::ArenaAllocator;
using ZEngine::Importers::AssetMaterial;

TEST(AssetMaterialLifetimeTest, CloneOwnsEveryStringAfterSourceScratchIsReused)
{
    ZEngine::Core::Memory::MemoryManager memory;
    memory.Initialize(ZMega(2), {});
    ArenaAllocator scratch;
    ArenaAllocator persistent;
    memory.MainArena.CreateSubArena(ZMega(1), &scratch);
    memory.MainArena.CreateSubArena(ZKilo(256), &persistent);

    AssetMaterial                source;
    const std::array<String*, 6> source_strings = {&source.Name, &source.AlbedoTexPath, &source.EmissiveTexPath, &source.NormalTexPath, &source.OpacityTexPath, &source.SpecularTexPath};
    std::array<std::string, 6>   expected;
    for (size_t i = 0; i < expected.size(); ++i)
    {
        expected[i] = "/textures/" + std::string(80, static_cast<char>('a' + i)) + ".png";
        source_strings[i]->init(&scratch, expected[i].c_str());
    }
    source.MaterialUUID                               = uuids::uuid::from_string("550e8400-e29b-41d4-a716-446655440000").value();
    source.AlbedoTexUUID                              = source.MaterialUUID;
    source.NormalTexUUID                              = source.MaterialUUID;
    source.AlbedoColor[2]                             = 0.75f;
    source.Factors[0]                                 = 0.5f;

    auto                               copy           = source.Clone(&persistent);
    const std::array<const String*, 6> copied_strings = {&copy.Name, &copy.AlbedoTexPath, &copy.EmissiveTexPath, &copy.NormalTexPath, &copy.OpacityTexPath, &copy.SpecularTexPath};
    for (size_t i = 0; i < copied_strings.size(); ++i)
        EXPECT_NE(copied_strings[i]->c_str(), source_strings[i]->c_str());
    scratch.Clear();
    ASSERT_NE(scratch.Allocate(ZKilo(4)), nullptr); // allocation zeroes and overwrites the old strings
    for (size_t i = 0; i < copied_strings.size(); ++i)
        EXPECT_STREQ(copied_strings[i]->c_str(), expected[i].c_str());
    EXPECT_EQ(copy.MaterialUUID, source.MaterialUUID);
    EXPECT_EQ(copy.AlbedoTexUUID, source.AlbedoTexUUID);
    EXPECT_EQ(copy.NormalTexUUID, source.NormalTexUUID);
    EXPECT_FLOAT_EQ(copy.AlbedoColor[2], 0.75f);
    EXPECT_FLOAT_EQ(copy.Factors[0], 0.5f);
}

TEST(AssetMaterialLifetimeTest, CloneSupportsUninitializedOptionalStrings)
{
    ZEngine::Core::Memory::MemoryManager memory;
    memory.Initialize(ZKilo(64), {});
    const AssetMaterial source;
    const auto          copy = source.Clone(&memory.MainArena);
    EXPECT_TRUE(copy.Name.empty());
    EXPECT_TRUE(copy.AlbedoTexPath.empty());
    EXPECT_TRUE(copy.EmissiveTexPath.empty());
    EXPECT_TRUE(copy.NormalTexPath.empty());
    EXPECT_TRUE(copy.OpacityTexPath.empty());
    EXPECT_TRUE(copy.SpecularTexPath.empty());
    EXPECT_TRUE(copy.MaterialUUID.is_nil());
}
