#pragma once
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <ZEngine/Rendering/Renderers/GridLabelData.h>
#include <ZEngine/Rendering/Scenes/SceneFrameData.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

namespace ZEngine::Rendering::Scenes
{
    /// Immutable presentation identity for candidate target preparation. Headless
    /// requests leave Captured false and do not establish viewport readiness.
    struct SceneInitialView
    {
        Cameras::CameraFrameData         Camera         = {};
        uint32_t                         Width          = 0;
        uint32_t                         Height         = 0;
        uint64_t                         ResizeSequence = 0;
        bool                             ShowGrid       = true;
        bool                             Captured       = false;
        Renderers::GridLabelFontSnapshot GridLabelFont  = {};
    };

    /// Camera-independent draw expansion from exact retained resource versions.
    /// Pick-request association and transparent ordering remain presentation data.
    struct PreparedSceneDraw
    {
        Meshes::SubMeshAllocation Allocation  = {};
        Core::Maths::Vec4f        WorldBounds = {};
        ECS::EntityID             Entity      = ECS::INVALID_ENTITY;
        bool                      Transparent = false;
    };

    /// Borrowed only while the corresponding scene resource lease is retained.
    /// No pointers to ECS, mutable asset caches or producer-owned scene arrays.
    struct PreparedSceneFrame
    {
        std::span<const Core::Maths::Mat4f>       Transforms            = {};
        std::span<const PreparedSceneDraw>        Draws                 = {};
        LightArrayUBO                             Lights                = {};
        SceneGridRenderSnapshot                   Grid                  = {};
        const Core::Memory::BufferView*           TransformBuffer       = nullptr;
        const Core::Memory::BufferView*           LightBuffer           = nullptr;
        const Core::Memory::BufferView*           MaterialBuffer        = nullptr;
        const Core::Memory::BufferView*           DrawBuffer            = nullptr;
        std::span<const Core::Memory::BufferView> CullingInputBuffers   = {};
        std::span<const Core::Memory::BufferView> CulledIndirectBuffers = {};
        SceneInitialView                          View                  = {};
        std::span<const Core::Memory::BufferView> SelectionTokenBuffers = {};
        uint32_t                                  OpaqueDrawCount       = 0;
        bool                                      PresentationPrepared  = false;
        const Core::Memory::BufferView*           CameraBuffer          = nullptr;
    };

    inline Core::Maths::Vec4f MakeDrawWorldBounds(const Core::Maths::Mat4f& transform, const Core::Maths::Vec3f& center, float radius)
    {
        auto world = transform * Core::Maths::Vec4f(center, 1.f);
        if (radius <= 0.f)
        {
            world.w = -1.f;
            return world;
        }
        // Nested rotation and nonuniform scale can shear the world transform.
        // The largest column length then underestimates the sphere's radius.
        // Bound the largest eigenvalue of A^T A by its absolute row-sum norm;
        // for orthogonal scaled columns this retains the ordinary max scale.
        // Double intermediates avoid overflow when squaring finite float scales.
        double scale_squared = 0.0;
        for (uint32_t column = 0; column < 3; ++column)
        {
            double row_sum = 0.0;
            for (uint32_t other = 0; other < 3; ++other)
            {
                double dot = 0.0;
                for (uint32_t row = 0; row < 3; ++row)
                {
                    dot += double(transform(row, column)) * double(transform(row, other));
                }
                row_sum += std::abs(dot);
            }
            scale_squared = std::max(scale_squared, row_sum);
        }
        const double world_radius = double(radius) * std::sqrt(scale_squared);
        world.w                   = static_cast<float>(world_radius);
        if (world_radius > 0.0 && std::isfinite(world.w))
        {
            // Round outward rather than accidentally excluding a surface point.
            world.w = std::nextafter(world.w, std::numeric_limits<float>::infinity());
        }
        return world;
    }
} // namespace ZEngine::Rendering::Scenes
