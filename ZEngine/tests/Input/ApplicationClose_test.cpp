#include <ZEngine/Engine.h>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

namespace
{
    struct CloseApplication : ZEngine::Applications::GameApplication
    {
        uint32_t        Requests = 0;
        bool            Approve  = true;
        std::thread::id Caller   = {};
        void            OverrideWindowConfiguration() override {}
        void            OnInitializing() override {}
        void            OnInitialized() override {}
        void            OnEvent(ZEngine::Core::CoreEvent&) override {}
        void            OnUpdate(float) override {}
        void            OnPreRender() override {}
        void            OnPostRender() override {}
        void            OnRenderUI() override {}
        void            OnClosing() override {}
        void            OnClosed() override {}
        bool            OnCloseRequested() override
        {
            ++Requests;
            Caller = std::this_thread::get_id();
            return Approve;
        }
    };

    class ApplicationCloseTest : public testing::Test
    {
    protected:
        std::unique_ptr<ZEngine::EngineContext> context = std::make_unique<ZEngine::EngineContext>();
        CloseApplication                        app     = {};
        void                                    SetUp() override
        {
            context->App = &app;
        }
    };
} // namespace

TEST_F(ApplicationCloseTest, EmptyRequestDoesNotInvokeTheApplicationOrStopRendering)
{
    EXPECT_FALSE(ZEngine::Engine::ProcessApplicationCloseRequest(*context));
    EXPECT_EQ(app.Requests, 0u);
    EXPECT_FALSE(context->CloseRequested.value.load());
    EXPECT_FALSE(context->RequestTerminate.value.load());
}

TEST_F(ApplicationCloseTest, DefaultApplicationHookAllowsNonEditorApplicationsToClose)
{
    EXPECT_TRUE(app.GameApplication::OnCloseRequested());
    EXPECT_EQ(app.Requests, 0u);
}

TEST_F(ApplicationCloseTest, ApprovedRequestIsConsumedOnce)
{
    context->ApplicationCloseRequested.value.store(true);
    EXPECT_TRUE(ZEngine::Engine::ProcessApplicationCloseRequest(*context));
    EXPECT_EQ(app.Requests, 1u);
    EXPECT_FALSE(context->ApplicationCloseRequested.value.load());
    EXPECT_TRUE(context->CloseRequested.value.load());
    EXPECT_TRUE(ZEngine::Engine::ProcessApplicationCloseRequest(*context));
    EXPECT_EQ(app.Requests, 1u);
}

TEST_F(ApplicationCloseTest, DeferredRequestLeavesBothLoopsAliveAndDoesNotRepeatItself)
{
    app.Approve = false;
    context->ApplicationCloseRequested.value.store(true);
    EXPECT_FALSE(ZEngine::Engine::ProcessApplicationCloseRequest(*context));
    EXPECT_EQ(app.Requests, 1u);
    for (uint32_t frame = 0; frame < 100; ++frame)
        EXPECT_FALSE(ZEngine::Engine::ProcessApplicationCloseRequest(*context));
    EXPECT_EQ(app.Requests, 1u);
    EXPECT_FALSE(context->CloseRequested.value.load());
    EXPECT_FALSE(context->RequestTerminate.value.load());
    app.Approve = true;
    context->ApplicationCloseRequested.value.store(true);
    EXPECT_TRUE(ZEngine::Engine::ProcessApplicationCloseRequest(*context));
    EXPECT_EQ(app.Requests, 2u);
}

TEST_F(ApplicationCloseTest, ForcedShutdownNeverInvokesOrWaitsForTheApplication)
{
    app.Approve = false;
    context->ApplicationCloseRequested.value.store(true);
    context->CloseRequested.value.store(true);
    EXPECT_TRUE(ZEngine::Engine::ProcessApplicationCloseRequest(*context));
    EXPECT_EQ(app.Requests, 0u);
}

TEST_F(ApplicationCloseTest, MissingApplicationUsesDefaultImmediateApproval)
{
    context->App = nullptr;
    context->ApplicationCloseRequested.value.store(true);
    EXPECT_TRUE(ZEngine::Engine::ProcessApplicationCloseRequest(*context));
    EXPECT_TRUE(context->CloseRequested.value.load());
}

TEST_F(ApplicationCloseTest, WorkerIntentIsCoalescedAndDeliveredOnlyByTheMainThreadPump)
{
    app.Approve = false;
    std::thread worker([&] {
        for (uint32_t request = 0; request < 1000; ++request)
            context->ApplicationCloseRequested.value.store(true, std::memory_order_release);
    });
    worker.join();
    EXPECT_EQ(app.Requests, 0u);
    EXPECT_FALSE(ZEngine::Engine::ProcessApplicationCloseRequest(*context));
    EXPECT_EQ(app.Requests, 1u);
    EXPECT_EQ(app.Caller, std::this_thread::get_id());
}

TEST_F(ApplicationCloseTest, ApplicationDeferralCannotUndoAConcurrentForcedShutdown)
{
    struct ForceDuringRequest : CloseApplication
    {
        ZEngine::EngineContext* Context = nullptr;
        bool                    OnCloseRequested() override
        {
            ++Requests;
            Context->CloseRequested.value.store(true, std::memory_order_release);
            return false;
        }
    } forced       = {};
    forced.Context = context.get();
    context->App   = &forced;
    context->ApplicationCloseRequested.value.store(true);
    EXPECT_TRUE(ZEngine::Engine::ProcessApplicationCloseRequest(*context));
    EXPECT_EQ(forced.Requests, 1u);
    EXPECT_TRUE(context->CloseRequested.value.load());
}
