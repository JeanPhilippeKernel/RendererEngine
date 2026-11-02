#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSScanner.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include "../Misc/SaturatedBackgroundPool.h"

using ZEngine::Core::Memory::MemoryManager;

namespace ZEngine::Core::VFS
{
    // Keeps watcher ingress private in production while allowing this focused
    // regression test to model a native event delivered after publication end.
    struct VFSContextTestAccess
    {
        static void Configure(VFSContext& context, const char* project_root, FileChangeListener listener, void* listener_context)
        {
            Helpers::secure_strcpy(context.m_project_root_native, sizeof(context.m_project_root_native), project_root);
            context.m_file_change_listener = listener;
            context.m_file_change_context  = listener_context;
        }

        static void Deliver(VFSContext& context, const VFSWatchEvent& event)
        {
            context.HandleWatchEvent(event);
        }

        static VFSResult<VFSPath> Resolve(VFSContext& context, const char* native)
        {
            return context.ToRelativeVFSPath(native);
        }

        static void SetRegistry(VFSContext& context, AssetRegistry* registry)
        {
            context.m_registry = registry;
        }

        static void SetScanner(VFSContext& context, VFSScanner* scanner, VFSDirectoryCache* cache)
        {
            context.m_scanner = scanner;
            context.m_directory_cache = cache;
        }

        static bool RescanRequested(const VFSContext& context)
        {
            return context.m_rescan_requested;
        }
    };
} // namespace ZEngine::Core::VFS

namespace
{
#if defined(_WIN32)
    constexpr const char* ProjectRootNative  = "C:\\project";
    constexpr const char* ArtifactPathNative = "C:\\project\\Assets\\Meshes\\robot.zemesh";
    constexpr const char* OtherPathNative    = "C:\\project\\Assets\\Meshes\\other.zemesh";
    constexpr const char* SiblingRootNative  = "C:\\project2\\Assets\\Meshes\\robot.zemesh";
    constexpr const char* CachePathNative    = "C:\\project\\.zodiacengine\\cache\\thumbnails\\preview.png";
#else
    constexpr const char* ProjectRootNative  = "/project";
    constexpr const char* ArtifactPathNative = "/project/Assets/Meshes/robot.zemesh";
    constexpr const char* OtherPathNative    = "/project/Assets/Meshes/other.zemesh";
    constexpr const char* SiblingRootNative  = "/project2/Assets/Meshes/robot.zemesh";
    constexpr const char* CachePathNative    = "/project/.zodiacengine/cache/thumbnails/preview.png";
#endif

    struct ListenerCounter
    {
        int                                Count    = 0;
        ZEngine::Core::VFS::VFSPath        LastPath = {};
        ZEngine::Core::VFS::WatchEventKind LastKind = {};
    };

    void CountEvent(void* context, const ZEngine::Core::VFS::VFSPath& path, ZEngine::Core::VFS::WatchEventKind kind)
    {
        auto& listener = *static_cast<ListenerCounter*>(context);
        ++listener.Count;
        listener.LastPath = path;
        listener.LastKind = kind;
    }

    ZEngine::Core::VFS::VFSWatchEvent MakeEvent(const char* path)
    {
        ZEngine::Core::VFS::VFSWatchEvent event{};
        ZEngine::Helpers::secure_strcpy(event.Path, sizeof(event.Path), path);
        event.Kind                  = ZEngine::Core::VFS::WatchEventKind::Modified;
        event.ObservedAtNanoseconds = ZEngine::Core::VFS::VFSWatchTimestampNowNanoseconds();
        return event;
    }
} // namespace

TEST(VFSScanAdmissionTest, RetriesSaturatedRootOnTickWithoutLosingTheRequest)
{
    using namespace ZEngine::Core::VFS;
    MemoryManager memory;
    memory.Initialize(ZGiga(1), {});
    VFSContext context;
    context.Initialize(&memory.MainArena);
    VFSMemoryBackend backend;
    backend.Initialize(&memory.MainArena);
    ASSERT_TRUE(context.Mount(&backend, VFSPath::Root(), 0).Succeeded());
    VFSDirectoryCache cache;
    cache.Initialize(&memory.MainArena);
    VFSScanner scanner;
    scanner.Initialize(&memory.MainArena);
    std::atomic<bool> complete = false;
    scanner.SetOnScanComplete(&complete, +[](void* ctx, ScanStats) { static_cast<std::atomic<bool>*>(ctx)->store(true); });
    VFSContextTestAccess::SetScanner(context, &scanner, &cache);
    SaturatedBackgroundPool background;
    ASSERT_TRUE(background.Fill());
    context.ScanProject();
    EXPECT_TRUE(VFSContextTestAccess::RescanRequested(context));
    EXPECT_FALSE(scanner.IsScanning());
    context.Tick();
    EXPECT_TRUE(VFSContextTestAccess::RescanRequested(context));
    EXPECT_FALSE(complete.load());
    background.Release();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (VFSContextTestAccess::RescanRequested(context) && std::chrono::steady_clock::now() < deadline)
    {
        context.Tick();
        std::this_thread::yield();
    }
    EXPECT_FALSE(VFSContextTestAccess::RescanRequested(context));
    while (!complete.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    EXPECT_TRUE(complete.load());
    scanner.Cancel();
    while (scanner.IsScanning())
        std::this_thread::yield();
    context.ShutdownWatcher();
}

TEST(VFSImportPublicationTest, SuppressesArtifactEventDeliveredAfterPublicationEnds)
{
    MemoryManager manager;

    manager.Initialize(ZMega(2), {});
    {
        ZEngine::Core::VFS::VFSContext context;
        ListenerCounter                listener;

        context.Initialize(&manager.MainArena);
        ZEngine::Core::VFS::VFSContextTestAccess::Configure(context, ProjectRootNative, &CountEvent, &listener);

        const auto artifact = ZEngine::Core::VFS::VFSPath::Parse("/Assets/Meshes/robot.zemesh");
        ASSERT_TRUE(artifact.Succeeded());

        const ZEngine::Core::VFS::VFSImportPublication publication = context.BeginImportPublication();
        context.RecordImportPublicationArtifact(publication, artifact.Value());
        context.EndImportPublication(publication);

        // The raw event is observed after EndImportPublication. It must still be
        // absorbed by the publication grace period instead of notifying the import
        // coordinator through the generic file-change listener.
        ZEngine::Core::VFS::VFSContextTestAccess::Deliver(context, MakeEvent(ArtifactPathNative));
        EXPECT_EQ(listener.Count, 0);

        // The grace period only suppresses artifacts owned by this import. Other
        // project edits retain the normal watcher behavior.
        ZEngine::Core::VFS::VFSContextTestAccess::Deliver(context, MakeEvent(OtherPathNative));
        EXPECT_EQ(listener.Count, 1);
        EXPECT_EQ(listener.LastPath, ZEngine::Core::VFS::VFSPath::Parse("/Assets/Meshes/other.zemesh").Value());
    }

    manager.Shutdown();
}

TEST(VFSImportPublicationTest, NativeWatcherPathsPreserveProjectRootBoundary)
{
    using namespace ZEngine::Core::VFS;
    VFSContext context;
    VFSContextTestAccess::Configure(context, ProjectRootNative, nullptr, nullptr);

    const auto root = VFSContextTestAccess::Resolve(context, ProjectRootNative);
    ASSERT_TRUE(root.Succeeded());
    EXPECT_EQ(root.Value(), VFSPath::Root());

    const auto artifact = VFSContextTestAccess::Resolve(context, ArtifactPathNative);
    ASSERT_TRUE(artifact.Succeeded());
    EXPECT_EQ(artifact.Value(), VFSPath::Parse("/Assets/Meshes/robot.zemesh").Value());

    const auto sibling = VFSContextTestAccess::Resolve(context, SiblingRootNative);
    ASSERT_TRUE(sibling.Succeeded());
    EXPECT_EQ(sibling.Value(), VFSPath::Parse("/project2/Assets/Meshes/robot.zemesh").Value());
}

TEST(VFSImportPublicationTest, IgnoresEngineCacheWatcherEvents)
{
    using namespace ZEngine::Core::VFS;
    MemoryManager   manager;
    VFSContext      context;
    ListenerCounter listener;

    manager.Initialize(ZMega(2), {});
    context.Initialize(&manager.MainArena);
    VFSContextTestAccess::Configure(context, ProjectRootNative, &CountEvent, &listener);

    const WatchEventKind kinds[] = {WatchEventKind::Created, WatchEventKind::Modified, WatchEventKind::Deleted, WatchEventKind::Renamed};
    for (const WatchEventKind kind : kinds)
    {
        auto event = MakeEvent(CachePathNative);
        event.Kind = kind;
        VFSContextTestAccess::Deliver(context, event);
        event.IsDirectory = true;
        VFSContextTestAccess::Deliver(context, event);
    }
    EXPECT_EQ(listener.Count, 0);

    char user_path[MAX_FILE_PATH_COUNT] = {};
    VFSPath::Parse("/.zodiacengine-assets/preview.png").Value().ResolveNative(ProjectRootNative, user_path, sizeof(user_path));
    VFSContextTestAccess::Deliver(context, MakeEvent(user_path));
    EXPECT_EQ(listener.Count, 1);

    context.Shutdown();
    manager.Shutdown();
}

TEST(VFSImportPublicationTest, RenameIntoEngineCacheRemovesTheSourceAsset)
{
    using namespace ZEngine::Core::VFS;
    MemoryManager manager;
    manager.Initialize(ZMega(32), {});
    AssetRegistry registry;
    registry.Initialize(&manager.MainArena);
    const auto   id = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    MetaFileData meta{};
    meta.AssetUUID    = id;
    const auto source = VFSPath::Parse("/Assets/Meshes/robot.zemesh").Value();
    ASSERT_TRUE(registry.Register(id, ZEngine::Managers::AssetType::MESH, source, meta).IsOk());
    VFSContext      context;
    ListenerCounter listener;
    context.Initialize(&manager.MainArena);
    VFSContextTestAccess::Configure(context, ProjectRootNative, &CountEvent, &listener);
    VFSContextTestAccess::SetRegistry(context, &registry);
    auto event = MakeEvent(CachePathNative);
    event.Kind = WatchEventKind::Renamed;
    ZEngine::Helpers::secure_strcpy(event.OldPath, sizeof(event.OldPath), ArtifactPathNative);
    VFSContextTestAccess::Deliver(context, event);

    EXPECT_EQ(registry.FindByUUID(id), nullptr);
    EXPECT_EQ(listener.Count, 1);
    EXPECT_EQ(listener.LastPath, source);
    EXPECT_EQ(listener.LastKind, WatchEventKind::Deleted);
    context.Shutdown();
    manager.Shutdown();
}

TEST(VFSImportPublicationTest, RenameOutOfEngineCacheIsANewUserAsset)
{
    using namespace ZEngine::Core::VFS;
    MemoryManager manager;
    manager.Initialize(ZMega(2), {});
    VFSContext      context;
    ListenerCounter listener;
    context.Initialize(&manager.MainArena);
    VFSContextTestAccess::Configure(context, ProjectRootNative, &CountEvent, &listener);
    auto event = MakeEvent(ArtifactPathNative);
    event.Kind = WatchEventKind::Renamed;
    ZEngine::Helpers::secure_strcpy(event.OldPath, sizeof(event.OldPath), CachePathNative);
    VFSContextTestAccess::Deliver(context, event);

    EXPECT_EQ(listener.Count, 1);
    EXPECT_EQ(listener.LastKind, WatchEventKind::Created);
    context.Shutdown();
    manager.Shutdown();
}
