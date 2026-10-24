#pragma once
#include <Tetragrama/Gizmo/GizmoTargetFrame.h>
#include <Tetragrama/Gizmo/GizmoTransformBatch.h>
#include <ZEngine/UI/ZUIContext.h>

namespace Tetragrama::Gizmo
{
    /// Main-thread viewport presentation/capture. Owns reusable value storage,
    /// never an ECS pointer, private selection, transaction or history stack.
    class Viewport
    {
    public:
        void               Bind(const ZEngine::UI::ZUIContext* ctx, const float rect[4], uint64_t viewport_key, uint64_t hit_key);
        [[nodiscard]] bool Bound(const ZEngine::UI::ZUIContext* ctx) const;
        /// Exact physical owner only; an arbitrary active session is not a viewport drag.
        [[nodiscard]] bool OwnsPointerCapture(const EditorSession& session, const ZEngine::UI::ZUIContext* ctx) const;
        void               Invalidate();
        void               Unbind()
        {
            m_bound = false;
            Invalidate();
        }
        /// Hierarchy must already be synchronized by the exclusive editor phase.
        void                      BuildHitFrame(EditorSession& session, const ZEngine::Rendering::Cameras::CameraFrameData& camera, const ZEngine::UI::ZUIContext* ctx, bool focused);
        /// Re-evaluate retained UI before camera navigation, without consuming input.
        void                      UpdateHover(EditorSession& session, const ZEngine::Rendering::Cameras::CameraFrameData& camera, ZEngine::UI::ZUIContext* ctx, bool focused);
        /// Every-update physical routing, inside the session's mutation phase.
        /// Begin remains explicitly gated until M0 acceptance is complete. An
        /// already-owned drag still processes release/cancel when begin is off.
        void                      UpdateTransformInput(EditorSession& session, const ZEngine::Rendering::Cameras::CameraFrameData& camera, ZEngine::UI::ZUIContext* ctx, bool focused, bool allow_begin);
        /// Build again with the exact camera captured for the published render.
        void                      BuildRender(EditorSession& session, const ZEngine::Rendering::Cameras::CameraFrameData& camera, const ZEngine::UI::ZUIContext* ctx, bool focused, uint32_t width, uint32_t height, ZEngine::Rendering::Renderers::GizmoFrameData& output);
        /// UI-only presentation from the same copied session snapshot, never an
        /// authored writer. May be skipped when an overlay slot is unavailable.
        void                      BuildPill(EditorSession& session, ZEngine::UI::ZUIContext* ctx);
        [[nodiscard]] static bool Filter(const ZEngine::UI::ZUIBox* box, const float point[2], const void* data);
        [[nodiscard]] Handle      Hovered() const
        {
            return m_hovered;
        }
        [[nodiscard]] const HitFrame& Hits() const
        {
            return m_hits;
        }
        [[nodiscard]] uint64_t HitKey() const
        {
            return m_hit_key;
        }

    private:
        void                      BuildHitFrame(EditorSession& session, const ZEngine::Rendering::Cameras::CameraFrameData& camera, const ZEngine::UI::ZUIContext* ctx, bool focused, bool pivot_editable);
        [[nodiscard]] static bool SameCamera(const ZEngine::Rendering::Cameras::CameraFrameData& a, const ZEngine::Rendering::Cameras::CameraFrameData& b);
        [[nodiscard]] bool        Eligible(const EditorSession& session, const ZEngine::UI::ZUIContext* ctx, bool focused) const;
        [[nodiscard]] Handle      QueryHover(const ZEngine::UI::ZUIContext* ctx) const;
        void                      UpdateGestures(const ZEngine::UI::ZUIContext* ctx);
        void                      BuildTranslationPresentation(EditorSession& session, FrameInput& input);
        void                      BuildRotationPresentation(EditorSession& session, FrameInput& input);
        void                      BuildScalePresentation(EditorSession& session, FrameInput& input);
        void                      BuildPivotPresentation(EditorSession& session, FrameInput& input);
        void                      BuildTransformDecorations(EditorSession& session, FrameInput& input, const EntityTarget& target, const ZEngine::Core::Maths::Mat4f& start, const ZEngine::Core::Maths::Mat4f& world, uint64_t key, uint64_t serial);
        [[nodiscard]] bool        BuildRootDecorations(EditorSession& session, FrameInput& input);
        void                      BuildBoxPresentation(EditorSession& session, FrameInput& input, const EntityTarget& target, const ZEngine::Core::Maths::Mat4f& start, const ZEngine::Core::Maths::Mat4f& world, uint64_t key, uint64_t serial);
        void                      RetirePointerCapture(EditorSession& session, ZEngine::UI::ZUIContext* ctx);

        // Physical-routing validity only. The session owns the captured camera
        // mapping, targets, immutable before-values and authored transaction.
        struct PointerCapture
        {
            ZEngine::Rendering::Cameras::CameraFrameData Camera = {};
            uint64_t                                     Key = 0, Serial = 0, SceneEpoch = 0, BindingRevision = 0;
            Operation                                    Mode       = Operation::Select;
            Handle                                       Constraint = Handle::None;
        };
        static_assert(sizeof(PointerCapture) <= 256);

        ZEngine::Rendering::Renderers::GizmoFrameData m_scratch       = {};
        HitFrame                                      m_hits          = {};
        FrameInput                                    m_input         = {};
        TargetFrame                                   m_target        = {};
        LightFrame                                    m_light_target  = {};
        CameraFrame                                   m_camera_target = {};
        ZEngine::Core::Maths::Geometry::Viewport      m_bounds        = {};
        uint64_t                                      m_ui_frame = 0, m_viewport_key = 0, m_hit_key = 0;
        uint32_t                                      m_screen_w = 0, m_screen_h = 0;
        float                                         m_input_scale[2]  = {};
        uint64_t                                      m_press_serial[3] = {};
        bool                                          m_was_down[3] = {}, m_navigation[3] = {}, m_handle_press[3] = {};
        Handle                                        m_hovered = Handle::None;
        bool                                          m_bound = false, m_cache_valid = false, m_allow_hits = false;
        uint64_t                                      m_drag_key = 0, m_drag_serial = 0, m_drag_epoch = 0;
        uuids::uuid                                   m_drag_mesh = {};
        ZEngine::Core::Maths::Vec3f                   m_drag_min = {}, m_drag_max = {};
        ZEngine::Core::Maths::Geometry::BoxCorners    m_ghost       = {};
        bool                                          m_ghost_valid = false;
        struct RootBounds
        {
            ZEngine::Core::Maths::Vec3f Minimum = {}, Maximum = {};
            bool                        Valid = false;
        };
        ZEngine::Core::Containers::FixedArray<TransformDecorations, RootFrameView::Capacity> m_root_decorations = {};
        ZEngine::Core::Containers::FixedArray<RootBounds, RootFrameView::Capacity>           m_root_bounds      = {};
        // Comparison-only cache owner; never dereferenced or published.
        const EditorSession*                                                                 m_root_session     = nullptr;
        uint64_t                                                                             m_root_generation = 0, m_root_epoch = 0;
        uint64_t                                                                             m_binding_revision = 0, m_observed_left_press = 0;
        PointerCapture                                                                       m_pointer_capture = {};
    };
} // namespace Tetragrama::Gizmo
