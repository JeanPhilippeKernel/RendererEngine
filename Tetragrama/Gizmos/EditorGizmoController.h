#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/EntityID.h>
#include <ZEngine/Rendering/Renderers/GizmoRenderData.h>
#include <ZEngine/UI/ZUIContext.h>

namespace ZEngine::ECS
{
    class Scene;
}
namespace ZEngine::ECS::Tools
{
    struct GizmoRay;
}
namespace ZEngine::Rendering::Cameras
{
    struct FlyCamera;
}
namespace Tetragrama
{
    struct EditorScene;
}

namespace Tetragrama::Gizmos
{
    /// @brief Native viewport gizmo interaction controller.
    /// @details The controller is main-thread-only. It keeps all manipulation state in
    /// fixed-capacity Arrays, leases pointer input through ZUI, and publishes only a
    /// value-type render snapshot for the asynchronous renderer.
    class EditorGizmoController
    {
    public:
        enum class CoordinateSpace : uint8_t
        {
            World,
            Local,
        };

        void                                                                 Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, uint32_t maximum_targets);

        void                                                                 SetOperation(ZEngine::Rendering::Renderers::GizmoOperation operation);
        [[nodiscard]] ZEngine::Rendering::Renderers::GizmoOperation          Operation() const;
        void                                                                 SetCoordinateSpace(CoordinateSpace space);
        [[nodiscard]] CoordinateSpace                                        Space() const;
        [[nodiscard]] bool                                                   IsManipulating() const;

        /// @brief Processes one UI frame for the viewport and updates the render snapshot.
        /// @param viewport_rect Logical UI coordinates [left, top, right, bottom].
        void                                                                 Update(ZEngine::UI::ZUIContext* ctx, EditorScene& editor_scene, ZEngine::ECS::Scene& scene, const ZEngine::Rendering::Cameras::FlyCamera& camera, const float viewport_rect[4], uint64_t viewport_key, uint64_t viewport_generation);

        [[nodiscard]] const ZEngine::Rendering::Renderers::GizmoRenderState& RenderState() const;

    private:
        using GizmoHandle    = ZEngine::Rendering::Renderers::GizmoHandle;
        using GizmoOperation = ZEngine::Rendering::Renderers::GizmoOperation;

        struct DragTarget
        {
            ZEngine::ECS::EntityID      Entity       = ZEngine::ECS::INVALID_ENTITY;
            ZEngine::Core::Maths::Mat4f InitialWorld = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        };

        /// @brief Camera state frozen at drag start.
        /// @details A pointer lease prevents normal camera navigation, but a
        /// manipulator must also remain stable if another editor action changes
        /// the live camera while the mouse is held.
        struct DragCamera
        {
            ZEngine::Core::Maths::Mat4f ViewProjection = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
            ZEngine::Core::Maths::Vec3f Position       = {};
            ZEngine::Core::Maths::Vec3f Forward        = {0.f, 0.f, -1.f};
            ZEngine::Core::Maths::Vec3f Right          = {1.f, 0.f, 0.f};
            ZEngine::Core::Maths::Vec3f Up             = {0.f, 1.f, 0.f};
            float                       Rect[4]        = {};
            float                       InputScale[2]  = {1.f, 1.f};
            float                       ProjectionY    = 1.f;
            float                       AspectRatio    = 1.f;
            bool                        Orthographic   = false;
            bool                        Valid          = false;
        };

        struct DragState
        {
            ZEngine::UI::ZUIPointerCaptureToken CaptureToken       = 0;
            GizmoHandle                         Handle             = GizmoHandle::None;
            ZEngine::Core::Maths::Vec3f         Pivot              = {};
            ZEngine::Core::Maths::Vec3f         AxisA              = {1.f, 0.f, 0.f};
            ZEngine::Core::Maths::Vec3f         AxisB              = {0.f, 1.f, 0.f};
            ZEngine::Core::Maths::Vec3f         AxisNormal         = {0.f, 0.f, 1.f};
            float                               GizmoScale         = 1.f;
            float                               StartScalar        = 0.f;
            float                               StartRadius        = 0.f;
            ZEngine::Core::Maths::Vec3f         StartPoint         = {};
            float                               StartScreen[2]     = {};
            float                               ScreenRotationSign = 1.f;
            bool                                UseWorldRotation   = false;
            DragCamera                          Camera             = {};
            bool                                Active             = false;
        };

        [[nodiscard]] bool                                            RebuildPresentation(EditorScene& editor_scene, ZEngine::ECS::Scene& scene, const ZEngine::Rendering::Cameras::FlyCamera& camera, const float viewport_rect[4], const float input_scale[2]);
        [[nodiscard]] GizmoHandle                                     HitTest(const ZEngine::Rendering::Cameras::FlyCamera& camera, const float viewport_rect[4], float mouse_x, float mouse_y) const;
        [[nodiscard]] bool                                            BeginDrag(ZEngine::UI::ZUIContext* ctx, EditorScene& editor_scene, ZEngine::ECS::Scene& scene, const ZEngine::Rendering::Cameras::FlyCamera& camera, const float viewport_rect[4], uint64_t viewport_key, uint64_t viewport_generation, GizmoHandle handle);
        [[nodiscard]] bool                                            PreviewDrag(ZEngine::UI::ZUIContext* ctx, EditorScene& editor_scene, ZEngine::ECS::Scene& scene);
        void                                                          FinishDrag(ZEngine::UI::ZUIContext* ctx, EditorScene& editor_scene, ZEngine::ECS::Scene& scene, bool commit);
        void                                                          ConsumePointerEnd(ZEngine::UI::ZUIContext* ctx, EditorScene& editor_scene, ZEngine::ECS::Scene& scene);
        void                                                          ResetDrag();

        [[nodiscard]] bool                                            CaptureDragCamera(const ZEngine::UI::ZUIContext& ctx, const ZEngine::Rendering::Cameras::FlyCamera& camera, const float viewport_rect[4]);
        [[nodiscard]] bool                                            CursorRayForDrag(const ZEngine::UI::ZUIContext& ctx, ZEngine::ECS::Tools::GizmoRay& out_ray) const;
        [[nodiscard]] bool                                            ProjectDragPoint(ZEngine::Core::Maths::Vec3f point, float& out_x, float& out_y) const;

        [[nodiscard]] bool                                            BuildRoots(const ZEngine::ECS::Scene& scene, ZEngine::Core::Containers::ArrayView<const ZEngine::ECS::EntityID> selection);

        GizmoOperation                                                m_operation           = GizmoOperation::None;
        CoordinateSpace                                               m_space               = CoordinateSpace::World;
        GizmoHandle                                                   m_hovered             = GizmoHandle::None;
        DragState                                                     m_drag                = {};
        ZEngine::UI::ZUIPointerCaptureToken                           m_pending_capture_end = 0;
        ZEngine::Core::Containers::Array<ZEngine::ECS::EntityID>      m_roots               = {};
        ZEngine::Core::Containers::Array<DragTarget>                  m_drag_targets        = {};
        ZEngine::Core::Containers::Array<ZEngine::Core::Maths::Mat4f> m_preview_world       = {};
        ZEngine::Rendering::Renderers::GizmoRenderState               m_render              = {};
    };
} // namespace Tetragrama::Gizmos
