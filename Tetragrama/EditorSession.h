#pragma once
#include <Tetragrama/EditorHistory.h>
#include <Tetragrama/Gizmo/GizmoCamera.h>
#include <Tetragrama/Gizmo/GizmoLight.h>
#include <Tetragrama/Gizmo/GizmoPivot.h>
#include <Tetragrama/Gizmo/GizmoRotation.h>
#include <Tetragrama/Gizmo/GizmoScale.h>
#include <Tetragrama/Gizmo/GizmoState.h>
#include <Tetragrama/Gizmo/GizmoTransformBatch.h>
#include <Tetragrama/Gizmo/GizmoTranslation.h>
#include <Tetragrama/SelectedRootCache.h>
#include <Tetragrama/SelectionModel.h>
#include <ZEngine/ECS/AuthoredComponentValues.h>
#include <ZEngine/ECS/SceneGridSettings.h>
#include <array>
#include <string_view>
#include <thread>

namespace Tetragrama
{
    struct EditorSessionConfig
    {
        uint32_t MaxTargets     = 4096;
        uint32_t HistoryEntries = 256;
        size_t   HistoryBytes   = ZMega(16);
    };

    enum class EditorCommand : uint8_t
    {
        Undo,
        Redo,
        Cancel,
        ClearSelection,
    };

    enum class TransformProperty : uint8_t
    {
        Position,
        Rotation,
        Scale,
    };

    /// @brief A numeric control's captured pointer mapping, independent from UI presentation.
    struct EditorControlDrag
    {
        uint64_t                         Key           = 0;
        uint64_t                         PointerSerial = 0;
        float                            StartX        = 0.f;
        float                            UnitsPerPixel = 0.05f;
        TransformProperty                Property      = TransformProperty::Position;
        ZEngine::ECS::AuthoredFloatField Field         = ZEngine::ECS::AuthoredFloatField::LightRange;
        uint8_t                          Axis          = 0;
        bool                             IsTransform   = true;
    };

    enum class EditorSceneMutation : uint8_t
    {
        CreateCollection,
        DeleteEntity,
    };

    /// @brief Remaining Inspector properties whose document-history operations are not implemented yet.
    enum class EditorUntrackedProperty : uint8_t
    {
        LightColor,
        CameraMain,
        RigidBodyMotion,
    };

    struct EditorUntrackedValue
    {
        std::array<float, 3>                                     Color      = {1.f, 1.f, 1.f};
        bool                                                     MainCamera = false;
        ZEngine::ECS::Components::RigidBodyComponent::MotionType Motion     = ZEngine::ECS::Components::RigidBodyComponent::MotionType::Dynamic;
    };

    /// @brief Scene-owner adapters for mutations that also affect actor/render lifetimes.
    /// Bound once per scene epoch at the exclusive mutation point. Exact repeated
    /// binding is allowed; a different owner/callback requires BindScene first.
    /// Requests retain no callback pointers.
    struct EditorSceneMutationHooks
    {
        void* Context                                                                                    = nullptr;
        bool (*Validate)(void*, const ZEngine::ECS::Scene&, EditorSceneMutation, ZEngine::ECS::EntityID) = nullptr;
        void (*Apply)(void*, ZEngine::ECS::Scene&, EditorSceneMutation, ZEngine::ECS::EntityID, cstring) = nullptr;
    };

    /// Main-thread semantic view. Readability and editability are separate so
    /// loss of editability can still roll back our own last preview safely.
    struct EditorGridView
    {
        ZEngine::ECS::SceneGridSettings Value     = {};
        uuids::uuid                     SceneUUID = {};
        bool                            Editable  = false;
    };

    /// Bound once per scene epoch; no owner pointers enter history or requests.
    /// Callbacks are nonthrowing and cannot fail/mutate ownership after validation.
    struct EditorSceneSettingsHooks
    {
        void* Context                                                                          = nullptr;
        bool (*ReadGrid)(void*, const ZEngine::ECS::Scene&, EditorGridView&)                   = nullptr;
        void (*ApplyGrid)(void*, ZEngine::ECS::Scene&, const ZEngine::ECS::SceneGridSettings&) = nullptr;
    };

    /// @brief Exclusive main-thread owner of selection, semantic transactions and shared history.
    /// No authored mutation is permitted outside BeginMutationPhase/EndMutationPhase.
    class EditorSession
    {
    public:
        [[nodiscard]] bool     Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, const EditorSessionConfig& config = {});
        void                   Shutdown();
        [[nodiscard]] bool     BeginMutationPhase(bool editable = true);
        void                   EndMutationPhase();
        [[nodiscard]] bool     BindScene(ZEngine::ECS::Scene* scene, bool saved = true);
        [[nodiscard]] bool     SetSceneMutationHooks(const EditorSceneMutationHooks& hooks);
        [[nodiscard]] bool     SetSceneSettingsHooks(const EditorSceneSettingsHooks& hooks);
        [[nodiscard]] bool     ReadGridSettings(EditorGridView& output) const;

        /// @brief UI/event callbacks enqueue intent; only the per-update mutation phase applies it.
        /// Entries retain scene epochs and UUIDs, never ECS pointers or actor handles.
        [[nodiscard]] bool     Request(EditorCommand command);
        [[nodiscard]] bool     RequestGizmoChange(const Gizmo::Change& change);
        [[nodiscard]] bool     RequestSelection(ZEngine::ECS::EntityID entity, SelectionMode mode = SelectionMode::Replace);
        /// Copy one complete hierarchy range into bounded session storage. UI/frame pointers
        /// never survive this call; application and additive union are atomic at mutation time.
        [[nodiscard]] bool     RequestSelectionRange(ZEngine::Core::Containers::ArrayView<const EntityTarget> range, const uuids::uuid& active, bool additive = false);
        [[nodiscard]] bool     RequestSceneSelection(SceneTarget target);
        [[nodiscard]] bool     RequestTransformValue(ZEngine::ECS::EntityID entity, const ZEngine::ECS::LocalTransformValue& value);
        [[nodiscard]] bool     RequestFieldValue(ZEngine::ECS::EntityID entity, ZEngine::ECS::AuthoredFloatField field, float value);
        [[nodiscard]] bool     RequestControlDrag(ZEngine::ECS::EntityID entity, const EditorControlDrag& control);
        [[nodiscard]] bool     RequestReparent(ZEngine::ECS::EntityID child, ZEngine::ECS::EntityID parent);
        /// @brief Untracked label edit: validate the original identity/value, then cancel and invalidate before mutation.
        [[nodiscard]] bool     RequestRename(const EntityTarget& target, std::string_view expected, std::string_view name);
        /// @brief Untracked structural edit, identified by registry name rather than a retained callback/type pointer.
        [[nodiscard]] bool     RequestAddComponent(ZEngine::ECS::EntityID entity, std::string_view component);
        /// @brief Validate the original semantic property, then invalidate history before writing only that property.
        [[nodiscard]] bool     RequestUntrackedProperty(ZEngine::ECS::EntityID entity, EditorUntrackedProperty property, const EditorUntrackedValue& value);
        [[nodiscard]] bool     RequestDelete(ZEngine::ECS::EntityID entity);
        [[nodiscard]] bool     RequestCollection(std::string_view name);
        [[nodiscard]] bool     HasPendingControl(uint64_t key) const;
        [[nodiscard]] bool     ProcessRequests();
        void                   DiscardRequests();
        /// @brief Cancel delayed value/control input without dropping selection or history commands.
        [[nodiscard]] bool     DiscardEditRequests();
        /// @brief Apply immutable-base pointer math and release/cancel on every main update.
        [[nodiscard]] bool     UpdateControlDrag(float pointer_x, bool pointer_down, bool focused = true, bool cancel = false, bool capture_lost = false);
        [[nodiscard]] uint64_t ControlKey() const
        {
            return m_control.Key;
        }
        [[nodiscard]] uint64_t ControlPointerSerial() const
        {
            return m_control.PointerSerial;
        }

        /// WP6/WP12 adapter. Production physical starts remain gated by M0.
        /// Captures the complete ECS selected-root set or scene-owned Grid.
        [[nodiscard]] bool BeginTranslation(const Gizmo::TranslationBegin& input);
        [[nodiscard]] bool UpdateTranslation(const ZEngine::Core::Maths::Vec2f& pointer, bool pointer_down, bool fine = false, bool invert_snap = false, bool focused = true, bool cancel = false, bool capture_lost = false);
        /// Read after hierarchy synchronization, normally outside the mutation
        /// phase. Invalid/inactive/stale/unsynchronized interactions preserve output.
        [[nodiscard]] bool ReadTranslationSnapshot(Gizmo::TranslationSnapshot& output);
        [[nodiscard]] bool TranslationActive() const
        {
            return Active() && m_translation.Key != 0;
        }
        [[nodiscard]] uint64_t TranslationKey() const
        {
            return m_translation.Key;
        }
        [[nodiscard]] uint64_t TranslationPointerSerial() const
        {
            return m_translation.PointerSerial;
        }

        /// WP7 adapter. Production physical starts remain gated by M0.
        [[nodiscard]] bool BeginRotation(const Gizmo::RotationBegin& input);
        [[nodiscard]] bool UpdateRotation(const ZEngine::Core::Maths::Vec2f& pointer, bool pointer_down, bool fine = false, bool invert_snap = false, bool focused = true, bool cancel = false, bool capture_lost = false);
        [[nodiscard]] bool ReadRotationSnapshot(Gizmo::RotationSnapshot& output);
        [[nodiscard]] bool RotationActive() const
        {
            return Active() && m_rotation.Key != 0;
        }
        [[nodiscard]] uint64_t RotationKey() const
        {
            return m_rotation.Key;
        }
        [[nodiscard]] uint64_t RotationPointerSerial() const
        {
            return m_rotation.PointerSerial;
        }

        /// WP8 adapter. Production physical starts remain gated by M0.
        [[nodiscard]] bool BeginScale(const Gizmo::ScaleBegin& input);
        [[nodiscard]] bool UpdateScale(const ZEngine::Core::Maths::Vec2f& pointer, bool pointer_down, bool fine = false, bool invert_snap = false, bool focused = true, bool cancel = false, bool capture_lost = false);
        [[nodiscard]] bool ReadScaleSnapshot(Gizmo::ScaleSnapshot& output);
        [[nodiscard]] bool ScaleActive() const
        {
            return Active() && m_scale.Key != 0;
        }
        [[nodiscard]] uint64_t ScaleKey() const
        {
            return m_scale.Key;
        }
        [[nodiscard]] uint64_t ScalePointerSerial() const
        {
            return m_scale.PointerSerial;
        }

        /// WP9 editor-local view-plane edit; participates in capture/cancellation
        /// but never allocates an authored transaction or changes document state.
        [[nodiscard]] bool BeginPivot(const Gizmo::PivotBegin& input);
        [[nodiscard]] bool UpdatePivot(const ZEngine::Core::Maths::Vec2f& pointer, bool pointer_down, bool fine = false, bool invert_snap = false, bool focused = true, bool cancel = false, bool capture_lost = false);
        [[nodiscard]] bool ReadPivotSnapshot(Gizmo::PivotSnapshot& output);
        [[nodiscard]] bool ReadCustomPivot(ZEngine::Core::Maths::Vec3f& output) const;
        [[nodiscard]] bool ClearCustomPivot();
        [[nodiscard]] bool PivotActive() const
        {
            return m_active == Interaction::Pivot;
        }
        [[nodiscard]] uint64_t PivotKey() const
        {
            return m_pivot.Key;
        }
        [[nodiscard]] uint64_t PivotPointerSerial() const
        {
            return m_pivot.PointerSerial;
        }

        /// WP10 pointer adapter over the existing typed component-field transaction.
        [[nodiscard]] bool BeginLight(const Gizmo::LightBegin& input);
        [[nodiscard]] bool UpdateLight(const ZEngine::Core::Maths::Vec2f& pointer, bool pointer_down, bool fine = false, bool invert_snap = false, bool focused = true, bool cancel = false, bool capture_lost = false);
        [[nodiscard]] bool ReadLightSnapshot(Gizmo::LightSnapshot& output);
        [[nodiscard]] bool LightActive() const
        {
            return Active() && m_light.Key != 0;
        }
        [[nodiscard]] uint64_t LightKey() const
        {
            return m_light.Key;
        }
        [[nodiscard]] uint64_t LightPointerSerial() const
        {
            return m_light.PointerSerial;
        }

        [[nodiscard]] bool BeginCamera(const Gizmo::CameraBegin& input);
        [[nodiscard]] bool UpdateCamera(const ZEngine::Core::Maths::Vec2f& pointer, bool pointer_down, bool fine = false, bool invert_snap = false, bool focused = true, bool cancel = false, bool capture_lost = false);
        [[nodiscard]] bool ReadCameraSnapshot(Gizmo::CameraSnapshot& output);
        [[nodiscard]] bool CameraActive() const
        {
            return Active() && m_camera_gizmo.Key != 0;
        }
        [[nodiscard]] uint64_t CameraKey() const
        {
            return m_camera_gizmo.Key;
        }
        [[nodiscard]] uint64_t CameraPointerSerial() const
        {
            return m_camera_gizmo.PointerSerial;
        }

        [[nodiscard]] bool                  Select(ZEngine::ECS::EntityID entity, SelectionMode mode = SelectionMode::Replace);
        [[nodiscard]] bool                  SetSelection(std::span<const EntityTarget> targets, const uuids::uuid& active = {});
        [[nodiscard]] bool                  SelectScene(SceneTarget target);
        /// Borrowed topology-pruned roots in selection order; the active target is unchanged.
        /// Requires the owner thread and a synchronized hierarchy. No authored mutation.
        [[nodiscard]] bool                  ReadSelectedRoots(ZEngine::Core::Containers::ArrayView<const EntityTarget>& output);
        [[nodiscard]] bool                  Reparent(ZEngine::ECS::EntityID child, ZEngine::ECS::EntityID parent);
        void                                ClearSelection();

        [[nodiscard]] bool                  BeginTransform(std::span<const EntityTarget> roots, const ZEngine::Core::Maths::Vec3f& pivot = {}, TransformSpacePolicy space = TransformSpacePolicy::World);
        [[nodiscard]] bool                  PreviewTransforms(std::span<const ZEngine::ECS::LocalTransformValue> values);
        [[nodiscard]] bool                  BeginField(const EntityTarget& target, ZEngine::ECS::AuthoredFloatField field);
        [[nodiscard]] bool                  PreviewField(float value);
        /// Shared scene-setting transaction, independent of entity selection.
        [[nodiscard]] bool                  BeginGridSettings();
        [[nodiscard]] bool                  PreviewGridSettings(const ZEngine::ECS::SceneGridSettings& value);
        /// Atomic copied-value UI intent. Preflight before cancelling any live interaction.
        [[nodiscard]] bool                  CanSetGridSettings(const ZEngine::ECS::SceneGridSettings& expected, const ZEngine::ECS::SceneGridSettings& value) const;
        [[nodiscard]] bool                  SetGridSettings(const ZEngine::ECS::SceneGridSettings& expected, const ZEngine::ECS::SceneGridSettings& value);
        [[nodiscard]] bool                  Commit();
        void                                Cancel();
        [[nodiscard]] bool                  Undo();
        [[nodiscard]] bool                  Redo();
        [[nodiscard]] bool                  MarkSaved();
        /// @brief Before any as-yet-untracked authored mutation, cancel and invalidate incompatible history.
        /// Scene-only settings may pass false; cancellation still preserves any pending TRS sync.
        void                                InvalidateForExternalMutation(bool hierarchy_changed = true);

        [[nodiscard]] const SelectionModel& Selection() const
        {
            return m_selection;
        }
        [[nodiscard]] const Gizmo::State& GizmoSettings() const
        {
            return m_gizmo;
        }
        [[nodiscard]] EntityTarget           TargetFor(ZEngine::ECS::EntityID entity);
        [[nodiscard]] ZEngine::ECS::EntityID Resolve(const EntityTarget& target);
        [[nodiscard]] ZEngine::ECS::EntityID Resolve(const uuids::uuid& identity);
        [[nodiscard]] const EditorHistory&   History() const
        {
            return m_history;
        }
        [[nodiscard]] ZEngine::ECS::Scene* Scene() const
        {
            return m_scene;
        }
        [[nodiscard]] uint64_t SceneEpoch() const
        {
            return m_epoch;
        }
        [[nodiscard]] uint64_t DocumentRevision() const
        {
            return m_document_revision;
        }
        [[nodiscard]] bool Active() const
        {
            return m_active != Interaction::None;
        }
        [[nodiscard]] bool Ready() const
        {
            return m_initialized;
        }
        [[nodiscard]] bool OnOwnerThread() const
        {
            return m_owner == std::this_thread::get_id() && m_initialized;
        }
        [[nodiscard]] bool InMutationPhase() const
        {
            return Allowed();
        }
        [[nodiscard]] bool Editable() const
        {
            return m_initialized && m_editable && m_scene;
        }
        [[nodiscard]] bool CanUndo() const
        {
            return Editable() && !Active() && m_history.UndoCount() != 0;
        }
        [[nodiscard]] bool CanRedo() const
        {
            return Editable() && !Active() && m_history.RedoCount() != 0;
        }
        [[nodiscard]] uint32_t PendingRequestCount() const
        {
            return m_request_count;
        }
        static constexpr uint32_t MaxPendingRequests = 128;
        [[nodiscard]] bool        Dirty() const
        {
            return !m_saved_content_known || m_content_digest != m_saved_digest;
        }
        [[nodiscard]] uint32_t                                 TransformCount() const;
        [[nodiscard]] const ZEngine::ECS::LocalTransformValue* TransformBefore(uint32_t index) const;
        /// Borrow only during this owner-thread read, after synchronization.
        /// Validates the entire live batch once; never publishes a partial edit
        /// set. Visual prefix limits are reported separately from TotalRoots.
        [[nodiscard]] bool                                     ReadGizmoRootFrames(Gizmo::RootFrameView& output);
        [[nodiscard]] cstring                                  LastError() const
        {
            return m_last_error;
        }

    private:
        enum class Interaction : uint8_t
        {
            None,
            Transform,
            Field,
            Pivot,
            Grid,
        };

        struct alignas(16) TransformBatchHeader
        {
            uint64_t                    Epoch = 0;
            uint32_t                    Count = 0;
            TransformSpacePolicy        Space = TransformSpacePolicy::World;
            ZEngine::Core::Maths::Vec3f Pivot = {};
        };

        struct TransformChange
        {
            uuids::uuid                       UUID   = {};
            ZEngine::ECS::LocalTransformValue Before = {};
            ZEngine::ECS::LocalTransformValue After  = {};
        };

        struct TransformRuntime
        {
            ZEngine::ECS::EntityID      Entity              = {};
            ZEngine::ECS::EntityID      Parent              = {};
            ZEngine::Core::Maths::Vec3f PreviousPosition    = {};
            bool                        HadParent           = false;
            uint64_t                    ParentWorldRevision = 0;
            uint64_t                    ComponentLifetime   = 0;
        };

        struct FieldChange
        {
            uuids::uuid                      UUID   = {};
            uint64_t                         Epoch  = 0;
            ZEngine::ECS::AuthoredFloatField Field  = ZEngine::ECS::AuthoredFloatField::LightRange;
            float                            Before = 0.f;
            float                            After  = 0.f;
        };

        struct GridChange
        {
            uuids::uuid                   SceneUUID = {};
            uint64_t                      Epoch     = 0;
            ZEngine::ECS::SceneGridBinary Before = {}, After = {};
        };
        static_assert(sizeof(GridChange) <= 320);

        enum class RequestKind : uint8_t
        {
            Command,
            EntitySelection,
            EntitySelectionRange,
            SceneSelection,
            TransformValue,
            FieldValue,
            ControlDrag,
            Reparent,
            Rename,
            AddComponent,
            UntrackedProperty,
            DeleteEntity,
            CreateCollection,
            GizmoSettings,
        };

        struct PendingRequest
        {
            EntityTarget                      Target          = {};
            EntityTarget                      ParentTarget    = {};
            ZEngine::ECS::EntityID            ExpectedParent  = {};
            uint64_t                          Epoch           = 0;
            RequestKind                       Kind            = RequestKind::Command;
            EditorCommand                     Command         = EditorCommand::Cancel;
            SelectionMode                     Mode            = SelectionMode::Replace;
            uuids::uuid                       SelectionActive = {};
            uint32_t                          SelectionFirst = 0, SelectionCount = 0;
            bool                              SelectionAdditive   = false;
            SceneTarget                       Scene               = SceneTarget::None;
            uint64_t                          SelectionRevision   = 0;
            uint64_t                          HierarchyRevision   = 0;
            uint64_t                          ParentWorldRevision = 0;
            uint64_t                          FrameRevision       = 0;
            bool                              CapturedFrame       = false;
            ZEngine::ECS::LocalTransformValue Transform           = {};
            ZEngine::ECS::LocalTransformValue ExpectedTransform   = {};
            EditorControlDrag                 Control             = {};
            ZEngine::ECS::AuthoredFloatField  Field               = ZEngine::ECS::AuthoredFloatField::LightRange;
            float                             Value               = 0.f;
            float                             ExpectedValue       = 0.f;
            EditorUntrackedProperty           Property            = EditorUntrackedProperty::LightColor;
            EditorUntrackedValue              PropertyValue       = {};
            EditorUntrackedValue              ExpectedProperty    = {};
            Gizmo::Change                     GizmoChange         = {};
            std::array<char, 128>             Name                = {};
            std::array<char, 128>             ExpectedName        = {};
        };

        bool                                                                                    Enqueue(const PendingRequest& request);
        bool                                                                                    ApplyGizmoChange(const Gizmo::Change& change);
        bool                                                                                    ApplySelectionSet(std::span<const EntityTarget> targets, const uuids::uuid& active, bool additive);
        bool                                                                                    CaptureEditRequest(PendingRequest& request, ZEngine::ECS::EntityID entity, bool transform, ZEngine::ECS::AuthoredFloatField field);
        bool                                                                                    MatchesEditRequest(const PendingRequest& request);
        bool                                                                                    ApplyValueRequest(const PendingRequest& request);
        bool                                                                                    BeginControlRequest(const PendingRequest& request);
        bool                                                                                    DiscardEditRequests(bool include_untracked);
        bool                                                                                    Allowed() const;
        bool                                                                                    Reject(cstring message);
        bool                                                                                    ValidateActive();
        bool                                                                                    BeginGizmoTransform(const Gizmo::TargetFrame& frame, Gizmo::Operation operation);
        bool                                                                                    ValidateGizmoObservers();
        bool                                                                                    ValidatePivot();
        bool                                                                                    ValidateLight();
        bool                                                                                    ValidateCamera();
        bool                                                                                    ValidateGrid() const;
        bool                                                                                    PreviewGridTranslation(const ZEngine::Core::Maths::Vec3f& origin);
        bool                                                                                    PreviewGridRotation(const ZEngine::Core::Maths::Vec3f& axis, double angle);
        bool                                                                                    PrepareGridSettings(const ZEngine::ECS::SceneGridSettings& expected, const ZEngine::ECS::SceneGridSettings& value, ZEngine::ECS::SceneGridBinary& before, ZEngine::ECS::SceneGridBinary& after) const;
        void                                                                                    CancelGrid();
        void                                                                                    SetCustomPivot(const Gizmo::PivotState& value);
        void                                                                                    SynchronizeHierarchy();
        void                                                                                    AccountTransform(const uuids::uuid& identity, const ZEngine::ECS::LocalTransformValue& before, const ZEngine::ECS::LocalTransformValue& after);
        void                                                                                    AccountField(const uuids::uuid& identity, ZEngine::ECS::AuthoredFloatField field, float before, float after);
        void                                                                                    AccountGrid(const uuids::uuid& identity, const ZEngine::ECS::SceneGridBinary& before, const ZEngine::ECS::SceneGridBinary& after);
        void                                                                                    StartInteraction(Interaction kind);
        TransformBatchHeader&                                                                   Batch();
        TransformChange*                                                                        Changes();
        const TransformChange*                                                                  Changes() const;
        static EditorOperation                                                                  TransformOperation();
        static EditorOperation                                                                  FieldOperation();
        static EditorOperation                                                                  GridOperation();
        static bool                                                                             ValidateGridOperation(void* context, std::span<const std::byte> payload, bool undo);
        static void                                                                             ApplyGridOperation(void* context, std::span<const std::byte> payload, bool undo);
        static bool                                                                             ValidateTransformOperation(void* context, std::span<const std::byte> payload, bool undo);
        static void                                                                             ApplyTransformOperation(void* context, std::span<const std::byte> payload, bool undo);
        static bool                                                                             ValidateFieldOperation(void* context, std::span<const std::byte> payload, bool undo);
        static void                                                                             ApplyFieldOperation(void* context, std::span<const std::byte> payload, bool undo);

        SelectionModel                                                                          m_selection                 = {};
        SelectedRootCache                                                                       m_selected_roots            = {};
        ZEngine::Core::Containers::Array<EntityTarget>                                          m_selection_requests        = {};
        Gizmo::State                                                                            m_gizmo                     = {};
        EntityTargetResolver                                                                    m_targets                   = {};
        EditorHistory                                                                           m_history                   = {};
        EditorSceneMutationHooks                                                                m_scene_mutations           = {};
        EditorSceneSettingsHooks                                                                m_scene_settings            = {};
        ZEngine::ECS::Scene*                                                                    m_scene                     = nullptr;
        ZEngine::Core::Containers::UnorderedHashMap<uuids::uuid, uint32_t>                      m_unique                    = {};
        TransformRuntime*                                                                       m_runtime                   = nullptr;
        Gizmo::TransformRoot*                                                                   m_gizmo_roots               = nullptr;
        ZEngine::ECS::LocalTransformValue*                                                      m_gizmo_values              = nullptr;
        Gizmo::TransformObserver*                                                               m_gizmo_observers           = nullptr;
        uint32_t                                                                                m_gizmo_observer_count      = 0;
        ZEngine::Core::Containers::FixedArray<Gizmo::RootFrame, Gizmo::RootFrameView::Capacity> m_gizmo_root_frames         = {};
        uint64_t                                                                                m_gizmo_root_generation     = 0;
        uint32_t                                                                                m_gizmo_root_frame_count    = 0;
        std::byte*                                                                              m_transaction               = nullptr;
        size_t                                                                                  m_transaction_bytes         = 0;
        uint32_t                                                                                m_max_targets               = 0;
        FieldChange                                                                             m_field                     = {};
        GridChange                                                                              m_grid                      = {};
        ZEngine::ECS::EntityID                                                                  m_field_entity              = {};
        std::thread::id                                                                         m_owner                     = {};
        uint64_t                                                                                m_epoch                     = 0;
        uint64_t                                                                                m_document_revision         = 0;
        uint64_t                                                                                m_start_structural_revision = 0;
        uint64_t                                                                                m_start_selection_revision  = 0;
        uint64_t                                                                                m_start_hierarchy_revision  = 0;
        uint64_t                                                                                m_field_frame_revision      = 0;
        uint64_t                                                                                m_field_component_lifetime  = 0;
        Interaction                                                                             m_active                    = Interaction::None;
        cstring                                                                                 m_last_error                = "";
        bool                                                                                    m_initialized               = false;
        bool                                                                                    m_in_phase                  = false;
        bool                                                                                    m_editable                  = true;
        bool                                                                                    m_preview_changed           = false;
        bool                                                                                    m_hierarchy_pending         = false;
        EditorControlDrag                                                                       m_control                   = {};
        Gizmo::TranslationInteraction                                                           m_translation               = {};
        Gizmo::RotationInteraction                                                              m_rotation                  = {};
        Gizmo::ScaleInteraction                                                                 m_scale                     = {};
        Gizmo::LightInteraction                                                                 m_light                     = {};
        Gizmo::CameraInteraction                                                                m_camera_gizmo              = {};
        Gizmo::PivotInteraction                                                                 m_pivot                     = {};
        Gizmo::PivotState                                                                       m_custom_pivot              = {};
        std::array<PendingRequest, MaxPendingRequests>                                          m_requests                  = {};
        uint32_t                                                                                m_request_first             = 0;
        uint32_t                                                                                m_request_count             = 0;
        // Incremental semantic source digest, relative to the bound scene. All untracked
        // mutations invalidate this checkpoint; no scene scan or heap allocation per preview.
        std::array<uint64_t, 2>                                                                 m_content_digest            = {};
        std::array<uint64_t, 2>                                                                 m_saved_digest              = {};
        bool                                                                                    m_saved_content_known       = true;
    };
} // namespace Tetragrama
