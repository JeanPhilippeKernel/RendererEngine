#include <pch.h>
#include <Rendering/Textures/Texture2D.h>

#define STB_IMAGE_IMPLEMENTATION
#ifdef __GNUC__
#define STBI_NO_SIMD
#endif
#include <stb/stb_image.h>

namespace ZEngine::Rendering::Textures {

    Texture* CreateTexture(const char* path) {
        return new Texture2D(path);
    }

    Texture* CreateTexture(unsigned int width, unsigned int height) {
        return new Texture2D(width, height);
    }
} // namespace ZEngine::Rendering::Textures

namespace ZEngine::Rendering::Textures {

    Texture2D::Texture2D(const char* path) : Texture(path) {
        int width = 0, height = 0, channel = 0;
        stbi_set_flip_vertically_on_load(1);
        stbi_uc* image_data = stbi_load(path, &width, &height, &channel, 0);

        if (image_data != nullptr) {

            m_internal_format = (channel == 3) ? GL_RGB8 : GL_RGBA8;
            m_data_format     = (channel == 3) ? GL_RGB : GL_RGBA;

#ifdef _WIN32
            glCreateTextures(GL_TEXTURE_2D, 1, &m_texture_id);
            glTextureStorage2D(m_texture_id, 1, m_internal_format, width, height);

            glTextureParameterf(m_texture_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameterf(m_texture_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

            glTextureParameteri(m_texture_id, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTextureParameteri(m_texture_id, GL_TEXTURE_WRAP_T, GL_REPEAT);
#else
            glGenTextures(1, &m_texture_id);
            glBindTexture(GL_TEXTURE_2D, m_texture_id);

            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
#endif
            m_width  = width;
            m_height = height;

#ifdef _WIN32
            glTextureSubImage2D(m_texture_id, 0, 0, 0, width, height, m_data_format, GL_UNSIGNED_BYTE, (const void*) (image_data));
#else
            glTexImage2D(GL_TEXTURE_2D, 0, m_internal_format, width, height, 0, m_data_format, GL_UNSIGNED_BYTE, (const void*) (image_data));
#endif
        }

        stbi_image_free(image_data);
    }

    Texture2D::Texture2D(unsigned int width, unsigned int height) : Texture(width, height) {
        m_internal_format = GL_RGBA8;
        m_data_format     = GL_RGBA;

#ifdef _WIN32
        glCreateTextures(GL_TEXTURE_2D, 1, &m_texture_id);
        glTextureStorage2D(m_texture_id, 1, m_internal_format, width, height);

        glTextureParameterf(m_texture_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameterf(m_texture_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glTextureParameteri(m_texture_id, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(m_texture_id, GL_TEXTURE_WRAP_T, GL_REPEAT);

#else
        glGenTextures(1, &m_texture_id);
        glBindTexture(GL_TEXTURE_2D, m_texture_id);

        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
#endif
        const char* data = "\xff\xff\xff\xff"; // R:255 G: 255 B: 255 A: 255
        SetData(data);
    }

    void Texture2D::SetData(const void* data) {
#ifdef _WIN32
        glTextureSubImage2D(m_texture_id, 0, 0, 0, m_width, m_height, m_data_format, GL_UNSIGNED_BYTE, data);
#else
        glTexImage2D(GL_TEXTURE_2D, 0, m_internal_format, m_width, m_height, 0, m_data_format, GL_UNSIGNED_BYTE, data);
#endif
    }

    void Texture2D::SetData(float r, float g, float b, float a) {
        unsigned char data[5] = {0, 0, 0, 0, '\0'};
        data[0]               = static_cast<unsigned char>(std::clamp(r, .0f, 255.0f));
        data[1]               = static_cast<unsigned char>(std::clamp(g, .0f, 255.0f));
        data[2]               = static_cast<unsigned char>(std::clamp(b, .0f, 255.0f));
        data[3]               = static_cast<unsigned char>(std::clamp(a, .0f, 255.0f));

        SetData(data);
    }

    Texture2D::~Texture2D() {
        glDeleteTextures(1, &m_texture_id);
    }

    void Texture2D::Bind(int slot) const {
#ifdef _WIN32
        glBindTextureUnit(slot, m_texture_id);
#else
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_texture_id);
#endif
    }

    void Texture2D::Unbind(int slot) const {
#ifdef _WIN32
        glBindTextureUnit(slot, 0);
#else
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, 0);
#endif
    }

} // namespace ZEngine::Rendering::Textures
