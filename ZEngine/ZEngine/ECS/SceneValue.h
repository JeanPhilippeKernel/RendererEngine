#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <uuid.h>
#include <variant>

namespace ZEngine::ECS
{
    struct SceneStringValue
    {
        cstring  Data = nullptr;
        uint32_t Size = 0;
    };

    using SceneScalar = std::variant<float, bool, uint32_t, Core::Maths::Vec3f, uuids::uuid, SceneStringValue>;

    struct SceneValueField
    {
        cstring     Key   = nullptr;
        SceneScalar Value = 0.f;
    };

    /// @brief Borrowed immutable semantic component value. Its field/key/string owners must outlive it.
    /// No runtime component layout, field offset, pointer or EntityID is part of the wire schema.
    class SceneValue
    {
    public:
        explicit SceneValue(Core::Containers::ArrayView<const SceneValueField> fields = {}) : m_fields(fields) {}

        [[nodiscard]] Core::Containers::ArrayView<const SceneValueField> Fields() const
        {
            return m_fields;
        }
        template <typename T>
        [[nodiscard]] const T* Find(cstring key) const
        {
            if (!key)
            {
                return nullptr;
            }
            const T* result = nullptr;
            for (const auto& field : m_fields)
            {
                if (field.Key && Helpers::secure_strcmp(key, field.Key) == 0)
                {
                    // Malformed duplicate keys are invalid, never last-writer-wins.
                    if (result)
                    {
                        return nullptr;
                    }
                    result = std::get_if<T>(&field.Value);
                    if (!result)
                    {
                        return nullptr;
                    }
                }
            }
            return result;
        }

    private:
        Core::Containers::ArrayView<const SceneValueField> m_fields = {};
    };

    /// @brief Caller-reserved field storage; capture and decode never allocate or silently truncate.
    class SceneValueWriter
    {
    public:
        explicit SceneValueWriter(Core::Containers::ArrayView<SceneValueField> storage) : m_storage(storage) {}
        [[nodiscard]] bool Add(cstring key, const SceneScalar& value)
        {
            if (!key || !key[0] || m_count == m_storage.size())
            {
                return false;
            }
            for (size_t i = 0; i < m_count; ++i)
            {
                if (Helpers::secure_strcmp(key, m_storage[i].Key) == 0)
                {
                    return false;
                }
            }
            m_storage[m_count++] = {key, value};
            return true;
        }
        [[nodiscard]] SceneValue Value() const
        {
            return SceneValue(m_storage.first(m_count));
        }
        [[nodiscard]] size_t Remaining() const
        {
            return m_storage.size() - m_count;
        }
        [[nodiscard]] bool Empty() const
        {
            return m_count == 0;
        }
        void Reset()
        {
            m_count = 0;
        }

    private:
        Core::Containers::ArrayView<SceneValueField> m_storage = {};
        size_t                                       m_count   = 0;
    };
} // namespace ZEngine::ECS
