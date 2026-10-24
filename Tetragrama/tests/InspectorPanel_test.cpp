#include <Tetragrama/EditorSession.h>
#include <Tetragrama/Panels/InspectorPanel.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
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
        static bool DrawSession(ZUIContext* ctx, const ComponentMeta& meta, const FieldDescriptor& field, EditorSession& session, EntityID entity, float width)
        {
            return InspectorPanel::DrawSessionField(ctx, meta, field, session, entity, width, 0, 0);
        }
        static void BeginName(InspectorPanel& panel, ZUIContext* ctx, EditorSession& session, EntityID entity, const char* key)
        {
            panel.BeginNameEdit(ctx, session, entity, ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key))));
        }
        static void DrawName(InspectorPanel& panel, ZUIContext* ctx, EditorSession& session, EntityID entity, const char* key)
        {
            panel.DrawNameEdit(ctx, session, entity, key, 200.f);
        }
        static uint64_t NameKey(const InspectorPanel& panel)
        {
            return panel.m_name_key;
        }
        static bool DrawUntracked(InspectorPanel& panel, ZUIContext* ctx, const ComponentMeta& meta, const FieldDescriptor& field, EditorSession& session, EntityID entity, float width)
        {
            return panel.DrawUntrackedField(ctx, meta, field, session, entity, width, 0, 0);
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
        {
            glyph.AdvanceX = 5.5f;
        }
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
        {
            root = ctx.Root;
        }
        if (root->Key == key)
        {
            return root;
        }
        for (auto* child = root->FirstChild; child; child = child->NextSib)
        {
            if (auto* found = Find(key, child))
            {
                return found;
            }
        }
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
        {
            result += Text(child);
        }
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

class InspectorSessionTest : public InspectorPanelTest
{
protected:
    ZEngine::Core::Memory::MemoryManager document_memory = {};
    Scene                                scene           = {};
    Tetragrama::EditorSession            session         = {};
    EntityID                             entity          = {};
    ComponentMeta                        transform_meta  = {.TypeID = ComponentTypeOf<ZEngine::ECS::Components::TransformComponent>()};
    FieldDescriptor                      position        = {.Name = "Position", .Type = FieldType::Vec3f, .Offset = offsetof(ZEngine::ECS::Components::TransformComponent, Position), .Size = sizeof(ZEngine::Core::Maths::Vec3f)};
    Tetragrama::Panels::InspectorPanel   panel           = {};

    void                                 SetUp() override
    {
        InspectorPanelTest::SetUp();
        document_memory.Initialize(ZMega(64), {});
        scene.Initialize(&document_memory.MainArena);
        entity = scene.CreateEntity();
        scene.AddComponent<ZEngine::ECS::Components::UUIDComponent>(entity, {uuids::uuid::from_string("780d9696-105b-4fe1-86cd-8435bdf45293").value()});
        scene.AddComponent<ZEngine::ECS::Components::TransformComponent>(entity, {});
        ASSERT_TRUE(session.Initialize(&document_memory.MainArena, {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 4096}));
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.BindScene(&scene));
        ASSERT_TRUE(session.Select(entity));
        session.EndMutationPhase();
    }
    void TearDown() override
    {
        session.Shutdown();
        scene.Shutdown();
        document_memory.Shutdown();
        InspectorPanelTest::TearDown();
    }
    void SessionFrame(const ComponentMeta& meta, const FieldDescriptor& field)
    {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(400.f), ZPx(900.f));
        EXPECT_TRUE(Tetragrama::Panels::InspectorPanelTestAccess::DrawSession(&ctx, meta, field, session, entity, 400.f));
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
    }
    void Process()
    {
        ASSERT_TRUE(session.BeginMutationPhase());
        EXPECT_TRUE(session.ProcessRequests());
        session.EndMutationPhase();
    }
    void NameFrame(const char* key = "##actor_name")
    {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(400.f), ZPx(900.f));
        Tetragrama::Panels::InspectorPanelTestAccess::DrawName(panel, &ctx, session, entity, key);
        char other[16] = {};
        ZUITextField(&ctx, "other", other, sizeof(other), 100.f);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
    }
    void BeginName(const char* key = "##actor_name")
    {
        Tetragrama::Panels::InspectorPanelTestAccess::BeginName(panel, &ctx, session, entity, key);
    }
    void TypeName(const char* text)
    {
        ZEngine::Helpers::secure_strcpy(ctx.TextInput, sizeof(ctx.TextInput), text);
        ctx.TextInputLen = static_cast<uint32_t>(secure_strlen(text));
    }
    void UntrackedFrame(const ComponentMeta& meta, const FieldDescriptor& field)
    {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(400.f), ZPx(900.f));
        EXPECT_TRUE(Tetragrama::Panels::InspectorPanelTestAccess::DrawUntracked(panel, &ctx, meta, field, session, entity, 400.f));
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
    }
};

TEST_F(InspectorSessionTest, LightColorUsesStablePresentationStorageAndQueuesOnlySemanticRgb)
{
    scene.AddComponent<LightComponent>(
        entity,
        {
        .Intensity = 4.f, .Color = {.18f, 4.5f, .25f}
    });
    const ComponentMeta meta = {.TypeID = ComponentTypeOf<LightComponent>()};
    UntrackedFrame(meta, color);
    ctx.FocusKey      = ColorKey("number");
    ctx.KeyboardFocus = true;
    UntrackedFrame(meta, color);
    auto* state = ZUIStateGetOrInsert(&ctx.StateStore, Hash("##fv_0_0"))->ColorState;
    ASSERT_NE(state, nullptr);
    ASSERT_TRUE(state->Numbers[0].Editing);
    ZEngine::Helpers::secure_strcpy(state->Numbers[0].Buffer, sizeof(state->Numbers[0].Buffer), "64");
    ctx.EnterPressed = true;
    UntrackedFrame(meta, color);
    const auto* component = scene.GetComponent<LightComponent>(entity);
    EXPECT_FLOAT_EQ(component->Color[0], .18f);
    EXPECT_FLOAT_EQ(component->Color[1], 4.5f);
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    Process();
    EXPECT_NEAR(component->Color[0], .05126946f, 1e-7f);
    EXPECT_EQ(component->Color[1], 4.5f);
    EXPECT_EQ(component->Color[2], .25f);
    EXPECT_FLOAT_EQ(component->Intensity, 4.f);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(InspectorSessionTest, EqualColorOnAnotherEntityRetiresOldTextAndUndoBindingWithoutAllocatingPerTarget)
{
    using namespace ZEngine::ECS::Components;
    scene.AddComponent<LightComponent>(entity, {});
    const ComponentMeta meta = {.TypeID = ComponentTypeOf<LightComponent>()};
    UntrackedFrame(meta, color);
    const auto* state = ZUIStateGetOrInsert(&ctx.StateStore, Hash("##fv_0_0"))->ColorState;
    ASSERT_NE(state, nullptr);
    const auto first_binding = state->BindingIdentity;
    ctx.FocusKey             = ColorKey("number");
    ctx.KeyboardFocus        = true;
    UntrackedFrame(meta, color);
    EXPECT_TRUE(state->Numbers[0].Editing);
    const auto first  = entity;
    const auto second = scene.CreateEntity();
    scene.AddComponent<UUIDComponent>(second, {uuids::uuid::from_string("780d9696-105b-4fe1-86cd-8435bdf45294").value()});
    scene.AddComponent<LightComponent>(second, {});
    entity = second;
    UntrackedFrame(meta, color);
    EXPECT_NE(state->BindingIdentity, first_binding);
    EXPECT_FALSE(state->Numbers[0].Editing);
    EXPECT_EQ(ctx.FocusKey, 0u);
    const auto persistent_bytes = ctx.PersistentArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        entity = i % 2 ? second : first;
        UntrackedFrame(meta, color);
        EXPECT_EQ(session.PendingRequestCount(), 0u);
        EXPECT_EQ(ctx.PersistentArena.m_current_offset, persistent_bytes);
    }
}

TEST_F(InspectorSessionTest, CameraCheckboxQueuesRatherThanWritingDuringUiBuildAndRespectsReadOnly)
{
    using ZEngine::ECS::Components::CameraComponent;
    scene.AddComponent<CameraComponent>(entity, {});
    const ComponentMeta   meta = {.TypeID = ComponentTypeOf<CameraComponent>()};
    const FieldDescriptor main = {.Name = "IsMain", .Type = FieldType::Bool, .Offset = offsetof(CameraComponent, IsMain), .Size = sizeof(bool)};
    ctx.FocusKey               = Hash("##cb_##fv_0_0");
    ctx.EnterPressed           = true;
    UntrackedFrame(meta, main);
    EXPECT_FALSE(scene.GetComponent<CameraComponent>(entity)->IsMain);
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    Process();
    EXPECT_TRUE(scene.GetComponent<CameraComponent>(entity)->IsMain);
    ASSERT_TRUE(session.BeginMutationPhase(false));
    session.EndMutationPhase();
    ctx.FocusKey     = Hash("##cb_##fv_0_0");
    ctx.EnterPressed = true;
    UntrackedFrame(meta, main);
    EXPECT_TRUE(scene.GetComponent<CameraComponent>(entity)->IsMain);
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_FALSE(ctx.Disabled);
}

TEST_F(InspectorSessionTest, MotionComboQueuesItsTypedChoiceAndRetiresAnOpenPopupOnSceneRebind)
{
    using ZEngine::ECS::Components::RigidBodyComponent;
    scene.AddComponent<RigidBodyComponent>(entity, {.BodyID = 42u});
    const ComponentMeta        meta      = {.TypeID = ComponentTypeOf<RigidBodyComponent>()};
    static constexpr EnumValue choices[] = {
        {   "Static", 0},
        {"Kinematic", 1},
        {  "Dynamic", 2}
    };
    const FieldDescriptor motion = {.Name = "MotionKind", .Type = FieldType::Enum, .Offset = offsetof(RigidBodyComponent, MotionKind), .Size = sizeof(RigidBodyComponent::MotionType), .EnumValues = choices, .EnumCount = 3};
    UntrackedFrame(meta, motion);
    ZUIOpenPopup(&ctx, "##fv_0_0", 0.f, 22.f);
    UntrackedFrame(meta, motion);
    UntrackedFrame(meta, motion);
    ASSERT_EQ(ctx.PopupStackSize, 1u);
    ctx.FocusKey      = Hash("Static");
    ctx.KeyboardFocus = true;
    ctx.EnterPressed  = true;
    UntrackedFrame(meta, motion);
    EXPECT_EQ(scene.GetComponent<RigidBodyComponent>(entity)->MotionKind, RigidBodyComponent::MotionType::Dynamic);
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    Process();
    EXPECT_EQ(scene.GetComponent<RigidBodyComponent>(entity)->MotionKind, RigidBodyComponent::MotionType::Static);
    EXPECT_EQ(scene.GetComponent<RigidBodyComponent>(entity)->BodyID, 42u);
    ZUIOpenPopup(&ctx, "##fv_0_0", 0.f, 22.f);
    UntrackedFrame(meta, motion);
    UntrackedFrame(meta, motion);
    ASSERT_EQ(ctx.PopupStackSize, 1u);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.BindScene(&scene));
    session.EndMutationPhase();
    ctx.FocusKey      = Hash("Dynamic");
    ctx.KeyboardFocus = true;
    ctx.EnterPressed  = true;
    UntrackedFrame(meta, motion);
    EXPECT_EQ(ctx.PopupStackSize, 0u);
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_EQ(scene.GetComponent<RigidBodyComponent>(entity)->MotionKind, RigidBodyComponent::MotionType::Static);
}

TEST_F(InspectorSessionTest, AllAdditionalNumericWidgetsQueueInsteadOfWritingDuringUIConstruction)
{
    using namespace ZEngine::ECS::Components;
    scene.AddComponent<LightComponent>(entity, {});
    scene.AddComponent<CameraComponent>(entity, {});
    scene.AddComponent<RigidBodyComponent>(entity, {.BodyID = 22u});
    struct NumericCase
    {
        ComponentMeta      Meta     = {};
        FieldDescriptor    Field    = {};
        AuthoredFloatField Identity = AuthoredFloatField::LightIntensity;
    };
    const NumericCase cases[] = {
        {    {.TypeID = ComponentTypeOf<LightComponent>()},         {.Name = "Intensity", .Type = FieldType::Float, .Offset = offsetof(LightComponent, Intensity), .Size = sizeof(float)},       AuthoredFloatField::LightIntensity},
        {   {.TypeID = ComponentTypeOf<CameraComponent>()},                  {.Name = "Near", .Type = FieldType::Float, .Offset = offsetof(CameraComponent, Near), .Size = sizeof(float)},           AuthoredFloatField::CameraNear},
        {   {.TypeID = ComponentTypeOf<CameraComponent>()},    {.Name = "AspectRatio", .Type = FieldType::Float, .Offset = offsetof(CameraComponent, AspectRatio), .Size = sizeof(float)},    AuthoredFloatField::CameraAspectRatio},
        {{.TypeID = ComponentTypeOf<RigidBodyComponent>()},               {.Name = "Mass", .Type = FieldType::Float, .Offset = offsetof(RigidBodyComponent, Mass), .Size = sizeof(float)},        AuthoredFloatField::RigidBodyMass},
        {{.TypeID = ComponentTypeOf<RigidBodyComponent>()},       {.Name = "Friction", .Type = FieldType::Float, .Offset = offsetof(RigidBodyComponent, Friction), .Size = sizeof(float)},    AuthoredFloatField::RigidBodyFriction},
        {{.TypeID = ComponentTypeOf<RigidBodyComponent>()}, {.Name = "Restitution", .Type = FieldType::Float, .Offset = offsetof(RigidBodyComponent, Restitution), .Size = sizeof(float)}, AuthoredFloatField::RigidBodyRestitution},
    };
    for (const auto& item : cases)
    {
        SCOPED_TRACE(item.Field.Name);
        float before = 0.f, after = 0.f;
        ASSERT_TRUE(ReadAuthoredFloatField(scene, entity, item.Identity, before));
        ctx.FocusKey       = Hash("##fv_0_0");
        ctx.ArrowUpPressed = true;
        SessionFrame(item.Meta, item.Field);
        ASSERT_TRUE(ReadAuthoredFloatField(scene, entity, item.Identity, after));
        EXPECT_EQ(after, before);
        EXPECT_EQ(session.PendingRequestCount(), 1u);
        Process();
        ASSERT_TRUE(ReadAuthoredFloatField(scene, entity, item.Identity, after));
        EXPECT_FLOAT_EQ(after, before + .05f);
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.Undo());
        session.EndMutationPhase();
        EXPECT_FALSE(session.Dirty());
        EXPECT_EQ(scene.GetComponent<RigidBodyComponent>(entity)->BodyID, 22u);
    }
}

TEST_F(InspectorSessionTest, NameTextIsAnIsolatedDraftUntilEnterQueuesTheExpectedOriginalValue)
{
    using ZEngine::ECS::Components::NameComponent;
    scene.AddComponent<NameComponent>(entity, {"Original"});
    BeginName();
    TypeName("Renamed");
    NameFrame();
    EXPECT_STREQ(scene.GetComponent<NameComponent>(entity)->Value, "Original");
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    ctx.EnterPressed = true;
    NameFrame();
    EXPECT_STREQ(scene.GetComponent<NameComponent>(entity)->Value, "Original");
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    EXPECT_FALSE(panel.m_renaming);
    Process();
    EXPECT_STREQ(scene.GetComponent<NameComponent>(entity)->Value, "Renamed");
    EXPECT_TRUE(session.Dirty());
}

TEST_F(InspectorSessionTest, NameEscapeDiscardsDraftWithoutHistoryOrDirtyState)
{
    using ZEngine::ECS::Components::NameComponent;
    scene.AddComponent<NameComponent>(entity, {"Original"});
    BeginName();
    TypeName("Discarded");
    NameFrame();
    ctx.EscapePressed = true;
    NameFrame();
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_STREQ(scene.GetComponent<NameComponent>(entity)->Value, "Original");
    EXPECT_FALSE(session.Dirty());
    EXPECT_FALSE(panel.m_renaming);
    EXPECT_EQ(ctx.FocusKey, 0u);
}

TEST_F(InspectorSessionTest, NameOutsidePressCommitsEvenWhenScrollableSpaceRetainsTextFocus)
{
    using ZEngine::ECS::Components::NameComponent;
    scene.AddComponent<NameComponent>(entity, {"Original"});
    BeginName();
    TypeName("Outside");
    NameFrame();
    ctx.MousePos[0] = ctx.MousePos[1] = 700.f;
    ctx.MousePressed[0]               = true;
    NameFrame();
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    EXPECT_EQ(ctx.FocusKey, 0u);
    Process();
    EXPECT_STREQ(scene.GetComponent<NameComponent>(entity)->Value, "Outside");
}

TEST_F(InspectorSessionTest, ReflectedNameUsesTheSameDraftAndCommitsOnFocusLoss)
{
    using ZEngine::ECS::Components::NameComponent;
    scene.AddComponent<NameComponent>(entity, {"Original"});
    constexpr auto key = "##fv_0_0";
    ctx.FocusKey       = Hash(key);
    TypeName("Property");
    NameFrame(key);
    EXPECT_FALSE(panel.m_renaming);
    EXPECT_STREQ(scene.GetComponent<NameComponent>(entity)->Value, "Original");
    ctx.FocusKey = Hash("other");
    NameFrame(key);
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    EXPECT_EQ(ctx.FocusKey, Hash("other"));
    Process();
    EXPECT_STREQ(scene.GetComponent<NameComponent>(entity)->Value, "Property");
}

TEST_F(InspectorSessionTest, IdleReflectedNameDoesNotOverwriteAnActiveIdentityDraft)
{
    using ZEngine::ECS::Components::NameComponent;
    scene.AddComponent<NameComponent>(entity, {"Original"});
    BeginName();
    TypeName("Identity");
    NameFrame();
    NameFrame("##fv_0_0");
    ctx.EnterPressed = true;
    NameFrame();
    Process();
    EXPECT_STREQ(scene.GetComponent<NameComponent>(entity)->Value, "Identity");
}

TEST_F(InspectorSessionTest, NameTargetRebindAndConcurrentRenameCannotOverwriteOtherAuthoredContent)
{
    using ZEngine::ECS::Components::NameComponent;
    scene.AddComponent<NameComponent>(entity, {"Original"});
    BeginName();
    TypeName("Stale");
    NameFrame();
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.BindScene(&scene));
    session.EndMutationPhase();
    NameFrame();
    EXPECT_EQ(Tetragrama::Panels::InspectorPanelTestAccess::NameKey(panel), 0u);
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_STREQ(scene.GetComponent<NameComponent>(entity)->Value, "Original");
    BeginName();
    TypeName("Stale");
    NameFrame();
    ZEngine::Helpers::secure_strcpy(scene.GetComponent<NameComponent>(entity)->Value, 128, "OtherOwner");
    ctx.EnterPressed = true;
    NameFrame();
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_STREQ(scene.GetComponent<NameComponent>(entity)->Value, "OtherOwner");
}

TEST_F(InspectorSessionTest, PureECSNumericPointerControlNeverMutatesAuthoredStorageDuringUIBuild)
{
    using ZEngine::ECS::Components::TransformComponent;
    const auto key          = Hash("##x_frow_0_0");
    ctx.ActiveKey           = key;
    ctx.MouseDown[0]        = true;
    ctx.MousePressPos[0][0] = ctx.PrevMousePos[0] = 100.f;
    ctx.MousePos[0]                               = 130.f;
    SessionFrame(transform_meta, position);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    Process();
    EXPECT_EQ(session.ControlKey(), key);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.UpdateControlDrag(130.f, true));
    session.EndMutationPhase();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 1.5f);
    SessionFrame(transform_meta, position);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 1.5f);
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.UpdateControlDrag(140.f, false));
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 2.f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(InspectorSessionTest, NumericKeyboardNudgeIsOneQueuedSemanticEdit)
{
    using ZEngine::ECS::Components::TransformComponent;
    ctx.FocusKey       = Hash("##y_frow_0_0");
    ctx.ArrowUpPressed = true;
    SessionFrame(transform_meta, position);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.y, 0.f);
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    Process();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.y, .05f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_FALSE(session.Active());
}

TEST_F(InspectorSessionTest, RotationPointerMappingConvertsDisplayDegreesToAuthoredRadians)
{
    using ZEngine::ECS::Components::TransformComponent;
    const FieldDescriptor rotation = {.Name = "Rotation", .Type = FieldType::Vec3f, .Offset = offsetof(TransformComponent, Rotation), .Size = sizeof(ZEngine::Core::Maths::Vec3f), .Tooltip = "Euler angles (radians)"};
    ctx.ActiveKey                  = Hash("##z_frow_0_0");
    ctx.MouseDown[0]               = true;
    ctx.MousePressPos[0][0] = ctx.PrevMousePos[0] = 100.f;
    ctx.MousePos[0]                               = 190.f;
    SessionFrame(transform_meta, rotation);
    Process();
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.UpdateControlDrag(190.f, false));
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Rotation.z, 1.570796f, 1e-5f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.z, 0.f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
}

TEST_F(InspectorSessionTest, RotationKeyboardEditPreservesUntouchedAuthoredAxesExactly)
{
    using ZEngine::ECS::Components::TransformComponent;
    auto* component                = scene.GetComponent<TransformComponent>(entity);
    component->Rotation            = {.1234567f, 1.234567f, 3.7654321f};
    const auto            before   = component->Rotation;
    const FieldDescriptor rotation = {.Name = "Rotation", .Type = FieldType::Vec3f, .Offset = offsetof(TransformComponent, Rotation), .Size = sizeof(ZEngine::Core::Maths::Vec3f), .Tooltip = "Euler angles (radians)"};
    ctx.FocusKey                   = Hash("##x_frow_0_0");
    ctx.ArrowUpPressed             = true;
    SessionFrame(transform_meta, rotation);
    EXPECT_EQ(component->Rotation.x, before.x);
    Process();
    EXPECT_NEAR(component->Rotation.x, before.x + .01745329f, 1e-7f);
    EXPECT_EQ(component->Rotation.y, before.y);
    EXPECT_EQ(component->Rotation.z, before.z);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Undo());
    EXPECT_EQ(component->Rotation.x, before.x);
    EXPECT_EQ(component->Rotation.y, before.y);
    EXPECT_EQ(component->Rotation.z, before.z);
}

TEST_F(InspectorSessionTest, LightRangeKeyboardEditUsesSharedFieldHistory)
{
    scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Point});
    const ComponentMeta   meta  = {.TypeID = ComponentTypeOf<LightComponent>()};
    const FieldDescriptor range = {.Name = "Range", .Type = FieldType::Float, .Offset = offsetof(LightComponent, Range), .Size = sizeof(float)};
    ctx.FocusKey                = Hash("##fv_0_0");
    ctx.ArrowUpPressed          = true;
    SessionFrame(meta, range);
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 10.f);
    Process();
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 10.05f);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 10.f);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(InspectorSessionTest, ConfirmedNumericTextQueuesOneFieldEditWithoutBindingTheWidgetToECSStorage)
{
    scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Point});
    const ComponentMeta   meta  = {.TypeID = ComponentTypeOf<LightComponent>()};
    const FieldDescriptor range = {.Name = "Range", .Type = FieldType::Float, .Offset = offsetof(LightComponent, Range), .Size = sizeof(float)};
    ctx.ActiveKey = ctx.HotKey = Hash("##fv_0_0");
    ctx.MouseReleased[0] = ctx.CtrlDown = true;
    SessionFrame(meta, range); // Ctrl+click starts the widget's text buffer at "10".
    ctx.CtrlDown = false;
    for (uint32_t i = 0; i < 2; ++i)
    {
        ctx.BackspacePressed = true;
        SessionFrame(meta, range);
    }
    ZEngine::Helpers::secure_strcpy(ctx.TextInput, sizeof(ctx.TextInput), "25");
    ctx.TextInputLen = 2;
    ctx.EnterPressed = true;
    SessionFrame(meta, range);
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 10.f);
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    Process();
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 25.f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_FALSE(session.Active());
}

TEST_F(InspectorSessionTest, UnsupportedLightFieldAndReadOnlySessionCannotBeginAnEdit)
{
    scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Directional});
    const ComponentMeta   meta  = {.TypeID = ComponentTypeOf<LightComponent>()};
    const FieldDescriptor range = {.Name = "Range", .Type = FieldType::Float, .Offset = offsetof(LightComponent, Range), .Size = sizeof(float)};
    ctx.FocusKey                = Hash("##fv_0_0");
    ctx.ArrowUpPressed          = true;
    SessionFrame(meta, range);
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 10.f);
    ASSERT_TRUE(session.BeginMutationPhase(false));
    session.EndMutationPhase();
    ctx.FocusKey       = Hash("##x_frow_0_0");
    ctx.ArrowUpPressed = true;
    SessionFrame(transform_meta, position);
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_FALSE(ctx.Disabled);
}

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
    {
        EXPECT_FALSE(Tetragrama::Panels::InspectorPanelTestAccess::Draw(&ctx, fields[i], &light, 300.f, i));
    }
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
    {
        EXPECT_FLOAT_EQ(color_label->TextColor[channel], ctx.Theme.TextDefault[channel]);
    }

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
        {
            EXPECT_FLOAT_EQ(color_label->TextColor[channel], label->TextColor[channel]);
        }
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
    {
        pad = 0x5a;
    }
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
    {
        EXPECT_EQ(pad, 0x5a);
    }
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
        {
            EXPECT_EQ(byte, 0x5a);
        }
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
        {
            EXPECT_EQ(byte, 0x5a);
        }
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
        {
            EXPECT_FLOAT_EQ(properties.Vector[i], static_cast<float>(i + 1));
        }
        for (int i = 0; i < 2; ++i)
        {
            EXPECT_FLOAT_EQ(properties.Vector2[i], static_cast<float>(i + 1));
        }
        for (int i = 0; i < 4; ++i)
        {
            EXPECT_FLOAT_EQ(properties.Vector4[i], static_cast<float>(i + 1));
        }
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
