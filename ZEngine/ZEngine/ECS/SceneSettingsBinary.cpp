#include <ZEngine/ECS/SceneBinaryValue.h>
#include <ZEngine/ECS/SceneSettingsBinary.h>
#include <ZEngine/ECS/SceneSettingsFields.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <array>

namespace ZEngine::ECS
{
    namespace
    {
        template <typename Operation>
        bool VisitSkyFloats(Rendering::Scenes::SkyConfig& sky, Operation operation)
        {
            const auto visit = [&](const auto& fields) {
                for (const auto& field : fields)
                    for (size_t i = 0; i < field.Count; ++i)
                        if (!operation(field.Data[i]))
                            return false;
                return true;
            };
            return visit(SceneSettingsDetail::SkyEnvironment(sky)) && visit(SceneSettingsDetail::SkyPresentation(sky)) && visit(SceneSettingsDetail::SkyAtmosphere(sky.Atmosphere)) && visit(SceneSettingsDetail::SkySphere(sky.Sphere));
        }
    } // namespace

    bool EncodeSceneSkyBinary(const Rendering::Scenes::SkyConfig& source, Core::Containers::ArrayView<std::byte> output)
    {
        if (!output.data() || output.size() != SceneSkyBinaryBytes || !source.IsValid())
            return false;
        auto                                       value = source;
        std::array<std::byte, SceneSkyBinaryBytes> bytes = {};
        SceneBinaryWriter                          writer(bytes);
        if (!writer.UInt32(1) || !writer.UInt32(static_cast<uint32_t>(value.Mode)))
            return false;
        for (const auto& uuid : {value.EnvironmentMap, value.PrimaryCelestialLight})
        {
            const auto id = uuid.as_bytes();
            if (!writer.Bytes({reinterpret_cast<const uint8_t*>(id.data()), id.size()}))
                return false;
        }
        if (!VisitSkyFloats(value, [&](float item) { return writer.Float(item); }) || !writer.UInt32(value.Sphere.ShowSunDisc ? 1u : 0u) || writer.Remaining())
            return false;
        return Helpers::secure_memcpy(output.data(), output.size(), bytes.data(), bytes.size()) == Helpers::MEMORY_OP_SUCCESS;
    }

    bool DecodeSceneSkyBinary(Core::Containers::ArrayView<const std::byte> input, Rendering::Scenes::SkyConfig& output)
    {
        if (!input.data() || input.size() != SceneSkyBinaryBytes)
            return false;
        SceneBinaryReader            reader({input.data(), input.size()});
        Rendering::Scenes::SkyConfig value   = {};
        uint32_t                     version = 0, mode = 0, show_sun_disc = 0;
        if (!reader.UInt32(version) || version != 1 || !reader.UInt32(mode) || mode > static_cast<uint32_t>(Rendering::Scenes::SkyMode::SkySphere))
            return false;
        for (auto* uuid : {&value.EnvironmentMap, &value.PrimaryCelestialLight})
        {
            std::span<const uint8_t> bytes = {};
            if (!reader.Bytes(16, bytes))
                return false;
            *uuid = uuids::uuid(bytes.begin(), bytes.end());
        }
        value.Mode = static_cast<Rendering::Scenes::SkyMode>(mode);
        if (!VisitSkyFloats(value, [&](float& item) { return reader.Float(item); }) || !reader.UInt32(show_sun_disc) || show_sun_disc > 1 || reader.Remaining())
            return false;
        value.Sphere.ShowSunDisc = show_sun_disc != 0;
        if (!value.IsValid())
            return false;
        output = value;
        return true;
    }
} // namespace ZEngine::ECS
