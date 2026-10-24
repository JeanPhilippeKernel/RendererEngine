#pragma once
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <uuid.h>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace ZEngine::Rendering::Renderers
{
    // Compact signed-normalized lanes, rather than four-lane math vectors.
    // Adjacent endpoints share the exact packed value before GPU conversion.
    struct GizmoJoinNormal
    {
        int16_t                 x = 0, y = 0;
        static constexpr double Scale = 32767.0, UnitTolerance = 1e-4;
        [[nodiscard]] bool      Enabled() const
        {
            return x != 0 || y != 0;
        }
        [[nodiscard]] bool Valid() const
        {
            const double square = (double(x) * x + double(y) * y) / (Scale * Scale);
            return square == 0.0 || std::abs(square - 1.0) <= UnitTolerance;
        }
        [[nodiscard]] float NormalX() const
        {
            return float(x) * float(1.0 / Scale);
        }
        [[nodiscard]] float NormalY() const
        {
            return float(y) * float(1.0 / Scale);
        }
        [[nodiscard]] static GizmoJoinNormal FromUnit(double x, double y)
        {
            const double square = x * x + y * y;
            if (!std::isfinite(square) || std::abs(square - 1.0) > UnitTolerance)
            {
                return {};
            }
            x = x < -1.0 ? -1.0 : x > 1.0 ? 1.0 : x;
            y = y < -1.0 ? -1.0 : y > 1.0 ? 1.0 : y;
            return {static_cast<int16_t>(std::lround(x * Scale)), static_cast<int16_t>(std::lround(y * Scale))};
        }
    };
    static_assert(sizeof(GizmoJoinNormal) == 4);

    struct GizmoLine
    {
        Core::Maths::Vec3f A = {}, B = {};
        float              Width = 0.f, DashOn = 0.f, DashOff = 0.f, DashPhase = 0.f;
        uint32_t           Color           = 0;
        bool               RoundCaps       = false;
        // Optional screen-pixel tangent bisectors at shared polyline vertices.
        // Opposite half-open ownership avoids both cap gaps and alpha overlap.
        GizmoJoinNormal    StartJoinNormal = {}, EndJoinNormal = {};
        [[nodiscard]] bool ValidJoins() const
        {
            return StartJoinNormal.Valid() && EndJoinNormal.Valid() && (RoundCaps || (!StartJoinNormal.Enabled() && !EndJoinNormal.Enabled()));
        }
    };
    static_assert(sizeof(GizmoLine) <= 64);
    struct GizmoTriangle
    {
        Core::Maths::Vec3f A = {}, B = {}, C = {};
        uint32_t           Color          = 0;
        // Bits 0/1/2 cover the edges opposite A/B/C. Shared fill diagonals
        // must not receive edge coverage or adjacent triangles show a seam.
        uint32_t           AntiAliasEdges = 7;
    };
    struct GizmoSprite
    {
        Core::Maths::Vec3f Position = {};
        float              Size = 0.f, OutlineWidth = 0.f;
        uint32_t           Fill = 0, Outline = 0;
        bool               Round = false;
    };

    /// Optional contiguous box-edge range. The render thread alone checks GPU
    /// residency before packing it; producers never read render-owned mesh slots.
    struct GizmoMeshLines
    {
        uuids::uuid MeshUUID = {};
        uint32_t    First = 0, Count = 0;
    };

    /// World-space primitives with render-target-pixel widths. No borrowed editor
    /// state. Reset/copy only live prefixes; keep this storage across updates.
    struct GizmoFrameData
    {
        static constexpr uint32_t MaxLines = 2048, MaxTriangles = 256, MaxSprites = 128;
        static constexpr uint32_t MaxMeshLineGroups = 128;
        uint32_t                  LineCount = 0, TriangleCount = 0, SpriteCount = 0;
        uint32_t                  MeshLineGroupCount                = 0;
        bool                      DecorationsOmitted                = false;
        GizmoLine                 Lines[MaxLines]                   = {};
        GizmoTriangle             Triangles[MaxTriangles]           = {};
        GizmoSprite               Sprites[MaxSprites]               = {};
        GizmoMeshLines            MeshLineGroups[MaxMeshLineGroups] = {};

        GizmoFrameData()                                            = default;
        GizmoFrameData(const GizmoFrameData& other) : GizmoFrameData()
        {
            *this = other;
        }
        GizmoFrameData& operator=(const GizmoFrameData& other)
        {
            if (this == &other)
            {
                return *this;
            }
            if (!other.ValidCounts() || !other.ValidMeshLineGroups())
            {
                Reset();
                return *this;
            }
            LineCount          = other.LineCount;
            TriangleCount      = other.TriangleCount;
            SpriteCount        = other.SpriteCount;
            MeshLineGroupCount = other.MeshLineGroupCount;
            DecorationsOmitted = other.DecorationsOmitted;
            if (LineCount)
            {
                Helpers::secure_memcpy(Lines, sizeof(Lines), other.Lines, LineCount * sizeof(GizmoLine));
            }
            if (TriangleCount)
            {
                Helpers::secure_memcpy(Triangles, sizeof(Triangles), other.Triangles, TriangleCount * sizeof(GizmoTriangle));
            }
            if (SpriteCount)
            {
                Helpers::secure_memcpy(Sprites, sizeof(Sprites), other.Sprites, SpriteCount * sizeof(GizmoSprite));
            }
            if (MeshLineGroupCount)
            {
                Helpers::secure_memcpy(MeshLineGroups, sizeof(MeshLineGroups), other.MeshLineGroups, MeshLineGroupCount * sizeof(GizmoMeshLines));
            }
            return *this;
        }
        void Reset()
        {
            LineCount = TriangleCount = SpriteCount = 0;
            MeshLineGroupCount                      = 0;
            DecorationsOmitted                      = false;
        }
        [[nodiscard]] bool ValidCounts() const
        {
            return LineCount <= MaxLines && TriangleCount <= MaxTriangles && SpriteCount <= MaxSprites && MeshLineGroupCount <= MaxMeshLineGroups;
        }
        /// Linear validation is kept out of the per-primitive emission path.
        /// Groups are sorted/nonoverlapping, so filtering is O(lines + groups).
        [[nodiscard]] bool ValidMeshLineGroups() const
        {
            if (!ValidCounts())
            {
                return false;
            }
            uint32_t end = 0;
            for (uint32_t i = 0; i < MeshLineGroupCount; ++i)
            {
                const auto& group = MeshLineGroups[i];
                if (group.MeshUUID.is_nil() || !group.Count || group.First < end || group.First > LineCount || group.Count > LineCount - group.First)
                {
                    return false;
                }
                end = group.First + group.Count;
            }
            return true;
        }
        [[nodiscard]] bool Empty() const
        {
            return LineCount == 0 && TriangleCount == 0 && SpriteCount == 0;
        }
    };
    static_assert(std::is_trivially_copyable_v<GizmoLine> && std::is_trivially_copyable_v<GizmoTriangle> && std::is_trivially_copyable_v<GizmoSprite> && std::is_trivially_copyable_v<GizmoMeshLines>);
} // namespace ZEngine::Rendering::Renderers
