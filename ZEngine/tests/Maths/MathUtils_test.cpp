#include <ZEngine/Core/Maths/MathUtils.h>
#include <gtest/gtest.h>
#include <cmath>

using namespace ZEngine::Core::Maths;

TEST(MathUtilsTest, TrigonometricFunctionsAreContinuousAcrossZero)
{
    constexpr float epsilon = 0.001f;

    EXPECT_NEAR(ZEngine::Core::Maths::sin(-epsilon), std::sin(-epsilon), 1e-6f);
    EXPECT_NEAR(ZEngine::Core::Maths::sin(epsilon), std::sin(epsilon), 1e-6f);
    EXPECT_NEAR(ZEngine::Core::Maths::cos(-epsilon), std::cos(-epsilon), 1e-6f);
    EXPECT_NEAR(ZEngine::Core::Maths::cos(epsilon), std::cos(epsilon), 1e-6f);
}

TEST(MathUtilsTest, SineHasTheCorrectSignInEveryQuadrant)
{
    constexpr float eighth_turn = PI<float> * 0.25f;

    EXPECT_GT(ZEngine::Core::Maths::sin(eighth_turn), 0.0f);
    EXPECT_GT(ZEngine::Core::Maths::sin(PI<float> - eighth_turn), 0.0f);
    EXPECT_LT(ZEngine::Core::Maths::sin(-eighth_turn), 0.0f);
    EXPECT_LT(ZEngine::Core::Maths::sin(-PI<float> + eighth_turn), 0.0f);
}

TEST(MathUtilsTest, TrigonometricFunctionsRemainContinuousAcrossFullTurns)
{
    constexpr float epsilon            = 0.001f;
    const float     around_full_turn[] = {TWO_PI<float> - epsilon, TWO_PI<float> + epsilon, -TWO_PI<float> - epsilon, -TWO_PI<float> + epsilon};

    for (const float angle : around_full_turn)
    {
        EXPECT_NEAR(ZEngine::Core::Maths::sin(angle), std::sin(angle), 1e-5f);
        EXPECT_NEAR(ZEngine::Core::Maths::cos(angle), std::cos(angle), 1e-5f);
    }
}
