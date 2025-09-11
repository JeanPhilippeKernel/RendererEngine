#pragma once
#include <Rendering/Buffers/BufferLayout.h>
#include <Rendering/Renderers/Storages/IVertex.h>
#include <array>

namespace ZEngine::Rendering::Renderers::Storages
{

    class GraphicVertex : public IVertex
    {
    public:
        struct Descriptor;

    public:
        explicit GraphicVertex();
        explicit GraphicVertex(const ZEngine::Core::Maths::Vec3f& position, const ZEngine::Core::Maths::Vec3f& normal = {0.0f, 0.0f, 0.0f}, const ZEngine::Core::Maths::Vec2f& texture_coord = {0.0f, 0.0f});

        ~GraphicVertex() = default;

        ZEngine::Core::Maths::Vec3f GetPosition() const;
        ZEngine::Core::Maths::Vec3f GetNormal() const;
        ZEngine::Core::Maths::Vec2f GetTextureCoord() const;

        void                        SetPosition(const ZEngine::Core::Maths::Vec3f& value);
        void                        SetNormal(const ZEngine::Core::Maths::Vec3f& value);
        void                        SetTextureCoord(const ZEngine::Core::Maths::Vec2f& value);

        void                        TransformPosition(const ZEngine::Core::Maths::Mat4f& matrix);

        const std::array<float, 8>& GetData() const
        {
            return m_buffer;
        }

    private:
        void _UpdateBuffer();

    private:
        std::array<float, 8> m_buffer;
    };

    struct GraphicVertex::Descriptor
    {
    public:
        static Buffers::Layout::BufferLayout<float>& GetLayout()
        {
            return m_internal_layout;
        }

    private:
        static Buffers::Layout::BufferLayout<float> m_internal_layout;
    };
} // namespace ZEngine::Rendering::Renderers::Storages
