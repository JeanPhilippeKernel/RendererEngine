#pragma once
#include <ZEngine/Core/ISerializer.h>
#include <ZEngine/Rendering/Scenes/GraphicScene.h>
#include <ZEngine/ZEngineDef.h>
#include <filesystem>

namespace ZEngine::Serializers
{
    struct GraphicSceneSerializer : public Core::ISerializer
    {
        GraphicSceneSerializer()          = default;
        virtual ~GraphicSceneSerializer() = default;

    protected:
        std::filesystem::path m_default_scene_directory_path;
        // Helpers::WeakRef<ZEngine::Rendering::Scenes::GraphicScene> m_scene;
    };
} // namespace ZEngine::Serializers