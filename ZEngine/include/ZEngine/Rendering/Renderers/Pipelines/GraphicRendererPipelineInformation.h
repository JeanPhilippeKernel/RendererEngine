#pragma once
#include <vector>
#include <queue>
#include <ZEngineDef.h>
#include <Rendering/Meshes/Mesh.h>
#include <Rendering/Geometries/IGeometry.h>
#include <Rendering/Materials/ShaderMaterial.h>
#include <Rendering/Renderers/Storages/GraphicRendererStorage.h>
#include <Rendering/Shaders/ShaderEnums.h>

#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers
{

    struct GraphicRendererInformationRecord
    {
        uint32_t ShaderIndex{0};
        uint32_t GeometryIndex{0};
        uint32_t MaterialIndex{0};
    };

    struct GraphicRendererPipelineInformation
    {
        bool IsPipelineStatesInitialized{false};

        uint32_t         DesiredWidth{1};
        uint32_t         DesiredHeight{1};

        uint32_t GeometryCollectionCount{0};
        uint32_t MaterialCollectionCount{0};

        std::vector<Rendering::Meshes::Mesh>                              MeshCollection;
        std::vector<Ref<Geometries::IGeometry>>                           GeometryCollection;
        std::vector<Ref<Materials::ShaderMaterial>>                       MaterialCollection;
        std::vector<GraphicRendererInformationRecord>                     RecordCollection;
        std::queue<Storages::GraphicRendererStorage<float, unsigned int>> GraphicStorageCollection;
    };
} // namespace ZEngine::Rendering::Renderers
