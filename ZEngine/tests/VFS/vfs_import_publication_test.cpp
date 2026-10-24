#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>

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
    };
} // namespace ZEngine::Core::VFS

namespace
{
#if defined(_WIN32)
    constexpr const char* ProjectRootNative  = "C:\\project";
    constexpr const char* ArtifactPathNative = "C:\\project\\Assets\\Meshes\\robot.zemesh";
    constexpr const char* OtherPathNative    = "C:\\project\\Assets\\Meshes\\other.zemesh";
    constexpr const char* SiblingRootNative  = "C:\\project2\\Assets\\Meshes\\robot.zemesh";
#else
    constexpr const char* ProjectRootNative  = "/project";
    constexpr const char* ArtifactPathNative = "/project/Assets/Meshes/robot.zemesh";
    constexpr const char* OtherPathNative    = "/project/Assets/Meshes/other.zemesh";
    constexpr const char* SiblingRootNative  = "/project2/Assets/Meshes/robot.zemesh";
#endif

    struct ListenerCounter
    {
        int                         Count    = 0;
        ZEngine::Core::VFS::VFSPath LastPath = {};
    };

    void CountEvent(void* context, const ZEngine::Core::VFS::VFSPath& path, ZEngine::Core::VFS::WatchEventKind)
    {
        auto& listener = *static_cast<ListenerCounter*>(context);
        ++listener.Count;
        listener.LastPath = path;
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
