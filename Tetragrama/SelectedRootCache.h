#pragma once
#include <Tetragrama/EditorTarget.h>
#include <ZEngine/Core/Containers/Array.h>

namespace Tetragrama
{
    class EditorSession;

    /// Topology-only pruning: transform eligibility belongs to the operation validator.
    /// Main-thread owned, with one bounded arena reservation and no per-read allocation.
    class SelectedRootCache
    {
    public:
        [[nodiscard]] bool     Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, uint32_t max_targets);
        void                   Invalidate();
        /// Returned views are borrowed until the next Read/Invalidate or arena teardown.
        /// Failure never publishes a partial set or changes the output view itself.
        [[nodiscard]] bool     Read(EditorSession& session, ZEngine::Core::Containers::ArrayView<const EntityTarget>& output);
        [[nodiscard]] uint64_t BuildCount() const
        {
            return m_build_count;
        }

    private:
        // Capacity views: only constructed records in [0, m_count) are exposed.
        ZEngine::Core::Containers::ArrayView<EntityTarget> m_storage  = {};
        ZEngine::Core::Containers::ArrayView<uint8_t>      m_coverage = {};
        const EditorSession*                               m_session  = nullptr;
        uint64_t                                           m_epoch = 0, m_selection_revision = 0, m_structural_revision = 0, m_hierarchy_revision = 0, m_build_count = 0;
        uint32_t                                           m_count = 0;
        bool                                               m_valid = false;
    };
} // namespace Tetragrama
