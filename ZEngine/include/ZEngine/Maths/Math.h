#pragma once

#include <glm/glm/glm.hpp>
#include <glm/glm/gtc/quaternion.hpp>
#include <glm/glm/gtx/quaternion.hpp>

#include <glm/glm/gtc/matrix_transform.hpp>
#include <glm/glm/gtc/type_ptr.hpp>


namespace ZEngine::Maths {
	using namespace  glm;

	using Vector4 = vec4;
	using Vector3 = vec3;
	using Vector2 = vec2;
	using Vector1 = vec1;

	using Matrix4 = mat4;
	using Matrix3 = mat3;
	using Matrix2 = mat2;

	using Quaternion = quat;
}