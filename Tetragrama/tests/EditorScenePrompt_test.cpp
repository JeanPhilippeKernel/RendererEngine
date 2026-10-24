#include <Tetragrama/Editor.h>
#include <ZEngine/Controllers/FlyCameraController.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/UI/ZUIInput.h>
#include <ZEngine/UI/ZUITokens.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>
#include <memory>

using namespace Tetragrama;
using namespace ZEngine::UI;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

class EditorScenePromptTest : public testing::Test
{
protected:
    struct CaptureCamera : ZEngine::Controllers::FlyCameraController
    {
        ZUIInputCapture Capture = {}, Used = {};
        void            SetInputCapture(bool pointer, bool keyboard) override
        {
            Capture = {pointer, keyboard};
        }
        void Update(ZEngine::Core::TimeStep) override
        {
            Used = Capture;
        }
    } camera                                                                = {};
    ZEngine::Core::Memory::MemoryManager                      memory        = {};
    Scene                                                     world         = {};
    EntityID                                                  entity        = {};
    EditorScene                                               owner         = {};
    EditorConfiguration                                       configuration = {};
    std::unique_ptr<Editor>                                   editor        = {};
    std::unique_ptr<ZEngine::EngineContext>                   engine        = {};
    std::unique_ptr<ZEngine::Applications::AppRenderPipeline> pipeline      = {};
    ZUIContext                                                ctx           = {};
    ZUIGlyph                                                  glyphs[96]    = {};
    ZUIFont                                                   font          = {};
    ZUIFontAtlas                                              atlas         = {};

    void                                                      SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        world.Initialize(&memory.MainArena);
        entity = world.CreateEntity();
        world.AddComponent<UUIDComponent>(entity, {*uuids::uuid::from_string("143de14c-803b-4667-8597-48640b1ef024")});
        world.AddComponent<TransformComponent>(entity, {});
        editor = std::make_unique<Editor>();
        ASSERT_TRUE(editor->Session.Initialize(&memory.MainArena, {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 4096}));
        ASSERT_TRUE(editor->Session.BeginMutationPhase());
        ASSERT_TRUE(editor->Session.BindScene(&world));
        const auto target = editor->Session.TargetFor(entity);
        ASSERT_TRUE(editor->Session.BeginTransform({&target, 1}));
        LocalTransformValue value = {};
        value.Position.x          = 5.f;
        ASSERT_TRUE(editor->Session.PreviewTransforms({&value, 1}));
        ASSERT_TRUE(editor->Session.Commit());
        editor->Session.EndMutationPhase();
        owner.Name            = "Dirty scene";
        owner.Session         = &editor->Session;
        editor->CurrentScene  = &owner;
        editor->Configuration = &configuration;
        engine                = std::make_unique<ZEngine::EngineContext>();
        engine->Scene         = &world;
        pipeline              = std::make_unique<ZEngine::Applications::AppRenderPipeline>();
        pipeline->ZUICtx      = &ctx;
        pipeline->OverlayBuildAvailable.value.store(false);
        editor->RenderPipeline   = pipeline.get();
        editor->CameraController = &camera;
        ZUIContextInit(&ctx, &memory.MainArena, ZMega(1), ZKilo(128), 256, 128);
        ctx.ScreenW          = 1024;
        ctx.ScreenH          = 768;
        ctx.ViewportInputKey = 42;
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
        ASSERT_TRUE(editor->RequestNewScene());
        ASSERT_TRUE(Process());
        ASSERT_TRUE(editor->SceneChangePromptState().Visible());
    }

    void TearDown() override
    {
        editor->OnClosing();
        editor->CameraController = nullptr;
        editor->RenderPipeline   = nullptr;
        editor->CurrentScene     = nullptr;
        ZUIContextDestroy(&ctx);
        world.Shutdown();
        memory.Shutdown();
    }

    bool Process()
    {
        if (!editor->Session.BeginMutationPhase())
        {
            return false;
        }
        const bool result = editor->ProcessNewScene(*engine);
        editor->Session.EndMutationPhase();
        return result;
    }

    void Frame()
    {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "##root", ZPx(float(ctx.ScreenW)), ZPx(float(ctx.ScreenH)));
        ctx.ViewportInputKey = ZUIButton(&ctx, "Background##background").Key;
        ZUIEndColumn(&ctx);
        editor->OnRenderUI();
        ZUIEndFrame(&ctx);
    }

    uint64_t Key(const char* key)
    {
        return ZUIHashStr(key, uint32_t(ZEngine::Helpers::secure_strlen(key)));
    }

    ZUIBox* Find(const char* key)
    {
        const auto hash = Key(key);
        for (auto* box = ctx.Root; box;)
        {
            if (box->Key == hash)
            {
                return box;
            }
            if (box->FirstChild)
            {
                box = box->FirstChild;
            }
            else
            {
                while (box && !box->NextSib)
                {
                    box = box->Parent;
                }
                if (box)
                {
                    box = box->NextSib;
                }
            }
        }
        return nullptr;
    }

    void Activate(const char* key)
    {
        ctx.FocusKey      = Key(key);
        ctx.KeyboardFocus = ctx.EnterPressed = true;
        Frame();
    }
};

TEST_F(EditorScenePromptTest, ExistingModalAndButtonRecipeFitsAndTrapsKeyboardFocus)
{
    ctx.FocusKey = Key("Background##background");
    Frame();
    Frame();
    auto* panel = Find("##editor_scene_change");
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(ctx.ModalBox, panel);
    EXPECT_EQ(ctx.ActiveModalKey, panel->Key);
    EXPECT_FLOAT_EQ(panel->ComputedSize[0], 288.f);
    EXPECT_EQ(panel->ShadowKind, ZUIShadowKind::Modal);
    EXPECT_FLOAT_EQ(panel->BorderColor[0], Tokens::BorderStrong.R);
    EXPECT_EQ(ctx.FocusKey, Key("Save##scene_change_save"));
    for (const char* key : {"Cancel##scene_change_cancel", "Discard##scene_change_discard", "Save##scene_change_save"})
    {
        const auto* button = Find(key);
        ASSERT_NE(button, nullptr);
        EXPECT_FLOAT_EQ(button->ComputedSize[1], Metrics::HeightControl);
        EXPECT_GE(button->ScreenMin[0], panel->ScreenMin[0] + panel->Padding[0]);
        EXPECT_LE(button->ScreenMax[0], panel->ScreenMax[0] - panel->Padding[2] + .01f);
        EXPECT_LE(button->ScreenMax[1], panel->ScreenMax[1] - panel->Padding[3] + .01f);
    }
}

TEST_F(EditorScenePromptTest, ConsentCapturesBeforeTheFirstOverlayAndReleasesWithoutOne)
{
    ASSERT_EQ(ctx.Root, nullptr);
    const auto frame = ctx.FrameIndex;
    editor->Update(1.f / 60.f);
    EXPECT_TRUE(camera.Used.Pointer);
    EXPECT_TRUE(camera.Used.Keyboard);
    EXPECT_NE(ctx.ActiveModalKey, 0u);
    ASSERT_TRUE(editor->ChooseSceneChange(SceneChangeChoice::Cancel, editor->SceneChangePromptState().Serial));
    ASSERT_TRUE(Process());
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
    EXPECT_EQ(ctx.ActiveModalKey, 0u);
    EXPECT_EQ(ctx.FrameIndex, frame);
    EXPECT_EQ(ctx.Root, nullptr);
}

TEST_F(EditorScenePromptTest, DiscardButtonOnlyQueuesIntentUntilTheMutationPhase)
{
    Frame();
    Frame();
    const auto epoch = editor->Session.SceneEpoch();
    Activate("Discard##scene_change_discard");
    EXPECT_EQ(world.AliveCount(), 1u);
    EXPECT_EQ(editor->Session.SceneEpoch(), epoch);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    ASSERT_TRUE(Process());
    EXPECT_EQ(world.AliveCount(), 0u);
    EXPECT_NE(editor->Session.SceneEpoch(), epoch);
}

TEST_F(EditorScenePromptTest, EscapeReleasesOwnedModalCaptureWithoutAnyNewOverlay)
{
    Frame();
    editor->Update(1.f / 60.f);
    ASSERT_TRUE(camera.Used.Pointer);
    ASSERT_TRUE(camera.Used.Keyboard);
    const auto ui_frame = ctx.FrameIndex;
    ctx.EscapePressed   = true;
    Frame();
    ASSERT_TRUE(Process());
    ZEngine::Applications::OverlayPayload* overlay = nullptr;
    uint32_t                               slot    = 0;
    ASSERT_FALSE(pipeline->BeginOverlayWrite(overlay, slot));
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
    EXPECT_EQ(ctx.ActiveModalKey, 0u);
    EXPECT_EQ(ctx.ModalBox, nullptr);
    EXPECT_EQ(ctx.FrameIndex, ui_frame + 1);
    EXPECT_EQ(world.AliveCount(), 1u);
    EXPECT_TRUE(owner.IsDirty());
    EXPECT_FALSE(ctx.EscapePressed);
}

TEST_F(EditorScenePromptTest, EscapeCancelsBeforeAnyOverlayIsBuilt)
{
    const auto frame  = ctx.FrameIndex;
    ctx.EscapePressed = true;
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(ctx.EscapePressed);
    EXPECT_TRUE(editor->SceneChangePromptState().Visible()); // intent only
    ASSERT_TRUE(Process());
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->SceneChangePromptState().Visible());
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
    EXPECT_EQ(ctx.FrameIndex, frame);
    EXPECT_EQ(ctx.Root, nullptr);
    EXPECT_EQ(world.AliveCount(), 1u);
    EXPECT_TRUE(owner.IsDirty());
}

TEST_F(EditorScenePromptTest, EscapeNeverCancelsAConfirmationBehindAnotherModal)
{
    const auto foreign = Key("foreign-modal");
    ctx.ActiveModalKey = foreign;
    ctx.EscapePressed  = true;
    editor->Update(1.f / 60.f);
    ASSERT_TRUE(Process());
    EXPECT_TRUE(ctx.EscapePressed);
    EXPECT_EQ(ctx.ActiveModalKey, foreign);
    EXPECT_TRUE(editor->SceneChangePromptState().Visible());
    EXPECT_EQ(world.AliveCount(), 1u);
    EXPECT_TRUE(owner.IsDirty());
}

TEST_F(EditorScenePromptTest, ReplacementEpochRejectsQueuedConsentAndReleasesOwnedCapture)
{
    Frame();
    editor->Update(1.f / 60.f);
    const auto prompt = editor->SceneChangePromptState();
    ASSERT_TRUE(editor->ChooseSceneChange(SceneChangeChoice::Discard, prompt.Serial));
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->Session.BindScene(nullptr));
    ASSERT_TRUE(editor->Session.BindScene(&world));
    editor->Session.EndMutationPhase();
    EXPECT_FALSE(Process()); // stale New request cannot touch the replacement
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->SceneChangePromptState().Visible());
    EXPECT_FALSE(editor->ChooseSceneChange(SceneChangeChoice::Discard, prompt.Serial));
    EXPECT_EQ(ctx.ActiveModalKey, 0u);
    EXPECT_EQ(ctx.ModalBox, nullptr);
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
    EXPECT_EQ(world.AliveCount(), 1u);
    EXPECT_FLOAT_EQ(world.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
}

TEST_F(EditorScenePromptTest, OutsideClickCannotDismissOrAuthorizeThePrompt)
{
    Frame();
    ZUIFeedMousePos(&ctx, 2.f, 2.f);
    ZUIFeedMouseButton(&ctx, 0, true);
    Frame();
    ZUIFeedMouseButton(&ctx, 0, false);
    Frame();
    ASSERT_TRUE(Process());
    EXPECT_TRUE(editor->SceneChangePromptState().Visible());
    EXPECT_EQ(world.AliveCount(), 1u);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
}

TEST_F(EditorScenePromptTest, SavingDisablesSaveAndDiscardButCancelPreservesTheScene)
{
    owner.SourcePath = ZEngine::Core::VFS::VFSPath::Parse("/Scenes/main.zescene").Value();
    Frame();
    Frame();
    Activate("Save##scene_change_save");
    ASSERT_TRUE(Process());
    ASSERT_TRUE(editor->SceneChangePromptState().Saving);
    Frame();
    for (const char* key : {"Discard##scene_change_discard", "Save##scene_change_save"})
    {
        const auto* button = Find(key);
        ASSERT_NE(button, nullptr);
        EXPECT_NE(button->Flags & ZUI_Disabled, 0u);
    }
    Activate("Cancel##scene_change_cancel");
    ASSERT_TRUE(Process());
    EXPECT_FALSE(editor->SceneChangePromptState().Visible());
    EXPECT_EQ(world.AliveCount(), 1u);
    EXPECT_TRUE(owner.IsDirty());
}

TEST_F(EditorScenePromptTest, AnotherModalOwnerIsNeverReplacedOrCleared)
{
    const auto foreign = Key("foreign-modal");
    ctx.ActiveModalKey = foreign;
    Frame();
    EXPECT_EQ(ctx.ActiveModalKey, foreign);
    EXPECT_EQ(Find("##editor_scene_change"), nullptr);
    ASSERT_TRUE(editor->ChooseSceneChange(SceneChangeChoice::Cancel, editor->SceneChangePromptState().Serial));
    ASSERT_TRUE(Process());
    editor->Update(1.f / 60.f);
    EXPECT_EQ(ctx.ActiveModalKey, foreign);
}

TEST_F(EditorScenePromptTest, BackgroundPopupStateIsRetiredWhenThePromptAcquiresModalOwnership)
{
    ctx.PopupStackSize    = 1;
    ctx.PopupStack[0].Key = Key("background-menu");
    ctx.PendingPopupKey   = Key("pending-menu");
    Frame();
    EXPECT_EQ(ctx.PopupStackSize, 0u);
    EXPECT_EQ(ctx.PendingPopupKey, 0u);
    EXPECT_EQ(ctx.ActiveModalKey, Key("##editor_scene_change"));
    EXPECT_NE(ctx.ModalBox, nullptr);
}

TEST_F(EditorScenePromptTest, SaveFailureKeepsThePromptAndShowsTheSharedErrorToken)
{
    owner.SourcePath = ZEngine::Core::VFS::VFSPath::Parse("/Scenes/main.zescene").Value();
    Frame();
    Frame();
    Activate("Save##scene_change_save");
    ASSERT_TRUE(Process());
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    EXPECT_FALSE(editor->ProcessSceneSave(*engine)); // deliberately missing I/O services
    editor->Session.EndMutationPhase();
    Frame();
    const auto* status = Find("##scene_change_status");
    ASSERT_NE(status, nullptr);
    EXPECT_FLOAT_EQ(status->TextColor[0], Tokens::AccentRed.R);
    EXPECT_TRUE(editor->SceneChangePromptState().SaveFailed);
    EXPECT_FALSE(editor->SceneChangePromptState().Saving);
    EXPECT_EQ(world.AliveCount(), 1u);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_TRUE(owner.IsDirty());
}

TEST_F(EditorScenePromptTest, RebuildingPromptReusesPersistentStateWithoutChangingDocumentOrHistory)
{
    Frame();
    Frame();
    const auto bytes    = ctx.PersistentArena.m_current_offset;
    const auto states   = ctx.StateStore.Count;
    const auto revision = editor->Session.DocumentRevision();
    const auto serial   = editor->SceneChangePromptState().Serial;
    for (uint32_t frame = 0; frame < 100; ++frame)
    {
        Frame();
    }
    EXPECT_EQ(ctx.PersistentArena.m_current_offset, bytes);
    EXPECT_EQ(ctx.StateStore.Count, states);
    EXPECT_EQ(editor->Session.DocumentRevision(), revision);
    EXPECT_EQ(editor->SceneChangePromptState().Serial, serial);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
}

TEST_F(EditorScenePromptTest, CloseConsentCapturesAndEscapeCancelsWithoutAnotherOverlay)
{
    ASSERT_TRUE(editor->ChooseSceneChange(SceneChangeChoice::Cancel, editor->SceneChangePromptState().Serial));
    ASSERT_TRUE(Process());
    EXPECT_FALSE(editor->OnCloseRequested());
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->ProcessCloseRequest(*engine));
    editor->Session.EndMutationPhase();
    EXPECT_EQ(editor->SceneChangePromptState().Kind, SceneChangeKind::CloseEditor);
    editor->Update(1.f / 60.f);
    EXPECT_TRUE(camera.Used.Pointer);
    EXPECT_TRUE(camera.Used.Keyboard);
    const auto frame  = ctx.FrameIndex;
    ctx.EscapePressed = true;
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(ctx.EscapePressed);
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->ProcessCloseRequest(*engine));
    editor->Session.EndMutationPhase();
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->SceneChangePromptState().Visible());
    EXPECT_FALSE(engine->CloseRequested.value.load());
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
    EXPECT_EQ(ctx.FrameIndex, frame);
    EXPECT_EQ(world.AliveCount(), 1u);
    EXPECT_TRUE(owner.IsDirty());
}
