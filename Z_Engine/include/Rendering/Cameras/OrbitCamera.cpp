#pragma once
#include "OrbitCamera.h"

#include "../../dependencies/glm/gtc/quaternion.hpp"
#include "../../dependencies/glm/gtx/quaternion.hpp"

namespace Z_Engine::Rendering::Cameras {

	OrbitCamera::OrbitCamera(float field_of_view, float aspect_ratio, float near, float far, float yaw_rad, float pitch_rad)
		: 
		PerspectiveCamera(field_of_view, aspect_ratio, near, far)
	{
		m_yaw_angle		= yaw_rad;
		m_pitch_angle	= pitch_rad;
	}


	void OrbitCamera::SetYawAngle(float degree) {
		m_yaw_angle = glm::radians(degree);
	}
	
	void OrbitCamera::SetPitchAngle(float degree) {
		auto rad = glm::radians(degree);
		m_pitch_angle = glm::clamp(rad, -glm::half_pi<float>() + 0.1f, glm::half_pi<float>() - 0.1f);
	}

	void OrbitCamera::SetRadius(float value) {
		m_radius = glm::clamp(value, 1.5f, 300.f);
		m_position = m_target + (m_radius * m_forward);
		PerspectiveCamera::UpdateViewMatrix();
	}


	void OrbitCamera::SetTarget(const glm::vec3& target) {
		PerspectiveCamera::SetTarget(target);
		const glm::vec3 direction	= m_position - m_target;
		m_radius = glm::length(direction);
	}



	void OrbitCamera::SetPosition(const glm::vec3& position) {
		Camera::SetPosition(position);
		m_forward	= m_position - m_target;
		m_radius	= glm::length(m_forward);

		glm::quat quat_around_y = glm::angleAxis(m_yaw_angle, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::quat quat_around_x = glm::angleAxis(m_pitch_angle, glm::vec3(1.0f, 0.0f, 0.0f));
		glm::quat quat_around_yx = quat_around_y * quat_around_x;

		m_forward = glm::rotate(glm::normalize(quat_around_yx), glm::normalize(m_forward));
		m_position = m_target + (m_radius * m_forward);
		m_forward = glm::normalize(m_position - m_target);

		PerspectiveCamera::UpdateCoordinateVectors();
		PerspectiveCamera::UpdateViewMatrix();
	}

	void OrbitCamera::SetPosition(float yaw_degree, float pitch_degree) {

		float yaw_radians = glm::radians(yaw_degree);
		float pitch_radians = glm::radians(pitch_degree);


		auto quat_around_y	= glm::angleAxis(yaw_radians, m_up);
		auto quat_around_x	= glm::angleAxis(pitch_radians, m_right);
		auto quat_around_xy = quat_around_y * quat_around_x;

		m_forward	= glm::rotate(glm::normalize(quat_around_xy), m_forward);
		m_position	= m_target + (m_radius * m_forward);

		PerspectiveCamera::UpdateCoordinateVectors();
		PerspectiveCamera::UpdateViewMatrix();
	}
}