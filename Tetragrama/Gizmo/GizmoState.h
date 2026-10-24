#pragma once
#include <Tetragrama/Gizmo/GizmoConstants.h>
#include <ZEngine/Core/Containers/Array.h>

namespace Tetragrama
{
    enum class TransformSpacePolicy : uint8_t
    {
        World,
        Local,
    };

    namespace Gizmo
    {
        // Stable logical IDs shared by presentation, picking and interaction.
        enum class Handle : uint32_t
        {
            None,
            TranslateX,
            TranslateY,
            TranslateZ,
            TranslateYZ,
            TranslateZX,
            TranslateXY,
            TranslateView,
            RotateX,
            RotateY,
            RotateZ,
            RotateView,
            ScaleX,
            ScaleY,
            ScaleZ,
            ScaleUniform,
            PivotEdit,
            LightRangePositiveX,
            LightRangeNegativeX,
            LightRangePositiveY,
            LightRangePositiveZ,
            LightRangeNegativeZ,
            SpotRange,
            SpotOuterAngle,
            CameraFovUpperLeft,
            CameraFovUpperRight,
            CameraFarLowerLeft,
            CameraFarLowerRight,
            CameraNearLeft,
            CameraNearRight,
        };

        enum class Operation : uint8_t
        {
            Select,
            Translate,
            Rotate,
            Scale,
        };

        enum class SnapTarget : uint8_t
        {
            Translation,
            Rotation,
            Scale,
        };

        struct SnapSetting
        {
            float Increment = 0.f;
            bool  Enabled   = false;
        };

        /// Editor-local preferences: never serialized or counted as authored edits.
        /// Scale always uses local axes without overwriting the saved Space.
        struct State
        {
            Operation                          Mode            = Operation::Select;
            TransformSpacePolicy               Space           = TransformSpacePolicy::World;
            SnapSetting                        TranslationSnap = {Constants::DefaultTranslateSnap, true};
            SnapSetting                        RotationSnap    = {Constants::DefaultRotateSnapDegrees, true};
            SnapSetting                        ScaleSnap       = {Constants::DefaultScaleSnap, false};
            bool                               ShowGrid        = true;
            uint64_t                           Revision        = 1;

            [[nodiscard]] TransformSpacePolicy EffectiveSpace() const
            {
                return Mode == Operation::Scale ? TransformSpacePolicy::Local : Space;
            }
        };

        enum class ChangeKind : uint8_t
        {
            Operation,
            Space,
            SnapEnabled,
            SnapIncrement,
            GridVisibility,
        };

        /// Small value-only queued intent; no panel or ECS lifetime dependency.
        struct Change
        {
            ChangeKind           Kind      = ChangeKind::Operation;
            Operation            Mode      = Operation::Select;
            TransformSpacePolicy Space     = TransformSpacePolicy::World;
            SnapTarget           Target    = SnapTarget::Translation;
            float                Increment = 0.f;
            bool                 Enabled   = false;
        };

        [[nodiscard]] ZEngine::Core::Containers::ArrayView<const SnapPreset> SnapPresets(SnapTarget target);
        [[nodiscard]] const SnapSetting*                                     SnapFor(const State& state, SnapTarget target);
        [[nodiscard]] bool                                                   ValidChange(const Change& change);
        [[nodiscard]] inline constexpr bool                                  IsCenterHandle(Handle handle)
        {
            return handle == Handle::TranslateView || handle == Handle::ScaleUniform || handle == Handle::PivotEdit;
        }
    } // namespace Gizmo
} // namespace Tetragrama
