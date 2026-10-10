#pragma once
#include <Tetragrama/EditorScene.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Serializers/Serializer.h>

namespace Tetragrama::Serializers
{
    struct EditorSceneSerializer : public ZEngine::Serializers::Serializer<EditorScene>
    {
        virtual void Serialize(EditorScene* const data) override;
        virtual void Deserialize(cstring filename) override;

    private:
        EditorScene* m_pending_serialize_scene                           = nullptr;
        char         m_pending_deserialize_filename[MAX_FILE_PATH_COUNT] = {};

        static void  RunSerializeTask(void* context);
        static void  RunDeserializeTask(void* context);
        void         SerializeOnWorker(EditorScene* scene);
        void         DeserializeOnWorker(cstring filename);
    };
} // namespace Tetragrama::Serializers
