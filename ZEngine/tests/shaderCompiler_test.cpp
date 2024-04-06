#include <Rendering/Shaders/Compilers/ShaderCompiler.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <iostream>

using namespace ZEngine::Rendering::Shaders::Compilers;
namespace fs = std::filesystem;

std::filesystem::path directoryPath("Shaders");

void copyFiles()
{
    std::filesystem::path rootPath = std::filesystem::current_path().parent_path().parent_path().parent_path();

    std::filesystem::path sourcePath = rootPath / "Resources/Shaders";
    std::filesystem::create_directories(directoryPath);
    for (const auto& entry : std::filesystem::directory_iterator(sourcePath))
    {
        if (entry.is_regular_file())
        {
            std::filesystem::path targetFile = directoryPath / entry.path().filename();
            std::filesystem::copy_file(entry.path(), targetFile, std::filesystem::copy_options::overwrite_existing);
        }
    }
}
void compileShaderFiles()
{
    ASSERT_TRUE(fs::exists(directoryPath) && fs::is_directory(directoryPath));

    for (const auto& entry : fs::directory_iterator(directoryPath))
    {
        if (entry.is_regular_file() && (entry.path().extension() == ".vert" || entry.path().extension() == ".frag"))
        {
            ShaderCompiler shaderCompiler(entry.path().string());
            shaderCompiler.CompileAsync();
        }
    }
}

TEST(ShaderCompilerTest, DefaultConstructorCreatesOutputDirectory)
{
    copyFiles();
    std::filesystem::path outputPath("Shaders/Cache");

    compileShaderFiles();

    ASSERT_TRUE(fs::exists(outputPath) && fs::is_directory(outputPath));

    unsigned int fileCount = 0;
    for (const auto& entry : fs::directory_iterator(outputPath))
    {
        if (entry.is_regular_file())
        {
            ++fileCount;
        }
    }
    EXPECT_EQ(fileCount, 11);
}
