#pragma once
#include <cstdint>

namespace ZEngine::ECS
{

    enum class FieldType : uint8_t
    {
        Bool,
        Int8,
        Int16,
        Int32,
        Int64,
        UInt8,
        UInt16,
        UInt32,
        UInt64,
        Float,
        Double,
        Vec2f,
        Vec3f,
        Vec4f,
        Quatf,
        Mat4f,
        EntityID,
        AssetUUID,
        String,
        Enum,
        Struct,
    };

} // namespace ZEngine::ECS
