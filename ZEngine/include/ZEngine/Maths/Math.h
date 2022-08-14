#pragma once

#include <glm/glm/glm.hpp>
#include <glm/glm/gtc/quaternion.hpp>
#include <glm/glm/gtx/quaternion.hpp>

#include <glm/glm/gtc/matrix_transform.hpp>
#include <glm/glm/gtc/type_ptr.hpp>

namespace ZEngine::Maths {
    using namespace glm;

    using Vector4 = glm::vec4;
    using Vector3 = glm::vec3;
    using Vector2 = glm::vec2;
    using Vector1 = glm::vec1;

    using Matrix4 = glm::mat4;
    using Matrix3 = glm::mat3;
    using Matrix2 = glm::mat2;

    using Quaternion = glm::quat;

    // From TheCherno : https://github.com/TheCherno/Hazel/blob/master/Hazel/src/Hazel/Math/Math.h
    bool DecomposeTransformComponent(const Matrix4& transform, Vector3& translation, Vector3& rotation, Vector3& scale);
} // namespace ZEngine::Maths
