#include <Tetragrama/Gizmo/GizmoState.h>
#include <cmath>

using namespace ZEngine::Core::Containers;

namespace Tetragrama::Gizmo
{
    ArrayView<const SnapPreset> SnapPresets(SnapTarget target)
    {
        switch (target)
        {
            case SnapTarget::Translation:
                return Constants::TranslationSnapPresets;
            case SnapTarget::Rotation:
                return Constants::RotationSnapPresets;
            case SnapTarget::Scale:
                return Constants::ScaleSnapPresets;
        }
        return {};
    }

    const SnapSetting* SnapFor(const State& state, SnapTarget target)
    {
        switch (target)
        {
            case SnapTarget::Translation:
                return &state.TranslationSnap;
            case SnapTarget::Rotation:
                return &state.RotationSnap;
            case SnapTarget::Scale:
                return &state.ScaleSnap;
        }
        return nullptr;
    }

    bool ValidChange(const Change& change)
    {
        switch (change.Kind)
        {
            case ChangeKind::Operation:
                return change.Mode <= Operation::Scale;
            case ChangeKind::Space:
                return change.Space == TransformSpacePolicy::World || change.Space == TransformSpacePolicy::Local;
            case ChangeKind::SnapEnabled:
                return change.Target <= SnapTarget::Scale;
            case ChangeKind::SnapIncrement:
                if (std::isfinite(change.Increment))
                {
                    for (const auto& preset : SnapPresets(change.Target))
                    {
                        if (preset.Increment == change.Increment)
                        {
                            return true;
                        }
                    }
                }
                return false;
            case ChangeKind::GridVisibility:
                return true;
        }
        return false;
    }
} // namespace Tetragrama::Gizmo
