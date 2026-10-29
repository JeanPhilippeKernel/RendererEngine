#pragma once
#include <Tetragrama/EditorTarget.h>
#include <span>

namespace Tetragrama
{
    enum class SelectionMode : uint8_t
    {
        Replace,
        Add,
        Toggle,
    };

    /// @brief Shared ordered entity selection. No authored values, history or dirty ownership.
    /// Storage is reserved once; rejected changes leave membership and revision unchanged.
    class SelectionModel
    {
    public:
        void                                        Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, uint32_t max_targets = 4096);

        [[nodiscard]] bool                          Select(EntityTargetResolver& resolver, ZEngine::ECS::EntityID entity, SelectionMode mode = SelectionMode::Replace);
        [[nodiscard]] bool                          Set(EntityTargetResolver& resolver, std::span<const EntityTarget> targets, const uuids::uuid& active = {});
        [[nodiscard]] bool                          Extend(EntityTargetResolver& resolver, std::span<const EntityTarget> targets, const uuids::uuid& active = {});
        [[nodiscard]] bool                          SetHovered(EntityTargetResolver& resolver, ZEngine::ECS::EntityID entity);
        void                                        ClearHovered();
        void                                        SelectScene(SceneTarget target);
        void                                        Clear();
        bool                                        PruneInvalid(EntityTargetResolver& resolver);

        [[nodiscard]] std::span<const EntityTarget> Entities() const
        {
            return {m_entities.data(), m_entities.size()};
        }
        [[nodiscard]] const uuids::uuid& ActiveUUID() const
        {
            return m_active;
        }
        [[nodiscard]] SceneTarget ActiveSceneTarget() const
        {
            return m_scene_target;
        }
        [[nodiscard]] uint64_t Revision() const
        {
            return m_revision;
        }
        [[nodiscard]] const EntityTarget& Hovered() const
        {
            return m_hovered;
        }
        [[nodiscard]] uint64_t HoverRevision() const
        {
            return m_hover_revision;
        }
        [[nodiscard]] bool Contains(const uuids::uuid& identity) const
        {
            return m_membership.contains(identity);
        }
        [[nodiscard]] uint32_t Capacity() const
        {
            return m_capacity;
        }

    private:
        friend class EditorSession;
        bool                                                               CanSelect(EntityTargetResolver& resolver, ZEngine::ECS::EntityID entity, SelectionMode mode);
        bool                                                               Prepare(EntityTargetResolver& resolver, std::span<const EntityTarget> targets, const uuids::uuid& active, bool additive);
        void                                                               PublishPrepared();
        void                                                               RebuildMembership();

        ZEngine::Core::Containers::Array<EntityTarget>                     m_entities           = {};
        ZEngine::Core::Containers::Array<EntityTarget>                     m_pending            = {};
        ZEngine::Core::Containers::UnorderedHashMap<uuids::uuid, uint32_t> m_membership         = {};
        ZEngine::Core::Containers::UnorderedHashMap<uuids::uuid, uint32_t> m_pending_membership = {};
        uuids::uuid                                                        m_active             = {};
        uuids::uuid                                                        m_pending_active     = {};
        SceneTarget                                                        m_scene_target       = SceneTarget::None;
        EntityTarget                                                       m_hovered            = {};
        uint64_t                                                           m_revision           = 0;
        uint64_t                                                           m_hover_revision     = 0;
        uint32_t                                                           m_capacity           = 0;
    };
} // namespace Tetragrama
