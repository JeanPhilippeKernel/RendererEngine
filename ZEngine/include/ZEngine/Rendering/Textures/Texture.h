#pragma once
#include <string>
#include <array>
#include <vulkan/vulkan.h>
#include <Core/IGraphicObject.h>

namespace ZEngine::Rendering::Textures
{
    class Texture : public Core::IGraphicObject
    {
    public:
        Texture(const char* path) : m_path(path) {}
        Texture(unsigned int width, unsigned int height) : m_width(width), m_height(height) {}

        virtual ~Texture() = default;

        virtual void                 SetData(void* const data)                   = 0;
        virtual void                 SetData(float r, float g, float b, float a) = 0;
        virtual std::array<float, 4> GetData() const
        {
            return {m_r, m_g, m_b, m_a};
        }

        virtual bool operator==(const Texture& right)
        {
            return false;
            //return m_texture_image == right.m_texture_image;
        }

        virtual bool operator!=(const Texture& right)
        {
            return false;
            //return m_texture_image != right.m_texture_image;
        }

        uint32_t GetIdentifier() const override
        {
            return 0;
        }

        unsigned int GetWidth() const
        {
            return m_width;
        }

        unsigned int GetHeight() const
        {
            return m_height;
        }

        std::string_view GetFilePath()
        {
            return m_path;
        }

        bool IsFromFile() const
        {
            return m_is_from_file;
        }

    protected:
        std::string  m_path;
        unsigned int m_width{0};
        unsigned int m_height{0};
        unsigned int m_byte_per_pixel{0};
        VkDeviceSize m_buffer_size{0};
        VkSampler    m_texture_sampler{VK_NULL_HANDLE};
        bool         m_is_from_file{false};

        // Those value are only set if SetDat(...) is using
        float m_r{0}, m_g{0}, m_b{0}, m_a{0};
    };

    Texture* CreateTexture(const char* path);
    Texture* CreateTexture(unsigned int width, unsigned int height);
} // namespace ZEngine::Rendering::Textures
