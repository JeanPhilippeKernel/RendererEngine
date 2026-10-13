#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/EntityID.h>
#include <ZEngine/ECS/Tools/GizmoMath.h>

namespace ZEngine::ECS
{
    class Scene;
}

namespace ZEngine::ECS::Tools
{
    /// @brief One persistent, authored local transform captured for a transform edit.
    struct TransformEditRecord
    {
        EntityID            Entity = INVALID_ENTITY;
        GizmoLocalTransform Before = {};
        GizmoLocalTransform After  = {};
    };

    /// @brief A bounded undo/redo stack for atomic transform edits.
    /// @details Records are stored in a single contiguous Array rather than one allocation per
    ///          command. New commits discard redo records. When the configured budget is full,
    ///          the oldest complete command is retired before accepting a new one.
    class TransformEditHistory
    {
    public:
        void                   Initialize(Core::Memory::ArenaAllocator* arena, uint32_t maximum_commands, uint32_t maximum_records);

        [[nodiscard]] bool     Push(const TransformEditRecord* records, uint32_t record_count);
        [[nodiscard]] bool     Undo(Scene& scene);
        [[nodiscard]] bool     Redo(Scene& scene);

        [[nodiscard]] bool     CanUndo() const;
        [[nodiscard]] bool     CanRedo() const;
        [[nodiscard]] uint32_t UndoCount() const;
        [[nodiscard]] uint32_t RedoCount() const;
        void                   Clear();

    private:
        struct CommandRange
        {
            uint32_t RecordOffset = 0;
            uint32_t RecordCount  = 0;
        };

        [[nodiscard]] bool                           CanApply(const CommandRange& command, Scene& scene) const;
        void                                         Apply(const CommandRange& command, Scene& scene, bool use_after) const;
        void                                         DiscardRedo();
        void                                         DiscardOldestCommand();

        Core::Containers::Array<CommandRange>        m_commands = {};
        Core::Containers::Array<TransformEditRecord> m_records  = {};
        uint32_t                                     m_cursor   = 0; // number of commands currently applied
    };

    /// @brief Editor-owned transform selection and one active atomic manipulation transaction.
    /// @details Selection is kept independently from rendering or UI widgets. During a drag it
    ///          captures local TRS and parent world matrices once, previews only fully validated
    ///          batches, and writes one history command on commit. Callers may cancel at any
    ///          point; live targets are restored to their captured local transforms.
    class TransformEditSession
    {
    public:
        void                                                                 Initialize(Core::Memory::ArenaAllocator* arena, uint32_t maximum_selection, uint32_t maximum_history_commands, uint32_t maximum_history_records);

        /// @brief Replaces the transformable selection with unique live entities.
        /// @return false only when the supplied selection cannot fit the configured capacity.
        [[nodiscard]] bool                                                   SetSelection(Scene& scene, Core::Containers::ArrayView<const EntityID> entities, EntityID active = INVALID_ENTITY);
        [[nodiscard]] bool                                                   AddToSelection(Scene& scene, EntityID entity, bool make_active = true);
        [[nodiscard]] bool                                                   RemoveFromSelection(EntityID entity);
        [[nodiscard]] bool                                                   ToggleSelection(Scene& scene, EntityID entity);
        void                                                                 ClearSelection();

        [[nodiscard]] Core::Containers::ArrayView<const EntityID>            Selection() const;
        [[nodiscard]] EntityID                                               ActiveEntity() const;
        [[nodiscard]] bool                                                   Contains(EntityID entity) const;

        /// @brief Captures a normalized root set from the current selection.
        /// @details Selected descendants of another selected target are deliberately excluded:
        ///          moving both would apply the same edit twice through the hierarchy.
        [[nodiscard]] bool                                                   Begin(Scene& scene);
        [[nodiscard]] bool                                                   PreviewLocal(Scene& scene, Core::Containers::ArrayView<const GizmoLocalTransform> transforms);
        [[nodiscard]] bool                                                   PreviewWorld(Scene& scene, Core::Containers::ArrayView<const Core::Maths::Mat4f> transforms);
        /// @brief Ends the transaction and stores exactly one undo command.
        [[nodiscard]] bool                                                   Commit(Scene& scene);
        /// @brief Restores every still-live captured target and ends the transaction.
        /// @return false if a captured target was deleted before it could be restored.
        [[nodiscard]] bool                                                   Cancel(Scene& scene);

        [[nodiscard]] bool                                                   Undo(Scene& scene);
        [[nodiscard]] bool                                                   Redo(Scene& scene);

        [[nodiscard]] bool                                                   IsActive() const;
        /// @brief Reports whether the most recently completed Commit authored a history record.
        /// @details This is reset when a new transaction begins or an active transaction is
        ///          cancelled. It lets editor integration update persistence state without
        ///          treating a click-without-motion as a scene change.
        [[nodiscard]] bool                                                   LastCommitChanged() const;
        [[nodiscard]] Core::Containers::ArrayView<const TransformEditRecord> ActiveTargets() const;
        [[nodiscard]] TransformEditHistory&                                  History();
        [[nodiscard]] const TransformEditHistory&                            History() const;

    private:
        struct ActiveTarget
        {
            TransformEditRecord Record              = {};
            Core::Maths::Mat4f  InitialWorld        = Core::Maths::Identity<Core::Maths::Mat4f>();
            Core::Maths::Mat4f  CapturedParentWorld = Core::Maths::Identity<Core::Maths::Mat4f>();
            bool                HasCapturedParent   = false;
        };

        [[nodiscard]] bool                           HasSelectedAncestor(Scene& scene, EntityID entity) const;
        [[nodiscard]] bool                           ValidateActiveTargets(Scene& scene, bool validate_captured_parents) const;
        [[nodiscard]] bool                           StageLocal(Core::Containers::ArrayView<const GizmoLocalTransform> transforms);
        [[nodiscard]] bool                           ApplyStaged(Scene& scene);
        void                                         EndTransaction();

        Core::Containers::Array<EntityID>            m_selection           = {};
        Core::Containers::Array<ActiveTarget>        m_targets             = {};
        Core::Containers::Array<GizmoLocalTransform> m_staged              = {};
        Core::Containers::Array<TransformEditRecord> m_view                = {};
        TransformEditHistory                         m_history             = {};
        EntityID                                     m_active              = INVALID_ENTITY;
        bool                                         m_is_active           = false;
        bool                                         m_last_commit_changed = false;
    };
} // namespace ZEngine::ECS::Tools
