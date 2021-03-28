#pragma once

#include "../dependencies/glm/glm.hpp"
#include "../dependencies/glm/gtc/quaternion.hpp"
#include "../dependencies/glm/gtx/quaternion.hpp"

#include "../dependencies/glm/gtc/matrix_transform.hpp"
#include "../dependencies/glm/gtc/type_ptr.hpp"


namespace Z_Engine::Maths {
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