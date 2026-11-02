#include <Tetragrama/Panels/DirectoryListing.h>
#include <Tetragrama/Panels/FolderTreeCache.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <gtest/gtest.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace ZEngine::Core::VFS;
using namespace Tetragrama::Panels;

namespace
{
    struct ManualExecutor
    {
        void* Context           = nullptr;
        void (*Run)(void*)      = nullptr;
        uint32_t    Submissions = 0;
        bool        Reject      = false;
        static bool Submit(void* executor, void* context, void (*run)(void*))
        {
            auto& self = *static_cast<ManualExecutor*>(executor);
            ++self.Submissions;
            if (self.Reject)
                return false;
            EXPECT_EQ(self.Run, nullptr); // One accepted lease, never an unbounded queue.
            self.Context = context;
            self.Run     = run;
            return true;
        }
        void Complete()
        {
            auto run = Run;
            Run      = nullptr;
            ASSERT_NE(run, nullptr);
            run(Context);
        }
    };

    struct ListingVFS : VFSContext
    {
        std::vector<std::string>                                 Paths;
        bool                                                     Fail  = false;
        uint32_t                                                 Calls = 0;
        VFSResult<ZEngine::Core::Containers::Array<VFSDirEntry>> List(const VFSPath& parent, ZEngine::Core::Memory::ArenaAllocator* arena) override
        {
            using Array = ZEngine::Core::Containers::Array<VFSDirEntry>;
            ++Calls;
            if (Fail)
                return VFSResult<Array>::Fail(VFSError::IOError);
            Array raw;
            raw.init(arena, Paths.size() + 1);
            for (const auto& text : Paths)
            {
                auto path = VFSPath::Parse(text.c_str()).Value();
                if (path.Parent() == parent)
                    raw.push({{}, path, true});
            }
            return VFSResult<Array>::Ok(std::move(raw));
        }
    };

    struct DirectoryListingTest : testing::Test
    {
        ManualExecutor   Executor;
        ListingVFS       VFS;
        DirectoryListing Listing;
        void             SetUp() override
        {
            Listing.SetSubmitter(&ManualExecutor::Submit, &Executor);
        }
        void TearDown() override
        {
            Listing.Close();
            if (Executor.Run)
                Executor.Complete();
            Listing.ShutdownAfterWorkersJoined();
        }
        void Request(const char* path)
        {
            Listing.Request(VFS, VFSPath::Parse(path).Value());
        }
    };
} // namespace

TEST_F(DirectoryListingTest, ListingAndNaturalSortRunOnlyOnAcceptedWorker)
{
    VFS.Paths = {"/Assets/Folder10", "/Assets/Folder2", "/Assets/.hidden"};
    Request("/Assets");
    EXPECT_EQ(Listing.Poll(), nullptr);
    EXPECT_EQ(VFS.Calls, 0u);
    EXPECT_EQ(Listing.GetStatus(), DirectoryListing::Status::Loading);
    for (int i = 0; i < 100; ++i)
        EXPECT_EQ(Listing.Poll(), nullptr);
    EXPECT_EQ(Executor.Submissions, 1u);
    Executor.Complete();
    const auto* result = Listing.Poll();
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->Succeeded);
    ASSERT_EQ(result->Count, 2u);
    EXPECT_STREQ(result->Entries[0].name, "Folder2");
    EXPECT_STREQ(result->Entries[1].name, "Folder10");
    EXPECT_EQ(Listing.Poll(), nullptr); // Publish once; idle does no IO or submit.
    EXPECT_EQ(VFS.Calls, 1u);
}

TEST_F(DirectoryListingTest, SaturationRetriesWithoutInlineIO)
{
    Executor.Reject = true;
    Request("/Assets");
    for (int i = 0; i < 10; ++i)
        EXPECT_EQ(Listing.Poll(), nullptr);
    EXPECT_EQ(VFS.Calls, 0u);
    EXPECT_EQ(Listing.GetStatus(), DirectoryListing::Status::Loading);
    Executor.Reject = false;
    Listing.Poll();
    Executor.Complete();
    ASSERT_NE(Listing.Poll(), nullptr);
    EXPECT_EQ(VFS.Calls, 1u);
}

TEST_F(DirectoryListingTest, CompletedOldFolderIsNeverPublishedAndNavigationCoalesces)
{
    VFS.Paths = {"/Old/old", "/Latest/current"};
    Request("/Old");
    Listing.Poll();
    Executor.Complete(); // Completion arrives before UI sees navigation.
    Request("/Skipped");
    Request("/Latest");
    EXPECT_EQ(Listing.Poll(), nullptr); // Discard Old; submit Latest, not Skipped.
    Executor.Complete();
    const auto* result = Listing.Poll();
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->Count, 1u);
    EXPECT_STREQ(result->Entries[0].full_path, "/Latest/current");
    EXPECT_EQ(Executor.Submissions, 2u);
    EXPECT_EQ(VFS.Calls, 2u);
}

TEST_F(DirectoryListingTest, QueuedStaleRequestsSkipIOAndRepeatedRefreshesCoalesce)
{
    Request("/Assets");
    Listing.Poll();
    for (int i = 0; i < 100; ++i)
    {
        Request("/Assets");
        EXPECT_EQ(Listing.Poll(), nullptr);
    }
    EXPECT_EQ(Executor.Submissions, 1u);
    Executor.Complete();
    EXPECT_EQ(VFS.Calls, 0u);
    Listing.Poll();
    Executor.Complete();
    ASSERT_NE(Listing.Poll(), nullptr);
    EXPECT_EQ(Executor.Submissions, 2u);
    EXPECT_EQ(VFS.Calls, 1u);
}

TEST_F(DirectoryListingTest, FailureIsNotEmptyAndRequiresExplicitRetry)
{
    VFS.Fail = true;
    Request("/Assets");
    Listing.Poll();
    Executor.Complete();
    const auto* failed = Listing.Poll();
    ASSERT_NE(failed, nullptr);
    EXPECT_FALSE(failed->Succeeded);
    EXPECT_EQ(Listing.GetStatus(), DirectoryListing::Status::Failed);
    for (int i = 0; i < 100; ++i)
        Listing.Poll();
    EXPECT_EQ(VFS.Calls, 1u);
    VFS.Fail = false;
    Request("/Assets");
    Listing.Poll();
    Executor.Complete();
    const auto* empty = Listing.Poll();
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->Succeeded);
    EXPECT_EQ(empty->Count, 0u);
    EXPECT_EQ(Listing.GetStatus(), DirectoryListing::Status::Ready);
}

TEST_F(DirectoryListingTest, CloseCancelsQueuedWorkAndRetainsLeaseUntilWorkersJoin)
{
    Request("/Assets");
    Listing.Poll();
    Listing.Close();
    Request("/Other");
    EXPECT_EQ(Listing.Poll(), nullptr);
    Executor.Complete();
    EXPECT_EQ(VFS.Calls, 0u);
    Listing.ShutdownAfterWorkersJoined();
}

TEST_F(DirectoryListingTest, JoinedShutdownReclaimsAcceptedButNeverStartedWork)
{
    Request("/Assets");
    Listing.Poll();
    // Simulate a pool which discards a queued task on shutdown. The owner, not
    // an unexecuted heap closure, owns and can reclaim all request storage.
    Executor.Run = nullptr;
    Listing.ShutdownAfterWorkersJoined();
    EXPECT_EQ(Listing.Poll(), nullptr);
    EXPECT_EQ(VFS.Calls, 0u);
}

TEST(FolderTreeAsyncTest, PendingRootDoesNotAdvanceSearchAndRefreshRejectsOldChildren)
{
    ManualExecutor executor;
    ListingVFS     vfs;
    auto           tree = std::make_unique<FolderTreeCache>();
    tree->Listings.SetSubmitter(&ManualExecutor::Submit, &executor);
    vfs.Paths = {"/Old"};
    tree->UpdateSearch(vfs, "new");
    EXPECT_EQ(tree->Count, 1u);
    EXPECT_TRUE(tree->Searching());
    EXPECT_FALSE(tree->Nodes[0].Loaded);
    executor.Complete();
    tree->Invalidate();
    vfs.Paths = {"/New"};
    tree->UpdateSearch(vfs, "new");
    EXPECT_EQ(tree->Count, 1u);
    executor.Complete();
    tree->UpdateSearch(vfs, "new");
    EXPECT_EQ(tree->Count, 2u);
    EXPECT_STREQ(tree->Nodes[1].Path, "/New");
    EXPECT_TRUE(tree->Nodes[1].Visible);
    executor.Complete(); // Finish New's empty child listing.
    tree->UpdateSearch(vfs, "new");
    EXPECT_FALSE(tree->Searching());
    tree->Listings.ShutdownAfterWorkersJoined();
}

TEST(DirectoryListingWorkerTest, BlockedFilesystemNeverBlocksPollingOrNavigation)
{
    struct GatedVFS : ListingVFS
    {
        std::mutex                                               Mutex;
        std::condition_variable                                  CV;
        bool                                                     Entered = false;
        bool                                                     Open    = false;
        std::thread::id                                          Thread;
        VFSResult<ZEngine::Core::Containers::Array<VFSDirEntry>> List(const VFSPath& path, ZEngine::Core::Memory::ArenaAllocator* arena) override
        {
            {
                std::unique_lock lock(Mutex);
                Thread  = std::this_thread::get_id();
                Entered = true;
                CV.notify_all();
                CV.wait(lock, [&] { return Open; });
            }
            return ListingVFS::List(path, arena);
        }
    } vfs;
    ZEngine::Helpers::ThreadPool pool(2);
    DirectoryListing             listing;
    listing.SetSubmitter([](void* pool, void* context, void (*run)(void*)) { return static_cast<ZEngine::Helpers::ThreadPool*>(pool)->Submit(context, run); }, &pool);
    vfs.Paths = {"/Latest/current"};
    listing.Request(vfs, VFSPath::Parse("/Old").Value());
    EXPECT_EQ(listing.Poll(), nullptr);
    {
        std::unique_lock lock(vfs.Mutex);
        EXPECT_TRUE(vfs.CV.wait_for(lock, std::chrono::seconds(5), [&] { return vfs.Entered; }));
        EXPECT_NE(vfs.Thread, std::this_thread::get_id());
    }
    const auto start = std::chrono::steady_clock::now();
    listing.Request(vfs, VFSPath::Parse("/Latest").Value());
    for (int i = 0; i < 100; ++i)
        EXPECT_EQ(listing.Poll(), nullptr);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(100));
    {
        std::lock_guard lock(vfs.Mutex);
        vfs.Open = true;
        vfs.CV.notify_all();
    }
    const DirectoryListing::Result* result   = nullptr;
    const auto                      deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!(result = listing.Poll()) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_NE(result, nullptr);
    if (result)
    {
        EXPECT_TRUE(result->Succeeded);
        EXPECT_EQ(result->Count, 1u);
        EXPECT_STREQ(result->Entries[0].full_path, "/Latest/current");
    }
    listing.Close();
    pool.Shutdown();
    listing.ShutdownAfterWorkersJoined();
}
