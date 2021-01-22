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
		m_pitch_angle = glm::clamp(rad, -glm::pi<float>() / 2.f + 0.1f, glm::pi<float>() /2.0f - 0.1f);
	}
	
	void FirstPersonShooterCamera::SetTarget(const glm::vec3& target) {
		Camera::SetTarget(target);
		PerspectiveCamera::UpdateCoordinateVectors();
		PerspectiveCamera::UpdateViewMatrix();
	}

	void FirstPersonShooterCamera::SetPosition(const glm::vec3& position) {
		Camera::SetPosition(position);
		PerspectiveCamera::UpdateCoordinateVectors();
		PerspectiveCamera::UpdateViewMatrix();
	}


	void FirstPersonShooterCamera::Move(const glm::vec3& offset_position) {
		m_position += offset_position;
		
		// We don't want the camera to fall down outside the ground plane position
		// ToDo : we need to revisit this part to provide better approach
		if (m_position.y <= 1.0f) m_position.y = 1.0f;

		this->UpdateCoordinateVectors();
		PerspectiveCamera::UpdateViewMatrix();
	}


	void FirstPersonShooterCamera::UpdateCoordinateVectors() {
		glm::vec3 forward_ = { 0.f, 0.0f, 0.0f };
		forward_.x = glm::cos(m_pitch_angle) * glm::sin(m_yaw_angle);
		forward_.y = glm::sin(m_pitch_angle);
		forward_.z = glm::cos(m_pitch_angle) * glm::cos(m_yaw_angle);

		m_forward	= glm::normalize(forward_);
		m_right		= glm::normalize(glm::cross(m_world_up, m_forward));
		m_up		= glm::normalize(glm::cross(m_forward, m_right));
		
		m_target = m_forward + m_position;
	}

	void FirstPersonShooterCamera::SetPosition(float yaw_degree, float pitch_degree) {

		float current_yaw = glm::degrees(m_yaw_angle);
		float current_pitch = glm::degrees(m_pitch_angle);

		current_yaw		+= yaw_degree;
		current_pitch	+= pitch_degree;

		this->SetPitchAngle(current_pitch);
		this->SetYawAngle(current_yaw);

		this->UpdateCoordinateVectors();
		PerspectiveCamera::UpdateViewMatrix();
	}
}