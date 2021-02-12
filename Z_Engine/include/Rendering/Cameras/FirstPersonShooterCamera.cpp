#pragma once
#include "FirstPersonShooterCamera.h"

namespace Z_Engine::Rendering::Cameras {

	FirstPersonShooterCamera::FirstPersonShooterCamera(float field_of_view, float aspect_ratio, float near, float far, float yaw_rad, float pitch_rad)
		:
		PerspectiveCamera(field_of_view, aspect_ratio, near, far)
	{
		m_yaw_angle = yaw_rad;
		m_pitch_angle = pitch_rad;
	}


	void FirstPersonShooterCamera::SetYawAngle(float degree) {
		m_yaw_angle = Maths::radians(degree);
	}

	void FirstPersonShooterCamera::SetPitchAngle(float degree) {
		float rad = Maths::radians(degree);
		m_pitch_angle = Maths::clamp(rad, -Maths::pi<float>() + 0.1f, Maths::pi<float>() - 0.1f);
	}
	
	void FirstPersonShooterCamera::SetTarget(const Maths::Vector3& target) {
		Camera::SetTarget(target);
		m_forward = Maths::normalize(m_position - m_target);
		m_radius = Maths::length(m_forward);
	}

	void FirstPersonShooterCamera::SetPosition(const Maths::Vector3& position) {
		Camera::SetPosition(position);
		Maths::Vector3 direction = m_position - m_target;
		m_radius			= Maths::length(direction);

		Maths::Quaternion quat_around_y	= Maths::angleAxis(m_yaw_angle, Maths::Vector3(0.0f, 1.0f, 0.0f));
		Maths::Quaternion quat_around_x = Maths::angleAxis(m_pitch_angle, Maths::Vector3(1.0f, 0.0f, 0.0f));
		Maths::Quaternion quat_around_yx = quat_around_y * quat_around_x;

		direction	= Maths::rotate(Maths::normalize(quat_around_yx), Maths::normalize(direction));
		m_position	= m_target + (m_radius * direction);
		m_forward	= Maths::normalize(m_position - m_target);

		UpdateCoordinateVectors();
		PerspectiveCamera::UpdateViewMatrix();
	}


	void FirstPersonShooterCamera::Move(const Maths::Vector3& offset) {

		m_position = Maths::translate(Maths::identity<Maths::Matrix4>(), offset) * Maths::Vector4(m_position, 1.0f);
		
		if (m_position.y <= 0.0f) {
			m_position.y = 0.0f;
		}
		
		UpdateCoordinateVectors();
		PerspectiveCamera::UpdateViewMatrix();
	}


	void FirstPersonShooterCamera::UpdateCoordinateVectors() {
		m_right		= Maths::cross(m_world_up, m_forward);
		m_up		= Maths::cross(m_forward, m_right);
		m_target 	= m_position - (m_radius * m_forward);
	}

	void FirstPersonShooterCamera::SetPosition(float yaw_degree, float pitch_degree) {

		float yaw_radians	= Maths::radians(yaw_degree);
		float pitch_radians = Maths::radians(pitch_degree);


		Maths::Quaternion quat_around_y		= Maths::angleAxis(yaw_radians, m_up);
		Maths::Quaternion quat_around_x		= Maths::angleAxis(pitch_radians, m_right);
		Maths::Quaternion quat_around_yx	= quat_around_y * quat_around_x;
		
		m_forward = Maths::rotate(Maths::normalize(quat_around_yx), Maths::normalize(m_forward));

		UpdateCoordinateVectors();
		PerspectiveCamera::UpdateViewMatrix();
	}
}