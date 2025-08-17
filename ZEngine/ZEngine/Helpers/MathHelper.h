#pragma once
#include <Core/Maths/Matrix.h>
#include <assimp/matrix4x4.h>

namespace ZEngine::Helpers
{
    ZEngine::Core::Maths::Mat4f ConvertToMat4(const aiMatrix4x4&);
}