#pragma once
#include <ZEngine/Core/Memory/SlabAllocationRoute.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneDocument.h>
#include <ZEngine/ECS/SceneStorage.h>
#include <ZEngine/ECS/WorldCommands.h>
#include <ZEngine/ECS/WorldTick.h>
#include <ZEngine/Managers/AssetCacheLease.h>
#include <memory>

namespace ZEngine::ECS
{
    /// Stable-address engine owner, transferable through unique ownership. No
    /// editor path, UI policy, active engine binding or GPU residency is implied.
    /// The storage lease outlives actors, ECS, document and their borrowed views.
    class SceneInstance final
    {
    public:
        ~SceneInstance();
        SceneInstance(const SceneInstance&)                       = delete;
        SceneInstance&            operator=(const SceneInstance&) = delete;

        /// Creates an initialized empty world and bound, empty actor manager.
        /// Failure preserves both the supplied lease and the output owner.
        [[nodiscard]] static bool Create(SceneStorageLease&& storage, std::unique_ptr<SceneInstance>* out, SceneDiagnostics* diagnostics);

        /// One reconstruction attempt, reusing SceneDocument's population path.
        /// Success takes the document; failure preserves it and requires discard
        /// of this unpublished instance. Never fabricates actors or invokes OnCreate.
        [[nodiscard]] bool        Populate(SceneDocument&& document, const SceneComponentSchemaRegistry& schemas, SceneDiagnostics* diagnostics);

        /// Exclusive unpublished-owner path. Input, libyaml, migrations, decoded
        /// tree and fixed codec scratch share one slab carved from the lease.
        /// Failure requires discarding this instance; no document escapes it.
        [[nodiscard]] bool        ReadSource(Core::VFS::IVFSContext& vfs, const Core::VFS::VFSPath& path, const SceneComponentSchemaRegistry& schemas, size_t working_bytes, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits = {}, const SceneMigrationRegistry* migrations = nullptr);
        [[nodiscard]] bool        PopulateSource(const SceneComponentSchemaRegistry& schemas, SceneDiagnostics* diagnostics);

        /// Adapter rollback only: return the input snapshot before immediately
        /// discarding an unpublished instance whose later preparation failed.
        /// Internally routed documents cannot escape their storage owner.
        [[nodiscard]] bool        TryReleaseDocumentForDiscard(SceneDocument* out) noexcept;

        struct DocumentMemoryUsage
        {
            size_t Capacity  = 0;
            size_t Allocated = 0;
            size_t Working   = 0;
            size_t Peak      = 0;
        };
        /// Query on the exclusive owner thread, or after worker Ready/join.
        [[nodiscard]] DocumentMemoryUsage DocumentMemory() const noexcept
        {
            return {m_document_route.Capacity(), m_document_route.AllocatedBytes(), m_document_route.WorkingBytes(), m_document_route.PeakBytes()};
        }
        /// Allocation-free diagnostics, called only by the exclusive owner.
        /// Arena charges include nested document/editor reservations; document
        /// charge peaks are a breakdown, not additional arena backing or RSS.
        void                 RecordMemoryUsage() const noexcept;

        [[nodiscard]] Scene& World() noexcept
        {
            return m_world;
        }
        [[nodiscard]] const Scene& World() const noexcept
        {
            return m_world;
        }
        [[nodiscard]] ActorManager& Actors() noexcept
        {
            return m_actors;
        }
        [[nodiscard]] const ActorManager& Actors() const noexcept
        {
            return m_actors;
        }
        [[nodiscard]] const SceneDocument& Document() const noexcept
        {
            return m_document;
        }
        [[nodiscard]] WorldCommands& Commands() noexcept
        {
            return m_commands;
        }
        [[nodiscard]] WorldTick& Tick() noexcept
        {
            return m_tick;
        }
        [[nodiscard]] Core::Memory::ArenaAllocator* Arena() const noexcept
        {
            return m_storage.Arena();
        }
        [[nodiscard]] Managers::AssetCacheLease& SourceAssets() noexcept
        {
            return m_source_assets;
        }

    private:
        SceneInstance() = default;
        [[nodiscard]] bool                PopulateDocument(const SceneDocument& document, const SceneComponentSchemaRegistry& schemas, SceneDiagnostics* diagnostics);

        SceneStorageLease                 m_storage               = {};
        Managers::AssetCacheLease         m_source_assets         = {};
        Core::Memory::SlabAllocationRoute m_document_route        = {};
        SceneDocument                     m_document              = {};
        Scene                             m_world                 = {};
        ActorManager                      m_actors                = {};
        WorldCommands                     m_commands              = {};
        WorldTick                         m_tick                  = {};
        bool                              m_initialized           = false;
        bool                              m_population_attempted  = false;
        bool                              m_source_read_attempted = false;
    };
} // namespace ZEngine::ECS
