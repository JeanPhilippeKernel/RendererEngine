#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

namespace ZEngine::ECS
{
    enum class SceneValueKind : uint8_t
    {
        Null     = 0,
        Bool     = 1,
        SInt     = 2,
        UInt     = 3,
        Float    = 4,
        String   = 5,
        Sequence = 6,
        Map      = 7,
    };

    struct SceneValue;

    struct SceneValueMember
    {
        cstring     Key   = nullptr;
        SceneValue* Value = nullptr;
    };

    struct SceneValue
    {
        SceneValueKind                            Kind        = SceneValueKind::Null;
        bool                                      BoolValue   = false;
        int64_t                                   SIntValue   = 0;
        uint64_t                                  UIntValue   = 0;
        double                                    FloatValue  = 0.0;
        cstring                                   StringValue = nullptr;
        Core::Containers::Array<SceneValue*>      Sequence    = {};
        Core::Containers::Array<SceneValueMember> Members     = {};

        // Map lookup
        const SceneValue*                         Find(cstring key) const;
    };

    class SceneValueWriter
    {
    public:
        void                          Initialize(Core::Memory::ArenaAllocator* arena);

        SceneValue*                   NewNull();
        SceneValue*                   NewBool(bool value);
        SceneValue*                   NewSInt(int64_t value);
        SceneValue*                   NewUInt(uint64_t value);
        SceneValue*                   NewFloat(double value);
        SceneValue*                   NewString(cstring value);
        SceneValue*                   NewSequence(uint32_t capacity);
        SceneValue*                   NewMap(uint32_t capacity);
        SceneValue*                   NewFloatArray(const float* values, uint32_t count);

        bool                          Append(SceneValue* sequence, SceneValue* value);
        // Copies key. Rejects duplicate keys so maps stay canonical.
        bool                          Set(SceneValue* map, cstring key, SceneValue* value);

        cstring                       CopyString(cstring value);

        void                          SetRoot(SceneValue* root);
        SceneValue*                   Root() const;
        bool                          Failed() const;
        Core::Memory::ArenaAllocator* Arena() const;

    private:
        SceneValue*                   NewValue(SceneValueKind kind);

        Core::Memory::ArenaAllocator* m_arena  = nullptr;
        SceneValue*                   m_root   = nullptr;
        bool                          m_failed = false;
    };
} // namespace ZEngine::ECS
