#include <ZEngine/Importers/TextureImporter.h>
#include <gtest/gtest.h>

using namespace ZEngine::Importers;

// CanImport is a pure function on the extension string — no Initialize()/Arena needed.

TEST(TextureImporterTest, CanImportClaimsAllEightRasterExtensions)
{
    TextureImporter importer;
    const char*     claimed[] = {"png", "jpg", "jpeg", "bmp", "tga", "gif", "psd", "pic"};
    for (const char* ext : claimed)
        EXPECT_TRUE(importer.CanImport(ext)) << ext;
}

TEST(TextureImporterTest, CanImportDoesNotClaimEnvironmentMapOrContainerFormats)
{
    TextureImporter importer;
    // HDR and EXR are claimed by EnvironmentMapImporter; KTX variants remain
    // recognized by AssetRegistry::InferTypeFromExtension but unsupported until
    // their dedicated decoder is introduced. None belong to this importer.
    const char*     unclaimed[] = {"hdr", "exr", "ktx", "ktx2", "zenvmap", "glb", "fbx", "obj"};
    for (const char* ext : unclaimed)
        EXPECT_FALSE(importer.CanImport(ext)) << ext;
}

TEST(TextureImporterTest, CanImportRejectsNull)
{
    TextureImporter importer;
    EXPECT_FALSE(importer.CanImport(nullptr));
}
