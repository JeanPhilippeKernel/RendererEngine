#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>

namespace ZEngine::ECS
{
    // Version + mode + two UUIDs + 54 float32 scalars + a uint32 boolean.
    inline constexpr size_t SceneSkyBinaryBytes = 260;
    [[nodiscard]] bool      EncodeSceneSkyBinary(const Rendering::Scenes::SkyConfig& value, Core::Containers::ArrayView<std::byte> output);
    [[nodiscard]] bool      DecodeSceneSkyBinary(Core::Containers::ArrayView<const std::byte> input, Rendering::Scenes::SkyConfig& output);
} // namespace ZEngine::ECS
