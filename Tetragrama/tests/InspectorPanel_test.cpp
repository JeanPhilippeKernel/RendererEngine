#include <Tetragrama/Panels/InspectorPanel.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>
#include <cstdio>
#include <limits>
#include <string>

using namespace ZEngine::UI;
using namespace ZEngine::ECS;
using ZEngine::ECS::Components::LightComponent;
using ZEngine::Helpers::secure_strlen;

namespace Tetragrama::Panels
{
    struct InspectorPanelTestAccess
    {
        static bool Draw(ZUIContext* ctx, const FieldDescriptor& field, void* component, float width, uint32_t field_index = 0)
        {
            return InspectorPanel::DrawField(ctx, field, component, width, 0, field_index);
        }
    };
} // namespace Tetragrama::Panels

class InspectorPanelTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory     = {};
    ZUIContext                           ctx        = {};
    ZUIGlyph                             glyphs[96] = {};
    ZUIFont                              font       = {};
    ZUIFontAtlas                         atlas      = {};
    LightComponent                       light      = {};
    FieldDescriptor                      color      = {.Name = "Color", .Type = FieldType::Color3f, .Offset = offsetof(LightComponent, Color), .Size = sizeof(LightComponent::Color)};

    void                                 SetUp() override
    {
        memory.Initialize(ZMega(8), {});
        ZUIContextInit(&ctx, &memory.MainArena, ZMega(4), ZMega(1), 1024, 1024);
        ctx.ScreenW     = 800;
        ctx.ScreenH     = 900;
        ctx.MousePos[0] = ctx.MousePos[1] = -100.f;
        for (auto& glyph : glyphs)
            glyph.AdvanceX = 5.5f;
        font.Glyphs     = glyphs;
        font.GlyphCount = 96;
        font.LineHeight = 13.f;
        atlas.Body = atlas.Small = atlas.Large = atlas.Header = atlas.Heading = atlas.Tiny = &font;
        ctx.Atlas                                                                          = &atlas;
    }
    void TearDown() override
    {
        ZUIContextDestroy(&ctx);
    }
    uint64_t Hash(const char* key)
    {
        return ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key)));
    }
    uint64_t ColorKey(const char* part, int index = 0)
    {
        char key[64] = {};
        snprintf(key, sizeof(key), "##color_%016llx_%s%d", static_cast<unsigned long long>(Hash("##fv_0_0")), part, index);
        return Hash(key);
    }
    ZUIBox* Find(uint64_t key, ZUIBox* root = nullptr)
    {
        if (!root)
            root = ctx.Root;
        if (root->Key == key)
            return root;
        for (auto* child = root->FirstChild; child; child = child->NextSib)
            if (auto* found = Find(key, child))
                return found;
        return nullptr;
    }
    bool Frame(const FieldDescriptor& field, float width = 300.f, void* target = nullptr)
    {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(width), ZPx(900.f));
        const bool changed = Tetragrama::Panels::InspectorPanelTestAccess::Draw(&ctx, field, target ? target : &light, width);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
        return changed;
    }
    std::string Text(const ZUIBox* box)
    {
        std::string result = (box->Flags & ZUI_DrawText) && box->Label.Ptr ? std::string(box->Label.Ptr, box->Label.Len) : "";
        for (auto* child = box->FirstChild; child; child = child->NextSib)
            result += Text(child);
        return result;
    }
    void EditRed(const char* value)
    {
        ctx.FocusKey      = ColorKey("number");
        ctx.KeyboardFocus = true;
        Frame(color);
        auto* state = ZUIStateGetOrInsert(&ctx.StateStore, Hash("##fv_0_0"))->ColorState;
        ASSERT_NE(state, nullptr);
        ZEngine::Helpers::secure_strcpy(state->Numbers[0].Buffer, sizeof(state->Numbers[0].Buffer), value);
        ctx.EnterPressed = true;
    }
};

TEST_F(InspectorPanelTest, ReadOnlyLightTypeIsTextWithoutAComboOrReset)
{
    static const EnumValue values[] = {
        {"Directional", 0},
        {      "Point", 1},
        {       "Spot", 2}
    };
    const FieldDescriptor type = {.Name = "LightType", .Type = FieldType::Enum, .Offset = offsetof(LightComponent, LightType), .Size = sizeof(LightComponent::Type), .EnumValues = values, .EnumCount = 3, .ReadOnly = true};
    for (uint8_t i = 0; i < 3; ++i)
    {
        light.LightType = static_cast<LightComponent::Type>(i);
        EXPECT_FALSE(Frame(type));
        EXPECT_EQ(Find(Hash("##fv_0_0")), nullptr);
        const auto* row = Find(Hash("##frow_0_0"));
        ASSERT_NE(row, nullptr);
        ASSERT_NE(row->FirstChild, nullptr);
        EXPECT_EQ(row->FirstChild->NextSib, row->LastChild);
        EXPECT_EQ(Text(ctx.Root), std::string("LightType") + values[i].Name);
        ctx.FocusKey     = Hash("##fv_0_0");
        ctx.EnterPressed = true;
        EXPECT_FALSE(Frame(type));
        EXPECT_EQ(light.LightType, static_cast<LightComponent::Type>(i));
        EXPECT_EQ(ctx.PopupStackSize, 0u);
    }
}

TEST_F(InspectorPanelTest, ColorFieldsShareNarrowPanelWidthWithoutClipping)
{
    for (float width : {180.f, 300.f, 480.f})
    {
        EXPECT_FALSE(Frame(color, width));
        EXPECT_FALSE(Frame(color, width));
        const auto* row = Find(Hash("##frow_0_0"));
        ASSERT_NE(row, nullptr);
        EXPECT_FLOAT_EQ(row->ComputedSize[1], 29.f);
        EXPECT_FLOAT_EQ(row->Padding[0], 10.f);
        EXPECT_FLOAT_EQ(row->Gap, 6.f);
        const auto* edit = Find(ColorKey("edit"));
        ASSERT_NE(edit, nullptr);
        EXPECT_LE(edit->ScreenMax[0], width - 10.f + .001f);
        EXPECT_EQ(Find(ColorKey("number", 3)), nullptr);
        for (int i = 0; i < 3; ++i)
        {
            const auto* number = Find(ColorKey("number", i));
            ASSERT_NE(number, nullptr);
            EXPECT_GT(number->ComputedSize[0], 0.f);
            EXPECT_FLOAT_EQ(number->ComputedSize[0], Find(ColorKey("number"))->ComputedSize[0]);
            EXPECT_LE(number->ScreenMax[0], edit->ScreenMax[0] + .001f);
        }
    }
}

TEST_F(InspectorPanelTest, ColorLabelMatchesRangeAndSpotAngleAlignmentAndTextStyle)
{
    const FieldDescriptor fields[] = {
        {    .Name = "Color", .Type = FieldType::Color3f, .Offset = offsetof(LightComponent,     Color), .Size = sizeof(LightComponent::Color)},
        {    .Name = "Range",   .Type = FieldType::Float, .Offset = offsetof(LightComponent,     Range),                 .Size = sizeof(float)},
        {.Name = "SpotAngle",   .Type = FieldType::Float, .Offset = offsetof(LightComponent, SpotAngle),                 .Size = sizeof(float)},
    };
    ZUIBeginFrame(&ctx, 1.f / 60.f);
    ZUIBeginColumn(&ctx, "root", ZPx(300.f), ZPx(900.f));
    for (uint32_t i = 0; i < 3; ++i)
        EXPECT_FALSE(Tetragrama::Panels::InspectorPanelTestAccess::Draw(&ctx, fields[i], &light, 300.f, i));
    ZUIEndColumn(&ctx);
    ZUIEndFrame(&ctx);

    const auto* color_row = Find(Hash("##frow_0_0"));
    ASSERT_NE(color_row, nullptr);
    const auto* color_label = color_row->FirstChild;
    ASSERT_NE(color_label, nullptr);
    EXPECT_EQ(Text(color_label), "Color");
    EXPECT_EQ(color_label->FontSize, ZUIFontSize::Body);
    EXPECT_EQ(color_label->TextAlign, ZUITextAlign::Left);
    for (int channel = 0; channel < 4; ++channel)
        EXPECT_FLOAT_EQ(color_label->TextColor[channel], ctx.Theme.TextDefault[channel]);

    for (const char* key : {"##frow_0_1", "##frow_0_2"})
    {
        const auto* row = Find(Hash(key));
        ASSERT_NE(row, nullptr);
        const auto* label = row->FirstChild;
        ASSERT_NE(label, nullptr);
        EXPECT_FLOAT_EQ(color_label->ScreenMin[0] + color_label->Padding[0], label->ScreenMin[0] + label->Padding[0]);
        EXPECT_FLOAT_EQ(color_label->ComputedSize[1], label->ComputedSize[1]);
        EXPECT_FLOAT_EQ(color_label->ScreenMin[1] - color_row->ScreenMin[1], label->ScreenMin[1] - row->ScreenMin[1]);
        EXPECT_FLOAT_EQ(color_row->ComputedSize[1], row->ComputedSize[1]);
        EXPECT_EQ(color_label->FontSize, label->FontSize);
        EXPECT_EQ(color_label->TextAlign, label->TextAlign);
        for (int channel = 0; channel < 4; ++channel)
            EXPECT_FLOAT_EQ(color_label->TextColor[channel], label->TextColor[channel]);
    }

    const auto* edit = Find(ColorKey("edit"));
    ASSERT_NE(edit, nullptr);
    EXPECT_FLOAT_EQ((color_label->ScreenMin[1] + color_label->ScreenMax[1]) * .5f, (edit->ScreenMin[1] + edit->ScreenMax[1]) * .5f);
    EXPECT_LE(color_label->ScreenMax[0], edit->ScreenMin[0]);
}

TEST_F(InspectorPanelTest, ColorPreservesExactHdrStorageAndUsesLinearDisplayBoundary)
{
    light.Color[0] = .18f;
    light.Color[1] = 4.5f;
    light.Color[2] = -.25f;
    for (auto& pad : light._pad)
        pad = 0x5a;
    EXPECT_FALSE(Frame(color));
    const auto* red = Find(ColorKey("number"));
    ASSERT_NE(red, nullptr);
    EXPECT_EQ(std::string(red->Label.Ptr, red->Label.Len), "118");
    ZUIOpenPopup(&ctx, "##fv_0_0", 0.f, 22.f);
    EXPECT_FALSE(Frame(color));
    EXPECT_FLOAT_EQ(light.Color[0], .18f);
    EXPECT_FLOAT_EQ(light.Color[1], 4.5f);
    EXPECT_FLOAT_EQ(light.Color[2], -.25f);
    EXPECT_EQ(Find(ColorKey("channelTrack", 3)), nullptr);
    for (auto pad : light._pad)
        EXPECT_EQ(pad, 0x5a);
}

TEST_F(InspectorPanelTest, ColorEditsUseComponentStorageAndSupportUndoRedoWithoutRebinding)
{
    light.Color[0] = .18f;
    light.Color[1] = 4.5f;
    light.Color[2] = -.25f;
    Frame(color);
    EditRed("64");
    EXPECT_TRUE(Frame(color));
    EXPECT_NEAR(light.Color[0], ColorMath::SrgbToLinear(64.0 / 255), 1e-7);
    EXPECT_FLOAT_EQ(light.Color[1], 4.5f);
    EXPECT_FLOAT_EQ(light.Color[2], -.25f);
    EXPECT_EQ(ctx.ColorUndoCount, 1u);
    const auto* number = Find(ColorKey("number"));
    const auto* edit   = Find(ColorKey("edit"));
    ASSERT_NE(number, nullptr);
    ASSERT_NE(edit, nullptr);
    EXPECT_LE(number->ScreenMax[0], edit->ScreenMax[0]);
    ctx.FocusKey      = ColorKey("swatch");
    ctx.KeyboardFocus = true;
    ctx.CtrlZPressed  = true;
    EXPECT_TRUE(Frame(color));
    EXPECT_FLOAT_EQ(light.Color[0], .18f);
    ctx.CtrlYPressed = true;
    EXPECT_TRUE(Frame(color));
    EXPECT_NEAR(light.Color[0], ColorMath::SrgbToLinear(64.0 / 255), 1e-7);
    auto other       = light;
    ctx.CtrlZPressed = true;
    EXPECT_FALSE(Frame(color, 300.f, &other));
    EXPECT_FLOAT_EQ(other.Color[0], light.Color[0]);
}

TEST_F(InspectorPanelTest, ReadOnlyColorRejectsStaleKeyboardInputAndRestoresDisabledState)
{
    Frame(color);
    EditRed("64");
    color.ReadOnly = true;
    EXPECT_FALSE(Frame(color));
    EXPECT_FLOAT_EQ(light.Color[0], 1.f);
    EXPECT_FALSE(ctx.Disabled);
    color.ReadOnly = false;
    ctx.Disabled   = true;
    EXPECT_FALSE(Frame(color));
    EXPECT_TRUE(ctx.Disabled);
}

TEST_F(InspectorPanelTest, IntegerLabelsReadOnlyTheDeclaredWidthAndPreserveUnsignedRange)
{
    const auto check = [&](auto value, FieldType type, const char* expected) {
        using Value = decltype(value);
        struct Property
        {
            Value   ValueField = {};
            uint8_t Guard[8]   = {0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a};
        } property{value};
        const FieldDescriptor field = {.Name = "Number", .Type = type, .Size = sizeof(Value)};
        EXPECT_FALSE(Frame(field, 300.f, &property));
        EXPECT_EQ(Text(ctx.Root), std::string("Number") + expected);
        EXPECT_EQ(property.ValueField, value);
        for (auto byte : property.Guard)
            EXPECT_EQ(byte, 0x5a);
    };
    check(uint8_t{255}, FieldType::UInt8, "255");
    check(uint16_t{65535}, FieldType::UInt16, "65535");
    check(std::numeric_limits<uint32_t>::max(), FieldType::UInt32, "4294967295");
    check(std::numeric_limits<uint64_t>::max(), FieldType::UInt64, "18446744073709551615");
    check(std::numeric_limits<int64_t>::min(), FieldType::Int64, "-9223372036854775808");
}

TEST_F(InspectorPanelTest, StringCapacityComesFromStorageAndCannotOverwriteAdjacentProperties)
{
    struct Property
    {
        char    Value[4] = "abc";
        uint8_t Guard[8] = {0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a};
    };
    for (uint32_t declared_capacity : {0u, 4u, 256u})
    {
        Property              property = {};
        const FieldDescriptor field    = {.Name = "Label", .Type = FieldType::String, .Size = sizeof(Property::Value), .StringCap = declared_capacity};
        ctx.FocusKey                   = Hash("##fv_0_0");
        ZEngine::Helpers::secure_strcpy(ctx.TextInput, sizeof(ctx.TextInput), "OVERFLOW");
        ctx.TextInputLen = 8;
        EXPECT_FALSE(Frame(field, 300.f, &property));
        EXPECT_STREQ(property.Value, "abc");
        for (auto byte : property.Guard)
            EXPECT_EQ(byte, 0x5a);
    }
}

TEST_F(InspectorPanelTest, ReadOnlyFieldsRejectInputAndRestoreCallerStyle)
{
    struct Properties
    {
        bool    Enabled    = true;
        int32_t Count      = 7;
        float   Scalar     = 2.f;
        double  Precise    = 3.5;
        float   Vector2[2] = {1.f, 2.f};
        float   Vector[3]  = {1.f, 2.f, 3.f};
        float   Vector4[4] = {1.f, 2.f, 3.f, 4.f};
        char    Label[8]   = "name";
    } properties                   = {};
    const FieldDescriptor fields[] = {
        {.Name = "Enabled", .Type = FieldType::Bool, .Offset = offsetof(Properties, Enabled), .Size = sizeof(Properties::Enabled), .ReadOnly = true},
        {.Name = "Count", .Type = FieldType::Int32, .Offset = offsetof(Properties, Count), .Size = sizeof(Properties::Count), .ReadOnly = true},
        {.Name = "Scalar", .Type = FieldType::Float, .Offset = offsetof(Properties, Scalar), .Size = sizeof(Properties::Scalar), .ReadOnly = true},
        {.Name = "Precise", .Type = FieldType::Double, .Offset = offsetof(Properties, Precise), .Size = sizeof(Properties::Precise), .ReadOnly = true},
        {.Name = "Vector2", .Type = FieldType::Vec2f, .Offset = offsetof(Properties, Vector2), .Size = sizeof(Properties::Vector2), .ReadOnly = true},
        {.Name = "Vector", .Type = FieldType::Vec3f, .Offset = offsetof(Properties, Vector), .Size = sizeof(Properties::Vector), .ReadOnly = true},
        {.Name = "Vector4", .Type = FieldType::Vec4f, .Offset = offsetof(Properties, Vector4), .Size = sizeof(Properties::Vector4), .ReadOnly = true},
        {.Name = "Label", .Type = FieldType::String, .Offset = offsetof(Properties, Label), .Size = sizeof(Properties::Label), .StringCap = sizeof(Properties::Label), .ReadOnly = true},
    };
    static const char suffix[] = " unit";
    for (const auto& field : fields)
    {
        SCOPED_TRACE(field.Name);
        const char* key    = field.Type == FieldType::Vec2f ? "##v2x_0_0" : field.Type == FieldType::Vec3f ? "##x_frow_0_0" : field.Type == FieldType::Vec4f ? "##v4_0_0_0" : "##fv_0_0";
        ctx.FocusKey       = Hash(key);
        ctx.ArrowUpPressed = ctx.SpacePressed = true;
        ZEngine::Helpers::secure_strcpy(ctx.TextInput, sizeof(ctx.TextInput), "9");
        ctx.TextInputLen      = 1;
        ctx.FlatInputs        = true;
        ctx.DragFloatDecimals = 5;
        ctx.DragFloatSuffix   = suffix;
        EXPECT_FALSE(Frame(field, 300.f, &properties));
        EXPECT_FALSE(ctx.Disabled);
        EXPECT_TRUE(ctx.FlatInputs);
        EXPECT_EQ(ctx.DragFloatDecimals, 5);
        EXPECT_EQ(ctx.DragFloatSuffix, suffix);
        EXPECT_FALSE(ctx.TextInputActive);
        EXPECT_TRUE(properties.Enabled);
        EXPECT_EQ(properties.Count, 7);
        EXPECT_FLOAT_EQ(properties.Scalar, 2.f);
        EXPECT_DOUBLE_EQ(properties.Precise, 3.5);
        for (int i = 0; i < 3; ++i)
            EXPECT_FLOAT_EQ(properties.Vector[i], static_cast<float>(i + 1));
        for (int i = 0; i < 2; ++i)
            EXPECT_FLOAT_EQ(properties.Vector2[i], static_cast<float>(i + 1));
        for (int i = 0; i < 4; ++i)
            EXPECT_FLOAT_EQ(properties.Vector4[i], static_cast<float>(i + 1));
        EXPECT_STREQ(properties.Label, "name");

        ctx.Disabled = true;
        EXPECT_FALSE(Frame(field, 300.f, &properties));
        EXPECT_TRUE(ctx.Disabled);
        ctx.Disabled = false;
    }
}

TEST_F(InspectorPanelTest, InvalidOrUnknownStringStorageIsNotReadOrEdited)
{
    char value[4] = {'a', 'b', 'c', 'd'};
    for (uint32_t size : {0u, 4u})
    {
        const FieldDescriptor field = {.Name = "Label", .Type = FieldType::String, .Size = size, .StringCap = 4};
        ctx.FocusKey                = Hash("##fv_0_0");
        ZEngine::Helpers::secure_strcpy(ctx.TextInput, sizeof(ctx.TextInput), "X");
        ctx.TextInputLen = 1;
        EXPECT_FALSE(Frame(field, 300.f, value));
        EXPECT_EQ(Text(ctx.Root), "Label(invalid string)");
        EXPECT_EQ(Find(Hash("##fv_0_0")), nullptr);
        EXPECT_FALSE(ctx.TextInputActive);
        EXPECT_EQ(std::string(value, sizeof(value)), "abcd");
    }
}

TEST_F(InspectorPanelTest, SizedStringsRemainEditableWithoutAnExplicitStringCapacity)
{
    char                  value[8] = "abc";
    const FieldDescriptor field    = {.Name = "Label", .Type = FieldType::String, .Size = sizeof(value)};
    ctx.FocusKey                   = Hash("##fv_0_0");
    ZEngine::Helpers::secure_strcpy(ctx.TextInput, sizeof(ctx.TextInput), "d");
    ctx.TextInputLen = 1;
    EXPECT_TRUE(Frame(field, 300.f, value));
    EXPECT_STREQ(value, "abcd");
}

TEST_F(InspectorPanelTest, NarrowSignedIntegerEditsSaturateWithoutWrappingOrFalseChanges)
{
    const auto check = [&](auto value, FieldType type) {
        using Value                 = decltype(value);
        const FieldDescriptor field = {.Name = "Number", .Type = type, .Size = sizeof(Value)};
        value                       = std::numeric_limits<Value>::max();
        ctx.FocusKey                = Hash("##fv_0_0");
        ctx.ArrowUpPressed          = true;
        EXPECT_FALSE(Frame(field, 300.f, &value));
        EXPECT_EQ(value, std::numeric_limits<Value>::max());
        value                = std::numeric_limits<Value>::min();
        ctx.FocusKey         = Hash("##fv_0_0");
        ctx.ArrowDownPressed = true;
        EXPECT_FALSE(Frame(field, 300.f, &value));
        EXPECT_EQ(value, std::numeric_limits<Value>::min());
        ctx.FocusKey       = Hash("##fv_0_0");
        ctx.ArrowUpPressed = true;
        EXPECT_TRUE(Frame(field, 300.f, &value));
        EXPECT_EQ(value, std::numeric_limits<Value>::min() + 1);
    };
    check(int8_t{}, FieldType::Int8);
    check(int16_t{}, FieldType::Int16);
}

TEST_F(InspectorPanelTest, AssetUuidUsesCanonicalTextWithoutRetainingPerFrameStorage)
{
    auto                  uuid           = uuids::uuid::from_string("00112233-4455-6677-8899-aabbccddeeff").value();
    const FieldDescriptor field          = {.Name = "Asset", .Type = FieldType::AssetUUID, .Size = sizeof(uuid), .ReadOnly = true};
    const auto            retained_bytes = ctx.PersistentArena.m_current_offset;
    for (int i = 0; i < 8; ++i)
    {
        EXPECT_FALSE(Frame(field, 300.f, &uuid));
        EXPECT_EQ(Text(ctx.Root), "Asset00112233-4455-6677-8899-aabbccddeeff");
        EXPECT_EQ(ctx.PersistentArena.m_current_offset, retained_bytes);
    }
    uuid = {};
    EXPECT_FALSE(Frame(field, 300.f, &uuid));
    EXPECT_EQ(Text(ctx.Root), "Asset(none)");
}
