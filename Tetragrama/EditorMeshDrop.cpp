#include <Tetragrama/EditorMeshDrop.h>
#include <Tetragrama/EditorScene.h>
#include <Tetragrama/EditorSession.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Managers/AssetManager.h>
#include <algorithm>
#include <atomic>
#include <new>

using namespace ZEngine::Core::VFS;
using namespace ZEngine::ECS;

namespace Tetragrama
{
    namespace
    {
        bool Reject(SceneDiagnostics* diagnostics, cstring message)
        {
            SceneDiagnosticError(diagnostics, message);
            return false;
        }
        bool Submit(void*, void* context, void (*task)(void*))
        {
            auto* pool = ZEngine::Helpers::ThreadPoolHelper::Pool.get();
            if (pool)
            {
                for (uint32_t i = 0; i < pool->WorkerCount; ++i)
                {
                    if (pool->SubmitToWorker(i, context, task))
                    {
                        return true;
                    }
                }
            }
            return false;
        }
        bool DrawCount(void*, const uuids::uuid& uuid, uint32_t* out)
        {
            auto* assets = ZEngine::Managers::AssetManager::Instance();
            return assets && assets->TryGetMeshDrawCount(uuid, out);
        }
        bool BindMesh(void*, ZEngine::Importers::AssetMesh& mesh, ZEngine::Importers::AssetNodeHierarchy& hierarchy, ZEngine::Core::Memory::ArenaAllocator* arena)
        {
            using ZEngine::Managers::AssetManager;
            if (!AssetManager::Instance() || !arena)
            {
                return false;
            }
            const auto uuid  = mesh.MeshUUID;
            const auto count = mesh.SubMeshes.size();
            uint32_t   draws = 0;
            // Dropping an already loaded mesh is placement, not a hot reload.
            if (DrawCount(nullptr, mesh.MeshUUID, &draws))
            {
                return draws == mesh.SubMeshes.size();
            }
            for (const auto& submesh : mesh.SubMeshes)
            {
                if (!submesh.MaterialUUID.is_nil())
                {
                    // Material decoding is transient. ArenaScratch restores the
                    // workspace on every path and prevents allocations in the
                    // parent arena while the decoder owns this temporary range.
                    ZEngine::Core::Memory::ArenaScratch scratch(arena);
                    if (!AssetManager::IngestMaterialFromUUID(scratch.Arena(), submesh.MaterialUUID))
                    {
                        return false;
                    }
                }
            }
            AssetManager::IngestMesh(std::move(mesh), std::move(hierarchy));
            return DrawCount(nullptr, uuid, &draws) && draws == count;
        }
    } // namespace

    struct EditorMeshDrop::Work
    {
        IVFSContext*                           VFS               = nullptr;
        ZEngine::Core::Memory::ArenaAllocator* Arena             = nullptr;
        VFSPath                                Path              = {};
        char                                   Name[128]         = {};
        uint64_t                               Epoch             = 0;
        bool                                   RequireNewBinding = false;
        ZEngine::Importers::AssetMesh          Mesh              = {};
        ZEngine::Importers::AssetNodeHierarchy Hierarchy         = {};
        VFSError                               Error             = VFSError::OK;
        PaddedAtomic<bool>                     Ready             = {};
        PaddedAtomic<bool>                     Cancelled         = {};

        static void                            Read(void* context)
        {
            auto& work = *static_cast<Work*>(context);
            if (!work.Cancelled.value.load(std::memory_order_acquire))
            {
                auto limits            = ZEngine::Importers::AssetCodec::MeshAssetReadLimits{};
                limits.MaxDecodedBytes = WorkspaceBytes - ZMega(8);
                const auto result      = ZEngine::Importers::AssetCodec::ReadMeshAssetFileVFS(work.Arena, *work.VFS, work.Path, &work.Mesh, &work.Hierarchy, {}, limits);
                if (result.Failed())
                {
                    work.Error = result.Error();
                }
            }
            // No access to the request after publication; the main thread may
            // reclaim it as soon as this release is observed.
            work.Ready.value.store(true, std::memory_order_release);
        }
    };

    EditorMeshDrop::EditorMeshDrop() = default;
    EditorMeshDrop::~EditorMeshDrop()
    {
        ZENGINE_VALIDATE_ASSERT(!m_work || m_work->Ready.value.load(std::memory_order_acquire), "Mesh-drop workers must be joined before editor destruction")
    }
    bool EditorMeshDrop::Initialize(ZEngine::Core::Memory::ArenaAllocator& import_arena)
    {
        if (m_closed || m_arena.m_memory || !import_arena.m_memory || import_arena.m_current_offset > import_arena.m_total_size || import_arena.m_total_size - import_arena.m_current_offset < WorkspaceBytes + import_arena.m_mem_page_size)
        {
            return false;
        }
        import_arena.CreateSubArena(WorkspaceBytes, &m_arena, "ImportPipeline/EditorMeshDrop");
        return m_arena.m_memory != nullptr;
    }
    bool EditorMeshDrop::Pending() const
    {
        return m_requested || m_work != nullptr;
    }
    bool EditorMeshDrop::Request(const VFSPath& path, uint64_t epoch, std::string_view name, IVFSContext* expected_vfs, bool require_new_binding)
    {
        if (m_closed || !m_arena.m_memory || Pending() || !epoch || !path.IsValid() || !path.Extension().Equals(".zemesh") || name.size() > 255 || name.find('\0') != std::string_view::npos)
        {
            return false;
        }
        size_t length = std::min(name.size(), sizeof(m_name) - 1);
        if (length < name.size())
        {
            while (length && (static_cast<uint8_t>(name[length]) & 0xc0) == 0x80)
            {
                --length;
            }
        }
        if (length && ZEngine::Helpers::secure_strncpy(m_name, sizeof(m_name), name.data(), length) != ZEngine::Helpers::MEMORY_OP_SUCCESS)
        {
            return false;
        }
        m_name[length]        = '\0';
        m_expected_vfs        = expected_vfs;
        m_require_new_binding = require_new_binding;
        m_path                = path;
        m_epoch               = epoch;
        m_requested           = true;
        return true;
    }
    void EditorMeshDrop::Cancel()
    {
        m_requested = false;
        if (m_work)
        {
            m_work->Cancelled.value.store(true, std::memory_order_release);
        }
    }
    void EditorMeshDrop::ReleaseWork()
    {
        m_work.reset();
        m_arena.Clear();
    }
    void EditorMeshDrop::ShutdownAfterWorkersJoined()
    {
        Cancel();
        m_closed = true;
        ReleaseWork(); // Also reclaims a queued context abandoned by pool shutdown.
        m_arena.Shutdown();
    }
    bool EditorMeshDrop::Process(ZEngine::EngineContext& engine, EditorScene* owner, EditorSession& session, SceneDiagnostics* diagnostics, const EditorMeshDropServices& services)
    {
        if (!Pending())
        {
            return true;
        }
        if (!session.InMutationPhase())
        {
            return false;
        }
        const auto epoch = m_work ? m_work->Epoch : m_epoch;
        if (m_closed || !owner || owner->Session != &session || owner->SourceReadOnly || !session.Editable() || engine.Scene != session.Scene() || epoch != session.SceneEpoch() || !engine.VFS || engine.CloseRequested.value.load(std::memory_order_acquire) || (m_expected_vfs && m_expected_vfs != engine.VFS) || (m_work && m_work->VFS != engine.VFS))
        {
            const bool already_cancelled = m_work && m_work->Cancelled.value.load(std::memory_order_acquire);
            Cancel();
            if (m_work && m_work->Ready.value.load(std::memory_order_acquire))
            {
                ReleaseWork();
            }
            return already_cancelled || Reject(diagnostics, "Mesh drop belongs to a stale or non-editable scene; no entity was created.");
        }
        if (!m_work)
        {
            m_requested = false;
            m_work.reset(new (std::nothrow) Work{});
            if (!m_work)
            {
                return Reject(diagnostics, "Cannot allocate a mesh-drop request.");
            }
            m_work->VFS               = engine.VFS;
            m_work->Arena             = &m_arena;
            m_work->Path              = m_path;
            m_work->Epoch             = m_epoch;
            m_work->RequireNewBinding = m_require_new_binding;
            ZEngine::Helpers::secure_strcpy(m_work->Name, sizeof(m_work->Name), m_name);
            const auto submit = services.Submit ? services.Submit : &Submit;
            if (submit(services.Context, m_work.get(), &Work::Read))
            {
                return true;
            }
            ReleaseWork();
            return Reject(diagnostics, "Mesh drop could not obtain a worker; the scene was preserved.");
        }
        if (!m_work->Ready.value.load(std::memory_order_acquire))
        {
            return true;
        }
        // Keep the work alive until main-thread binding and component creation finish.
        // Every return below releases it and its decode storage exactly once.
        struct Completion
        {
            EditorMeshDrop& Owner;
            ~Completion()
            {
                Owner.ReleaseWork();
            }
        } completion{*this};
        if (m_work->Cancelled.value.load(std::memory_order_acquire))
        {
            return true;
        }
        if (m_work->Error != VFSError::OK)
        {
            return Reject(diagnostics, "Dropped mesh failed checked decoding; the scene was preserved.");
        }
        constexpr auto budget = ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS;
        if (!owner->CanSpawnMeshActor(engine) || owner->Instances.size() >= budget || m_work->Mesh.SubMeshes.empty() || m_work->Mesh.SubMeshes.size() > budget)
        {
            return Reject(diagnostics, "Dropped mesh exceeds entity, instance or component-storage capacity.");
        }
        uint32_t   total        = static_cast<uint32_t>(m_work->Mesh.SubMeshes.size());
        const auto draw_count   = services.DrawCount ? services.DrawCount : &DrawCount;
        uint32_t   loaded_draws = 0;
        if (m_work->RequireNewBinding && draw_count(services.Context, m_work->Mesh.MeshUUID, &loaded_draws))
        {
            return Reject(diagnostics, "Import succeeded, but placement requires replacing a live mesh binding; no entity was created.");
        }
        for (const auto& instance : owner->Instances)
        {
            uint32_t count = 0;
            if (!draw_count(services.Context, instance.MeshUUID, &count) || !count || count > budget - total)
            {
                return Reject(diagnostics, "Dropped mesh exceeds the complete GPU draw budget or has unresolved existing bindings.");
            }
            total += count;
        }
        if (!owner->Instances.try_reserve(owner->Instances.size() + 1))
        {
            return Reject(diagnostics, "Cannot retain the new mesh instance within its scene arena.");
        }
        char       name[sizeof(Components::NameComponent::Value)] = {};
        const auto stem                                           = m_work->Path.Stem();
        const auto source                                         = m_work->Name[0] ? std::string_view(m_work->Name) : std::string_view(stem.Data, stem.Length);
        size_t     length                                         = std::min(source.size(), sizeof(name) - 1);
        // VFS filenames are UTF-8. If the byte budget cuts a code point, omit
        // the entire point rather than storing an invalid display/source name.
        if (length < source.size())
        {
            while (length && (static_cast<uint8_t>(source[length]) & 0xc0) == 0x80)
            {
                --length;
            }
        }
        if (ZEngine::Helpers::secure_strncpy(name, sizeof(name), source.data(), length) != ZEngine::Helpers::MEMORY_OP_SUCCESS)
        {
            return Reject(diagnostics, "Dropped mesh name is invalid.");
        }
        const auto bind = services.BindMesh ? services.BindMesh : &BindMesh;
        const auto uuid = m_work->Mesh.MeshUUID;
        if (!bind(services.Context, m_work->Mesh, m_work->Hierarchy, &m_arena))
        {
            return Reject(diagnostics, "Dropped mesh could not obtain a complete asset binding; the scene was preserved.");
        }
        return owner->SpawnMeshActor(engine, uuid, name).Valid() || Reject(diagnostics, "Dropped mesh actor creation failed.");
    }
} // namespace Tetragrama
