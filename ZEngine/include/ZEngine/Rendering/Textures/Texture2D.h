#pragma once
#include <Rendering/Textures/Texture.h>
#include <Rendering/Buffers/Image2DBuffer.h>

namespace ZEngine::Rendering::Textures {

    class Texture2D : public Texture
    {
    public:
        Texture2D(const char* path);
        Texture2D(unsigned int width, unsigned int height);
        Texture2D(unsigned int width, unsigned int height, float r, float g, float b, float a);
        ~Texture2D();

        void SetData(void* const data) override;
        void SetData(float r, float g, float b, float a) override;

         Ref<Buffers::Image2DBuffer> GetImage2DBuffer() const;

    private:
        Ref<Buffers::Image2DBuffer> m_image_2d_buffer;
    };
} // namespace ZEngine::Rendering::Textures
