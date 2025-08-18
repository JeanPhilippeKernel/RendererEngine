#pragma once
#include <EditorScene.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Serializers/Serializer.h>

namespace Tetragrama::Serializers
{
    struct EditorSceneSerializer : public ZEngine::Serializers::Serializer<EditorScene>
    {
        virtual void Serialize(ZRawPtr(EditorScene) const data) override;
        virtual void Deserialize(cstring filename) override;
    };
} // namespace Tetragrama::Serializers