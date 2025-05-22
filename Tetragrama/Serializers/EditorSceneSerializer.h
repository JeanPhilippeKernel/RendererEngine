#pragma once
#include <Editor.h>
#include <Importers/IAssetImporter.h>
#include <Serializers/Serializer.h>

namespace Tetragrama::Serializers
{
    struct EditorSceneSerializer : public Serializer<EditorScene>
    {
        virtual void Serialize(ZRawPtr(EditorScene) const data) override;
        virtual void Deserialize(std::string_view filename) override;
    };
} // namespace Tetragrama::Serializers