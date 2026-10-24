#pragma once
#include <ZEngine/Core/Maths/Vec.h>
#include <bit>
#include <cmath>
#include <limits>
#include <span>

namespace ZEngine::ECS
{
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "Scene float codec requires IEEE-754 float32");
    /// @brief Bounded little-endian scalar codec. No native struct/padding writes.
    class SceneBinaryWriter
    {
    public:
        explicit SceneBinaryWriter(std::span<std::byte> bytes) : m_bytes(bytes) {}
        [[nodiscard]] size_t Remaining() const
        {
            return m_bytes.size() - m_position;
        }
        [[nodiscard]] size_t Position() const
        {
            return m_position;
        }
        [[nodiscard]] bool UInt32(uint32_t value)
        {
            if (Remaining() < sizeof(value))
            {
                return false;
            }
            for (uint32_t shift = 0; shift < 32; shift += 8)
            {
                m_bytes[m_position++] = std::byte((value >> shift) & 0xffu);
            }
            return true;
        }
        [[nodiscard]] bool Float(float value)
        {
            return std::isfinite(value) && UInt32(std::bit_cast<uint32_t>(value));
        }
        [[nodiscard]] bool Vector(const Core::Maths::Vec3f& value)
        {
            if (Remaining() < 3 * sizeof(float) || !std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
            {
                return false;
            }
            return Float(value.x) && Float(value.y) && Float(value.z);
        }
        [[nodiscard]] bool Bytes(std::span<const uint8_t> value)
        {
            if (Remaining() < value.size())
            {
                return false;
            }
            for (const auto byte : value)
            {
                m_bytes[m_position++] = std::byte(byte);
            }
            return true;
        }

    private:
        std::span<std::byte> m_bytes    = {};
        size_t               m_position = 0;
    };

    class SceneBinaryReader
    {
    public:
        explicit SceneBinaryReader(std::span<const std::byte> bytes) : m_bytes(bytes) {}
        [[nodiscard]] size_t Remaining() const
        {
            return m_bytes.size() - m_position;
        }
        [[nodiscard]] size_t Position() const
        {
            return m_position;
        }
        [[nodiscard]] bool UInt32(uint32_t& out)
        {
            if (Remaining() < sizeof(out))
            {
                return false;
            }
            uint32_t value = 0;
            for (uint32_t shift = 0; shift < 32; shift += 8)
            {
                value |= uint32_t(std::to_integer<uint8_t>(m_bytes[m_position++])) << shift;
            }
            out = value;
            return true;
        }
        [[nodiscard]] bool Float(float& out)
        {
            auto     candidate = *this;
            uint32_t bits      = 0;
            if (!candidate.UInt32(bits))
            {
                return false;
            }
            const float value = std::bit_cast<float>(bits);
            if (!std::isfinite(value))
            {
                return false;
            }
            out   = value;
            *this = candidate;
            return true;
        }
        [[nodiscard]] bool Vector(Core::Maths::Vec3f& out)
        {
            auto               candidate = *this;
            Core::Maths::Vec3f value     = {};
            if (!candidate.Float(value.x) || !candidate.Float(value.y) || !candidate.Float(value.z))
            {
                return false;
            }
            out   = value;
            *this = candidate;
            return true;
        }
        [[nodiscard]] bool Bytes(size_t count, std::span<const uint8_t>& out)
        {
            if (Remaining() < count)
            {
                return false;
            }
            out         = {reinterpret_cast<const uint8_t*>(m_bytes.data() + m_position), count};
            m_position += count;
            return true;
        }

    private:
        std::span<const std::byte> m_bytes    = {};
        size_t                     m_position = 0;
    };
} // namespace ZEngine::ECS
