#pragma once
#include <span>
#include <Serializers/Serializer.h>
#include <Editor.h>


namespace Tetragrama::Serializers
{
    class EditorSceneSerializer : public Serializer<EditorScene>
    {
    public:
        virtual void Serialize(const ZEngine::Ref<EditorScene>& data) override;
        virtual void Deserialize(std::string_view filename) override;
    };
}