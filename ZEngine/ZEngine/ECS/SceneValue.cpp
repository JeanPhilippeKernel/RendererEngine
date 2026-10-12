#include <ZEngine/ECS/SceneValue.h>
#include <ZEngine/Helpers/MemoryOperations.h>

namespace ZEngine::ECS
{
    namespace
    {
        template <typename T>
        bool CheckedPush(Core::Containers::Array<T>& array, const T& value)
        {
            if (array.size() == array.capacity())
            {
                array.reserve(array.capacity() == 0 ? 4 : array.capacity() * 2);
                if (!array.data())
                {
                    return false;
                }
            }
            array.push(value);
            return true;
        }
    } // namespace

    const SceneValue* SceneValue::Find(cstring key) const
    {
        if (Kind != SceneValueKind::Map || !key)
        {
            return nullptr;
        }
        for (uint32_t i = 0; i < Members.size(); ++i)
        {
            if (Helpers::secure_strcmp(Members[i].Key, key) == 0)
            {
                return Members[i].Value;
            }
        }
        return nullptr;
    }

    void SceneValueWriter::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        ZENGINE_VALIDATE_ASSERT(arena != nullptr, "SceneValueWriter::Initialize: arena must not be null")
        m_arena  = arena;
        m_root   = nullptr;
        m_failed = false;
    }

    SceneValue* SceneValueWriter::NewValue(SceneValueKind kind)
    {
        if (m_failed || !m_arena)
        {
            m_failed = true;
            return nullptr;
        }
        auto* value = ZPushStructCtor(m_arena, SceneValue);
        if (!value)
        {
            m_failed = true;
            return nullptr;
        }
        value->Kind = kind;
        return value;
    }

    SceneValue* SceneValueWriter::NewNull()
    {
        return NewValue(SceneValueKind::Null);
    }

    SceneValue* SceneValueWriter::NewBool(bool v)
    {
        SceneValue* value = NewValue(SceneValueKind::Bool);
        if (value)
        {
            value->BoolValue = v;
        }
        return value;
    }

    SceneValue* SceneValueWriter::NewSInt(int64_t v)
    {
        SceneValue* value = NewValue(SceneValueKind::SInt);
        if (value)
        {
            value->SIntValue = v;
        }
        return value;
    }

    SceneValue* SceneValueWriter::NewUInt(uint64_t v)
    {
        SceneValue* value = NewValue(SceneValueKind::UInt);
        if (value)
        {
            value->UIntValue = v;
        }
        return value;
    }

    SceneValue* SceneValueWriter::NewFloat(double v)
    {
        SceneValue* value = NewValue(SceneValueKind::Float);
        if (value)
        {
            value->FloatValue = v;
        }
        return value;
    }

    cstring SceneValueWriter::CopyString(cstring v)
    {
        if (m_failed || !m_arena)
        {
            m_failed = true;
            return nullptr;
        }
        const size_t len  = v ? Helpers::secure_strlen(v) : 0;
        char*        copy = ZPushString(m_arena, len + 1);
        if (!copy)
        {
            m_failed = true;
            return nullptr;
        }
        if (len > 0)
        {
            Helpers::secure_strncpy(copy, len + 1, v, len);
        }
        return copy;
    }

    SceneValue* SceneValueWriter::NewString(cstring v)
    {
        cstring copy = CopyString(v);
        if (!copy)
        {
            return nullptr;
        }
        SceneValue* value = NewValue(SceneValueKind::String);
        if (value)
        {
            value->StringValue = copy;
        }
        return value;
    }

    SceneValue* SceneValueWriter::NewSequence(uint32_t capacity)
    {
        SceneValue* value = NewValue(SceneValueKind::Sequence);
        if (!value)
        {
            return nullptr;
        }
        value->Sequence.init(m_arena, capacity ? capacity : 1);
        if (!value->Sequence.data())
        {
            m_failed = true;
            return nullptr;
        }
        return value;
    }

    SceneValue* SceneValueWriter::NewMap(uint32_t capacity)
    {
        SceneValue* value = NewValue(SceneValueKind::Map);
        if (!value)
        {
            return nullptr;
        }
        value->Members.init(m_arena, capacity ? capacity : 1);
        if (!value->Members.data())
        {
            m_failed = true;
            return nullptr;
        }
        return value;
    }

    SceneValue* SceneValueWriter::NewFloatArray(const float* values, uint32_t count)
    {
        SceneValue* sequence = NewSequence(count);
        for (uint32_t i = 0; sequence && i < count; ++i)
        {
            if (!Append(sequence, NewFloat(values[i])))
            {
                return nullptr;
            }
        }
        return sequence;
    }

    bool SceneValueWriter::Append(SceneValue* sequence, SceneValue* value)
    {
        if (m_failed || !sequence || !value || sequence->Kind != SceneValueKind::Sequence)
        {
            return false;
        }
        if (!CheckedPush(sequence->Sequence, value))
        {
            m_failed = true;
            return false;
        }
        return true;
    }

    bool SceneValueWriter::Set(SceneValue* map, cstring key, SceneValue* value)
    {
        if (m_failed || !map || !key || !value || map->Kind != SceneValueKind::Map || map->Find(key))
        {
            return false;
        }
        cstring key_copy = CopyString(key);
        if (!key_copy)
        {
            return false;
        }
        if (!CheckedPush(map->Members, SceneValueMember{key_copy, value}))
        {
            m_failed = true;
            return false;
        }
        return true;
    }

    void SceneValueWriter::SetRoot(SceneValue* root)
    {
        m_root = root;
    }

    SceneValue* SceneValueWriter::Root() const
    {
        return m_root;
    }

    bool SceneValueWriter::Failed() const
    {
        return m_failed;
    }

    Core::Memory::ArenaAllocator* SceneValueWriter::Arena() const
    {
        return m_arena;
    }
} // namespace ZEngine::ECS
