#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/ECS/SceneComponentSchemaRegistry.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Importers/AssetTypes.h>
#include <memory>
#include <string_view>

namespace ZEngine::Core::VFS
{
    struct IVFSContext;
}

namespace ZEngine
{
    struct EngineContext;
}

namespace Tetragrama
{
    struct EditorScene;
    class EditorSession;

    /// Host services run on the main thread. Submit must either queue the task
    /// without running it inline, or return false without retaining its context.
    struct EditorMeshDropServices
    {
        void* Context                                                                                                                            = nullptr;
        bool (*Submit)(void*, void*, void (*)(void*))                                                                                            = nullptr;
        bool (*BindMesh)(void*, ZEngine::Importers::AssetMesh&, ZEngine::Importers::AssetNodeHierarchy&, ZEngine::Core::Memory::ArenaAllocator*) = nullptr;
        bool (*DrawCount)(void*, const uuids::uuid&, uint32_t*)                                                                                  = nullptr;
    };

    /// One bounded, editor-owned asynchronous load. Workers retain only their
    /// request, VFS and private decode arena, never a panel/editor/scene pointer.
    /// Cancel does not release running/queued work. ShutdownAfterWorkersJoined
    /// must run before the owner/import-pipeline arena is destroyed.
    class EditorMeshDrop final
    {
    public:
        static constexpr size_t WorkspaceBytes = ZMega(256);
        EditorMeshDrop();
        ~EditorMeshDrop();
        EditorMeshDrop(const EditorMeshDrop&)               = delete;
        EditorMeshDrop&    operator=(const EditorMeshDrop&) = delete;

        [[nodiscard]] bool Initialize(ZEngine::Core::Memory::ArenaAllocator& import_arena);
        /// Recooked imports require a new binding; reusing a loaded UUID could
        /// place stale geometry. Atomic replacement of live assets is separate.
        [[nodiscard]] bool Request(const ZEngine::Core::VFS::VFSPath& path, uint64_t epoch, std::string_view name = {}, ZEngine::Core::VFS::IVFSContext* expected_vfs = nullptr, bool require_new_binding = false);
        [[nodiscard]] bool Pending() const;
        /// Every-update mutation-point polling, independent of overlay publishing.
        [[nodiscard]] bool Process(ZEngine::EngineContext& engine, EditorScene* owner, EditorSession& session, ZEngine::ECS::SceneDiagnostics* diagnostics, const EditorMeshDropServices& services = {});
        void               Cancel();
        void               ShutdownAfterWorkersJoined();

    private:
        struct Work;
        ZEngine::Core::Memory::ArenaAllocator m_arena = {};
        ZEngine::Helpers::Scope<Work>         m_work;
        ZEngine::Core::VFS::VFSPath           m_path                = {};
        ZEngine::Core::VFS::IVFSContext*      m_expected_vfs        = nullptr;
        char                                  m_name[128]           = {};
        uint64_t                              m_epoch               = 0;
        bool                                  m_requested           = false;
        bool                                  m_closed              = false;
        bool                                  m_require_new_binding = false;
        void                                  ReleaseWork();
    };
} // namespace Tetragrama
