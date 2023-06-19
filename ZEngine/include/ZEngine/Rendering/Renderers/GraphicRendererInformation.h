#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <queue>
#include <ZEngineDef.h>
#include <Rendering/Shaders/Shader.h>
#include <Rendering/Geometries/IGeometry.h>
#include <Rendering/Materials/ShaderMaterial.h>
#include <Rendering/Renderers/Storages/GraphicRendererStorage.h>
#include <Rendering/Shaders/ShaderEnums.h>

namespace ZEngine::Rendering::Renderers
{

    struct GraphicRendererInformation
    {
        GraphicRendererInformation()
        {
            ShaderCollection      = {{Shaders::ShaderBuiltInType::STANDARD, "Resources/Windows/Shaders/standard_shader.glsl"},
                     {Shaders::ShaderBuiltInType::BASIC_2, "Resources/Windows/Shaders/basic_shader_2.glsl"}};
            ShaderCollectionCount = ShaderCollection.size();
        }

        uint32_t                                                    ShaderCollectionCount{0};
        Storages::GraphicRendererStorageType                        GraphicStorageType{Storages::GraphicRendererStorageType::GRAPHIC_STORAGE_TYPE_UNDEFINED};
        std::unordered_map<Shaders::ShaderBuiltInType, std::string> ShaderCollection;
    };
} // namespace ZEngine::Rendering::Renderers
