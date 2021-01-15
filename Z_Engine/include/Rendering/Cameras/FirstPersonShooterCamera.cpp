#pragma once
#include "FirstPersonShooterCamera.h"

namespace Z_Engine::Rendering::Cameras {

	FirstPersonShooterCamera::FirstPersonShooterCamera(float field_of_view, float aspect_ratio, float near, float far, float yaw_rad, float pitch_rad)
		:
		PerspectiveCamera(field_of_view, aspect_ratio, near, far),
		m_yaw_angle(yaw_rad),
		m_pitch_angle(pitch_rad),
		m_radius(0.0f)
	{
	}


	void FirstPersonShooterCamera::SetYawAngle(float degree) {
		m_yaw_angle = glm::radians(degree);
	}

	void FirstPersonShooterCamera::SetPitchAngle(float degree) {
		auto rad = glm::radians(degree);
		m_pitch_angle = glm::clamp(rad, -glm::half_pi<float>() + 0.1f, glm::half_pi<float>() - 0.1f);
	}

	void FirstPersonShooterCamera::SetRadius(float value) {
		m_radius = glm::clamp(value, 1.5f, 100.f);
	}


	void FirstPersonShooterCamera::SetTarget(const glm::vec3& target) {
		PerspectiveCamera::SetTarget(target);
		const glm::vec3 direction = m_position - m_target;
		m_radius = glm::sqrt((direction.x * direction.x) + (direction.y * direction.y) + (direction.z * direction.z));

	}



	void FirstPersonShooterCamera::SetPosition(const glm::vec3& position) {
		PerspectiveCamera::SetPosition(position);
		const glm::vec3 direction = m_position - m_target;
		m_radius = glm::sqrt((direction.x * direction.x) + (direction.y * direction.y) + (direction.z * direction.z));
	}

	void FirstPersonShooterCamera::SetPosition(float yaw_degree, float pitch_degree) {

		this->SetYawAngle(yaw_degree);
		this->SetPitchAngle(pitch_degree);

		m_position.x = m_target.x + (m_radius * glm::cos(m_pitch_angle) * glm::sin(m_yaw_angle));
		m_position.y = m_target.y + (m_radius * glm::sin(m_pitch_angle));
		m_position.z = m_target.z + (m_radius * glm::cos(m_pitch_angle) * glm::cos(m_yaw_angle));

		PerspectiveCamera::UpdateViewMatrix();
	}
}