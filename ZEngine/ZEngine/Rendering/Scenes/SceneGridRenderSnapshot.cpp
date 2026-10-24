#include <ZEngine/ECS/SceneGridSettings.h>
#include <ZEngine/Rendering/Scenes/SceneGridRenderSnapshot.h>

namespace ZEngine::Rendering::Scenes
{
    bool MakeSceneGridRenderSnapshot(const ECS::SceneGridSettings& settings, SceneGridRenderSnapshot& output)
    {
        auto canonical = settings;
        if (!ECS::CanonicalizeSceneGridSettings(canonical))
            return false;
        SceneGridRenderSnapshot value = {};
        value.Origin                  = {canonical.Origin.x, canonical.Origin.y, canonical.Origin.z};
        value.Orientation             = canonical.Orientation;
        value.CellSize                = canonical.BaseSpacing;
        value.MajorLineEvery          = canonical.MajorLineEvery;
        value.FadeRadius              = canonical.FadeRadius;
        value.FadeStrength            = canonical.FadeStrength;
        value.LineWidth               = canonical.LineWidth;
        value.MaxLOD                  = int32_t(canonical.MaxLOD);
        value.Labels                  = uint32_t(canonical.Labels);
        value.Enabled                 = canonical.Enabled;
        for (uint32_t i = 0; i < 4; ++i)
        {
            value.ColorThin[i]  = canonical.MinorColor[i];
            value.ColorThick[i] = canonical.MajorColor[i];
            value.ColorUAxis[i] = canonical.UColor[i];
            value.ColorVAxis[i] = canonical.VColor[i];
        }
        output = value;
        return true;
    }

    bool ReadSceneGridRenderSnapshot(const SceneGridRenderSnapshot& snapshot, ECS::SceneGridSettings& output)
    {
        ECS::SceneGridSettings value = {};
        value.Origin                 = {snapshot.Origin.x, snapshot.Origin.y, snapshot.Origin.z};
        value.Orientation            = snapshot.Orientation;
        value.BaseSpacing            = snapshot.CellSize;
        value.MajorLineEvery         = snapshot.MajorLineEvery;
        value.FadeRadius             = snapshot.FadeRadius;
        value.FadeStrength           = snapshot.FadeStrength;
        value.LineWidth              = snapshot.LineWidth;
        value.MaxLOD                 = uint32_t(snapshot.MaxLOD);
        value.Labels                 = ECS::SceneGridLabels(snapshot.Labels);
        value.Enabled                = snapshot.Enabled;
        for (uint32_t i = 0; i < 4; ++i)
        {
            value.MinorColor[i] = snapshot.ColorThin[i];
            value.MajorColor[i] = snapshot.ColorThick[i];
            value.UColor[i]     = snapshot.ColorUAxis[i];
            value.VColor[i]     = snapshot.ColorVAxis[i];
        }
        if (!ECS::CanonicalizeSceneGridSettings(value))
            return false;
        output = value;
        return true;
    }
} // namespace ZEngine::Rendering::Scenes
