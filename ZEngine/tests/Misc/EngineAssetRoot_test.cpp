#include <ZEngine/Core/EngineAssetRoot.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <filesystem>

namespace
{
    class EngineAssetRootTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            static std::atomic<uint32_t> next_id = 0;
            const auto                   now     = std::chrono::steady_clock::now().time_since_epoch().count();
            m_root                               = std::filesystem::temp_directory_path() / ("zengine_engine_asset_root_" + std::to_string(now) + "_" + std::to_string(next_id.fetch_add(1, std::memory_order_relaxed)));
            std::error_code error;
            std::filesystem::remove_all(m_root, error);
            std::filesystem::create_directories(m_root, error);
            ASSERT_FALSE(error);
        }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove_all(m_root, error);
        }

        std::filesystem::path m_root;
    };
} // namespace

TEST_F(EngineAssetRootTest, ResolvesPackageBesideTheExecutable)
{
    const std::filesystem::path executable = m_root / "bin" / "Obelisk";

    const auto                  result     = ZEngine::Core::ResolveEngineAssetRoot(executable);

    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Root, m_root / "bin" / "ZodiacEngine");
}

TEST_F(EngineAssetRootTest, ExplicitOverrideTakesPrecedence)
{
    const std::filesystem::path executable = m_root / "bin" / "Obelisk";
    const std::filesystem::path override   = m_root / "tooling-assets";

    const auto                  result     = ZEngine::Core::ResolveEngineAssetRoot(executable, override);

    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Root, override);
}

TEST_F(EngineAssetRootTest, RootValidationRejectsMissingPackageDirectory)
{
    const auto result = ZEngine::Core::ValidateEngineAssetRoot(m_root / "missing");

    EXPECT_FALSE(result.Succeeded());
    EXPECT_NE(result.Diagnostic.find("missing"), std::string::npos);
}

TEST_F(EngineAssetRootTest, RootValidationRejectsEmptyPath)
{
    const auto result = ZEngine::Core::ValidateEngineAssetRoot({});

    EXPECT_FALSE(result.Succeeded());
    EXPECT_NE(result.Diagnostic.find("empty"), std::string::npos);
}

TEST_F(EngineAssetRootTest, RootValidationAcceptsPackageDirectory)
{
    const std::filesystem::path package_root = m_root / "ZodiacEngine";
    std::filesystem::create_directories(package_root);

    const auto result = ZEngine::Core::ValidateEngineAssetRoot(package_root);

    EXPECT_TRUE(result.Succeeded()) << result.Diagnostic;
}
