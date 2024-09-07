#pragma once
#include <Helpers/IntrusivePtr.h>
#include <Helpers/MemoryOperations.h>
#include <Rendering/GPUTypes.h>
#include <span>

namespace ZEngine::Rendering::Lights
{

    enum class LightType : int
    {
        UNDEFINED   = -1,
        DIRECTIONAL = 0,
        POINT,
        SPOT
    };

    struct Light : public Helpers::RefCounted
    {
        virtual void UpdateBuffer() = 0;

        LightType GetLightType() const
        {
            return m_light_type;
        }

    protected:
        LightType m_light_type;
    };

    struct LightVNext : public Helpers::RefCounted
    {
        gpuvec4 Ambient  = 1.0f;
        gpuvec4 Diffuse  = 1.0f;
        gpuvec4 Specular = 1.0f;

        LightType GetLightType() const
        {
            return m_type;
        }

    protected:
        LightType m_type{LightType::UNDEFINED};
    };

    struct LightBuffer
    {
        uint32_t Count;
        uint32_t Padding[3];
    };

    /*
     * Directional Light defintion
     */
    struct alignas(16) GpuDirectionLight
    {
        gpuvec4 Direction = 1.0f;
        gpuvec4 Ambient   = 1.0f;
        gpuvec4 Diffuse   = 1.0f;
        gpuvec4 Specular  = 1.0f;
    };

    struct DirectionalLight : public LightVNext
    {
        DirectionalLight()
        {
            m_type = LightType::DIRECTIONAL;
        }
        gpuvec4 Direction{1.0f};

        GpuDirectionLight GPUPackedData() const
        {
            return GpuDirectionLight{.Direction = Direction, .Ambient = Ambient, .Diffuse = Diffuse, .Specular = Specular};
        }
    };

    /*
     * Point Light defintion
     */
    struct alignas(16) GpuPointLight
    {
        gpuvec4 Position  = 1.0f;
        gpuvec4 Ambient   = 1.0f;
        gpuvec4 Diffuse   = 1.0f;
        gpuvec4 Specular  = 1.0f;
        float   Constant  = 0.0f;
        float   Linear    = 0.0f;
        float   Quadratic = 0.0f;
        float   Padding   = 0.0f;
    };

    struct PointLight : public LightVNext
    {
        PointLight()
        {
            m_type = LightType::POINT;
        }

        gpuvec4 Position  = 1.0f;
        float   Constant  = 1.0f;
        float   Linear    = 0.7f;
        float   Quadratic = 1.8f;

        GpuPointLight GPUPackedData() const
        {
            return GpuPointLight{
                .Position = Position, .Ambient = Ambient, .Diffuse = Diffuse, .Specular = Specular, .Constant = Constant, .Linear = Linear, .Quadratic = Quadratic};
        }
    };

    /*
     * Spot Light defintion
     */
    struct alignas(16) GpuSpotlight
    {
        gpuvec4 Position  = 1.0f;
        gpuvec4 Direction = 1.0f;
        gpuvec4 Ambient   = 1.0f;
        gpuvec4 Diffuse   = 1.0f;
        gpuvec4 Specular  = 1.0f;
        float   CutOff    = 0.0f;
        float   Constant  = 1.0f;
        float   Linear    = 0.7f;
        float   Quadratic = 1.8f;
    };

    struct Spotlight : public LightVNext
    {
        Spotlight()
        {
            m_type = LightType::SPOT;
        }

        gpuvec4 Position  = 1.0f;
        gpuvec4 Direction = 1.0f;
        float   CutOff    = 0.0f;
        float   Constant  = 1.0f;
        float   Linear    = 0.7f;
        float   Quadratic = 1.8f;

        GpuSpotlight GPUPackedData() const
        {
            return GpuSpotlight{
                .Position  = Position,
                .Direction = Direction,
                .Ambient   = Ambient,
                .Diffuse   = Diffuse,
                .Specular  = Specular,
                .CutOff    = CutOff,
                .Constant  = Constant,
                .Linear    = Linear,
                .Quadratic = Quadratic};
        }
    };

    template <typename T>
    std::vector<uint8_t> CreateLightBuffer(std::span<const T> data)
    {
        auto                 count       = data.size();
        size_t               buffer_size = sizeof(LightBuffer) + (sizeof(T) * count);
        std::vector<uint8_t> buffer(buffer_size);

        auto light_buffer   = reinterpret_cast<LightBuffer*>(buffer.data());
        light_buffer->Count = count;
        Helpers::secure_memset(light_buffer->Padding, 0, sizeof(light_buffer->Padding), sizeof(light_buffer->Padding));
        Helpers::secure_memcpy((buffer.data() + sizeof(Lights::LightBuffer)), data.size_bytes(), data.data(), data.size_bytes());

        return buffer;
    }
} // namespace ZEngine::Rendering::Lights
