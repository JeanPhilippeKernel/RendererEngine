#pragma once
#include <assimp/matrix4x4.h>
#include <glm/glm.hpp>

namespace ZEngine::Helpers
{
    glm::mat4 ConvertToMat4(const aiMatrix4x4&);
}