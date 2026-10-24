#pragma once
#include <Tetragrama/Gizmo/GizmoState.h>
#include <ZEngine/UI/ZUITokens.h>

namespace Tetragrama::Gizmo
{
    [[nodiscard]] inline constexpr ZEngine::UI::Tokens::Color ConstraintColor(Handle handle)
    {
        using namespace ZEngine::UI::Tokens;
        switch (handle)
        {
            case Handle::TranslateX:
            case Handle::TranslateYZ:
            case Handle::RotateX:
            case Handle::ScaleX:
                return AccentRed;
            case Handle::TranslateY:
            case Handle::TranslateZX:
            case Handle::RotateY:
            case Handle::ScaleY:
                return AccentGreen;
            case Handle::TranslateZ:
            case Handle::TranslateXY:
            case Handle::RotateZ:
            case Handle::ScaleZ:
            case Handle::CameraFovUpperLeft:
            case Handle::CameraFovUpperRight:
            case Handle::CameraFarLowerLeft:
            case Handle::CameraFarLowerRight:
            case Handle::CameraNearLeft:
            case Handle::CameraNearRight:
                return AccentBlue;
            case Handle::RotateView:
                return TextPrimary;
            case Handle::LightRangePositiveX:
            case Handle::LightRangeNegativeX:
            case Handle::LightRangePositiveY:
            case Handle::LightRangePositiveZ:
            case Handle::LightRangeNegativeZ:
            case Handle::SpotRange:
            case Handle::SpotOuterAngle:
                return PinVector;
            default:
                return AccentOrange;
        }
    }
} // namespace Tetragrama::Gizmo
