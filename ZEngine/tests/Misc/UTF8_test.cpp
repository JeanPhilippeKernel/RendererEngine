#include <ZEngine/Helpers/UTF8.h>
#include <gtest/gtest.h>

namespace
{
    TEST(UTF8Test, HandlesEmptyAndLengthDelimitedInputs)
    {
        EXPECT_TRUE(ZEngine::Helpers::IsValidUTF8(nullptr, 0));
        EXPECT_FALSE(ZEngine::Helpers::IsValidUTF8(nullptr, 1));
        const char raw[] = {'a', 'b', '\0'};
        EXPECT_TRUE(ZEngine::Helpers::IsValidUTF8(raw, 2));
        EXPECT_FALSE(ZEngine::Helpers::IsValidUTF8(raw, 3));
    }

    TEST(UTF8Test, AllowsTextControlsButCanRejectPathControls)
    {
        constexpr cstring controls[] = {"\n", "\t", "\x7f", "\xc2\x80", "\xc2\x9f"};
        for (cstring control : controls)
        {
            const size_t length = control[0] == '\xc2' ? 2 : 1;
            EXPECT_TRUE(ZEngine::Helpers::IsValidUTF8(control, length));
            EXPECT_FALSE(ZEngine::Helpers::IsValidUTF8(control, length, false));
        }
        EXPECT_TRUE(ZEngine::Helpers::IsValidUTF8("\xc2\xa0", 2, false));
    }

    TEST(UTF8Test, ValidatesUnicodeScalarBoundaries)
    {
        struct Case
        {
            cstring Text  = nullptr;
            size_t  Size  = 0;
            bool    Valid = false;
        };
        constexpr Case cases[] = {
            {        "\xc2\x80", 2,  true},
            {        "\xdf\xbf", 2,  true},
            {    "\xe0\xa0\x80", 3,  true},
            {    "\xed\x9f\xbf", 3,  true},
            {    "\xee\x80\x80", 3,  true},
            {"\xf0\x90\x80\x80", 4,  true},
            {"\xf4\x8f\xbf\xbf", 4,  true},
            {        "\xc0\x80", 2, false},
            {    "\xe0\x9f\xbf", 3, false},
            {    "\xed\xa0\x80", 3, false},
            {"\xf0\x8f\xbf\xbf", 4, false},
            {"\xf4\x90\x80\x80", 4, false},
            {    "\xf0\x90\x80", 3, false},
            {            "\x80", 1, false}
        };
        for (const auto& item : cases)
        {
            EXPECT_EQ(ZEngine::Helpers::IsValidUTF8(item.Text, item.Size), item.Valid);
        }
    }
} // namespace
