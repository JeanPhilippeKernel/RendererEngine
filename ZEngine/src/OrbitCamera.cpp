#include <ZEngine/pch.h>
#include <Rendering/Cameras/OrbitCamera.h>

namespace ZEngine::Rendering::Cameras {

	OrbitCamera::OrbitCamera(float field_of_view, float aspect_ratio, float near, float far, float yaw_rad, float pitch_rad)
		: 
		PerspectiveCamera(field_of_view, aspect_ratio, near, far)
	{
		m_yaw_angle		= yaw_rad;
		m_pitch_angle	= pitch_rad;
	}


	void OrbitCamera::SetYawAngle(float degree) {
		m_yaw_angle = Maths::radians(degree);
	}
	
	void OrbitCamera::SetPitchAngle(float degree) {
		auto rad		= Maths::radians(degree);
		m_pitch_angle	= Maths::clamp(rad, -Maths::half_pi<float>() + 0.1f, Maths::half_pi<float>() - 0.1f);
	}

	void OrbitCamera::SetRadius(float value) {
		m_radius	= Maths::clamp(value, 1.5f, 300.f);
		m_position	= m_target + (m_radius * m_forward);
		PerspectiveCamera::UpdateViewMatrix();
	}


	void OrbitCamera::SetTarget(const Maths::Vector3& target) {
		PerspectiveCamera::SetTarget(target);
		const Maths::Vector3 direction = m_position - m_target;
		m_radius					= Maths::length(direction);
	}



	void OrbitCamera::SetPosition(const Maths::vec3& position) {
		Camera::SetPosition(position);
		m_forward	= m_position - m_target;
		m_radius	= Maths::length(m_forward);

		Maths::Quaternion quat_around_y		= Maths::angleAxis(m_yaw_angle, Maths::Vector3(0.0f, 1.0f, 0.0f));
		Maths::Quaternion quat_around_x		= Maths::angleAxis(m_pitch_angle, Maths::Vector3(1.0f, 0.0f, 0.0f));
		Maths::Quaternion quat_around_yx	= quat_around_y * quat_around_x;

		m_forward	= Maths::rotate(Maths::normalize(quat_around_yx), Maths::normalize(m_forward));
		m_position	= m_target + (m_radius * m_forward);
		m_forward	= Maths::normalize(m_position - m_target);

		PerspectiveCamera::UpdateCoordinateVectors();
		PerspectiveCamera::UpdateViewMatrix();
	}

	void OrbitCamera::SetPosition(float yaw_degree, float pitch_degree) {

		float yaw_radians	= Maths::radians(yaw_degree);
		float pitch_radians = Maths::radians(pitch_degree);


		auto quat_around_y	= Maths::angleAxis(yaw_radians, m_up);
		auto quat_around_x	= Maths::angleAxis(pitch_radians, m_right);
		auto quat_around_xy = quat_around_y * quat_around_x;

		m_forward	= Maths::rotate(Maths::normalize(quat_around_xy), m_forward);
		m_position	= m_target + (m_radius * m_forward);

		PerspectiveCamera::UpdateCoordinateVectors();
		PerspectiveCamera::UpdateViewMatrix();
	}
}