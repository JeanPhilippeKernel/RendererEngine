#include <Tetragrama/EditorSession.h>
#include <Tetragrama/Panels/ViewportPanel.h>
#include <ZEngine/Controllers/FlyCameraController.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>
#include <limits>
#include <string_view>

using namespace Tetragrama;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::UI;
namespace G = Tetragrama::Gizmo;

namespace Tetragrama::Panels
{
    struct ViewportPanelTestAccess
    {
        static void Projection(ViewportPanel& panel, ZUIContext& ctx, const EditorSession& session, ZEngine::Controllers::ICameraController* controller)
        {
            panel.BuildProjectionControl(&ctx, session, controller);
        }

        static void Toolbar(ViewportPanel& panel, ZUIContext& ctx, EditorSession& session)
        {
            panel.BuildTransformToolbar(&ctx, session);
        }
    };
} // namespace Tetragrama::Panels

class GizmoStateTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory  = {};
    Scene                                scene   = {};
    EditorSession                        session = {};
    EntityID                             entity  = {};

    void                                 SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        scene.Initialize(&memory.MainArena);
        entity = scene.CreateEntity();
        scene.AddComponent<UUIDComponent>(entity, {uuids::uuid::from_string("004f8787-4e16-4d29-a43a-77f761c74470").value()});
        scene.AddComponent<TransformComponent>(entity, {});
        ASSERT_TRUE(session.Initialize(&memory.MainArena, {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 4096}));
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.BindScene(&scene));
        session.EndMutationPhase();
    }

    void TearDown() override
    {
        session.Shutdown();
        scene.Shutdown();
        memory.Shutdown();
    }

    bool Apply(const G::Change& change)
    {
        if (!session.RequestGizmoChange(change) || !session.BeginMutationPhase())
        {
            return false;
        }
        const bool result = session.ProcessRequests();
        session.EndMutationPhase();
        return result;
    }

    void StartPreview()
    {
        ASSERT_TRUE(session.BeginMutationPhase());
        const auto target = session.TargetFor(entity);
        ASSERT_TRUE(session.BeginTransform({&target, 1}));
        LocalTransformValue value = {};
        value.Position.x          = 7.f;
        ASSERT_TRUE(session.PreviewTransforms({&value, 1}));
        session.EndMutationPhase();
    }
};

TEST_F(GizmoStateTest, DefaultsMatchApprovedSnapStateAndPreserveSelectMode)
{
    const auto& state = session.GizmoSettings();
    EXPECT_EQ(state.Mode, G::Operation::Select);
    EXPECT_EQ(state.Space, TransformSpacePolicy::World);
    EXPECT_FLOAT_EQ(state.TranslationSnap.Increment, .1f);
    EXPECT_FLOAT_EQ(state.RotationSnap.Increment, 10.f);
    EXPECT_FLOAT_EQ(state.ScaleSnap.Increment, .25f);
    EXPECT_TRUE(state.TranslationSnap.Enabled);
    EXPECT_TRUE(state.RotationSnap.Enabled);
    EXPECT_FALSE(state.ScaleSnap.Enabled);
    EXPECT_TRUE(state.ShowGrid);
    EXPECT_EQ(state.Revision, 1u);
}

TEST_F(GizmoStateTest, QueuedChangeCannotMutateStateBeforeTheExclusivePhase)
{
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = G::ChangeKind::Operation, .Mode = G::Operation::Translate}));
    EXPECT_EQ(session.GizmoSettings().Mode, G::Operation::Select);
    EXPECT_FALSE(session.ProcessRequests());
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_TRUE(session.ProcessRequests());
    session.EndMutationPhase();
    EXPECT_EQ(session.GizmoSettings().Mode, G::Operation::Translate);
    EXPECT_EQ(session.GizmoSettings().Revision, 2u);
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoStateTest, ModeTransitionRestoresThePreviewWithoutAddingHistory)
{
    StartPreview();
    ASSERT_TRUE(session.Active());
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::Operation, .Mode = G::Operation::Rotate}));
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoStateTest, UnchangedModeDoesNotCancelOrInvalidateTheActiveInteraction)
{
    StartPreview();
    const auto revision = session.GizmoSettings().Revision;
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::Operation, .Mode = G::Operation::Select}));
    EXPECT_TRUE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 7.f);
    EXPECT_EQ(session.GizmoSettings().Revision, revision);
}

TEST_F(GizmoStateTest, SpaceTransitionRestoresThePreviewBeforeUpdatingPreference)
{
    StartPreview();
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::Space, .Space = TransformSpacePolicy::Local}));
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_EQ(session.GizmoSettings().Space, TransformSpacePolicy::Local);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoStateTest, ScaleUsesLocalAxesWhileSpacePreferenceRemainsEditable)
{
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::Operation, .Mode = G::Operation::Scale}));
    EXPECT_EQ(session.GizmoSettings().Space, TransformSpacePolicy::World);
    EXPECT_EQ(session.GizmoSettings().EffectiveSpace(), TransformSpacePolicy::Local);
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::Space, .Space = TransformSpacePolicy::Local}));
    EXPECT_EQ(session.GizmoSettings().Space, TransformSpacePolicy::Local);
    EXPECT_EQ(session.GizmoSettings().EffectiveSpace(), TransformSpacePolicy::Local);
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::Operation, .Mode = G::Operation::Translate}));
    EXPECT_EQ(session.GizmoSettings().Space, TransformSpacePolicy::Local);
    EXPECT_EQ(session.GizmoSettings().EffectiveSpace(), TransformSpacePolicy::Local);
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::Space, .Space = TransformSpacePolicy::World}));
    EXPECT_EQ(session.GizmoSettings().EffectiveSpace(), TransformSpacePolicy::World);
}

TEST_F(GizmoStateTest, ModeTransitionDropsDelayedControlInputButPreservesSelectionIntents)
{
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = G::ChangeKind::Operation, .Mode = G::Operation::Rotate}));
    ASSERT_TRUE(session.RequestControlDrag(entity, {.Key = 42, .UnitsPerPixel = 1.f}));
    ASSERT_TRUE(session.RequestSelection(entity));
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_TRUE(session.ProcessRequests());
    session.EndMutationPhase();
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.ControlKey(), 0u);
    EXPECT_EQ(session.Selection().Entities().size(), 1u);
    EXPECT_EQ(session.PendingRequestCount(), 0u);
}

TEST_F(GizmoStateTest, SnapChangesDoNotCancelOrRewriteAnActivePreview)
{
    StartPreview();
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::SnapEnabled, .Target = G::SnapTarget::Translation, .Enabled = false}));
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::SnapIncrement, .Target = G::SnapTarget::Translation, .Increment = .5f}));
    EXPECT_TRUE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 7.f);
    EXPECT_FALSE(session.GizmoSettings().TranslationSnap.Enabled);
    EXPECT_FLOAT_EQ(session.GizmoSettings().TranslationSnap.Increment, .5f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoStateTest, EveryApprovedPresetIsAcceptedAndRetainsTheSnapEnabledFlag)
{
    for (auto target : {G::SnapTarget::Translation, G::SnapTarget::Rotation, G::SnapTarget::Scale})
    {
        ASSERT_TRUE(Apply({.Kind = G::ChangeKind::SnapEnabled, .Target = target, .Enabled = false}));
        for (const auto& preset : G::SnapPresets(target))
        {
            ASSERT_TRUE(Apply({.Kind = G::ChangeKind::SnapIncrement, .Target = target, .Increment = preset.Increment}));
            const auto* value = G::SnapFor(session.GizmoSettings(), target);
            ASSERT_NE(value, nullptr);
            EXPECT_FLOAT_EQ(value->Increment, preset.Increment);
            EXPECT_FALSE(value->Enabled);
        }
    }
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoStateTest, InvalidEnumsAndUnapprovedIncrementsNeverEnterTheQueue)
{
    EXPECT_FALSE(session.RequestGizmoChange({.Kind = static_cast<G::ChangeKind>(255)}));
    EXPECT_FALSE(session.RequestGizmoChange({.Mode = static_cast<G::Operation>(255)}));
    EXPECT_FALSE(session.RequestGizmoChange({.Kind = G::ChangeKind::Space, .Space = static_cast<TransformSpacePolicy>(255)}));
    EXPECT_FALSE(session.RequestGizmoChange({.Kind = G::ChangeKind::SnapEnabled, .Target = static_cast<G::SnapTarget>(255)}));
    for (float value : {0.f, -1.f, .123f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        EXPECT_FALSE(session.RequestGizmoChange({.Kind = G::ChangeKind::SnapIncrement, .Increment = value}));
    }
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_EQ(session.GizmoSettings().Revision, 1u);
    EXPECT_EQ(G::SnapFor(session.GizmoSettings(), static_cast<G::SnapTarget>(255)), nullptr);
    EXPECT_TRUE(G::SnapPresets(static_cast<G::SnapTarget>(255)).empty());
}

TEST_F(GizmoStateTest, GridVisibilityAndSnappingAreIndependentAndDoNotDirtyTheScene)
{
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::GridVisibility, .Enabled = false}));
    EXPECT_FALSE(session.GizmoSettings().ShowGrid);
    EXPECT_TRUE(session.GizmoSettings().TranslationSnap.Enabled);
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::SnapEnabled, .Target = G::SnapTarget::Translation, .Enabled = false}));
    EXPECT_FALSE(session.GizmoSettings().ShowGrid);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoStateTest, SceneReplacementKeepsPreferencesAndDiscardsOldQueuedCommands)
{
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::Space, .Space = TransformSpacePolicy::Local}));
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::GridVisibility, .Enabled = false}));
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = G::ChangeKind::Operation, .Mode = G::Operation::Scale}));
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.BindScene(&scene));
    EXPECT_TRUE(session.ProcessRequests());
    session.EndMutationPhase();
    EXPECT_EQ(session.GizmoSettings().Mode, G::Operation::Select);
    EXPECT_EQ(session.GizmoSettings().Space, TransformSpacePolicy::Local);
    EXPECT_FALSE(session.GizmoSettings().ShowGrid);
}

TEST_F(GizmoStateTest, FullQueueRejectsWithoutChangingPreferences)
{
    for (uint32_t i = 0; i < EditorSession::MaxPendingRequests; ++i)
    {
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = G::ChangeKind::Operation, .Mode = G::Operation::Translate}));
    }
    EXPECT_FALSE(session.RequestGizmoChange({.Kind = G::ChangeKind::GridVisibility, .Enabled = false}));
    EXPECT_TRUE(session.GizmoSettings().ShowGrid);
    EXPECT_EQ(session.GizmoSettings().Mode, G::Operation::Select);
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_TRUE(session.ProcessRequests());
    session.EndMutationPhase();
    EXPECT_EQ(session.GizmoSettings().Mode, G::Operation::Translate);
    EXPECT_EQ(session.GizmoSettings().Revision, 2u);
}

TEST_F(GizmoStateTest, ReadOnlyScenesStillAllowEditorLocalPreferences)
{
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = G::ChangeKind::GridVisibility, .Enabled = false}));
    ASSERT_TRUE(session.BeginMutationPhase(false));
    EXPECT_TRUE(session.ProcessRequests());
    session.EndMutationPhase();
    EXPECT_FALSE(session.GizmoSettings().ShowGrid);
    EXPECT_FALSE(session.Dirty());
}

class GizmoToolbarTest : public GizmoStateTest
{
protected:
    struct CameraController : ZEngine::Controllers::FlyCameraController
    {
        void Bind(ZEngine::Rendering::Cameras::FlyCamera* camera)
        {
            m_camera = camera;
        }
    } controller                                                   = {};
    ZEngine::Controllers::ICameraController* projection_controller = &controller;
    ZEngine::Rendering::Cameras::FlyCamera   camera{4.f / 3.f, {}};
    Panels::ViewportPanel                    panel      = {};
    ZUIContext                               ctx        = {};
    ZUIGlyph                                 glyphs[96] = {};
    ZUIFont                                  font       = {};
    ZUIFontAtlas                             atlas      = {};

    void                                     SetUp() override
    {
        GizmoStateTest::SetUp();
        controller.Bind(&camera);
        ZUIContextInit(&ctx, &memory.MainArena, ZMega(4), ZMega(1), 1024, 1024);
        ctx.ScreenW     = 800;
        ctx.ScreenH     = 600;
        ctx.MousePos[0] = ctx.MousePos[1] = -100.f;
        for (auto& glyph : glyphs)
        {
            glyph.AdvanceX = 5.5f;
        }
        font.Glyphs     = glyphs;
        font.GlyphCount = 96;
        font.LineHeight = 14.f;
        atlas.Body = atlas.Small = atlas.Large = atlas.Heading = &font;
        ctx.Atlas                                              = &atlas;
    }

    void TearDown() override
    {
        ZUIContextDestroy(&ctx);
        GizmoStateTest::TearDown();
    }

    void Frame()
    {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(800.f), ZPx(600.f));
        Panels::ViewportPanelTestAccess::Projection(panel, ctx, session, projection_controller);
        Panels::ViewportPanelTestAccess::Toolbar(panel, ctx, session);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
    }

    ZUIBox* Find(std::string_view key, ZUIBox* root = nullptr)
    {
        if (!root)
        {
            root = ctx.Root;
        }
        const auto hash = ZUIHashStr(key.data(), static_cast<uint32_t>(key.size()));
        if (root->Key == hash)
        {
            return root;
        }
        for (auto* child = root->FirstChild; child; child = child->NextSib)
        {
            if (auto* box = Find(key, child))
            {
                return box;
            }
        }
        return nullptr;
    }

    ZUIBox* TextBytes(std::string_view value, ZUIBox* root)
    {
        if ((root->Flags & ZUI_DrawText) && root->Label.Ptr && std::string_view(root->Label.Ptr, root->Label.Len) == value)
        {
            return root;
        }
        for (auto* child = root->FirstChild; child; child = child->NextSib)
        {
            if (auto* box = TextBytes(value, child))
            {
                return box;
            }
        }
        return nullptr;
    }

    ZUIBox* Text(std::string_view value, ZUIBox* root)
    {
        // ZUI folds supported UTF-8 symbols into its single-byte atlas slots.
        const auto folded = ZUIPushStr(&ctx.FrameArena, value.data(), static_cast<uint32_t>(value.size()));
        return TextBytes({folded.Ptr, folded.Len}, root);
    }

    void Open(const char* key)
    {
        Frame();
        ZUIOpenPopup(&ctx, key, 100.f, 100.f);
        Frame(); // pending popup becomes active at EndFrame
        Frame(); // popup contents build from the active stack
    }
};

TEST_F(GizmoToolbarTest, ProjectionLabelTracksTheCameraWithoutPanelState)
{
    using ZEngine::Rendering::Cameras::CameraType;
    Frame();
    const auto* control = Find("##vp_persp");
    ASSERT_NE(control, nullptr);
    EXPECT_NE(Text("Perspective", Find("##vp_persp")), nullptr);
    EXPECT_EQ(control->FirstChild->IconId, static_cast<uint16_t>(ZUIIcon::Box));
    EXPECT_EQ(control->LastChild->IconId, static_cast<uint16_t>(ZUIIcon::ChevronDown));
    EXPECT_FLOAT_EQ(control->ComputedSize[1], Metrics::HeightControl);
    camera.SetProjectionType(CameraType::ORTHOGRAPHIC);
    Frame();
    EXPECT_NE(Text("Orthographic", Find("##vp_persp")), nullptr);
    camera.SetProjectionType(CameraType::PERSPECTIVE);
    Frame();
    EXPECT_NE(Text("Perspective", Find("##vp_persp")), nullptr);
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoToolbarTest, ProjectionMenuOpensBelowTheControlAndChecksTheLiveCamera)
{
    Frame();
    const auto* control = Find("##vp_persp");
    ASSERT_NE(control, nullptr);
    const float x     = control->ScreenMin[0];
    const float y     = control->ScreenMax[1];
    ctx.FocusKey      = control->Key;
    ctx.KeyboardFocus = ctx.EnterPressed = true;
    Frame();
    ASSERT_EQ(ctx.PopupStackSize, 1u);
    EXPECT_FLOAT_EQ(ctx.PopupStack[0].PosX, x);
    EXPECT_FLOAT_EQ(ctx.PopupStack[0].PosY, y);
    Frame();
    ASSERT_NE(Find("##vp_projection_menu"), nullptr);
    ASSERT_NE(Find("##miex_ck_Perspective"), nullptr);
    EXPECT_TRUE(Find("##miex_ck_Perspective")->Flags & ZUI_DrawIcon);
    EXPECT_FALSE(Find("##miex_ck_Orthographic")->Flags & ZUI_DrawIcon);
}

TEST_F(GizmoToolbarTest, ProjectionMenuChoosesBothViewsWithoutEditingTheScene)
{
    using ZEngine::Rendering::Cameras::CameraType;
    for (auto type : {CameraType::ORTHOGRAPHIC, CameraType::ORTHOGRAPHIC, CameraType::PERSPECTIVE})
    {
        Open("##vp_projection_menu");
        const char* key   = type == CameraType::ORTHOGRAPHIC ? "##miex_Orthographic" : "##miex_Perspective";
        ctx.FocusKey      = ZUIHashStr(key, static_cast<uint32_t>(ZEngine::Helpers::secure_strlen(key)));
        ctx.KeyboardFocus = ctx.EnterPressed = true;
        Frame();
        EXPECT_EQ(camera.GetProjectionType(), type);
        EXPECT_EQ(ctx.PopupStackSize, 0u);
        ctx.FocusKey = 0;
        Frame();
        EXPECT_NE(Text(type == CameraType::ORTHOGRAPHIC ? "Orthographic" : "Perspective", Find("##vp_persp")), nullptr);
    }
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoToolbarTest, ActiveInteractionLocksProjectionEvenIfTheMenuWasAlreadyOpen)
{
    using ZEngine::Rendering::Cameras::CameraType;
    Open("##vp_projection_menu");
    StartPreview();
    Frame();
    EXPECT_TRUE(Find("##vp_persp")->Flags & ZUI_Disabled);
    EXPECT_EQ(Find("##vp_projection_menu"), nullptr);
    EXPECT_EQ(ctx.PopupStackSize, 0u);
    ctx.FocusKey      = ZUIHashStr("##miex_Orthographic", 19);
    ctx.KeyboardFocus = ctx.EnterPressed = true;
    Frame();
    EXPECT_EQ(camera.GetProjectionType(), CameraType::PERSPECTIVE);
    EXPECT_TRUE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 7.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoToolbarTest, MissingCameraOrControllerClosesTheMenuWithoutDereferencingIt)
{
    for (bool missing_controller : {false, true})
    {
        controller.Bind(&camera);
        projection_controller = &controller;
        Open("##vp_projection_menu");
        if (missing_controller)
        {
            projection_controller = nullptr;
        }
        else
        {
            controller.Bind(nullptr);
        }
        ctx.FocusKey      = ZUIHashStr("##miex_Orthographic", 19);
        ctx.KeyboardFocus = ctx.EnterPressed = true;
        Frame();
        EXPECT_TRUE(Find("##vp_persp")->Flags & ZUI_Disabled);
        EXPECT_NE(Text("Projection", Find("##vp_persp")), nullptr);
        EXPECT_EQ(Find("##vp_projection_menu"), nullptr);
        EXPECT_EQ(ctx.PopupStackSize, 0u);
        EXPECT_EQ(session.PendingRequestCount(), 0u);
    }
}

TEST_F(GizmoToolbarTest, DefaultLabelsAndIconsUseRawUnitsAndValidUtf8Degrees)
{
    Frame();
    ASSERT_NE(Find("##vp_grid"), nullptr);
    EXPECT_NE(Text("0.1", Find("##vp_grid")), nullptr);
    EXPECT_NE(Text("10\xC2\xB0", Find("##vp_snaprot")), nullptr);
    EXPECT_NE(Text("0.25", Find("##vp_snapscale")), nullptr);
    EXPECT_EQ(Find("##vp_space")->FirstChild->IconId, static_cast<uint16_t>(ZUIIcon::Globe));
    EXPECT_NE(Text("4", Find("##vp_cam")), nullptr);
    EXPECT_FLOAT_EQ(Find("##vp_transform")->ComputedSize[1], 26.f);
}

TEST_F(GizmoToolbarTest, ModeButtonsQueueSharedStateAndRenderExactlyOneActiveMode)
{
    Frame();
    ctx.FocusKey     = ZUIHashStr("##vp_rot", 8);
    ctx.EnterPressed = true;
    Frame();
    EXPECT_EQ(session.GizmoSettings().Mode, G::Operation::Select);
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    session.EndMutationPhase();
    ctx.FocusKey = 0;
    Frame();
    const auto* active = Find("##vp_rot");
    ASSERT_NE(active, nullptr);
    const float idle_border[4] = ZUI_TOKEN(Tokens::BorderField);
    for (int channel = 0; channel < 4; ++channel)
    {
        EXPECT_FLOAT_EQ(active->BorderColor[channel], ctx.Theme.InputFocusBorder[channel]);
        EXPECT_FLOAT_EQ(Find("##vp_sel")->BorderColor[channel], idle_border[channel]);
    }
}

TEST_F(GizmoToolbarTest, LocalSpaceUsesBoxGlyphAndRemainsInteractiveDuringScale)
{
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::Space, .Space = TransformSpacePolicy::Local}));
    Frame();
    EXPECT_EQ(Find("##vp_space")->FirstChild->IconId, static_cast<uint16_t>(ZUIIcon::Box));
    EXPECT_FALSE(Find("##vp_space")->Flags & ZUI_Disabled);
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::Operation, .Mode = G::Operation::Scale}));
    Frame();
    EXPECT_FALSE(Find("##vp_space")->Flags & ZUI_Disabled);
    ctx.FocusKey     = ZUIHashStr("##vp_space", 10);
    ctx.EnterPressed = true;
    Frame();
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    session.EndMutationPhase();
    EXPECT_EQ(session.GizmoSettings().Space, TransformSpacePolicy::World);
    EXPECT_EQ(session.GizmoSettings().EffectiveSpace(), TransformSpacePolicy::Local);
}

TEST_F(GizmoToolbarTest, GridDisablesScaleAndEntitySpaceWithoutChangingTheSavedSpacePreference)
{
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.SelectScene(SceneTarget::Grid));
    session.EndMutationPhase();
    Frame();
    EXPECT_TRUE(Find("##vp_scale")->Flags & ZUI_Disabled);
    EXPECT_TRUE(Find("##vp_snapscale")->Flags & ZUI_Disabled);
    EXPECT_TRUE(Find("##vp_space")->Flags & ZUI_Disabled);
    EXPECT_EQ(Find("##vp_space")->FirstChild->IconId, static_cast<uint16_t>(ZUIIcon::Box));
    EXPECT_FALSE(Find("##vp_move")->Flags & ZUI_Disabled);
    EXPECT_FALSE(Find("##vp_rot")->Flags & ZUI_Disabled);
    EXPECT_EQ(session.GizmoSettings().Space, TransformSpacePolicy::World);
    EXPECT_FALSE(Apply({.Kind = G::ChangeKind::Operation, .Mode = G::Operation::Scale}));
    EXPECT_EQ(session.GizmoSettings().Mode, G::Operation::Select);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Select(entity));
    session.EndMutationPhase();
    Frame();
    EXPECT_FALSE(Find("##vp_scale")->Flags & ZUI_Disabled);
    EXPECT_FALSE(Find("##vp_snapscale")->Flags & ZUI_Disabled);
    EXPECT_FALSE(Find("##vp_space")->Flags & ZUI_Disabled);
    EXPECT_EQ(Find("##vp_space")->FirstChild->IconId, static_cast<uint16_t>(ZUIIcon::Globe));
}

TEST_F(GizmoToolbarTest, GridPopupMatchesApprovedWidthChecksAndShowGridOwnership)
{
    Open("##vp_grid_menu");
    auto* popup = Find("##vp_grid_menu");
    ASSERT_NE(popup, nullptr);
    EXPECT_FLOAT_EQ(popup->ComputedSize[0], G::Constants::SnapMenuWidthPx);
    EXPECT_NE(Text("Snap to grid", popup), nullptr);
    EXPECT_NE(Text("Show grid", popup), nullptr);
    for (const auto& preset : G::SnapPresets(G::SnapTarget::Translation))
    {
        EXPECT_NE(Text(preset.Label, popup), nullptr);
    }
    const auto* selected = Find("##miex_ck_0.1");
    ASSERT_NE(selected, nullptr);
    EXPECT_TRUE(selected->Flags & ZUI_DrawIcon);
    EXPECT_EQ(selected->IconId, static_cast<uint16_t>(ZUIIcon::Check));
    ctx.FocusKey      = ZUIHashStr("##miex_Show grid", 16);
    ctx.KeyboardFocus = ctx.EnterPressed = true;
    Frame();
    EXPECT_EQ(ctx.PopupStackSize, 0u);
    EXPECT_TRUE(session.GizmoSettings().ShowGrid);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    session.EndMutationPhase();
    EXPECT_FALSE(session.GizmoSettings().ShowGrid);
    EXPECT_TRUE(session.GizmoSettings().TranslationSnap.Enabled);
}

TEST_F(GizmoToolbarTest, AngleAndScalePopupsContainAllApprovedValues)
{
    Open("##vp_angle_menu");
    auto* angle = Find("##vp_angle_menu");
    ASSERT_NE(angle, nullptr);
    for (const auto& preset : G::SnapPresets(G::SnapTarget::Rotation))
    {
        EXPECT_NE(Text(preset.Label, angle), nullptr);
    }
    EXPECT_NE(Text("Snap to angle", angle), nullptr);
    ZUIClosePopup(&ctx);
    Open("##vp_scale_menu");
    auto* scale = Find("##vp_scale_menu");
    ASSERT_NE(scale, nullptr);
    for (const auto& preset : G::SnapPresets(G::SnapTarget::Scale))
    {
        EXPECT_NE(Text(preset.Label, scale), nullptr);
    }
    EXPECT_NE(Text("Snap scale", scale), nullptr);
}

TEST_F(GizmoToolbarTest, PopupActivationSelectsIncrementWithoutEnablingSnapping)
{
    Open("##vp_scale_menu");
    ctx.FocusKey      = ZUIHashStr("##miex_0.5", 10);
    ctx.KeyboardFocus = ctx.EnterPressed = true;
    Frame();
    EXPECT_EQ(ctx.PopupStackSize, 0u);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    session.EndMutationPhase();
    EXPECT_FLOAT_EQ(session.GizmoSettings().ScaleSnap.Increment, .5f);
    EXPECT_FALSE(session.GizmoSettings().ScaleSnap.Enabled);
    Frame();
    EXPECT_NE(Text("0.5", Find("##vp_snapscale")), nullptr);
}

TEST_F(GizmoToolbarTest, SnapButtonActiveStylingDependsOnSnapNotGridVisibility)
{
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::GridVisibility, .Enabled = false}));
    Frame();
    const float active[4] = ZUI_TOKEN(Tokens::AccentBlue);
    const float idle[4]   = ZUI_TOKEN(Tokens::BorderField);
    for (int channel = 0; channel < 4; ++channel)
    {
        EXPECT_FLOAT_EQ(Find("##vp_grid")->BorderColor[channel], active[channel]);
        EXPECT_FLOAT_EQ(Find("##vp_snaprot")->BorderColor[channel], active[channel]);
        EXPECT_FLOAT_EQ(Find("##vp_snapscale")->BorderColor[channel], idle[channel]);
    }
    ASSERT_TRUE(Apply({.Kind = G::ChangeKind::SnapEnabled, .Target = G::SnapTarget::Translation, .Enabled = false}));
    Frame();
    for (int channel = 0; channel < 4; ++channel)
    {
        EXPECT_FLOAT_EQ(Find("##vp_grid")->BorderColor[channel], idle[channel]);
    }
    EXPECT_FALSE(session.GizmoSettings().ShowGrid);
}

TEST_F(GizmoToolbarTest, ClickingSnapControlOpensItsMenuUnderTheControlWithoutTogglingGrid)
{
    Frame();
    const auto* control = Find("##vp_grid");
    ASSERT_NE(control, nullptr);
    const float x     = control->ScreenMin[0];
    const float y     = control->ScreenMax[1];
    ctx.FocusKey      = control->Key;
    ctx.KeyboardFocus = ctx.EnterPressed = true;
    Frame();
    ASSERT_EQ(ctx.PopupStackSize, 1u);
    EXPECT_FLOAT_EQ(ctx.PopupStack[0].PosX, x);
    EXPECT_FLOAT_EQ(ctx.PopupStack[0].PosY, y);
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_TRUE(session.GizmoSettings().ShowGrid);
    Frame();
    ASSERT_NE(Find("##vp_grid_menu"), nullptr);
}
