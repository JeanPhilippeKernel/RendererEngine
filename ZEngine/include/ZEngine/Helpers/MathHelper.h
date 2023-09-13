#pragma once
#include <glm/glm.hpp>
#include <assimp/matrix4x4.h>

namespace ZEngine::Helpers
{
    glm::mat4 ConvertToMat4(const aiMatrix4x4&);
}