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
    // hdr/exr stay EnvironmentMapImporter's domain; ktx/ktx2 are recognized by
    // AssetRegistry::InferTypeFromExtension but stb_image cannot decode them — neither
    // should be claimed here, or ImportCoordinator's first-match routing gets ambiguous.
    const char*     unclaimed[] = {"hdr", "exr", "ktx", "ktx2", "zenvmap", "glb", "fbx", "obj"};
    for (const char* ext : unclaimed)
        EXPECT_FALSE(importer.CanImport(ext)) << ext;
}

TEST(TextureImporterTest, CanImportRejectsNull)
{
    TextureImporter importer;
    EXPECT_FALSE(importer.CanImport(nullptr));
}
