#pragma once
#include <ZEngine/Core/Maths/Geometry.h>
#include <ZEngine/Rendering/Renderers/GizmoFrameData.h>

namespace ZEngine::Rendering::Renderers
{
    enum class GizmoEmitResult : uint8_t
    {
        Emitted,
        Culled,
        Invalid,
        CapacityExceeded
    };

    /// Allocation-free, checked primitive sink. Clips in world space against the
    /// camera's actual near/far planes (including reverse-Z), then converts logical
    /// pixel sizes to render-target pixels exactly once. It never clears storage.
    class GizmoPrimitiveBuilder
    {
    public:
        GizmoPrimitiveBuilder(GizmoFrameData& output, const Core::Maths::Geometry::ProjectionFrame& camera, float logical_height, uint32_t target_width, uint32_t target_height);
        [[nodiscard]] GizmoEmitResult Line(const GizmoLine& line);
        [[nodiscard]] GizmoEmitResult Triangle(const GizmoTriangle& triangle);
        [[nodiscard]] GizmoEmitResult Sprite(const GizmoSprite& sprite);
        [[nodiscard]] bool            Valid() const
        {
            return m_valid;
        }

    private:
        [[nodiscard]] bool                     Distance(const Core::Maths::Vec3f& point, bool near_plane, double* distance) const;
        [[nodiscard]] bool                     Visible(const Core::Maths::Vec3f& point) const;
        [[nodiscard]] static bool              Intersection(const Core::Maths::Vec3f& a, const Core::Maths::Vec3f& b, double da, double db, Core::Maths::Vec3f* output);
        GizmoFrameData&                        m_output;
        Core::Maths::Geometry::ProjectionFrame m_camera          = {};
        float                                  m_pixel_scale     = 0.f;
        Core::Maths::Geometry::Viewport        m_target_viewport = {};
        bool                                   m_valid           = false;
    };
} // namespace ZEngine::Rendering::Renderers
