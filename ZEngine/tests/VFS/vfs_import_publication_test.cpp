#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>

using ZEngine::Core::Memory::MemoryManager;

namespace ZEngine::Core::VFS
{
    // Keeps watcher ingress private in production while allowing this focused
    // regression test to model an inotify event delivered after publication end.
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
    };
} // namespace ZEngine::Core::VFS

namespace
{
    struct ListenerCounter
    {
        int Count = 0;
    };

    void CountEvent(void* context, const ZEngine::Core::VFS::VFSPath&, ZEngine::Core::VFS::WatchEventKind)
    {
        static_cast<ListenerCounter*>(context)->Count++;
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
        ZEngine::Core::VFS::VFSContextTestAccess::Configure(context, "/project", &CountEvent, &listener);

        const auto artifact = ZEngine::Core::VFS::VFSPath::Parse("/Assets/Meshes/robot.zemesh");
        ASSERT_TRUE(artifact.Succeeded());

        const ZEngine::Core::VFS::VFSImportPublication publication = context.BeginImportPublication();
        context.RecordImportPublicationArtifact(publication, artifact.Value());
        context.EndImportPublication(publication);

        // The raw event is observed after EndImportPublication. It must still be
        // absorbed by the publication grace period instead of notifying the import
        // coordinator through the generic file-change listener.
        ZEngine::Core::VFS::VFSContextTestAccess::Deliver(context, MakeEvent("/project/Assets/Meshes/robot.zemesh"));
        EXPECT_EQ(listener.Count, 0);

        // The grace period only suppresses artifacts owned by this import. Other
        // project edits retain the normal watcher behavior.
        ZEngine::Core::VFS::VFSContextTestAccess::Deliver(context, MakeEvent("/project/Assets/Meshes/other.zemesh"));
        EXPECT_EQ(listener.Count, 1);
    }

    manager.Shutdown();
}
