#pragma once
#include <Core/ISerializer.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <ZEngineDef.h>
#include <filesystem>

namespace ZEngine::Serializers
{
    struct GraphicSceneSerializer : public Core::ISerializer
    {
        GraphicSceneSerializer()          = default;
        virtual ~GraphicSceneSerializer() = default;

    protected:
        std::filesystem::path                             m_default_scene_directory_path;
        WeakRef<ZEngine::Rendering::Scenes::GraphicScene> m_scene;
    };
} // namespace ZEngine::Serializers